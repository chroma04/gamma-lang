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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t2lqu8jsj511d a[3]; } gm_t8o2b5qnbhu2s;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
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
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 a[27]; } gm_tlg1vmfbpbvtq;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t8o2b5qnbhu2s) == 48 && _Alignof(gm_t8o2b5qnbhu2s) == 8 && offsetof(gm_t8o2b5qnbhu2s, a) == 0, "layout of `[3](u64, u64)`");
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlg1vmfbpbvtq) == 432 && _Alignof(gm_tlg1vmfbpbvtq) == 8 && offsetof(gm_tlg1vmfbpbvtq, a) == 0, "layout of `[27]string`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_complex_call_8so3bss {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* args */
    gm_trqfvmjtt4sij l10; /* stmts */
    void * ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compound_number_binary_p7eerjs {
    gm_frame h;
    gm_trqfvmjtt4sij l33; /* stmts */
    gm_trqfvmjtt4sij l36; /* elems */
    void * ret;
};
struct gm_fr_compound_number_elem_6d1d2ni {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compound_number_unary_4h478fs {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* stmts */
    gm_trqfvmjtt4sij l14; /* elems */
    void * ret;
};
struct gm_fr_core_call_values_qq2la05 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    gm_trqfvmjtt4sij l5; /* written */
    gm_tc3oht4ksth9d l13; /* e */
    void * ret;
};
struct gm_fr_describe_operand_509et87 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
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
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_imaginary_literal_6d41gfv {
    gm_frame h;
    void * ret;
};
struct gm_fr_is_complex_var_bk0jb4c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_compound_number_kbipjl5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_3jk89qu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
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
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_var_3sb5sti {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_math_elem_iuerhch {
    gm_frame h;
    void * ret;
};
struct gm_fr_math_vector_conversion_0cttjm0 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* stmts */
    gm_trqfvmjtt4sij l14; /* elems */
    void * ret;
};
struct gm_fr_math_vector_method_ko2814s {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    gm_trqfvmjtt4sij l24; /* elems */
    gm_t8o2b5qnbhu2s l25; /* <tmp> */
    gm_trqfvmjtt4sij l34; /* elems */
    gm_trqfvmjtt4sij l39; /* elems */
    void * ret;
};
struct gm_fr_mentions_complex_constan_lrgkqac {
    gm_frame h;
    gm_t5vbkdq81qfs7 l7; /* r */
    gm_t0ialelq3ql1k l8; /* v */
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_plain_31j25h8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_promote_real_constant_v0nhili {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* parts */
    void * ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_settle_complex_operands_9439n4u {
    gm_frame h;
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
struct gm_fr_untyped_complex_20agrmu {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* re */
    gm_tngtuiuskn938 l1; /* im */
    gm_teti411eujvf4 ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_teti411eujvf4 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_zero_9r6o7sj {
    gm_frame h;
    gm_tngtuiuskn938 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arith_op_hsstk43(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_complex_call_8so3bss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_compound_number_elem_6d1d2ni(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_core_call_values_qq2la05(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_operand_509et87(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_1oelod5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6up4rm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_86pfrlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_davqh84(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gdra5k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gmfdekn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_iepqo20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jn4ifp1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mcgdakk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p0j9lf7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_unf194k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uubl3s3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_vq3bur7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_math_elem_iuerhch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_fnepb5f(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_new_var_up2m1nq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_plain_31j25h8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_settle_complex_operands_9439n4u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1lpoo3h6o[40] __attribute__((aligned(8)));
extern uint8_t gm_ktdkih9htpv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s26md5sqfb6u7[7];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s4bo4cvutg59h[16];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s6fdn1h3c830p[5];
extern const uint8_t gm_s7sn5glap1gkf[7];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd7vgd2pm42gk[14];
extern const uint8_t gm_sdib2k2ql9mgl[9];
extern const uint8_t gm_sf372rql24e3n[17];
extern const uint8_t gm_sfa8f3bk569gj[26];
extern const uint8_t gm_sg1h22ve4uqmq[4];
extern const uint8_t gm_sgvnpogt67see[6];
extern const uint8_t gm_sjsojd1o1489e[34];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_sp66nadh171v3[19];
extern const uint8_t gm_sprospqi1g16o[7];
extern const uint8_t gm_sq5sjasaqn114[7];
extern const uint8_t gm_sr486u97qk9sk[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr7qn5gtrg5md[114];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srci09ctgf0djoe[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssnn9u3skptri[75];
extern const uint8_t gm_su3gef13fgq4c[7];
extern const uint8_t gm_sump4nk3r1lgv[8];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_complex_var_bk0jb4c F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 96);
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_taari1s0m51a7 t7 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t2, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t9;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[1] = 1;
    gm_taari1s0m51a7 t10 = t7;
    gm_taari1s0m51a7 t11 = t9;
    uint8_t t12 = (gm_eq5307j5k9ms(((uint8_t *)&t10), ((uint8_t *)&t11)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_3jk89qu F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
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
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t8 != ((uint8_t)3ULL)) goto mn7;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 10, 56);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_math_vector_parts_49c7865((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_tb7u19loo3j87 t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
me6:;
    F.ret = t5;
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fa4sjvq();
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
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_plain_31j25h8(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plain_31j25h8 F;
    void * gm_l0; /* ts */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 141u);
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
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t8 != ((uint8_t)11ULL)) goto mn7;
    uint8_t t9 = GM_LD(uint8_t, (((((uint8_t *)&t4) + 8) + 12) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn7;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn10:;
    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 430, 53);
me6:;
    F.ret = t5;
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
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_untyped_complex_20agrmu F;
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tu15p0hi2qlv6 t1;
    memcpy((uint8_t *)&t1 + 0, ((uint8_t *)a0), 96);
    memcpy((uint8_t *)&t1 + 96, ((uint8_t *)&F.l1), 96);
    gm_tq7s98ihonkju t2 = gm_f_new_fnepb5f((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktdkih9htpv)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t3 = t2;
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_srcod98slk3vnra, 564, 89);
    void * t4 = GM_LD(void *, (((uint8_t *)&t3) + 8));
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)3ULL));
    GM_ST(void *, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf1lpoo3h6o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcod98slk3vnra, 561, 80);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
    gm_teti411eujvf4 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_9r6o7sj F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_zero_s25l990((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1);
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
__attribute__((aligned(4))) void * gm_f_complex_call_8so3bss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_complex_call_8so3bss F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* args0 */
    uint8_t gm_l3; /* swap */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* f */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* a */
    void * gm_l11; /* first */
    gm_t5fru0lp11n7t gm_l12; /* <temporary array> */
    void * gm_l13; /* call */
    GM_STACK_CHECK(&F, 810u);
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
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sump4nk3r1lgv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = gm_l1;
        gm_t9805qtonbsag t5 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t6 = t5;
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = gm_l4;
                gm_b16_8 t12 = gm_l1;
                gm_tnpifv20irnph t13;
                GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
                gm_tabb710d9mq2e t14;
                gm_f_format_unf194k((void *)&t14, (void *)NULL, cx, NULL, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t15 = t14;
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srci09ctgf0djoe, 298, 24);
                gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
                gm_b16_8 t17 = t16;
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                gm_b0_1 t19 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t10, t11, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_tl874t2atnlh3 t22 = gm_l4;
            void * t23 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        gm_l5 = t7;
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t26 = gm_l2;
            gm_l7 = t26;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t28 = gm_l8;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    uint64_t t33 = gm_l8;
                    uint64_t t34 = (uintptr_t)t33;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t36 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t34 * 8));
                    void * t37 = t36;
                    void * t40 = GM_LD(void *, ((uint8_t *)&t37));
                    gm_l9 = t40;
                    {
                        void * t42 = (void *)((uint8_t *)&F.l6);
                        void * t43 = gm_l9;
                        gm_tftb1odc739t5 t44 = gm_f_push_pf02nv6(t42, t43);
                        gm_tftb1odc739t5 t45 = t44;
                        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srci09ctgf0djoe, 302, 22);
                        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 302, 5);
me38:;
                }
lc27:;
                {
                    uint64_t t48 = gm_l8;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l8 = t51;
                }
            }
lb27:;
        }
    }
    {
        uint8_t t53 = gm_l3;
        if (!t53) goto ie52;
        {
            gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t55 = gm_l0;
            uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srci09ctgf0djoe, 306, 31);
            void * t58 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t56 * 8));
            void * t59 = (void *)((uint8_t *)&F.l10);
            void * t60 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t55, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t60;
        }
        {
        }
        {
            void * t61 = gm_l0;
            uint32_t t62 = gm_l5;
            {
                uint64_t t64 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srci09ctgf0djoe, 307, 49);
                void * t66 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t64 * 8));
                void * t67 = gm_l11;
                gm_t5fru0lp11n7t t68;
                GM_ST(void *, ((uint8_t *)&t68 + 0), t66);
                GM_ST(void *, ((uint8_t *)&t68 + 8), t67);
                gm_l12 = t68;
            }
            void * t69 = ((uint8_t *)&gm_l12);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 307, 40);
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t69 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t71 = t70;
            gm_tl874t2atnlh3 t72 = gm_l4;
            void * t73 = gm_f_core_call_values_qq2la05((void *)NULL, cx, NULL, t61, t62, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t73;
        }
        {
            void * t74 = gm_l13;
            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t74) + 0));
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t77 = gm_l13;
            gm_tl874t2atnlh3 t78 = gm_l4;
            void * t79 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t75, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t80 = 0;
            F.ret = t79;
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        void * t81 = gm_l0;
        uint32_t t82 = gm_l5;
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t84 = t83;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        gm_tl874t2atnlh3 t86 = gm_l4;
        void * t87 = gm_f_core_call_values_qq2la05((void *)NULL, cx, NULL, t81, t82, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t88 = 0;
        F.ret = t87;
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
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_compound_number_binary_p7eerjs F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs0 */
    void * gm_l3; /* r0 */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* lhs */
    void * gm_l7; /* r */
    gm_b16_8 gm_l8; /* opn */
    gm_t88rcucg1b7uk gm_l9; /* lt */
    gm_t88rcucg1b7uk gm_l10; /* rt */
    uint8_t gm_l11; /* promoted */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    void * gm_l13; /* p */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    void * gm_l15; /* p */
    uint8_t gm_l16; /* left_is_agg */
    gm_t88rcucg1b7uk gm_l17; /* agg */
    gm_t88rcucg1b7uk gm_l18; /* elem */
    gm_t88rcucg1b7uk gm_l19; /* other */
    uint8_t gm_l20; /* both */
    uint8_t gm_l21; /* complex */
    gm_b16_8 gm_l22; /* what */
    uint8_t gm_l23; /* shift */
    uint8_t gm_l24; /* scales */
    gm_b16_8 gm_l25; /* hint */
    gm_b16_8 gm_l26; /* l */
    gm_b16_8 gm_l27; /* rr */
    gm_b16_8 gm_l28; /* shown */
    gm_b16_8 gm_l29; /* name */
    uint8_t gm_l30; /* swap */
    gm_t5fru0lp11n7t gm_l31; /* <temporary array> */
    uint64_t gm_l32; /* n */
    void * gm_l34; /* l */
    void * gm_l35; /* rv */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* i */
    void * gm_l40; /* a */
    void * gm_l41; /* b */
    void * gm_l42; /* e */
    gm_t9qkh0rsooc8d gm_l43; /* f */
    gm_ts6oqppjh0ba3 gm_l44; /* top */
    void * gm_l45; /* arr */
    GM_STACK_CHECK(&F, 4568u);
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
        void * t5 = gm_l2;
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l3;
        gm_l7 = t6;
    }
    {
        gm_ts6oqppjh0ba3 t7 = gm_l1;
        gm_b16_8 t8 = gm_f_binop_text_0ljfc0r(t7);
        gm_l8 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_ts6oqppjh0ba3 t10 = gm_l1;
        void * t11 = gm_l6;
        void * t12 = gm_l7;
        gm_tl874t2atnlh3 t13 = gm_l4;
        gm_b0_1 t14 = gm_f_settle_complex_operands_9439n4u((void *)NULL, cx, NULL, t9, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 96);
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = gm_l6;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 0));
        gm_t88rcucg1b7uk t21 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t16, t18, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 96);
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l7;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
        gm_t88rcucg1b7uk t28 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t23, t25, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t28;
    }
    {
        uint8_t t30 = 0;
        void * t31 = gm_l5;
        gm_t88rcucg1b7uk t32 = gm_l9;
        uint8_t t33 = gm_f_is_error_8k27gd1(t31, GM_CONV(gm_csguankko4lmhd, t32));
        if (!t33) {
            void * t34 = gm_l5;
            gm_t88rcucg1b7uk t35 = gm_l10;
            uint8_t t36 = gm_f_is_error_8k27gd1(t34, GM_CONV(gm_csguankko4lmhd, t35));
            t30 = t36;
        } else t30 = 1;
        if (!t30) goto ie29;
        {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            gm_tl874t2atnlh3 t40 = gm_l4;
            void * t41 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        uint8_t t44 = 0;
        uint8_t t45 = 0;
        gm_ts6oqppjh0ba3 t46 = gm_l1;
        gm_ts6oqppjh0ba3 t47 = t46;
        gm_t9qkh0rsooc8d t48;
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t49 = t48;
        uint8_t t50 = (((GM_LD(uint8_t, ((uint8_t *)&t47) + 0) == ((uint8_t)0ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t47) + 1), ((uint8_t *)&t49))));
        if (!t50) {
            gm_ts6oqppjh0ba3 t51 = gm_l1;
            gm_ts6oqppjh0ba3 t52 = t51;
            gm_t9qkh0rsooc8d t53;
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t54 = t53;
            uint8_t t55 = (((GM_LD(uint8_t, ((uint8_t *)&t52) + 0) == ((uint8_t)1ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t52) + 1), ((uint8_t *)&t54))));
            t45 = t55;
        } else t45 = 1;
        if (!t45) {
            uint8_t t56 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            t44 = t56;
        } else t44 = 1;
        if (!t44) goto ie43;
        {
            gm_l11 = ((uint8_t)0);
        }
        {
            void * t59 = gm_l0;
            void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
            gm_t88rcucg1b7uk t61 = gm_l9;
            gm_tkn42u7na3t81 t62 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t63 = t62;
            if (!((((uint8_t *)&t63))[4] != 0)) goto ie58;
            gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t63));
            gm_l12 = t64;
            {
                uint8_t t67 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
                if (!t67) goto ie66;
                {
                    void * t70 = gm_l0;
                    void * t71 = gm_l7;
                    gm_t88rcucg1b7uk t72 = gm_l9;
                    gm_t88rcucg1b7uk t73 = gm_l12;
                    void * t74 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t70, t71, t72, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t75 = t74;
                    if (!(GM_LD(void *, ((uint8_t *)&t75)) != 0)) goto ie69;
                    void * t76 = GM_LD(void *, ((uint8_t *)&t75));
                    gm_l13 = t76;
                    {
                        void * t78 = gm_l13;
                        gm_l7 = t78;
                    }
                    {
                        gm_t88rcucg1b7uk t79 = gm_l9;
                        gm_l10 = t79;
                    }
                    {
                        gm_l11 = ((uint8_t)1);
                    }
                    goto ix69;
ie69:;
ix69:;
                }
                goto ix66;
ie66:;
ix66:;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint8_t t81 = gm_l11;
            uint8_t t82 = (uint8_t)!t81;
            if (!t82) goto ie80;
            {
                void * t85 = gm_l0;
                void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                gm_t88rcucg1b7uk t87 = gm_l10;
                gm_tkn42u7na3t81 t88 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t89 = t88;
                if (!((((uint8_t *)&t89))[4] != 0)) goto ie84;
                gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t89));
                gm_l14 = t90;
                {
                    void * t93 = gm_l0;
                    void * t94 = gm_l6;
                    gm_t88rcucg1b7uk t95 = gm_l10;
                    gm_t88rcucg1b7uk t96 = gm_l14;
                    void * t97 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t93, t94, t95, t96);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t98 = t97;
                    if (!(GM_LD(void *, ((uint8_t *)&t98)) != 0)) goto ie92;
                    void * t99 = GM_LD(void *, ((uint8_t *)&t98));
                    gm_l15 = t99;
                    {
                        void * t101 = gm_l15;
                        gm_l6 = t101;
                    }
                    {
                        gm_t88rcucg1b7uk t102 = gm_l10;
                        gm_l9 = t102;
                    }
                    goto ix92;
ie92:;
ix92:;
                }
                goto ix84;
ie84:;
ix84:;
            }
            goto ix80;
ie80:;
ix80:;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t103 = gm_l0;
        gm_t88rcucg1b7uk t104 = gm_l9;
        uint8_t t105 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t105;
    }
    {
        gm_t88rcucg1b7uk t106;
        uint8_t t108 = gm_l16;
        if (!t108) goto ie107;
        gm_t88rcucg1b7uk t110 = gm_l9;
        gm_t88rcucg1b7uk t111 = t110;
        t106 = t111;
        goto ix107;
ie107:;
        gm_t88rcucg1b7uk t113 = gm_l10;
        gm_t88rcucg1b7uk t114 = t113;
        t106 = t114;
ix107:;
        gm_l17 = t106;
    }
    {
        void * t115 = gm_l0;
        gm_t88rcucg1b7uk t116 = gm_l17;
        gm_tkn42u7na3t81 t117 = gm_f_compound_number_elem_6d1d2ni((void *)NULL, cx, NULL, t115, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t118 = t117;
        gm_t88rcucg1b7uk t119;
        if (((((uint8_t *)&t118))[4] != 0)) {
            gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t118) + 0));
            t119 = t120;
        } else {
            void * t121 = gm_l0;
            void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
            gm_tl874t2atnlh3 t123 = gm_l4;
            void * t124 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t122, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t125 = 0;
            F.ret = t124;
            goto gm_fret;
        }
        gm_l18 = t119;
    }
    {
        gm_t88rcucg1b7uk t126;
        uint8_t t128 = gm_l16;
        if (!t128) goto ie127;
        gm_t88rcucg1b7uk t130 = gm_l10;
        gm_t88rcucg1b7uk t131 = t130;
        t126 = t131;
        goto ix127;
ie127:;
        gm_t88rcucg1b7uk t133 = gm_l9;
        gm_t88rcucg1b7uk t134 = t133;
        t126 = t134;
ix127:;
        gm_l19 = t126;
    }
    {
        void * t135 = gm_l0;
        gm_t88rcucg1b7uk t136 = gm_l19;
        uint8_t t137 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t137;
    }
    {
        void * t138 = gm_l5;
        gm_t88rcucg1b7uk t139 = gm_l17;
        uint8_t t140 = gm_f_is_math_vector_09mtuao(t138, GM_CONV(gm_csguankko4lmhd, t139));
        uint8_t t141 = (uint8_t)!t140;
        gm_l21 = t141;
    }
    {
        gm_b16_8 t142;
        uint8_t t144 = gm_l21;
        if (!t144) goto ie143;
        gm_b16_8 t146;
        GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s4bo4cvutg59h));
        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(15));
        gm_b16_8 t147 = t146;
        t142 = t147;
        goto ix143;
ie143:;
        gm_b16_8 t149;
        GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sr486u97qk9sk));
        GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(12));
        gm_b16_8 t150 = t149;
        t142 = t150;
ix143:;
        gm_l22 = t142;
    }
    {
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t155 != ((uint8_t)16ULL)) goto pa154;
        goto po153;
pa154:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t157 != ((uint8_t)17ULL)) goto pa156;
        goto po153;
pa156:;
        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t159 != ((uint8_t)18ULL)) goto pa158;
        goto po153;
pa158:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t161 != ((uint8_t)19ULL)) goto pa160;
        goto po153;
pa160:;
        goto mn152;
po153:;
        {
            void * t163 = gm_l0;
            gm_tl874t2atnlh3 t164 = gm_l4;
            gm_b16_8 t165 = gm_l22;
            gm_tnpifv20irnph t166;
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t165);
            gm_tabb710d9mq2e t167;
            gm_f_format_vq3bur7((void *)&t167, (void *)NULL, cx, NULL, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t168 = t167;
            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_srci09ctgf0djoe, 166, 28);
            gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
            gm_b16_8 t170 = t169;
            gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
            gm_b0_1 t172 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t163, t164, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t173 = gm_l0;
            void * t174 = GM_LD(void *, (((uint8_t *)t173) + 0));
            gm_tl874t2atnlh3 t175 = gm_l4;
            void * t176 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t174, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t177 = 0;
            F.ret = t176;
            goto gm_fret;
        }
        goto me151;
mn152:;
        goto me151;
mn178:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 164, 5);
me151:;
    }
    {
        gm_ts6oqppjh0ba3 t181 = gm_l1;
        gm_t9qkh0rsooc8d t182;
        GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t183;
        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t183 + 1), t182);
        gm_t9qkh0rsooc8d t184;
        GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t185;
        GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t185 + 1), t184);
        gm_t9qkh0rsooc8d t186;
        GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t187;
        GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t187 + 1), t186);
        gm_t9qkh0rsooc8d t188;
        GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t189;
        GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t189 + 1), t188);
        gm_t6htphat2tk14 t190;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t190 + 0), t183);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t190 + 2), t185);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t190 + 4), t187);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t190 + 6), t189);
        gm_t6htphat2tk14 t191 = t190;
        uint8_t t192 = 0;
        gm_ts6oqppjh0ba3 t193 = t181;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t191) + 0, ((uint8_t *)&t193))) t192 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t191) + 2, ((uint8_t *)&t193))) t192 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t191) + 4, ((uint8_t *)&t193))) t192 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t191) + 6, ((uint8_t *)&t193))) t192 = 1;
        uint8_t t194 = (uint8_t)(t192);
        if (!t194) goto ie180;
        {
            void * t196 = gm_l0;
            gm_tl874t2atnlh3 t197 = gm_l4;
            gm_t6j6smj7qoctu t198;
            gm_b16_8 t199 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 0, t199);
            gm_b16_8 t200 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 16, t200);
            gm_tabb710d9mq2e t201;
            gm_f_format_1oelod5((void *)&t201, (void *)NULL, cx, NULL, &t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t202 = t201;
            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 8, gm_srci09ctgf0djoe, 172, 24);
            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&t202) + 8));
            gm_b16_8 t204 = t203;
            gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
            gm_b0_1 t206 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t196, t197, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t207 = gm_l0;
            void * t208 = GM_LD(void *, (((uint8_t *)t207) + 0));
            gm_tl874t2atnlh3 t209 = gm_l4;
            void * t210 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t208, t209);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t211 = 0;
            F.ret = t210;
            goto gm_fret;
        }
        goto ix180;
ie180:;
ix180:;
    }
    {
        uint8_t t212 = 0;
        uint8_t t213;
        uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t216 != ((uint8_t)3ULL)) goto mn215;
        t213 = ((uint8_t)1);
        goto me214;
mn215:;
        t213 = ((uint8_t)0);
        goto me214;
mn217:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 175, 14);
me214:;
        if (!t213) {
            uint8_t t218 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)6ULL)));
            t212 = t218;
        } else t212 = 1;
        gm_l23 = t212;
    }
    {
        uint8_t t220 = gm_l21;
        if (!t220) goto ie219;
        uint8_t t221 = 0;
        uint8_t t222 = 0;
        uint8_t t223 = 0;
        gm_ts6oqppjh0ba3 t224 = gm_l1;
        gm_ts6oqppjh0ba3 t225 = t224;
        gm_t9qkh0rsooc8d t226;
        GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t227 = t226;
        uint8_t t228 = (((GM_LD(uint8_t, ((uint8_t *)&t225) + 0) == ((uint8_t)0ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t225) + 1), ((uint8_t *)&t227))));
        if (!t228) {
            gm_ts6oqppjh0ba3 t229 = gm_l1;
            gm_ts6oqppjh0ba3 t230 = t229;
            gm_t9qkh0rsooc8d t231;
            GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t232 = t231;
            uint8_t t233 = (((GM_LD(uint8_t, ((uint8_t *)&t230) + 0) == ((uint8_t)1ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t230) + 1), ((uint8_t *)&t232))));
            t223 = t233;
        } else t223 = 1;
        if (!t223) {
            gm_ts6oqppjh0ba3 t234 = gm_l1;
            gm_ts6oqppjh0ba3 t235 = t234;
            gm_t9qkh0rsooc8d t236;
            GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t237 = t236;
            uint8_t t238 = (((GM_LD(uint8_t, ((uint8_t *)&t235) + 0) == ((uint8_t)2ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t235) + 1), ((uint8_t *)&t237))));
            t222 = t238;
        } else t222 = 1;
        if (!t222) {
            uint8_t t239 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            t221 = t239;
        } else t221 = 1;
        uint8_t t240 = (uint8_t)!t221;
        if (!t240) goto ie219;
        {
            void * t242 = gm_l0;
            gm_tl874t2atnlh3 t243 = gm_l4;
            gm_b16_8 t244 = gm_l8;
            gm_tnpifv20irnph t245;
            GM_ST(gm_b16_8, ((uint8_t *)&t245 + 0), t244);
            gm_tabb710d9mq2e t246;
            gm_f_format_s7bl5bp((void *)&t246, (void *)NULL, cx, NULL, t245);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t247 = t246;
            if (((((uint8_t *)&t247))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t247) + 8, gm_srci09ctgf0djoe, 177, 24);
            gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&t247) + 8));
            gm_b16_8 t249 = t248;
            gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&t249));
            gm_b0_1 t251 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t242, t243, t250);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t252 = gm_l0;
            void * t253 = GM_LD(void *, (((uint8_t *)t252) + 0));
            gm_tl874t2atnlh3 t254 = gm_l4;
            void * t255 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t253, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t256 = 0;
            F.ret = t255;
            goto gm_fret;
        }
        goto ix219;
ie219:;
ix219:;
    }
    {
        uint8_t t258 = gm_l23;
        if (!t258) goto ie257;
        {
            uint8_t t261 = 0;
            uint8_t t262 = gm_l16;
            uint8_t t263 = (uint8_t)!t262;
            if (!t263) {
                uint8_t t264 = gm_l20;
                t261 = t264;
            } else t261 = 1;
            if (!t261) goto ie260;
            {
                void * t266 = gm_l0;
                gm_tl874t2atnlh3 t267 = gm_l4;
                gm_b16_8 t268;
                GM_ST(void *, (uint8_t *)&t268, (void *)(gm_ssnn9u3skptri));
                GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(74));
                gm_b0_1 t269 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t266, t267, t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t270 = gm_l0;
                void * t271 = GM_LD(void *, (((uint8_t *)t270) + 0));
                gm_tl874t2atnlh3 t272 = gm_l4;
                void * t273 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t271, t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t274 = 0;
                F.ret = t273;
                goto gm_fret;
            }
            goto ix260;
ie260:;
ix260:;
        }
        {
            void * t276 = gm_l0;
            void * t277 = (void *)(((uint8_t *)t276) + 96);
            void * t278 = gm_l5;
            void * t279 = gm_l7;
            gm_t88rcucg1b7uk t280 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t279) + 0));
            gm_t9qkh0rsooc8d t281;
            GM_ST(uint8_t, ((uint8_t *)&t281 + 0), ((uint8_t)1ULL));
            uint8_t t282 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t277, t278, t280, t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t283 = (uint8_t)!t282;
            if (!t283) goto ie275;
            {
                void * t285 = gm_l0;
                void * t286 = gm_l7;
                gm_tl874t2atnlh3 t287 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t286) + 72));
                void * t288 = gm_l0;
                gm_t88rcucg1b7uk t289 = gm_l10;
                gm_b16_8 t290 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t288, t289);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t291;
                GM_ST(gm_b16_8, ((uint8_t *)&t291 + 0), t290);
                gm_tabb710d9mq2e t292;
                gm_f_format_pdashts((void *)&t292, (void *)NULL, cx, NULL, t291);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t293 = t292;
                if (((((uint8_t *)&t293))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t293) + 8, gm_srci09ctgf0djoe, 187, 30);
                gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&t293) + 8));
                gm_b16_8 t295 = t294;
                gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
                gm_b0_1 t297 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t285, t287, t296);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t298 = gm_l0;
                void * t299 = GM_LD(void *, (((uint8_t *)t298) + 0));
                gm_tl874t2atnlh3 t300 = gm_l4;
                void * t301 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t299, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t302 = 0;
                F.ret = t301;
                goto gm_fret;
            }
            goto ix275;
ie275:;
ix275:;
        }
        goto ix257;
ie257:;
        uint8_t t304 = gm_l20;
        if (!t304) goto ie303;
        {
            void * t307 = gm_l5;
            void * t308 = gm_l0;
            gm_t88rcucg1b7uk t309 = gm_l9;
            gm_t88rcucg1b7uk t310 = gm_l10;
            gm_tl874t2atnlh3 t311 = gm_l4;
            gm_b16_8 t312 = gm_l8;
            gm_tnpifv20irnph t313;
            GM_ST(gm_b16_8, ((uint8_t *)&t313 + 0), t312);
            gm_tabb710d9mq2e t314;
            gm_f_format_ko5t8rr((void *)&t314, (void *)NULL, cx, NULL, t313);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t315 = t314;
            if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 8, gm_srci09ctgf0djoe, 191, 57);
            gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&t315) + 8));
            gm_b16_8 t317 = t316;
            gm_b16_8 t318 = GM_LD(gm_b16_8, ((uint8_t *)&t317));
            gm_t88rcucg1b7uk t319 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t308, t309, t310, t311, t318);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t320 = gm_f_is_error_8k27gd1(t307, GM_CONV(gm_csguankko4lmhd, t319));
            if (!t320) goto ie306;
            {
                void * t322 = gm_l0;
                void * t323 = GM_LD(void *, (((uint8_t *)t322) + 0));
                gm_tl874t2atnlh3 t324 = gm_l4;
                void * t325 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t323, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t326 = 0;
                F.ret = t325;
                goto gm_fret;
            }
            goto ix306;
ie306:;
ix306:;
        }
        goto ix303;
ie303:;
        {
            uint8_t t328 = 0;
            uint8_t t329;
            uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t332 != ((uint8_t)2ULL)) goto mn331;
            t329 = ((uint8_t)1);
            goto me330;
mn331:;
            t329 = ((uint8_t)0);
            goto me330;
mn333:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 194, 19);
me330:;
            if (!t329) {
                uint8_t t334 = 0;
                uint8_t t335 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
                if (t335) {
                    uint8_t t336 = gm_l16;
                    t334 = t336;
                } else t334 = 0;
                t328 = t334;
            } else t328 = 1;
            gm_l24 = t328;
        }
        {
            uint8_t t338 = gm_l24;
            uint8_t t339 = (uint8_t)!t338;
            if (!t339) goto ie337;
            {
                gm_b16_8 t341;
                uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t347) { case ((uint8_t)0ULL): goto ma344; case ((uint8_t)1ULL): goto ma344; case ((uint8_t)2ULL): goto ma345; case ((uint8_t)3ULL): goto ma345; case ((uint8_t)4ULL): goto ma343; case ((uint8_t)5ULL): goto ma345; case ((uint8_t)6ULL): goto ma345; case ((uint8_t)7ULL): goto ma345; case ((uint8_t)8ULL): goto ma345; case ((uint8_t)9ULL): goto ma345; case ((uint8_t)10ULL): goto ma345; case ((uint8_t)11ULL): goto ma345; case ((uint8_t)12ULL): goto ma345; case ((uint8_t)13ULL): goto ma345; case ((uint8_t)14ULL): goto ma345; case ((uint8_t)15ULL): goto ma345; case ((uint8_t)16ULL): goto ma345; case ((uint8_t)17ULL): goto ma345; case ((uint8_t)18ULL): goto ma345; case ((uint8_t)19ULL): goto ma345; case ((uint8_t)20ULL): goto ma345; case ((uint8_t)21ULL): goto ma345; default: goto ma345; }
ma343:;
                uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t349 != ((uint8_t)4ULL)) goto mn348;
                gm_b16_8 t350;
                GM_ST(void *, (uint8_t *)&t350, (void *)(gm_sjsojd1o1489e));
                GM_ST(uintptr_t, (uint8_t *)&t350 + 8, (uintptr_t)(33));
                t341 = t350;
                goto me342;
mn348:;
ma344:;
                uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t354 != ((uint8_t)0ULL)) goto pa353;
                goto po352;
pa353:;
                uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t356 != ((uint8_t)1ULL)) goto pa355;
                goto po352;
pa355:;
                goto mn351;
po352:;
                gm_b16_8 t357;
                uint8_t t359 = gm_l21;
                if (!t359) goto ie358;
                gm_b16_8 t361;
                GM_ST(void *, (uint8_t *)&t361, (void *)(gm_sr7qn5gtrg5md));
                GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(113));
                gm_b16_8 t362 = t361;
                t357 = t362;
                goto ix358;
ie358:;
                gm_b16_8 t364 = gm_l22;
                gm_tnpifv20irnph t365;
                GM_ST(gm_b16_8, ((uint8_t *)&t365 + 0), t364);
                gm_tabb710d9mq2e t366;
                gm_f_format_jn4ifp1((void *)&t366, (void *)NULL, cx, NULL, t365);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t367 = t366;
                if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 8, gm_srci09ctgf0djoe, 201, 21);
                gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&t367) + 8));
                gm_b16_8 t369 = t368;
                gm_b16_8 t370 = t369;
                gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)&t370));
                t357 = t371;
ix358:;
                t341 = t357;
                goto me342;
mn351:;
ma345:;
                gm_b16_8 t373;
                GM_ST(void *, (uint8_t *)&t373, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(0));
                t341 = t373;
                goto me342;
mn372:;
ma346:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 196, 28);
me342:;
                gm_l25 = t341;
            }
            {
                void * t374 = gm_l0;
                gm_t88rcucg1b7uk t375 = gm_l9;
                gm_b16_8 t376 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t374, t375);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t376;
            }
            {
                void * t377 = gm_l0;
                gm_t88rcucg1b7uk t378 = gm_l10;
                gm_b16_8 t379 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t377, t378);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t379;
            }
            {
                void * t380 = gm_l0;
                gm_tl874t2atnlh3 t381 = gm_l4;
                gm_tud7oovfvnf43 t382;
                gm_b16_8 t383 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t382 + 0, t383);
                void * t384 = gm_l0;
                gm_t88rcucg1b7uk t385 = gm_l17;
                gm_b16_8 t386 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t384, t385);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t382 + 16, t386);
                gm_b16_8 t387 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t382 + 32, t387);
                gm_b16_8 t388 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t382 + 48, t388);
                gm_b16_8 t389 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t382 + 64, t389);
                gm_tabb710d9mq2e t390;
                gm_f_format_p0j9lf7((void *)&t390, (void *)NULL, cx, NULL, &t382);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t391 = t390;
                if (((((uint8_t *)&t391))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t391) + 8, gm_srci09ctgf0djoe, 207, 28);
                gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)&t391) + 8));
                gm_b16_8 t393 = t392;
                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                gm_b0_1 t395 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t380, t381, t394);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t396 = gm_l0;
                void * t397 = GM_LD(void *, (((uint8_t *)t396) + 0));
                gm_tl874t2atnlh3 t398 = gm_l4;
                void * t399 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t397, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t400 = 0;
                F.ret = t399;
                goto gm_fret;
            }
            goto ix337;
ie337:;
ix337:;
        }
        {
            void * t402 = gm_l0;
            void * t403 = (void *)(((uint8_t *)t402) + 96);
            void * t404 = gm_l5;
            gm_t88rcucg1b7uk t405 = gm_l19;
            gm_t88rcucg1b7uk t406 = gm_l18;
            gm_t9qkh0rsooc8d t407 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t403, t404, t405, t406);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t408 = t407;
            uint8_t t409;
            if (!((((uint8_t *)&t408))[0] != 0)) goto mn411;
            t409 = ((uint8_t)1);
            goto me410;
mn411:;
            t409 = ((uint8_t)0);
            goto me410;
mn412:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 210, 12);
me410:;
            if (!t409) goto ie401;
            {
                void * t414 = gm_l0;
                void * t415 = gm_l0;
                void * t416 = (void *)(((uint8_t *)t415) + 96);
                void * t417 = gm_l0;
                void * t418 = GM_LD(void *, (((uint8_t *)t417) + 0));
                gm_t88rcucg1b7uk t419 = gm_l19;
                gm_t88rcucg1b7uk t420 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t416, t418, t419);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t421 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t414, t420);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t421;
            }
            {
                void * t422 = gm_l0;
                gm_tl874t2atnlh3 t423 = gm_l4;
                gm_t0uuhaljsf410 t424;
                gm_b16_8 t425 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t424 + 0, t425);
                void * t426 = gm_l0;
                gm_t88rcucg1b7uk t427 = gm_l17;
                gm_b16_8 t428 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t426, t427);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t424 + 16, t428);
                void * t429 = gm_l0;
                gm_t88rcucg1b7uk t430 = gm_l18;
                gm_b16_8 t431 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t429, t430);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t424 + 32, t431);
                gm_b16_8 t432 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t424 + 48, t432);
                gm_tabb710d9mq2e t433;
                gm_f_format_davqh84((void *)&t433, (void *)NULL, cx, NULL, &t424);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t434 = t433;
                if (((((uint8_t *)&t434))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t434) + 8, gm_srci09ctgf0djoe, 212, 28);
                gm_b16_8 t435 = GM_LD(gm_b16_8, (((uint8_t *)&t434) + 8));
                gm_b16_8 t436 = t435;
                gm_b16_8 t437 = GM_LD(gm_b16_8, ((uint8_t *)&t436));
                gm_b0_1 t438 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t422, t423, t437);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t439 = gm_l0;
                void * t440 = GM_LD(void *, (((uint8_t *)t439) + 0));
                gm_tl874t2atnlh3 t441 = gm_l4;
                void * t442 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t440, t441);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t443 = 0;
                F.ret = t442;
                goto gm_fret;
            }
            goto ix401;
ie401:;
ix401:;
        }
ix303:;
ix257:;
    }
    {
        uint8_t t445 = gm_l21;
        if (!t445) goto ie444;
        {
            gm_b16_8 t447;
            uint8_t t454 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t454) { case ((uint8_t)0ULL): goto ma449; case ((uint8_t)1ULL): goto ma450; case ((uint8_t)2ULL): goto ma451; case ((uint8_t)3ULL): goto ma452; case ((uint8_t)4ULL): goto ma452; case ((uint8_t)5ULL): goto ma452; case ((uint8_t)6ULL): goto ma452; case ((uint8_t)7ULL): goto ma452; case ((uint8_t)8ULL): goto ma452; case ((uint8_t)9ULL): goto ma452; case ((uint8_t)10ULL): goto ma452; case ((uint8_t)11ULL): goto ma452; case ((uint8_t)12ULL): goto ma452; case ((uint8_t)13ULL): goto ma452; case ((uint8_t)14ULL): goto ma452; case ((uint8_t)15ULL): goto ma452; case ((uint8_t)16ULL): goto ma452; case ((uint8_t)17ULL): goto ma452; case ((uint8_t)18ULL): goto ma452; case ((uint8_t)19ULL): goto ma452; case ((uint8_t)20ULL): goto ma452; case ((uint8_t)21ULL): goto ma452; default: goto ma452; }
ma449:;
            uint8_t t456 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t456 != ((uint8_t)0ULL)) goto mn455;
            gm_b16_8 t457;
            GM_ST(void *, (uint8_t *)&t457, (void *)(gm_s26md5sqfb6u7));
            GM_ST(uintptr_t, (uint8_t *)&t457 + 8, (uintptr_t)(6));
            t447 = t457;
            goto me448;
mn455:;
ma450:;
            uint8_t t459 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t459 != ((uint8_t)1ULL)) goto mn458;
            gm_b16_8 t460;
            GM_ST(void *, (uint8_t *)&t460, (void *)(gm_s7sn5glap1gkf));
            GM_ST(uintptr_t, (uint8_t *)&t460 + 8, (uintptr_t)(6));
            t447 = t460;
            goto me448;
mn458:;
ma451:;
            uint8_t t462 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t462 != ((uint8_t)2ULL)) goto mn461;
            gm_b16_8 t463;
            uint8_t t465 = gm_l20;
            if (!t465) goto ie464;
            gm_b16_8 t467;
            GM_ST(void *, (uint8_t *)&t467, (void *)(gm_su3gef13fgq4c));
            GM_ST(uintptr_t, (uint8_t *)&t467 + 8, (uintptr_t)(6));
            gm_b16_8 t468 = t467;
            t463 = t468;
            goto ix464;
ie464:;
            gm_b16_8 t470;
            GM_ST(void *, (uint8_t *)&t470, (void *)(gm_sdib2k2ql9mgl));
            GM_ST(uintptr_t, (uint8_t *)&t470 + 8, (uintptr_t)(8));
            gm_b16_8 t471 = t470;
            t463 = t471;
ix464:;
            t447 = t463;
            goto me448;
mn461:;
ma452:;
            gm_b16_8 t473;
            uint8_t t475 = gm_l20;
            if (!t475) goto ie474;
            gm_b16_8 t477;
            GM_ST(void *, (uint8_t *)&t477, (void *)(gm_sq5sjasaqn114));
            GM_ST(uintptr_t, (uint8_t *)&t477 + 8, (uintptr_t)(6));
            gm_b16_8 t478 = t477;
            t473 = t478;
            goto ix474;
ie474:;
            gm_b16_8 t480;
            GM_ST(void *, (uint8_t *)&t480, (void *)(gm_sd7vgd2pm42gk));
            GM_ST(uintptr_t, (uint8_t *)&t480 + 8, (uintptr_t)(13));
            gm_b16_8 t481 = t480;
            t473 = t481;
ix474:;
            t447 = t473;
            goto me448;
mn472:;
ma453:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 217, 24);
me448:;
            gm_l29 = t447;
        }
        {
            uint8_t t482 = 0;
            uint8_t t483 = gm_l20;
            uint8_t t484 = (uint8_t)!t483;
            if (t484) {
                uint8_t t485 = gm_l16;
                uint8_t t486 = (uint8_t)!t485;
                t482 = t486;
            } else t482 = 0;
            gm_l30 = t482;
        }
        {
        }
        {
            void * t487 = gm_l0;
            gm_b16_8 t488 = gm_l29;
            {
                void * t490 = gm_l6;
                void * t491 = gm_l7;
                gm_t5fru0lp11n7t t492;
                GM_ST(void *, ((uint8_t *)&t492 + 0), t490);
                GM_ST(void *, ((uint8_t *)&t492 + 8), t491);
                gm_l31 = t492;
            }
            void * t493 = ((uint8_t *)&gm_l31);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 225, 38);
            gm_b16_8 t494;
            GM_ST(void *, (uint8_t *)&t494, (void *)(((uint8_t *)t493 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t494 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t495 = t494;
            uint8_t t496 = gm_l30;
            gm_tl874t2atnlh3 t497 = gm_l4;
            void * t498 = gm_f_complex_call_8so3bss((void *)NULL, cx, NULL, t487, t488, t495, t496, t497);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t499 = 0;
            F.ret = t498;
            goto gm_fret;
        }
        goto ix444;
ie444:;
ix444:;
    }
    {
        void * t500 = gm_l0;
        void * t501 = GM_LD(void *, (((uint8_t *)t500) + 0));
        void * t502 = (void *)(((uint8_t *)t501) + 8);
        gm_t88rcucg1b7uk t503 = gm_l17;
        gm_tb7u19loo3j87 t504;
        gm_f_math_vector_parts_49c7865((void *)&t504, t502, GM_CONV(gm_csguankko4lmhd, t503));
        gm_tb7u19loo3j87 t505 = t504;
        if (!((((uint8_t *)&t505))[16] != 0)) gm_trap("unwrap of null", gm_srci09ctgf0djoe, 227, 10);
        gm_tlcm4lvu3g1a8 t506 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t505));
        gm_tlcm4lvu3g1a8 t507 = t506;
        uint64_t t508 = GM_LD(uint64_t, (((uint8_t *)&t507) + 0));
        gm_l32 = t508;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t509 = gm_l0;
        void * t510 = gm_l6;
        void * t511 = (void *)((uint8_t *)&F.l33);
        void * t512 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t509, t510, t511);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l34 = t512;
    }
    {
        void * t513 = gm_l0;
        void * t514 = gm_l7;
        void * t515 = (void *)((uint8_t *)&F.l33);
        void * t516 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t513, t514, t515);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l35 = t516;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l38 = ((uint64_t)0ULL);
        }
        {
            uint64_t t518 = gm_l32;
            gm_l37 = t518;
        }
        {
            for (;;) {
                uint64_t t520 = gm_l38;
                uint64_t t521 = gm_l37;
                uint8_t t522 = (t520 < t521);
                if (!t522) goto lb519;
                {
                    uint64_t t525 = gm_l38;
                    uint64_t t526 = t525;
                    uint64_t t529 = GM_LD(uint64_t, ((uint8_t *)&t526));
                    gm_l39 = t529;
                    {
                        void * t531;
                        uint8_t t533 = gm_l16;
                        if (!t533) goto ie532;
                        void * t535 = gm_l0;
                        void * t536 = gm_l34;
                        gm_t88rcucg1b7uk t537 = gm_l18;
                        uint64_t t538 = gm_l39;
                        void * t539 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t535, t536, t537, t538);
                        if (gm_unw.kind) {
                            goto gm_ll523;
                        }
                        void * t540 = t539;
                        t531 = t540;
                        goto ix532;
ie532:;
                        void * t542 = gm_l34;
                        void * t543 = t542;
                        t531 = t543;
ix532:;
                        gm_l40 = t531;
                    }
                    {
                        void * t544;
                        uint8_t t546 = 0;
                        uint8_t t547 = gm_l16;
                        uint8_t t548 = (uint8_t)!t547;
                        if (!t548) {
                            uint8_t t549 = gm_l20;
                            t546 = t549;
                        } else t546 = 1;
                        if (!t546) goto ie545;
                        void * t551 = gm_l0;
                        void * t552 = gm_l35;
                        gm_t88rcucg1b7uk t553 = gm_l18;
                        uint64_t t554 = gm_l39;
                        void * t555 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t551, t552, t553, t554);
                        if (gm_unw.kind) {
                            goto gm_ll523;
                        }
                        void * t556 = t555;
                        t544 = t556;
                        goto ix545;
ie545:;
                        void * t558 = gm_l35;
                        void * t559 = t558;
                        t544 = t559;
ix545:;
                        gm_l41 = t544;
                    }
                    {
                        void * t560 = gm_l0;
                        void * t561 = GM_LD(void *, (((uint8_t *)t560) + 0));
                        gm_tl874t2atnlh3 t562 = gm_l4;
                        void * t563 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t561, t562);
                        if (gm_unw.kind) {
                            goto gm_ll523;
                        }
                        gm_l42 = t563;
                    }
                    {
                        uint8_t t565 = gm_l23;
                        if (!t565) goto ie564;
                        {
                            void * t567 = gm_l0;
                            void * t568 = (void *)(((uint8_t *)t567) + 320);
                            gm_t91vm1g0k2rpm t569;
                            memset((uint8_t *)&t569 + 34, 0, 6);
                            gm_t88rcucg1b7uk t570 = gm_l18;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t569 + 0, t570);
                            gm_tl874t2atnlh3 t571 = gm_l4;
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t569 + 4, t571);
                            gm_b16_8 t572 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t569 + 16, t572);
                            GM_ST(uint8_t, (uint8_t *)&t569 + 32, ((uint8_t)1));
                            GM_ST(uint8_t, (uint8_t *)&t569 + 33, ((uint8_t)0));
                            gm_tpm220j5qhjke t573;
                            GM_ST(uint8_t, (uint8_t *)&t573 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t573 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t573 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t573 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t573 + 8), t569);
                            gm_tftb1odc739t5 t574 = gm_f_push_7koe9lk(t568, &t573);
                            gm_tftb1odc739t5 t575 = t574;
                            if (((((uint8_t *)&t575))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t575) + 1, gm_srci09ctgf0djoe, 237, 13);
                            gm_b0_1 t576 = GM_LD(gm_b0_1, (((uint8_t *)&t575) + 1));
                        }
                        {
                            gm_ts6oqppjh0ba3 t577;
                            uint8_t t579 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            if (t579 != ((uint8_t)3ULL)) goto ie578;
                            gm_t9qkh0rsooc8d t580 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                            gm_l43 = t580;
                            gm_t9qkh0rsooc8d t582 = gm_l43;
                            gm_ts6oqppjh0ba3 t583;
                            GM_ST(uint8_t, ((uint8_t *)&t583 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t583 + 1), t582);
                            gm_ts6oqppjh0ba3 t584 = t583;
                            t577 = t584;
                            goto ix578;
ie578:;
                            gm_ts6oqppjh0ba3 t586;
                            GM_ST(uint8_t, (uint8_t *)&t586 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t586 + 0), ((uint8_t)6ULL));
                            gm_ts6oqppjh0ba3 t587 = t586;
                            t577 = t587;
ix578:;
                            gm_l44 = t577;
                        }
                        {
                            gm_t88rcucg1b7uk t588 = gm_l18;
                            gm_trl9d7m1k5lbd t589;
                            memset((uint8_t *)&t589 + 2, 0, 6);
                            gm_ts6oqppjh0ba3 t590 = gm_l44;
                            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t589 + 0, t590);
                            void * t591 = gm_l40;
                            GM_ST(void *, (uint8_t *)&t589 + 8, t591);
                            void * t592 = gm_l41;
                            GM_ST(void *, (uint8_t *)&t589 + 16, t592);
                            gm_teti411eujvf4 t593;
                            memset(&t593, 0, sizeof t593);
                            GM_ST(uint8_t, ((uint8_t *)&t593 + 0), ((uint8_t)30ULL));
                            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t593 + 8), t589);
                            gm_tl874t2atnlh3 t594 = gm_l4;
                            void * t595 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t588, &t593, t594);
                            if (gm_unw.kind) {
                                goto gm_ll523;
                            }
                            gm_l42 = t595;
                        }
                        goto ix564;
ie564:;
                        {
                            void * t597 = gm_l0;
                            gm_ts6oqppjh0ba3 t598 = gm_l1;
                            void * t599 = gm_l40;
                            void * t600 = gm_l41;
                            gm_tl874t2atnlh3 t601 = gm_l4;
                            void * t602 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t597, t598, t599, t600, t601);
                            if (gm_unw.kind) {
                                goto gm_ll523;
                            }
                            gm_l42 = t602;
                        }
ix564:;
                    }
                    {
                        void * t604 = gm_l5;
                        void * t605 = gm_l42;
                        gm_t88rcucg1b7uk t606 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t605) + 0));
                        uint8_t t607 = gm_f_is_error_8k27gd1(t604, GM_CONV(gm_csguankko4lmhd, t606));
                        if (!t607) goto ie603;
                        {
                            void * t609 = gm_l42;
                            uint8_t t610 = 0;
                            F.ret = t609;
                            goto gm_fret;
                        }
                        goto ix603;
ie603:;
ix603:;
                    }
                    {
                        void * t611 = (void *)((uint8_t *)&F.l36);
                        void * t612 = gm_l42;
                        gm_tftb1odc739t5 t613 = gm_f_push_d943vgp_2(t611, &(*(gm_t69m56meqefs8 *)((uint8_t *)t612)));
                        gm_tftb1odc739t5 t614 = t613;
                        if (((((uint8_t *)&t614))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t614) + 1, gm_srci09ctgf0djoe, 244, 9);
                        gm_b0_1 t615 = GM_LD(gm_b0_1, (((uint8_t *)&t614) + 1));
                    }
                    goto me527;
mn528:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 232, 5);
me527:;
                }
lc519:;
                {
                    uint64_t t617 = gm_l38;
                    uint64_t r618; bool o619 = __builtin_add_overflow(t617, ((uint64_t)1ULL), &r618);
                    uint64_t t620 = r618;
                    gm_l38 = t620;
                }
            }
gm_ll523:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb519; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc519; }
            }
            goto gm_lad;
lb519:;
        }
    }
    {
        gm_t88rcucg1b7uk t621 = gm_l17;
        gm_b16_8 t622 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
        gm_teti411eujvf4 t623;
        memset(&t623, 0, sizeof t623);
        GM_ST(uint8_t, ((uint8_t *)&t623 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t623 + 8), t622);
        gm_tl874t2atnlh3 t624 = gm_l4;
        void * t625 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t621, &t623, t624);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l45 = t625;
    }
    {
        gm_t88rcucg1b7uk t626 = gm_l17;
        gm_b16_8 t627 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
        void * t628 = gm_l45;
        gm_tl874t2atnlh3 t629 = gm_l4;
        void * t630 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t626, t627, t628, t629);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t631 = 0;
        F.ret = t630;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_compound_number_elem_6d1d2ni(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_compound_number_elem_6d1d2ni F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tlcm4lvu3g1a8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_tb7u19loo3j87 t7;
        gm_f_math_vector_parts_49c7865((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie2;
        gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t8));
        gm_l2 = t9;
        {
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
            gm_tkn42u7na3t81 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[4] = 1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_tkn42u7na3t81 t17 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = 0;
        F.ret = t17;
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
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_compound_number_unary_4h478fs F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tn3r4rvlh0rsv gm_l6; /* <temporary array> */
    gm_tlcm4lvu3g1a8 gm_l7; /* p */
    gm_t9qkh0rsooc8d gm_l8; /* top */
    uint8_t gm_l9; /* int_only */
    uint8_t gm_l10; /* signed_only */
    gm_b16_8 gm_l11; /* s */
    void * gm_l13; /* x */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* arr */
    GM_STACK_CHECK(&F, 1542u);
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
        void * t6 = gm_l0;
        void * t7 = gm_l2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        uint8_t t9 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t9) goto ie5;
        {
            {
                void * t12 = gm_l0;
                void * t13 = gm_l2;
                gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
                gm_tl874t2atnlh3 t15 = gm_l3;
                gm_t88rcucg1b7uk t16 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t12, t14, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 96);
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = gm_l2;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
        gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t23;
    }
    {
        void * t25 = gm_l4;
        gm_t88rcucg1b7uk t26 = gm_l5;
        uint8_t t27 = gm_f_is_error_8k27gd1(t25, GM_CONV(gm_csguankko4lmhd, t26));
        if (!t27) goto ie24;
        {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            gm_tl874t2atnlh3 t31 = gm_l3;
            void * t32 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t34 = gm_l4;
        gm_t88rcucg1b7uk t35 = gm_l5;
        gm_tb7u19loo3j87 t36;
        gm_f_math_vector_parts_49c7865((void *)&t36, t34, GM_CONV(gm_csguankko4lmhd, t35));
        gm_tb7u19loo3j87 t37 = t36;
        gm_tlcm4lvu3g1a8 t38;
        if (((((uint8_t *)&t37))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t39 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t37) + 0));
            t38 = t39;
        } else {
            {
                uint8_t t42 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                if (!t42) goto ie41;
                {
                }
                {
                    void * t44 = gm_l0;
                    gm_b16_8 t45;
                    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sprospqi1g16o));
                    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(6));
                    {
                        void * t47 = gm_l2;
                        gm_tn3r4rvlh0rsv t48;
                        GM_ST(void *, ((uint8_t *)&t48 + 0), t47);
                        gm_l6 = t48;
                    }
                    void * t49 = ((uint8_t *)&gm_l6);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 257, 58);
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t49 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t51 = t50;
                    gm_tl874t2atnlh3 t52 = gm_l3;
                    void * t53 = gm_f_complex_call_8so3bss((void *)NULL, cx, NULL, t44, t45, t51, ((uint8_t)0), t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t54 = 0;
                    F.ret = t53;
                    goto gm_fret;
                }
                goto ix41;
ie41:;
ix41:;
            }
            {
                void * t55 = gm_l0;
                gm_tl874t2atnlh3 t56 = gm_l3;
                gm_b16_8 t57;
                uint8_t t59 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
                if (!t59) goto ie58;
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sa831sfid0hf3));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(1));
                gm_b16_8 t62 = t61;
                t57 = t62;
                goto ix58;
ie58:;
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_scvhpklutvpjg));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(3));
                gm_b16_8 t65 = t64;
                t57 = t65;
ix58:;
                gm_tnpifv20irnph t66;
                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t57);
                gm_tabb710d9mq2e t67;
                gm_f_format_gdra5k8((void *)&t67, (void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t68 = t67;
                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srci09ctgf0djoe, 258, 24);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                gm_b16_8 t70 = t69;
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t55, t56, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            gm_tl874t2atnlh3 t75 = gm_l3;
            void * t76 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        gm_l7 = t38;
    }
    {
        gm_t9qkh0rsooc8d t78;
        GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)0ULL));
        gm_l8 = t78;
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        gm_l10 = ((uint8_t)0);
    }
    {
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(1));
        gm_l11 = t79;
    }
    {
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t85) { case ((uint8_t)0ULL): goto ma81; case ((uint8_t)1ULL): goto ma82; case ((uint8_t)2ULL): goto ma83; case ((uint8_t)3ULL): goto ma83; case ((uint8_t)4ULL): goto ma83; case ((uint8_t)5ULL): goto ma83; default: goto ma83; }
ma81:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t87 != ((uint8_t)0ULL)) goto mn86;
        {
            gm_l10 = ((uint8_t)1);
        }
        goto me80;
mn86:;
ma82:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t90 != ((uint8_t)1ULL)) goto mn89;
        {
            gm_t9qkh0rsooc8d t92;
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)1ULL));
            gm_l8 = t92;
        }
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(1));
            gm_l11 = t93;
        }
        goto me80;
mn89:;
ma83:;
        {
            void * t96 = gm_l0;
            gm_tl874t2atnlh3 t97 = gm_l3;
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l5;
            gm_b16_8 t100 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t101;
            GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
            gm_tabb710d9mq2e t102;
            gm_f_format_6up4rm5((void *)&t102, (void *)NULL, cx, NULL, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t103 = t102;
            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_srci09ctgf0djoe, 273, 28);
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
            gm_b16_8 t105 = t104;
            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
            gm_b0_1 t107 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t96, t97, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t108 = gm_l0;
            void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
            gm_tl874t2atnlh3 t110 = gm_l3;
            void * t111 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t109, t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t112 = 0;
            F.ret = t111;
            goto gm_fret;
        }
        goto me80;
mn94:;
ma84:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 265, 5);
me80:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t113 = gm_l0;
        void * t114 = gm_l2;
        void * t115 = (void *)((uint8_t *)&F.l12);
        void * t116 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t113, t114, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t116;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
            gm_l15 = t118;
        }
        {
            for (;;) {
                uint64_t t120 = gm_l16;
                uint64_t t121 = gm_l15;
                uint8_t t122 = (t120 < t121);
                if (!t122) goto lb119;
                {
                    uint64_t t125 = gm_l16;
                    uint64_t t126 = t125;
                    uint64_t t129 = GM_LD(uint64_t, ((uint8_t *)&t126));
                    gm_l17 = t129;
                    {
                        void * t131 = gm_l0;
                        void * t132 = (void *)(((uint8_t *)t131) + 320);
                        gm_t91vm1g0k2rpm t133;
                        memset((uint8_t *)&t133 + 34, 0, 6);
                        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t133 + 0, t134);
                        gm_tl874t2atnlh3 t135 = gm_l3;
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t133 + 4, t135);
                        gm_b16_8 t136 = gm_l11;
                        GM_ST(gm_b16_8, (uint8_t *)&t133 + 16, t136);
                        uint8_t t137 = gm_l9;
                        GM_ST(uint8_t, (uint8_t *)&t133 + 32, t137);
                        uint8_t t138 = gm_l10;
                        GM_ST(uint8_t, (uint8_t *)&t133 + 33, t138);
                        gm_tpm220j5qhjke t139;
                        GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t139 + 8), t133);
                        gm_tftb1odc739t5 t140 = gm_f_push_7koe9lk(t132, &t139);
                        gm_tftb1odc739t5 t141 = t140;
                        if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srci09ctgf0djoe, 281, 9);
                        gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
                    }
                    {
                        void * t143 = (void *)((uint8_t *)&F.l14);
                        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        gm_t9qkh0rsooc8d t145 = gm_l8;
                        void * t146 = gm_l0;
                        void * t147 = gm_l13;
                        gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t149 = gm_l17;
                        void * t150 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t146, t147, t148, t149);
                        if (gm_unw.kind) {
                            goto gm_ll123;
                        }
                        gm_t8ghgl2jj0qha t151;
                        GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t151 + 0), t145);
                        GM_ST(void *, ((uint8_t *)&t151 + 8), t150);
                        gm_teti411eujvf4 t152;
                        memset(&t152, 0, sizeof t152);
                        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)29ULL));
                        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t152 + 8), t151);
                        gm_tl874t2atnlh3 t153 = gm_l3;
                        void * t154 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t144, &t152, t153);
                        if (gm_unw.kind) {
                            goto gm_ll123;
                        }
                        gm_tftb1odc739t5 t155 = gm_f_push_d943vgp_2(t143, &(*(gm_t69m56meqefs8 *)((uint8_t *)t154)));
                        gm_tftb1odc739t5 t156 = t155;
                        if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_srci09ctgf0djoe, 282, 9);
                        gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                    }
                    goto me127;
mn128:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 280, 5);
me127:;
                }
lc119:;
                {
                    uint64_t t159 = gm_l16;
                    uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                    uint64_t t162 = r160;
                    gm_l16 = t162;
                }
            }
gm_ll123:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb119; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc119; }
            }
            goto gm_lad;
lb119:;
        }
    }
    {
        gm_t88rcucg1b7uk t163 = gm_l5;
        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_teti411eujvf4 t165;
        memset(&t165, 0, sizeof t165);
        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t165 + 8), t164);
        gm_tl874t2atnlh3 t166 = gm_l3;
        void * t167 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t163, &t165, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t167;
    }
    {
        gm_t88rcucg1b7uk t168 = gm_l5;
        gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        void * t170 = gm_l18;
        gm_tl874t2atnlh3 t171 = gm_l3;
        void * t172 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t168, t169, t170, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t173 = 0;
        F.ret = t172;
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
__attribute__((aligned(4))) void * gm_f_core_call_values_qq2la05(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_core_call_values_qq2la05 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* v */
    gm_t88rcucg1b7uk gm_l9; /* vt */
    gm_b16_8 gm_l10; /* local_name */
    uint32_t gm_l11; /* local */
    gm_t08olq97rscn3 gm_l12; /* id */
    void * gm_l14; /* call */
    GM_STACK_CHECK(&F, 1542u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_ir0h3tc_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_6oos54v((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t11 * 8));
                    void * t14 = t13;
                    void * t17 = GM_LD(void *, ((uint8_t *)&t14));
                    gm_l8 = t17;
                    {
                        void * t19 = gm_l0;
                        void * t20 = (void *)(((uint8_t *)t19) + 96);
                        void * t21 = gm_l0;
                        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
                        void * t23 = gm_l8;
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
                        gm_t88rcucg1b7uk t25 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t20, t22, t24);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l9 = t25;
                    }
                    {
                        void * t26 = gm_l0;
                        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 16) + 0) + 8);
                        gm_t4ce0jd3tdro4 t28;
                        GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t27);
                        gm_tabb710d9mq2e t29;
                        gm_f_format_uubl3s3((void *)&t29, (void *)NULL, cx, NULL, t28);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tabb710d9mq2e t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srci09ctgf0djoe, 319, 23);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                        gm_l10 = t31;
                    }
                    {
                        void * t32 = gm_l0;
                        gm_b16_8 t33 = gm_l10;
                        gm_b16_8 t34 = t33;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                        gm_t88rcucg1b7uk t36 = gm_l9;
                        void * t37 = gm_l8;
                        gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
                        uint32_t t39 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t32, t35, t36, ((uint8_t)0), t38);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l11 = t39;
                    }
                    {
                        void * t40 = (void *)((uint8_t *)&F.l4);
                        uint32_t t41 = gm_l11;
                        void * t42 = gm_l8;
                        gm_ttggq7ng4kp3p t43;
                        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t41);
                        GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
                        gm_trcmmd12cefb1 t44;
                        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t44 + 8), t43);
                        gm_tftb1odc739t5 t45 = gm_f_push_d6c76ud_2(t40, &t44);
                        gm_tftb1odc739t5 t46 = t45;
                        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srci09ctgf0djoe, 321, 9);
                        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                    }
                    {
                        void * t48 = gm_l0;
                        gm_b16_8 t49 = gm_l10;
                        gm_b16_8 t50 = t49;
                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                        uint32_t t52 = gm_l11;
                        gm_t930q5l38hdap t53;
                        memset(&t53, 0, sizeof t53);
                        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t53 + 8), t52);
                        void * t54 = gm_l8;
                        gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
                        gm_b0_1 t56 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t48, t51, &t53, t55);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        memset((uint8_t *)&gm_l12 + 28, 0, 4);
                        gm_b16_8 t57 = gm_l10;
                        gm_b16_8 t58 = t57;
                        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                        GM_ST(gm_b16_8, (uint8_t *)&gm_l12 + 0, t59);
                        void * t60 = gm_l8;
                        gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&gm_l12 + 16, t61);
                    }
                    {
                        memset((uint8_t *)&F.l13 + 4, 0, 4);
                        memset((uint8_t *)&F.l13 + 84, 0, 4);
                        GM_ST(uint32_t, (uint8_t *)&F.l13 + 0, ((uint32_t)4294967295ULL));
                        gm_t08olq97rscn3 t62 = gm_l12;
                        gm_t2gtbh39ptekd t63;
                        GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t63 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t63 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t63 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t63 + 8), t62);
                        GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&F.l13 + 8, t63);
                        void * t64 = gm_l8;
                        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l13 + 72, t65);
                    }
                    {
                        void * t66 = (void *)((uint8_t *)&F.l5);
                        gm_t7te2tdpebbt4 t67;
                        memset((uint8_t *)&t67 + 140, 0, 4);
                        gm_tc3oht4ksth9d t68 = F.l13;
                        gm_t22mtv3sd9prf t69;
                        memset(&t69, 0, sizeof t69);
                        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t69 + 8), t68);
                        GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t67 + 0, t69);
                        void * t70 = gm_l8;
                        gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t67 + 128, t71);
                        gm_tftb1odc739t5 t72 = gm_f_push_jis5lhu(t66, &t67);
                        gm_tftb1odc739t5 t73 = t72;
                        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srci09ctgf0djoe, 325, 9);
                        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 317, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t76 = gm_l7;
                    uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                    uint64_t t79 = r77;
                    gm_l7 = t79;
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
        void * t80 = gm_l0;
        uint32_t t81 = gm_l1;
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t83 = t82;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_tl874t2atnlh3 t85 = gm_l3;
        void * t86 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t80, t81, ((void *)0), t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t86;
    }
    {
        void * t87 = gm_l14;
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t87) + 0));
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        void * t90 = gm_l14;
        gm_tl874t2atnlh3 t91 = gm_l3;
        void * t92 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t88, t89, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t93 = 0;
        F.ret = t92;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_operand_509et87(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_describe_operand_509et87 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 96);
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_taari1s0m51a7 t8 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t3, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_taari1s0m51a7 t9 = t8;
        if (!((((uint8_t *)&t9))[1] != 0)) goto mn11;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sp66nadh171v3));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(18));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
        goto me10;
mn11:;
        if (!((((uint8_t *)&t9))[1] != 0)) goto mn15;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sf372rql24e3n));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(16));
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me10;
mn15:;
        if (!((((uint8_t *)&t9))[1] != 0)) goto mn19;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t20 != ((uint8_t)2ULL)) goto mn19;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfa8f3bk569gj));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(25));
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me10;
mn19:;
        if (((((uint8_t *)&t9))[1] != 0)) goto mn23;
        goto me10;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 27, 5);
me10:;
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 96);
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        gm_t88rcucg1b7uk t30 = gm_l1;
        gm_t88rcucg1b7uk t31 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t27, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t32 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t25, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_h4jcodi((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srci09ctgf0djoe, 33, 12);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        uint8_t t39 = 0;
        F.ret = t38;
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
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_imaginary_literal_6d41gfv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* l */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_t9qkh0rsooc8d gm_l5; /* suffix */
    gm_t88rcucg1b7uk gm_l6; /* part */
    uint32_t gm_l7; /* c */
    gm_tnqk2qt7j42o9 gm_l8; /* <temporary array> */
    GM_STACK_CHECK(&F, 554u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t6;
    }
    {
        void * t8 = gm_l1;
        if (!(((((uint8_t *)t8) + 96))[1] != 0)) goto ie7;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t8) + 96));
        gm_l5 = t9;
        {
            void * t11 = gm_l3;
            uint16_t t12;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            switch (t18) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma16; default: goto ma17; }
ma14:;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t20 != ((uint8_t)0ULL)) goto mn19;
            t12 = ((uint16_t)16ULL);
            goto me13;
mn19:;
ma15:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t22 != ((uint8_t)1ULL)) goto mn21;
            t12 = ((uint16_t)32ULL);
            goto me13;
mn21:;
ma16:;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t24 != ((uint8_t)2ULL)) goto mn23;
            t12 = ((uint16_t)64ULL);
            goto me13;
mn23:;
ma17:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 41, 35);
me13:;
            gm_t53at80pfdus3 t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t25 + 8), t12);
            gm_t88rcucg1b7uk t26 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t11, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t26;
        }
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s0rphlcbno91v));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(7));
            gm_t9805qtonbsag t31 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t32 = t31;
            if (!((((uint8_t *)&t32))[4] != 0)) goto ie27;
            uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)&t32));
            gm_l7 = t33;
            {
            }
            {
                void * t35 = gm_l0;
                void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                uint32_t t37 = gm_l7;
                {
                    gm_t88rcucg1b7uk t39 = gm_l6;
                    gm_tnqk2qt7j42o9 t40;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40 + 0), t39);
                    gm_l8 = t40;
                }
                void * t41 = ((uint8_t *)&gm_l8);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 46, 81);
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t41 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t43 = t42;
                gm_t88rcucg1b7uk t44 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t36, t37, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t44;
            }
            goto ix27;
ie27:;
ix27:;
        }
        goto ix7;
ie7:;
        {
            void * t46 = gm_l0;
            gm_t9qkh0rsooc8d t47;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t48 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t48;
        }
ix7:;
    }
    {
        void * t50 = gm_l0;
        uint8_t t51;
        if (((((((uint8_t *)t50) + 96) + 56))[8] != 0)) goto mn53;
        t51 = ((uint8_t)1);
        goto me52;
mn53:;
        t51 = ((uint8_t)0);
        goto me52;
mn54:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 50, 8);
me52:;
        if (!t51) goto ie49;
        void * t55 = gm_l3;
        gm_t88rcucg1b7uk t56 = gm_l4;
        uint8_t t57 = gm_f_is_error_8k27gd1(t55, GM_CONV(gm_csguankko4lmhd, t56));
        uint8_t t58 = (uint8_t)!t57;
        if (!t58) goto ie49;
        {
            {
                void * t61 = gm_l0;
                gm_t9qkh0rsooc8d t62;
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)2ULL));
                gm_t88rcucg1b7uk t63 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t61, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        gm_t88rcucg1b7uk t64 = gm_l4;
        gm_tngtuiuskn938 t65;
        gm_f_zero_9r6o7sj((void *)&t65, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t66 = gm_l1;
        gm_teti411eujvf4 t67;
        gm_f_untyped_complex_20agrmu((void *)&t67, (void *)NULL, cx, NULL, &t65, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t66) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t68 = gm_l2;
        void * t69 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t64, &t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t70 = 0;
        F.ret = t69;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_compound_number_kbipjl5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t0 */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 96);
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t3, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        uint8_t t8 = 0;
        uint8_t t9 = 0;
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_t88rcucg1b7uk t13 = gm_l2;
        uint8_t t14 = gm_f_is_math_vector_09mtuao(t12, GM_CONV(gm_csguankko4lmhd, t13));
        if (!t14) {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_t88rcucg1b7uk t17 = gm_l2;
            gm_tkn42u7na3t81 t18 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t19 = t18;
            uint8_t t20;
            if (((((uint8_t *)&t19))[4] != 0)) goto mn22;
            t20 = ((uint8_t)0);
            goto me21;
mn22:;
            t20 = ((uint8_t)1);
            goto me21;
mn23:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 15, 42);
me21:;
            t9 = t20;
        } else t9 = 1;
        if (!t9) {
            void * t24 = gm_l0;
            gm_t88rcucg1b7uk t25 = gm_l2;
            uint8_t t26 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t8 = t26;
        } else t8 = 1;
        uint8_t t27 = 0;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_literal_var_3sb5sti F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    uint32_t gm_l2; /* c */
    gm_tnqk2qt7j42o9 gm_l3; /* <temporary array> */
    GM_STACK_CHECK(&F, 325u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)2ULL)));
        if (!t3) goto ie2;
        void * t4 = gm_l0;
        uint8_t t5;
        if (((((((uint8_t *)t4) + 96) + 56))[8] != 0)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 56, 29);
me6:;
        if (!t5) goto ie2;
        {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s0rphlcbno91v));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
            gm_t9805qtonbsag t14 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t15 = t14;
            if (!((((uint8_t *)&t15))[4] != 0)) goto ie10;
            uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&t15));
            gm_l2 = t16;
            {
            }
            {
                void * t18 = gm_l0;
                uint8_t *t19 = ((((uint8_t *)t18) + 96) + 56);
                uint32_t t20 = gm_l2;
                void * t21 = gm_l0;
                void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
                uint32_t t23 = gm_l2;
                {
                    void * t25 = gm_l0;
                    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                    void * t27 = (void *)(((uint8_t *)t26) + 8);
                    gm_t53at80pfdus3 t28;
                    memset(&t28, 0, sizeof t28);
                    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)3ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t28 + 8), ((uint16_t)64ULL));
                    gm_t88rcucg1b7uk t29 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t27, &t28);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnqk2qt7j42o9 t30;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30 + 0), t29);
                    gm_l3 = t30;
                }
                void * t31 = ((uint8_t *)&gm_l3);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 58, 74);
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t31 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t33 = t32;
                gm_t88rcucg1b7uk t34 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t22, t23, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tplpbqf3duvn3 t35;
                GM_ST(uint32_t, ((uint8_t *)&t35 + 0), t20);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 4), t34);
                gm_thdmr9qicg35r t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 10, 0);
                GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)&t36), t35);
                ((uint8_t *)&t36)[8] = 1;
                GM_ST(gm_thdmr9qicg35r, t19, t36);
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)(((uint8_t *)t37) + 96);
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_t9qkh0rsooc8d t42 = gm_l1;
        gm_t88rcucg1b7uk t43 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t38, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = 0;
        F.ret = t43;
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
inline __attribute__((aligned(4))) void * gm_f_math_elem_iuerhch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3) {
    struct gm_fr_math_elem_iuerhch F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* index */
    GM_STACK_CHECK(&F, 365u);
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
        gm_t88rcucg1b7uk t5 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t6 = gm_l3;
        gm_tbci5ospk5nnn t7;
        gm_f_from_u64_ud13q5v((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t8 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t8 + 8), t7);
        void * t9 = gm_l1;
        gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
        void * t11 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t5, &t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t11;
    }
    {
        gm_t88rcucg1b7uk t12 = gm_l2;
        gm_tlcl4u558l6p5 t13;
        memset((uint8_t *)&t13 + 17, 0, 7);
        void * t14 = gm_l1;
        GM_ST(void *, (uint8_t *)&t13 + 0, t14);
        void * t15 = gm_l4;
        GM_ST(void *, (uint8_t *)&t13 + 8, t15);
        GM_ST(uint8_t, (uint8_t *)&t13 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t16 + 8), t13);
        void * t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 72));
        void * t19 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t12, &t16, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = 0;
        F.ret = t19;
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
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_math_vector_conversion_0cttjm0 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* target */
    void * gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* st */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* a */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* b */
    uint8_t gm_l10; /* both */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    void * gm_l13; /* v */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_tn3r4rvlh0rsv gm_l18; /* <temporary array> */
    void * gm_l19; /* arr */
    gm_b16_8 gm_l20; /* why */
    GM_STACK_CHECK(&F, 2161u);
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
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = gm_l1;
        uint8_t t15 = gm_f_is_math_vector_09mtuao(t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie12;
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l5;
        uint8_t t19 = gm_f_is_math_vector_09mtuao(t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie12;
        {
            uint8_t t22 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t24 = gm_l4;
        gm_t88rcucg1b7uk t25 = gm_l5;
        gm_t53at80pfdus3 t26;
        gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
        gm_t53at80pfdus3 t27 = t26;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t28 != ((uint8_t)11ULL)) goto ie23;
        uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)&t27) + 8) + 0));
        gm_l6 = t29;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t27) + 8) + 8));
        gm_l7 = t30;
        {
            void * t33 = gm_l4;
            gm_t88rcucg1b7uk t34 = gm_l1;
            gm_t53at80pfdus3 t35;
            gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            gm_t53at80pfdus3 t36 = t35;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            if (t37 != ((uint8_t)11ULL)) goto ie32;
            uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)&t36) + 8) + 0));
            gm_l8 = t38;
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 8));
            gm_l9 = t39;
            {
                gm_b0_1 t41;
            }
            {
                uint8_t t42 = 0;
                void * t43 = gm_l4;
                gm_t88rcucg1b7uk t44 = gm_l5;
                uint8_t t45 = gm_f_is_math_vector_09mtuao(t43, GM_CONV(gm_csguankko4lmhd, t44));
                if (t45) {
                    void * t46 = gm_l4;
                    gm_t88rcucg1b7uk t47 = gm_l1;
                    uint8_t t48 = gm_f_is_math_vector_09mtuao(t46, GM_CONV(gm_csguankko4lmhd, t47));
                    t42 = t48;
                } else t42 = 0;
                gm_l10 = t42;
            }
            {
                uint64_t t50 = gm_l6;
                uint64_t t51 = gm_l8;
                uint8_t t52 = ((t50 == t51));
                if (!t52) goto ie49;
                gm_t88rcucg1b7uk t53 = gm_l7;
                gm_t88rcucg1b7uk t54 = gm_l9;
                gm_t88rcucg1b7uk t55 = t53;
                gm_t88rcucg1b7uk t56 = t54;
                uint8_t t57 = (gm_eq9m6mvtmgde(((uint8_t *)&t55), ((uint8_t *)&t56)));
                if (!t57) goto ie49;
                uint8_t t58 = 0;
                uint8_t t59 = 0;
                uint8_t t60 = gm_l10;
                if (!t60) {
                    void * t61 = gm_l4;
                    gm_t88rcucg1b7uk t62 = gm_l5;
                    uint8_t t63 = gm_f_plain_31j25h8(t61, GM_CONV(gm_csguankko4lmhd, t62));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t59 = t63;
                } else t59 = 1;
                if (!t59) {
                    void * t64 = gm_l4;
                    gm_t88rcucg1b7uk t65 = gm_l1;
                    uint8_t t66 = gm_f_plain_31j25h8(t64, GM_CONV(gm_csguankko4lmhd, t65));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t58 = t66;
                } else t58 = 1;
                if (!t58) goto ie49;
                {
                }
                {
                    gm_tvdg94bk7aa22 t68;
                    GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t68 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)7ULL));
                    gm_t88rcucg1b7uk t69 = gm_l1;
                    {
                        void * t71 = gm_l2;
                        gm_tn3r4rvlh0rsv t72;
                        GM_ST(void *, ((uint8_t *)&t72 + 0), t71);
                        gm_l11 = t72;
                    }
                    void * t73 = ((uint8_t *)&gm_l11);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 432, 109);
                    gm_b16_8 t74;
                    GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t73 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t75 = t74;
                    gm_tl874t2atnlh3 t76 = gm_l3;
                    void * t77 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t68, t69, t75, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t78 = 0;
                    F.ret = t77;
                    goto gm_fret;
                }
                goto ix49;
ie49:;
ix49:;
            }
            {
                uint8_t t80 = gm_l10;
                if (!t80) goto ie79;
                uint64_t t81 = gm_l6;
                uint64_t t82 = gm_l8;
                uint8_t t83 = ((t81 == t82));
                if (!t83) goto ie79;
                void * t84 = gm_l0;
                void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
                gm_t88rcucg1b7uk t86 = gm_l7;
                gm_t88rcucg1b7uk t87 = gm_l9;
                uint8_t t88 = gm_f_lossless_a54o69s(t85, GM_CONV(gm_csguankko4lmhd, t86), GM_CONV(gm_csguankko4lmhd, t87));
                if (!t88) goto ie79;
                {
                    gm_f_list_ir0h3tc_2((void *)&F.l12, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t90 = gm_l0;
                    void * t91 = gm_l2;
                    void * t92 = (void *)((uint8_t *)&F.l12);
                    void * t93 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t90, t91, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t93;
                }
                {
                    gm_f_list_vp1djv1_2((void *)&F.l14, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_l16 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t95 = gm_l6;
                        gm_l15 = t95;
                    }
                    {
                        for (;;) {
                            uint64_t t97 = gm_l16;
                            uint64_t t98 = gm_l15;
                            uint8_t t99 = (t97 < t98);
                            if (!t99) goto lb96;
                            {
                                uint64_t t102 = gm_l16;
                                uint64_t t103 = t102;
                                uint64_t t106 = GM_LD(uint64_t, ((uint8_t *)&t103));
                                gm_l17 = t106;
                                {
                                }
                                {
                                    void * t108 = (void *)((uint8_t *)&F.l14);
                                    gm_tvdg94bk7aa22 t109;
                                    GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t109 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)0ULL));
                                    gm_t88rcucg1b7uk t110 = gm_l9;
                                    {
                                        void * t112 = gm_l0;
                                        void * t113 = gm_l13;
                                        gm_t88rcucg1b7uk t114 = gm_l7;
                                        uint64_t t115 = gm_l17;
                                        void * t116 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t112, t113, t114, t115);
                                        if (gm_unw.kind) {
                                            goto gm_ll100;
                                        }
                                        gm_tn3r4rvlh0rsv t117;
                                        GM_ST(void *, ((uint8_t *)&t117 + 0), t116);
                                        gm_l18 = t117;
                                    }
                                    void * t118 = ((uint8_t *)&gm_l18);
                                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 437, 57);
                                    gm_b16_8 t119;
                                    GM_ST(void *, (uint8_t *)&t119, (void *)(((uint8_t *)t118 + (uintptr_t)0 * 8)));
                                    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                                    gm_b16_8 t120 = t119;
                                    gm_tl874t2atnlh3 t121 = gm_l3;
                                    void * t122 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t109, t110, t120, t121);
                                    if (gm_unw.kind) {
                                        goto gm_ll100;
                                    }
                                    gm_tftb1odc739t5 t123 = gm_f_push_d943vgp_2(t108, &(*(gm_t69m56meqefs8 *)((uint8_t *)t122)));
                                    gm_tftb1odc739t5 t124 = t123;
                                    if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_srci09ctgf0djoe, 437, 25);
                                    gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
                                }
                                goto me104;
mn105:;
                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 437, 9);
me104:;
                            }
lc96:;
                            {
                                uint64_t t127 = gm_l16;
                                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                                uint64_t t130 = r128;
                                gm_l16 = t130;
                            }
                        }
gm_ll100:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb96; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc96; }
                        }
                        goto gm_lad;
lb96:;
                    }
                }
                {
                    gm_t88rcucg1b7uk t131 = gm_l1;
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                    gm_teti411eujvf4 t133;
                    memset(&t133, 0, sizeof t133);
                    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 8), t132);
                    gm_tl874t2atnlh3 t134 = gm_l3;
                    void * t135 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t131, &t133, t134);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t135;
                }
                {
                    gm_t88rcucg1b7uk t136 = gm_l1;
                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                    void * t138 = gm_l19;
                    gm_tl874t2atnlh3 t139 = gm_l3;
                    void * t140 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t136, t137, t138, t139);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t141 = 0;
                    F.ret = t140;
                    goto gm_fret;
                }
                goto ix79;
ie79:;
ix79:;
            }
            {
                gm_b16_8 t142;
                uint8_t t144 = gm_l10;
                if (!t144) goto ie143;
                uint64_t t145 = gm_l6;
                uint64_t t146 = gm_l8;
                uint8_t t147 = ((t145 == t146));
                if (!t147) goto ie143;
                gm_t6j6smj7qoctu t149;
                void * t150 = gm_l0;
                gm_t88rcucg1b7uk t151 = gm_l7;
                gm_b16_8 t152 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t150, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t149 + 0, t152);
                void * t153 = gm_l0;
                gm_t88rcucg1b7uk t154 = gm_l9;
                gm_b16_8 t155 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t153, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t149 + 16, t155);
                gm_tabb710d9mq2e t156;
                gm_f_format_mrlevsk((void *)&t156, (void *)NULL, cx, NULL, &t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t157 = t156;
                if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 8, gm_srci09ctgf0djoe, 441, 40);
                gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&t157) + 8));
                gm_b16_8 t159 = t158;
                gm_b16_8 t160 = t159;
                gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
                t142 = t161;
                goto ix143;
ie143:;
                gm_b16_8 t163;
                GM_ST(void *, (uint8_t *)&t163, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(0));
                gm_b16_8 t164 = t163;
                t142 = t164;
ix143:;
                gm_l20 = t142;
            }
            {
                void * t165 = gm_l0;
                gm_tl874t2atnlh3 t166 = gm_l3;
                gm_tfeab279c16rr t167;
                void * t168 = gm_l0;
                gm_t88rcucg1b7uk t169 = gm_l5;
                gm_b16_8 t170 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t170);
                void * t171 = gm_l0;
                gm_t88rcucg1b7uk t172 = gm_l1;
                gm_b16_8 t173 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t171, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t167 + 16, t173);
                gm_b16_8 t174 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t167 + 32, t174);
                gm_tabb710d9mq2e t175;
                gm_f_format_d0lfl1k((void *)&t175, (void *)NULL, cx, NULL, &t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t176 = t175;
                if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 8, gm_srci09ctgf0djoe, 442, 20);
                gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t176) + 8));
                gm_b16_8 t178 = t177;
                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                gm_b0_1 t180 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t165, t166, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t181 = gm_l0;
                void * t182 = GM_LD(void *, (((uint8_t *)t181) + 0));
                gm_tl874t2atnlh3 t183 = gm_l3;
                void * t184 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t182, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t185 = 0;
                F.ret = t184;
                goto gm_fret;
            }
            goto ix32;
ie32:;
            {
                {
                    void * t188 = gm_l0;
                    gm_tl874t2atnlh3 t189 = gm_l3;
                    gm_t6j6smj7qoctu t190;
                    void * t191 = gm_l0;
                    gm_t88rcucg1b7uk t192 = gm_l5;
                    gm_b16_8 t193 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t191, t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t193);
                    void * t194 = gm_l0;
                    gm_t88rcucg1b7uk t195 = gm_l1;
                    gm_b16_8 t196 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t194, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t190 + 16, t196);
                    gm_tabb710d9mq2e t197;
                    gm_f_format_9rhpk5t((void *)&t197, (void *)NULL, cx, NULL, &t190);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t198 = t197;
                    if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srci09ctgf0djoe, 427, 24);
                    gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
                    gm_b16_8 t200 = t199;
                    gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                    gm_b0_1 t202 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t188, t189, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t203 = gm_l0;
                    void * t204 = GM_LD(void *, (((uint8_t *)t203) + 0));
                    gm_tl874t2atnlh3 t205 = gm_l3;
                    void * t206 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t204, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t207 = 0;
                    F.ret = t206;
                    goto gm_fret;
                }
            }
ix32:;
        }
        goto ix23;
ie23:;
        {
            {
                void * t210 = gm_l0;
                gm_tl874t2atnlh3 t211 = gm_l3;
                gm_t6j6smj7qoctu t212;
                void * t213 = gm_l0;
                gm_t88rcucg1b7uk t214 = gm_l5;
                gm_b16_8 t215 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t213, t214);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t215);
                void * t216 = gm_l0;
                gm_t88rcucg1b7uk t217 = gm_l1;
                gm_b16_8 t218 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t216, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t218);
                gm_tabb710d9mq2e t219;
                gm_f_format_9rhpk5t((void *)&t219, (void *)NULL, cx, NULL, &t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t220 = t219;
                if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 8, gm_srci09ctgf0djoe, 423, 24);
                gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&t220) + 8));
                gm_b16_8 t222 = t221;
                gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                gm_b0_1 t224 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t210, t211, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t225 = gm_l0;
                void * t226 = GM_LD(void *, (((uint8_t *)t225) + 0));
                gm_tl874t2atnlh3 t227 = gm_l3;
                void * t228 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t226, t227);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t229 = 0;
                F.ret = t228;
                goto gm_fret;
            }
        }
ix23:;
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
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_math_vector_method_ko2814s F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    void * gm_l11; /* x */
    gm_ts6oqppjh0ba3 gm_l12; /* mul */
    gm_ts6oqppjh0ba3 gm_l13; /* add */
    gm_ts6oqppjh0ba3 gm_l14; /* sub */
    void * gm_l15; /* y0 */
    void * gm_l16; /* y */
    void * gm_l17; /* tail */
    void * gm_l18; /* acc */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* p */
    void * gm_l23; /* a */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t2lqu8jsj511d gm_l28; /* ij */
    uint64_t gm_l29; /* i */
    uint64_t gm_l30; /* j */
    void * gm_l31; /* p */
    void * gm_l32; /* q */
    gm_tvdg94bk7aa22 gm_l33; /* op */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* i */
    gm_t5fru0lp11n7t gm_l38; /* <temporary array> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_tn3r4rvlh0rsv gm_l43; /* <temporary array> */
    void * gm_l44; /* tail */
    void * gm_l45; /* acc */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* i */
    void * gm_l49; /* e */
    void * gm_l50; /* p */
    void * gm_l51; /* a */
    void * gm_l52; /* total */
    gm_tn3r4rvlh0rsv gm_l53; /* <temporary array> */
    void * gm_l54; /* tail */
    gm_b16_8 gm_l55; /* hint */
    GM_STACK_CHECK(&F, 5365u);
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
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l6;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tb7u19loo3j87 t7;
        gm_f_math_vector_parts_49c7865((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        gm_tlcm4lvu3g1a8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t10 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            gm_tl874t2atnlh3 t13 = gm_l5;
            void * t14 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l7 = t9;
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t16;
    }
    {
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t17;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        void * t20 = (void *)((uint8_t *)&F.l10);
        void * t21 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t21;
    }
    {
        gm_t9qkh0rsooc8d t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 1), t22);
        gm_l12 = t23;
    }
    {
        gm_t9qkh0rsooc8d t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 1), t24);
        gm_l13 = t25;
    }
    {
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t27;
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 1), t26);
        gm_l14 = t27;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sg1h22ve4uqmq, 3) == 0)) goto pa31;
        goto po30;
pa31:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgvnpogt67see, 5) == 0)) goto pa32;
        goto po30;
pa32:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0)) goto pa33;
        goto po30;
pa33:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s2mtoab140jp6, 3) == 0)) goto pa34;
        goto po30;
pa34:;
        goto mn29;
po30:;
        {
            void * t37 = gm_l0;
            gm_b16_8 t38 = gm_l4;
            gm_b16_8 t39 = gm_l3;
            gm_tl874t2atnlh3 t40 = gm_l5;
            uint8_t t41 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t37, t38, ((uint64_t)1ULL), t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = (uint8_t)!t41;
            if (!t42) goto ie36;
            {
                void * t44 = gm_l0;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
                gm_tl874t2atnlh3 t46 = gm_l5;
                void * t47 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t45, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t48 = 0;
                F.ret = t47;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            void * t49 = gm_l0;
            gm_b16_8 t50 = gm_l4;
            gm_t88rcucg1b7uk t51 = gm_l2;
            gm_tl874t2atnlh3 t52 = gm_l5;
            void * t53 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t49, t50, ((uint64_t)0ULL), t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t53;
        }
        {
            void * t54 = gm_l0;
            void * t55 = gm_l15;
            void * t56 = (void *)((uint8_t *)&F.l10);
            void * t57 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t57;
        }
        {
            uint8_t t59 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgvnpogt67see, 5) == 0));
            if (!t59) goto ie58;
            uint64_t t60 = gm_l8;
            uint8_t t61 = (!(t60 == ((uint64_t)3ULL)));
            if (!t61) goto ie58;
            {
                void * t63 = gm_l0;
                gm_tl874t2atnlh3 t64 = gm_l5;
                gm_t3877pk7lv748 t65;
                void * t66 = gm_l0;
                gm_t88rcucg1b7uk t67 = gm_l2;
                gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t68);
                uint64_t t69 = gm_l8;
                GM_ST(uint64_t, (uint8_t *)&t65 + 16, t69);
                gm_tabb710d9mq2e t70;
                gm_f_format_c8fgv1v((void *)&t70, (void *)NULL, cx, NULL, &t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srci09ctgf0djoe, 348, 32);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
                gm_b16_8 t73 = t72;
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                gm_b0_1 t75 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t64, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                gm_tl874t2atnlh3 t78 = gm_l5;
                void * t79 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            void * t81 = gm_l0;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
            gm_tl874t2atnlh3 t83 = gm_l5;
            void * t84 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t84;
        }
        {
            uint8_t t86 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sg1h22ve4uqmq, 3) == 0));
            if (!t86) goto ie85;
            {
                gm_l18 = ((void *)0);
            }
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t89 = gm_l8;
                    gm_l19 = t89;
                }
                {
                    for (;;) {
                        uint64_t t91 = gm_l20;
                        uint64_t t92 = gm_l19;
                        uint8_t t93 = (t91 < t92);
                        if (!t93) goto lb90;
                        {
                            uint64_t t96 = gm_l20;
                            uint64_t t97 = t96;
                            uint64_t t100 = GM_LD(uint64_t, ((uint8_t *)&t97));
                            gm_l21 = t100;
                            {
                                void * t102 = gm_l0;
                                gm_ts6oqppjh0ba3 t103 = gm_l12;
                                void * t104 = gm_l0;
                                void * t105 = gm_l11;
                                gm_t88rcucg1b7uk t106 = gm_l9;
                                uint64_t t107 = gm_l21;
                                void * t108 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t104, t105, t106, t107);
                                if (gm_unw.kind) {
                                    goto gm_ll94;
                                }
                                void * t109 = gm_l0;
                                void * t110 = gm_l16;
                                gm_t88rcucg1b7uk t111 = gm_l9;
                                uint64_t t112 = gm_l21;
                                void * t113 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t109, t110, t111, t112);
                                if (gm_unw.kind) {
                                    goto gm_ll94;
                                }
                                gm_tl874t2atnlh3 t114 = gm_l5;
                                void * t115 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t102, t103, t108, t113, t114);
                                if (gm_unw.kind) {
                                    goto gm_ll94;
                                }
                                gm_l22 = t115;
                            }
                            {
                                void * t116;
                                void * t118 = gm_l18;
                                void * t119 = t118;
                                if (!(GM_LD(void *, ((uint8_t *)&t119)) != 0)) goto ie117;
                                void * t120 = GM_LD(void *, ((uint8_t *)&t119));
                                gm_l23 = t120;
                                void * t122 = gm_l0;
                                gm_ts6oqppjh0ba3 t123 = gm_l13;
                                void * t124 = gm_l23;
                                void * t125 = gm_l22;
                                gm_tl874t2atnlh3 t126 = gm_l5;
                                void * t127 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t122, t123, t124, t125, t126);
                                if (gm_unw.kind) {
                                    goto gm_ll94;
                                }
                                void * t128 = t127;
                                t116 = t128;
                                goto ix117;
ie117:;
                                void * t130 = gm_l22;
                                void * t131 = t130;
                                t116 = t131;
ix117:;
                                gm_l18 = t116;
                            }
                            goto me98;
mn99:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 354, 17);
me98:;
                        }
lc90:;
                        {
                            uint64_t t133 = gm_l20;
                            uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                            uint64_t t136 = r134;
                            gm_l20 = t136;
                        }
                    }
gm_ll94:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb90; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc90; }
                    }
                    goto gm_lad;
lb90:;
                }
            }
            {
                void * t137 = gm_l18;
                void * t138 = t137;
                void * t139;
                if ((GM_LD(void *, ((uint8_t *)&t138)) != 0)) {
                    void * t140 = GM_LD(void *, (((uint8_t *)&t138) + 0));
                    t139 = t140;
                } else {
                    void * t141 = gm_l0;
                    void * t142 = GM_LD(void *, (((uint8_t *)t141) + 0));
                    gm_tl874t2atnlh3 t143 = gm_l5;
                    void * t144 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t142, t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t145 = 0;
                    F.ret = t144;
                    goto gm_fret;
                }
                gm_l17 = t139;
            }
            goto ix85;
ie85:;
            uint8_t t147 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgvnpogt67see, 5) == 0));
            if (!t147) goto ie146;
            {
                gm_f_list_vp1djv1_2((void *)&F.l24, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    {
                        gm_t2lqu8jsj511d t151;
                        GM_ST(uint64_t, ((uint8_t *)&t151 + 0), ((uint64_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t151 + 8), ((uint64_t)2ULL));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&F.l25 + 0, t151);
                        gm_t2lqu8jsj511d t152;
                        GM_ST(uint64_t, ((uint8_t *)&t152 + 0), ((uint64_t)2ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t152 + 8), ((uint64_t)0ULL));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&F.l25 + 16, t152);
                        gm_t2lqu8jsj511d t153;
                        GM_ST(uint64_t, ((uint8_t *)&t153 + 0), ((uint64_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t153 + 8), ((uint64_t)1ULL));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&F.l25 + 32, t153);
                    }
                    void * t154 = ((uint8_t *)&F.l25);
                    if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srci09ctgf0djoe, 361, 17);
                    gm_b16_8 t155;
                    GM_ST(void *, (uint8_t *)&t155, (void *)(((uint8_t *)t154 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                    gm_b16_8 t156 = t155;
                    gm_l26 = t156;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t158 = gm_l27;
                        uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint8_t t160 = (t158 < t159);
                        if (!t160) goto lb157;
                        {
                            uint64_t t163 = gm_l27;
                            uint64_t t164 = (uintptr_t)t163;
                            uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                            gm_t2lqu8jsj511d t168 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t164 * 16));
                            gm_l28 = t168;
                            {
                                uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 0));
                                gm_l29 = t170;
                            }
                            {
                                uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                gm_l30 = t171;
                            }
                            {
                                void * t172 = gm_l0;
                                gm_ts6oqppjh0ba3 t173 = gm_l12;
                                void * t174 = gm_l0;
                                void * t175 = gm_l11;
                                gm_t88rcucg1b7uk t176 = gm_l9;
                                uint64_t t177 = gm_l29;
                                void * t178 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t174, t175, t176, t177);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                void * t179 = gm_l0;
                                void * t180 = gm_l16;
                                gm_t88rcucg1b7uk t181 = gm_l9;
                                uint64_t t182 = gm_l30;
                                void * t183 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t179, t180, t181, t182);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tl874t2atnlh3 t184 = gm_l5;
                                void * t185 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t172, t173, t178, t183, t184);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_l31 = t185;
                            }
                            {
                                void * t186 = gm_l0;
                                gm_ts6oqppjh0ba3 t187 = gm_l12;
                                void * t188 = gm_l0;
                                void * t189 = gm_l11;
                                gm_t88rcucg1b7uk t190 = gm_l9;
                                uint64_t t191 = gm_l30;
                                void * t192 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t188, t189, t190, t191);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                void * t193 = gm_l0;
                                void * t194 = gm_l16;
                                gm_t88rcucg1b7uk t195 = gm_l9;
                                uint64_t t196 = gm_l29;
                                void * t197 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t193, t194, t195, t196);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tl874t2atnlh3 t198 = gm_l5;
                                void * t199 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t186, t187, t192, t197, t198);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_l32 = t199;
                            }
                            {
                                void * t200 = (void *)((uint8_t *)&F.l24);
                                void * t201 = gm_l0;
                                gm_ts6oqppjh0ba3 t202 = gm_l14;
                                void * t203 = gm_l31;
                                void * t204 = gm_l32;
                                gm_tl874t2atnlh3 t205 = gm_l5;
                                void * t206 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t201, t202, t203, t204, t205);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tftb1odc739t5 t207 = gm_f_push_d943vgp_2(t200, &(*(gm_t69m56meqefs8 *)((uint8_t *)t206)));
                                gm_tftb1odc739t5 t208 = t207;
                                if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 1, gm_srci09ctgf0djoe, 366, 21);
                                gm_b0_1 t209 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 1));
                            }
                            goto me166;
mn167:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 361, 17);
me166:;
                        }
lc157:;
                        {
                            uint64_t t211 = gm_l27;
                            uint64_t r212; bool o213 = __builtin_add_overflow(t211, ((uint64_t)1ULL), &r212);
                            uint64_t t214 = r212;
                            gm_l27 = t214;
                        }
                    }
gm_ll161:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb157; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc157; }
                    }
                    goto gm_lad;
lb157:;
                }
            }
            {
                gm_t88rcucg1b7uk t215 = gm_l2;
                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                gm_teti411eujvf4 t217;
                memset(&t217, 0, sizeof t217);
                GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)36ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t217 + 8), t216);
                gm_tl874t2atnlh3 t218 = gm_l5;
                void * t219 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t215, &t217, t218);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t219;
            }
            goto ix146;
ie146:;
            {
                gm_tvdg94bk7aa22 t221;
                uint8_t t223 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0));
                if (!t223) goto ie222;
                gm_tvdg94bk7aa22 t225;
                GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t225 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)16ULL));
                gm_tvdg94bk7aa22 t226 = t225;
                t221 = t226;
                goto ix222;
ie222:;
                gm_tvdg94bk7aa22 t228;
                GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t228 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)17ULL));
                gm_tvdg94bk7aa22 t229 = t228;
                t221 = t229;
ix222:;
                gm_l33 = t221;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l34, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l36 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t231 = gm_l8;
                    gm_l35 = t231;
                }
                {
                    for (;;) {
                        uint64_t t233 = gm_l36;
                        uint64_t t234 = gm_l35;
                        uint8_t t235 = (t233 < t234);
                        if (!t235) goto lb232;
                        {
                            uint64_t t238 = gm_l36;
                            uint64_t t239 = t238;
                            uint64_t t242 = GM_LD(uint64_t, ((uint8_t *)&t239));
                            gm_l37 = t242;
                            {
                            }
                            {
                                void * t244 = (void *)((uint8_t *)&F.l34);
                                gm_tvdg94bk7aa22 t245 = gm_l33;
                                gm_t88rcucg1b7uk t246 = gm_l9;
                                {
                                    void * t248 = gm_l0;
                                    void * t249 = gm_l11;
                                    gm_t88rcucg1b7uk t250 = gm_l9;
                                    uint64_t t251 = gm_l37;
                                    void * t252 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t248, t249, t250, t251);
                                    if (gm_unw.kind) {
                                        goto gm_ll236;
                                    }
                                    void * t253 = gm_l0;
                                    void * t254 = gm_l16;
                                    gm_t88rcucg1b7uk t255 = gm_l9;
                                    uint64_t t256 = gm_l37;
                                    void * t257 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t253, t254, t255, t256);
                                    if (gm_unw.kind) {
                                        goto gm_ll236;
                                    }
                                    gm_t5fru0lp11n7t t258;
                                    GM_ST(void *, ((uint8_t *)&t258 + 0), t252);
                                    GM_ST(void *, ((uint8_t *)&t258 + 8), t257);
                                    gm_l38 = t258;
                                }
                                void * t259 = ((uint8_t *)&gm_l38);
                                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 372, 64);
                                gm_b16_8 t260;
                                GM_ST(void *, (uint8_t *)&t260, (void *)(((uint8_t *)t259 + (uintptr_t)0 * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                                gm_b16_8 t261 = t260;
                                gm_tl874t2atnlh3 t262 = gm_l5;
                                void * t263 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t245, t246, t261, t262);
                                if (gm_unw.kind) {
                                    goto gm_ll236;
                                }
                                gm_tftb1odc739t5 t264 = gm_f_push_d943vgp_2(t244, &(*(gm_t69m56meqefs8 *)((uint8_t *)t263)));
                                gm_tftb1odc739t5 t265 = t264;
                                if (((((uint8_t *)&t265))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t265) + 1, gm_srci09ctgf0djoe, 372, 35);
                                gm_b0_1 t266 = GM_LD(gm_b0_1, (((uint8_t *)&t265) + 1));
                            }
                            goto me240;
mn241:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 372, 17);
me240:;
                        }
lc232:;
                        {
                            uint64_t t268 = gm_l36;
                            uint64_t r269; bool o270 = __builtin_add_overflow(t268, ((uint64_t)1ULL), &r269);
                            uint64_t t271 = r269;
                            gm_l36 = t271;
                        }
                    }
gm_ll236:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb232; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc232; }
                    }
                    goto gm_lad;
lb232:;
                }
            }
            {
                gm_t88rcucg1b7uk t272 = gm_l2;
                gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                gm_teti411eujvf4 t274;
                memset(&t274, 0, sizeof t274);
                GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)36ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t274 + 8), t273);
                gm_tl874t2atnlh3 t275 = gm_l5;
                void * t276 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t272, &t274, t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t276;
            }
ix146:;
ix85:;
        }
        {
            void * t277 = gm_l17;
            gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t277) + 0));
            gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t280 = gm_l17;
            gm_tl874t2atnlh3 t281 = gm_l5;
            void * t282 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t278, t279, t280, t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t283 = 0;
            F.ret = t282;
            goto gm_fret;
        }
        goto me28;
mn29:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbl3pbs9195ho, 3) == 0)) goto mn284;
        {
            void * t287 = gm_l0;
            gm_b16_8 t288 = gm_l4;
            gm_b16_8 t289 = gm_l3;
            gm_tl874t2atnlh3 t290 = gm_l5;
            uint8_t t291 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t287, t288, ((uint64_t)0ULL), t289, t290);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t292 = (uint8_t)!t291;
            if (!t292) goto ie286;
            {
                void * t294 = gm_l0;
                void * t295 = GM_LD(void *, (((uint8_t *)t294) + 0));
                gm_tl874t2atnlh3 t296 = gm_l5;
                void * t297 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t295, t296);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t298 = 0;
                F.ret = t297;
                goto gm_fret;
            }
            goto ix286;
ie286:;
ix286:;
        }
        {
            uint8_t t300 = 0;
            void * t301 = gm_l6;
            gm_t88rcucg1b7uk t302 = gm_l9;
            uint8_t t303 = gm_f_is_signed_integer_s66djm7(t301, GM_CONV(gm_csguankko4lmhd, t302));
            if (!t303) {
                void * t304 = gm_l6;
                gm_t88rcucg1b7uk t305 = gm_l9;
                uint8_t t306 = gm_f_is_float_3jk89qu(t304, GM_CONV(gm_csguankko4lmhd, t305));
                t300 = t306;
            } else t300 = 1;
            uint8_t t307 = (uint8_t)!t300;
            if (!t307) goto ie299;
            {
                void * t309 = gm_l0;
                gm_tl874t2atnlh3 t310 = gm_l5;
                void * t311 = gm_l0;
                gm_t88rcucg1b7uk t312 = gm_l9;
                gm_b16_8 t313 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t311, t312);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t314;
                GM_ST(gm_b16_8, ((uint8_t *)&t314 + 0), t313);
                gm_tabb710d9mq2e t315;
                gm_f_format_gmfdekn((void *)&t315, (void *)NULL, cx, NULL, t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t316 = t315;
                if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 8, gm_srci09ctgf0djoe, 380, 32);
                gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&t316) + 8));
                gm_b16_8 t318 = t317;
                gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
                gm_b0_1 t320 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t309, t310, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t321 = gm_l0;
                void * t322 = GM_LD(void *, (((uint8_t *)t321) + 0));
                gm_tl874t2atnlh3 t323 = gm_l5;
                void * t324 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t322, t323);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t325 = 0;
                F.ret = t324;
                goto gm_fret;
            }
            goto ix299;
ie299:;
ix299:;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l39, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                uint64_t t327 = gm_l8;
                gm_l40 = t327;
            }
            {
                for (;;) {
                    uint64_t t329 = gm_l41;
                    uint64_t t330 = gm_l40;
                    uint8_t t331 = (t329 < t330);
                    if (!t331) goto lb328;
                    {
                        uint64_t t334 = gm_l41;
                        uint64_t t335 = t334;
                        uint64_t t338 = GM_LD(uint64_t, ((uint8_t *)&t335));
                        gm_l42 = t338;
                        {
                        }
                        {
                            void * t340 = (void *)((uint8_t *)&F.l39);
                            gm_tvdg94bk7aa22 t341;
                            GM_ST(uint8_t, (uint8_t *)&t341 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t341 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t341 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t341 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)19ULL));
                            gm_t88rcucg1b7uk t342 = gm_l9;
                            {
                                void * t344 = gm_l0;
                                void * t345 = gm_l11;
                                gm_t88rcucg1b7uk t346 = gm_l9;
                                uint64_t t347 = gm_l42;
                                void * t348 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t344, t345, t346, t347);
                                if (gm_unw.kind) {
                                    goto gm_ll332;
                                }
                                gm_tn3r4rvlh0rsv t349;
                                GM_ST(void *, ((uint8_t *)&t349 + 0), t348);
                                gm_l43 = t349;
                            }
                            void * t350 = ((uint8_t *)&gm_l43);
                            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 384, 62);
                            gm_b16_8 t351;
                            GM_ST(void *, (uint8_t *)&t351, (void *)(((uint8_t *)t350 + (uintptr_t)0 * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t351 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                            gm_b16_8 t352 = t351;
                            gm_tl874t2atnlh3 t353 = gm_l5;
                            void * t354 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t341, t342, t352, t353);
                            if (gm_unw.kind) {
                                goto gm_ll332;
                            }
                            gm_tftb1odc739t5 t355 = gm_f_push_d943vgp_2(t340, &(*(gm_t69m56meqefs8 *)((uint8_t *)t354)));
                            gm_tftb1odc739t5 t356 = t355;
                            if (((((uint8_t *)&t356))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t356) + 1, gm_srci09ctgf0djoe, 384, 31);
                            gm_b0_1 t357 = GM_LD(gm_b0_1, (((uint8_t *)&t356) + 1));
                        }
                        goto me336;
mn337:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 384, 13);
me336:;
                    }
lc328:;
                    {
                        uint64_t t359 = gm_l41;
                        uint64_t r360; bool o361 = __builtin_add_overflow(t359, ((uint64_t)1ULL), &r360);
                        uint64_t t362 = r360;
                        gm_l41 = t362;
                    }
                }
gm_ll332:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb328; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc328; }
                }
                goto gm_lad;
lb328:;
            }
        }
        {
            gm_t88rcucg1b7uk t363 = gm_l2;
            gm_b16_8 t364 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
            gm_teti411eujvf4 t365;
            memset(&t365, 0, sizeof t365);
            GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)36ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t365 + 8), t364);
            gm_tl874t2atnlh3 t366 = gm_l5;
            void * t367 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t363, &t365, t366);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t367;
        }
        {
            gm_t88rcucg1b7uk t368 = gm_l2;
            gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t370 = gm_l44;
            gm_tl874t2atnlh3 t371 = gm_l5;
            void * t372 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t368, t369, t370, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t373 = 0;
            F.ret = t372;
            goto gm_fret;
        }
        goto me28;
mn284:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6fdn1h3c830p, 4) == 0)) goto mn374;
        {
            void * t377 = gm_l0;
            gm_b16_8 t378 = gm_l4;
            gm_b16_8 t379 = gm_l3;
            gm_tl874t2atnlh3 t380 = gm_l5;
            uint8_t t381 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t377, t378, ((uint64_t)0ULL), t379, t380);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t382 = (uint8_t)!t381;
            if (!t382) goto ie376;
            {
                void * t384 = gm_l0;
                void * t385 = GM_LD(void *, (((uint8_t *)t384) + 0));
                gm_tl874t2atnlh3 t386 = gm_l5;
                void * t387 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t385, t386);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t388 = 0;
                F.ret = t387;
                goto gm_fret;
            }
            goto ix376;
ie376:;
ix376:;
        }
        {
            void * t390 = gm_l6;
            gm_t88rcucg1b7uk t391 = gm_l9;
            uint8_t t392 = gm_f_is_float_3jk89qu(t390, GM_CONV(gm_csguankko4lmhd, t391));
            uint8_t t393 = (uint8_t)!t392;
            if (!t393) goto ie389;
            {
                void * t395 = gm_l0;
                gm_tl874t2atnlh3 t396 = gm_l5;
                void * t397 = gm_l0;
                gm_t88rcucg1b7uk t398 = gm_l9;
                gm_b16_8 t399 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t397, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t400;
                GM_ST(gm_b16_8, ((uint8_t *)&t400 + 0), t399);
                gm_tabb710d9mq2e t401;
                gm_f_format_mcgdakk((void *)&t401, (void *)NULL, cx, NULL, t400);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t402 = t401;
                if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 8, gm_srci09ctgf0djoe, 391, 32);
                gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&t402) + 8));
                gm_b16_8 t404 = t403;
                gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
                gm_b0_1 t406 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t395, t396, t405);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t407 = gm_l0;
                void * t408 = GM_LD(void *, (((uint8_t *)t407) + 0));
                gm_tl874t2atnlh3 t409 = gm_l5;
                void * t410 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t408, t409);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t411 = 0;
                F.ret = t410;
                goto gm_fret;
            }
            goto ix389;
ie389:;
ix389:;
        }
        {
            gm_l45 = ((void *)0);
        }
        {
            {
                gm_l47 = ((uint64_t)0ULL);
            }
            {
                uint64_t t413 = gm_l8;
                gm_l46 = t413;
            }
            {
                for (;;) {
                    uint64_t t415 = gm_l47;
                    uint64_t t416 = gm_l46;
                    uint8_t t417 = (t415 < t416);
                    if (!t417) goto lb414;
                    {
                        uint64_t t420 = gm_l47;
                        uint64_t t421 = t420;
                        uint64_t t424 = GM_LD(uint64_t, ((uint8_t *)&t421));
                        gm_l48 = t424;
                        {
                            void * t426 = gm_l0;
                            void * t427 = gm_l11;
                            gm_t88rcucg1b7uk t428 = gm_l9;
                            uint64_t t429 = gm_l48;
                            void * t430 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t426, t427, t428, t429);
                            if (gm_unw.kind) {
                                goto gm_ll418;
                            }
                            gm_l49 = t430;
                        }
                        {
                            void * t431 = gm_l0;
                            gm_ts6oqppjh0ba3 t432 = gm_l12;
                            void * t433 = gm_l49;
                            void * t434 = gm_l49;
                            gm_tl874t2atnlh3 t435 = gm_l5;
                            void * t436 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t431, t432, t433, t434, t435);
                            if (gm_unw.kind) {
                                goto gm_ll418;
                            }
                            gm_l50 = t436;
                        }
                        {
                            void * t437;
                            void * t439 = gm_l45;
                            void * t440 = t439;
                            if (!(GM_LD(void *, ((uint8_t *)&t440)) != 0)) goto ie438;
                            void * t441 = GM_LD(void *, ((uint8_t *)&t440));
                            gm_l51 = t441;
                            void * t443 = gm_l0;
                            gm_ts6oqppjh0ba3 t444 = gm_l13;
                            void * t445 = gm_l51;
                            void * t446 = gm_l50;
                            gm_tl874t2atnlh3 t447 = gm_l5;
                            void * t448 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t443, t444, t445, t446, t447);
                            if (gm_unw.kind) {
                                goto gm_ll418;
                            }
                            void * t449 = t448;
                            t437 = t449;
                            goto ix438;
ie438:;
                            void * t451 = gm_l50;
                            void * t452 = t451;
                            t437 = t452;
ix438:;
                            gm_l45 = t437;
                        }
                        goto me422;
mn423:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 395, 13);
me422:;
                    }
lc414:;
                    {
                        uint64_t t454 = gm_l47;
                        uint64_t r455; bool o456 = __builtin_add_overflow(t454, ((uint64_t)1ULL), &r455);
                        uint64_t t457 = r455;
                        gm_l47 = t457;
                    }
                }
gm_ll418:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb414; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc414; }
                }
                goto gm_lad;
lb414:;
            }
        }
        {
            void * t458 = gm_l45;
            void * t459 = t458;
            void * t460;
            if ((GM_LD(void *, ((uint8_t *)&t459)) != 0)) {
                void * t461 = GM_LD(void *, (((uint8_t *)&t459) + 0));
                t460 = t461;
            } else {
                void * t462 = gm_l0;
                void * t463 = GM_LD(void *, (((uint8_t *)t462) + 0));
                gm_tl874t2atnlh3 t464 = gm_l5;
                void * t465 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t463, t464);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t466 = 0;
                F.ret = t465;
                goto gm_fret;
            }
            gm_l52 = t460;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t467;
            GM_ST(uint8_t, (uint8_t *)&t467 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t467 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t467 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t467 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t467 + 0), ((uint8_t)26ULL));
            gm_t88rcucg1b7uk t468 = gm_l9;
            {
                void * t470 = gm_l52;
                gm_tn3r4rvlh0rsv t471;
                GM_ST(void *, ((uint8_t *)&t471 + 0), t470);
                gm_l53 = t471;
            }
            void * t472 = ((uint8_t *)&gm_l53);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 401, 42);
            gm_b16_8 t473;
            GM_ST(void *, (uint8_t *)&t473, (void *)(((uint8_t *)t472 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t473 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t474 = t473;
            gm_tl874t2atnlh3 t475 = gm_l5;
            void * t476 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t467, t468, t474, t475);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l54 = t476;
        }
        {
            void * t477 = gm_l54;
            gm_t88rcucg1b7uk t478 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t477) + 0));
            gm_b16_8 t479 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t480 = gm_l54;
            gm_tl874t2atnlh3 t481 = gm_l5;
            void * t482 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t478, t479, t480, t481);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t483 = 0;
            F.ret = t482;
            goto gm_fret;
        }
        goto me28;
mn374:;
        goto me28;
mn484:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 342, 5);
me28:;
    }
    {
        gm_b16_8 t486;
        uint8_t t488 = 0;
        gm_b16_8 t489 = gm_l3;
        gm_tlg1vmfbpbvtq t490 = GM_LD(gm_tlg1vmfbpbvtq, gm_cukabrstocl);
        gm_tlg1vmfbpbvtq t491 = t490;
        uint8_t t492 = 0;
        gm_b16_8 t493 = t489;
        for (uintptr_t i = 0; i < (uintptr_t)(27); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t491) + i * 16, ((uint8_t *)&t493))) { t492 = 1; break; }
        uint8_t t494 = (uint8_t)(t492);
        if (!t494) {
            gm_b16_8 t495 = gm_l3;
            gm_tfeab279c16rr t496;
            gm_b16_8 t497;
            GM_ST(void *, (uint8_t *)&t497, (void *)(gm_sbb1ri5er9djq));
            GM_ST(uintptr_t, (uint8_t *)&t497 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t496 + 0, t497);
            gm_b16_8 t498;
            GM_ST(void *, (uint8_t *)&t498, (void *)(gm_snivf25e5fme8));
            GM_ST(uintptr_t, (uint8_t *)&t498 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t496 + 16, t498);
            gm_b16_8 t499;
            GM_ST(void *, (uint8_t *)&t499, (void *)(gm_s4pmpk07ugvq6));
            GM_ST(uintptr_t, (uint8_t *)&t499 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t496 + 32, t499);
            gm_tfeab279c16rr t500 = t496;
            uint8_t t501 = 0;
            gm_b16_8 t502 = t495;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t500) + 0, ((uint8_t *)&t502))) t501 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t500) + 16, ((uint8_t *)&t502))) t501 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t500) + 32, ((uint8_t *)&t502))) t501 = 1;
            uint8_t t503 = (uint8_t)(t501);
            t488 = t503;
        } else t488 = 1;
        if (!t488) goto ie487;
        gm_b16_8 t505 = gm_l3;
        gm_tnpifv20irnph t506;
        GM_ST(gm_b16_8, ((uint8_t *)&t506 + 0), t505);
        gm_tabb710d9mq2e t507;
        gm_f_format_86pfrlj((void *)&t507, (void *)NULL, cx, NULL, t506);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t508 = t507;
        if (((((uint8_t *)&t508))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t508) + 8, gm_srci09ctgf0djoe, 407, 9);
        gm_b16_8 t509 = GM_LD(gm_b16_8, (((uint8_t *)&t508) + 8));
        gm_b16_8 t510 = t509;
        gm_b16_8 t511 = t510;
        gm_b16_8 t512 = GM_LD(gm_b16_8, ((uint8_t *)&t511));
        t486 = t512;
        goto ix487;
ie487:;
        gm_b16_8 t514;
        GM_ST(void *, (uint8_t *)&t514, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t514 + 8, (uintptr_t)(0));
        gm_b16_8 t515 = t514;
        t486 = t515;
ix487:;
        gm_l55 = t486;
    }
    {
        void * t516 = gm_l0;
        gm_tl874t2atnlh3 t517 = gm_l5;
        gm_t6j6smj7qoctu t518;
        gm_b16_8 t519 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t518 + 0, t519);
        gm_b16_8 t520 = gm_l55;
        GM_ST(gm_b16_8, (uint8_t *)&t518 + 16, t520);
        gm_tabb710d9mq2e t521;
        gm_f_format_iepqo20((void *)&t521, (void *)NULL, cx, NULL, &t518);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t522 = t521;
        if (((((uint8_t *)&t522))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t522) + 8, gm_srci09ctgf0djoe, 411, 20);
        gm_b16_8 t523 = GM_LD(gm_b16_8, (((uint8_t *)&t522) + 8));
        gm_b16_8 t524 = t523;
        gm_b16_8 t525 = GM_LD(gm_b16_8, ((uint8_t *)&t524));
        gm_b0_1 t526 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t516, t517, t525);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t527 = gm_l0;
        void * t528 = GM_LD(void *, (((uint8_t *)t527) + 0));
        gm_tl874t2atnlh3 t529 = gm_l5;
        void * t530 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t528, t529);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t531 = 0;
        F.ret = t530;
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
__attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_mentions_complex_constan_lrgkqac F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* l */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* lhs */
    void * gm_l6; /* rhs */
    GM_STACK_CHECK(&F, 1057u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; default: goto ma9; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        void * t14 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t14;
        void * t15 = gm_l2;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 98));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me3;
mn12:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)9ULL)) goto mn18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t20;
        void * t21 = gm_l0;
        void * t22 = gm_l3;
        uint8_t t23 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me3;
mn18:;
ma6:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)12ULL)) goto mn25;
        void * t27 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t27;
        void * t28 = gm_l0;
        void * t29 = gm_l4;
        uint8_t t30 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
        goto me3;
mn25:;
ma7:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)13ULL)) goto mn32;
        void * t34 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t34;
        void * t35 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l6 = t35;
        uint8_t t36 = 0;
        void * t37 = gm_l0;
        void * t38 = gm_l5;
        uint8_t t39 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t39) {
            void * t40 = gm_l0;
            void * t41 = gm_l6;
            uint8_t t42 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t36 = t42;
        } else t36 = 1;
        uint8_t t43 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me3;
mn32:;
ma8:;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t47 != ((uint8_t)6ULL)) goto pa46;
        goto po45;
pa46:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t49 != ((uint8_t)22ULL)) goto pa48;
        goto po45;
pa48:;
        goto mn44;
po45:;
        {
            void * t52 = gm_l0;
            void * t53 = gm_l1;
            gm_tkv1vap90vht2 t54;
            gm_f_try_untyped_8j8up35((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t55 = t54;
            if (!((((uint8_t *)&t55))[208] != 0)) goto ie51;
            gm_t5vbkdq81qfs7 t56 = GM_LD(gm_t5vbkdq81qfs7, ((uint8_t *)&t55));
            F.l7 = t56;
            {
                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t59 != ((uint8_t)0ULL)) goto ie58;
                gm_t0ialelq3ql1k t60 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l7) + 8));
                F.l8 = t60;
                uint8_t t61;
                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                if (t64 != ((uint8_t)4ULL)) goto mn63;
                t61 = ((uint8_t)1);
                goto me62;
mn63:;
                t61 = ((uint8_t)0);
                goto me62;
mn65:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 74, 36);
me62:;
                if (!t61) goto ie58;
                {
                    uint8_t t67 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix58;
ie58:;
ix58:;
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            uint8_t t68 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn44:;
ma9:;
        uint8_t t70 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn69:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 67, 5);
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
extern __attribute__((aligned(4))) uint8_t gm_f_plain_31j25h8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_promote_real_constant_v0nhili F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* agg */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_taari1s0m51a7 gm_l4; /* k */
    void * gm_l5; /* zero */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        uint8_t t4;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        t4 = ((uint8_t)0);
        goto me5;
mn11:;
ma7:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t14 != ((uint8_t)2ULL)) goto mn13;
        t4 = ((uint8_t)0);
        goto me5;
mn13:;
ma8:;
        t4 = ((uint8_t)1);
        goto me5;
mn15:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 114, 8);
me5:;
        if (!t4) goto ie2;
        {
            uint8_t t17 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 96);
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
        gm_taari1s0m51a7 t25 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t19, t22, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t25;
    }
    {
        uint8_t t27 = 0;
        uint8_t t28;
        if (((((uint8_t *)&gm_l4))[1] != 0)) goto mn30;
        t28 = ((uint8_t)1);
        goto me29;
mn30:;
        t28 = ((uint8_t)0);
        goto me29;
mn31:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 116, 8);
me29:;
        if (!t28) {
            gm_taari1s0m51a7 t32 = gm_l4;
            gm_t9qkh0rsooc8d t33;
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t34;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[1] = 1;
            gm_taari1s0m51a7 t35 = t32;
            gm_taari1s0m51a7 t36 = t34;
            uint8_t t37 = (gm_eq5307j5k9ms(((uint8_t *)&t35), ((uint8_t *)&t36)));
            t27 = t37;
        } else t27 = 1;
        if (!t27) goto ie26;
        {
            uint8_t t39 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t41 = gm_l0;
        void * t42 = (void *)(((uint8_t *)t41) + 96);
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        void * t46 = gm_l1;
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
        gm_t88rcucg1b7uk t48 = gm_l3;
        gm_t9qkh0rsooc8d t49 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t42, t45, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t50 = t49;
        uint8_t t51;
        if (!((((uint8_t *)&t50))[0] != 0)) goto mn53;
        t51 = ((uint8_t)1);
        goto me52;
mn53:;
        t51 = ((uint8_t)0);
        goto me52;
mn54:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 117, 8);
me52:;
        if (!t51) goto ie40;
        {
            uint8_t t56 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        gm_t88rcucg1b7uk t57 = gm_l3;
        gm_tngtuiuskn938 t58;
        gm_f_zero_9r6o7sj((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t59;
        gm_f_untyped_float_m6lbr0g((void *)&t59, (void *)NULL, cx, NULL, &t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t60 = gm_l1;
        gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
        void * t62 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t57, &t59, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t62;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t63 = (void *)((uint8_t *)&F.l6);
        void * t64 = gm_l1;
        gm_tftb1odc739t5 t65 = gm_f_push_d943vgp_2(t63, &(*(gm_t69m56meqefs8 *)((uint8_t *)t64)));
        gm_tftb1odc739t5 t66 = t65;
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srci09ctgf0djoe, 120, 5);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
    }
    {
        void * t68 = (void *)((uint8_t *)&F.l6);
        void * t69 = gm_l5;
        gm_tftb1odc739t5 t70 = gm_f_push_d943vgp_2(t68, &(*(gm_t69m56meqefs8 *)((uint8_t *)t69)));
        gm_tftb1odc739t5 t71 = t70;
        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srci09ctgf0djoe, 121, 5);
        gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
    }
    {
        gm_t88rcucg1b7uk t73 = gm_l2;
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_teti411eujvf4 t75;
        memset(&t75, 0, sizeof t75);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 8), t74);
        void * t76 = gm_l1;
        gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t76) + 72));
        void * t78 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t73, &t75, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t79 = 0;
        F.ret = t78;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_settle_complex_operands_9439n4u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_settle_complex_operands_9439n4u F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* r */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* lc */
    uint8_t gm_l7; /* rc */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* side */
    gm_t88rcucg1b7uk gm_l11; /* var */
    gm_t88rcucg1b7uk gm_l12; /* other0 */
    uint8_t gm_l13; /* var_left */
    gm_t88rcucg1b7uk gm_l14; /* other */
    uint32_t gm_l15; /* c */
    gm_tnqk2qt7j42o9 gm_l16; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l17; /* t */
    gm_tqsk8dt6ib3qq gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_t88rcucg1b7uk gm_l21; /* t */
    GM_STACK_CHECK(&F, 1012u);
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
        void * t5 = gm_l0;
        void * t6 = gm_l2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        uint8_t t12 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t12;
    }
    {
        uint8_t t14 = gm_l6;
        uint8_t t15 = (uint8_t)!t14;
        if (!t15) goto ie13;
        uint8_t t16 = gm_l7;
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie13;
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint8_t t22 = gm_l6;
        if (!t22) goto ie21;
        uint8_t t23 = gm_l7;
        if (!t23) goto ie21;
        {
            {
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 96);
                void * t28 = gm_l5;
                void * t29 = gm_l2;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
                void * t31 = gm_l3;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 0));
                gm_t9qkh0rsooc8d t33 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t27, t28, t30, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            gm_l8 = ((uint64_t)2ULL);
        }
        {
            for (;;) {
                uint64_t t36 = gm_l9;
                uint64_t t37 = gm_l8;
                uint8_t t38 = (t36 < t37);
                if (!t38) goto lb35;
                {
                    uint64_t t41 = gm_l9;
                    uint64_t t42 = t41;
                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)&t42));
                    gm_l10 = t45;
                    {
                        gm_t88rcucg1b7uk t47;
                        uint64_t t49 = gm_l10;
                        uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
                        if (!t50) goto ie48;
                        void * t52 = gm_l2;
                        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + 0));
                        gm_t88rcucg1b7uk t54 = t53;
                        t47 = t54;
                        goto ix48;
ie48:;
                        void * t56 = gm_l3;
                        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 0));
                        gm_t88rcucg1b7uk t58 = t57;
                        t47 = t58;
ix48:;
                        gm_l11 = t47;
                    }
                    {
                        gm_t88rcucg1b7uk t59;
                        uint64_t t61 = gm_l10;
                        uint8_t t62 = ((t61 == ((uint64_t)0ULL)));
                        if (!t62) goto ie60;
                        void * t64 = gm_l3;
                        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 0));
                        gm_t88rcucg1b7uk t66 = t65;
                        t59 = t66;
                        goto ix60;
ie60:;
                        void * t68 = gm_l2;
                        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 0));
                        gm_t88rcucg1b7uk t70 = t69;
                        t59 = t70;
ix60:;
                        gm_l12 = t59;
                    }
                    {
                        uint64_t t71 = gm_l10;
                        uint8_t t72 = ((t71 == ((uint64_t)0ULL)));
                        gm_l13 = t72;
                    }
                    {
                        void * t74 = gm_l0;
                        gm_t88rcucg1b7uk t75 = gm_l11;
                        uint8_t t76 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t74, t75);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        uint8_t t77 = (uint8_t)!t76;
                        if (!t77) goto ie73;
                        {
                            goto lc35;
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    {
                        void * t79 = gm_l0;
                        void * t80 = (void *)(((uint8_t *)t79) + 96);
                        void * t81 = gm_l0;
                        void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
                        gm_t88rcucg1b7uk t83 = gm_l12;
                        gm_t88rcucg1b7uk t84 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t80, t82, t83);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        gm_l14 = t84;
                    }
                    {
                        void * t86 = gm_l0;
                        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                        gm_t88rcucg1b7uk t88 = gm_l14;
                        gm_tkn42u7na3t81 t89 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t87, t88);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        gm_tkn42u7na3t81 t90 = t89;
                        uint8_t t91;
                        if (((((uint8_t *)&t90))[4] != 0)) goto mn93;
                        t91 = ((uint8_t)0);
                        goto me92;
mn93:;
                        t91 = ((uint8_t)1);
                        goto me92;
mn94:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 96, 12);
me92:;
                        if (!t91) goto ie85;
                        {
                            {
                                void * t97 = gm_l0;
                                void * t98 = (void *)(((uint8_t *)t97) + 96);
                                void * t99 = gm_l5;
                                gm_t88rcucg1b7uk t100 = gm_l11;
                                gm_t88rcucg1b7uk t101 = gm_l14;
                                gm_t9qkh0rsooc8d t102 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t98, t99, t100, t101);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                            }
                        }
                        goto ix85;
ie85:;
                        uint8_t t104 = 0;
                        void * t105 = gm_l5;
                        gm_t88rcucg1b7uk t106 = gm_l14;
                        uint8_t t107 = gm_f_is_float_3jk89qu(t105, GM_CONV(gm_csguankko4lmhd, t106));
                        if (!t107) {
                            void * t108 = gm_l5;
                            gm_t88rcucg1b7uk t109 = gm_l14;
                            gm_t53at80pfdus3 t110;
                            gm_f_kind_jcnt4hm((void *)&t110, t108, GM_CONV(gm_csguankko4lmhd, t109));
                            gm_t53at80pfdus3 t111 = t110;
                            uint8_t t112;
                            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t111) + 0));
                            if (t115 != ((uint8_t)19ULL)) goto mn114;
                            t112 = ((uint8_t)1);
                            goto me113;
mn114:;
                            t112 = ((uint8_t)0);
                            goto me113;
mn116:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 98, 43);
me113:;
                            t104 = t112;
                        } else t104 = 1;
                        if (!t104) goto ie103;
                        uint8_t t117 = 0;
                        uint8_t t118;
                        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        if (t121 != ((uint8_t)2ULL)) goto mn120;
                        t118 = ((uint8_t)1);
                        goto me119;
mn120:;
                        t118 = ((uint8_t)0);
                        goto me119;
mn122:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 98, 78);
me119:;
                        if (!t118) {
                            uint8_t t123 = 0;
                            uint8_t t124 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
                            if (t124) {
                                uint8_t t125 = gm_l13;
                                t123 = t125;
                            } else t123 = 0;
                            t117 = t123;
                        } else t117 = 1;
                        if (!t117) goto ie103;
                        {
                            void * t128 = gm_l0;
                            void * t129 = GM_LD(void *, (((uint8_t *)t128) + 0));
                            gm_b16_8 t130;
                            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s0rphlcbno91v));
                            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(7));
                            gm_t9805qtonbsag t131 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t129, t130);
                            if (gm_unw.kind) {
                                goto gm_ll39;
                            }
                            gm_t9805qtonbsag t132 = t131;
                            if (!((((uint8_t *)&t132))[4] != 0)) goto ie127;
                            uint32_t t133 = GM_LD(uint32_t, ((uint8_t *)&t132));
                            gm_l15 = t133;
                            {
                            }
                            {
                                void * t135 = gm_l0;
                                void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
                                uint32_t t137 = gm_l15;
                                {
                                    gm_t88rcucg1b7uk t139 = gm_l14;
                                    gm_tnqk2qt7j42o9 t140;
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t140 + 0), t139);
                                    gm_l16 = t140;
                                }
                                void * t141 = ((uint8_t *)&gm_l16);
                                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 101, 46);
                                gm_b16_8 t142;
                                GM_ST(void *, (uint8_t *)&t142, (void *)(((uint8_t *)t141 + (uintptr_t)0 * 4)));
                                GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                                gm_b16_8 t143 = t142;
                                gm_t88rcucg1b7uk t144 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t136, t137, t143);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                                gm_l17 = t144;
                            }
                            {
                                {
                                    void * t146 = gm_l0;
                                    void * t147 = (void *)(((uint8_t *)t146) + 96);
                                    void * t148 = gm_l5;
                                    gm_t88rcucg1b7uk t149 = gm_l11;
                                    gm_t88rcucg1b7uk t150 = gm_l17;
                                    gm_t9qkh0rsooc8d t151 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t147, t148, t149, t150);
                                    if (gm_unw.kind) {
                                        goto gm_ll39;
                                    }
                                }
                            }
                            goto ix127;
ie127:;
ix127:;
                        }
                        goto ix103;
ie103:;
ix103:;
ix85:;
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 90, 5);
me43:;
                }
lc35:;
                {
                    uint64_t t153 = gm_l9;
                    uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                    uint64_t t156 = r154;
                    gm_l9 = t156;
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
        {
            {
                void * t159 = gm_l2;
                gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t159) + 0));
                void * t161 = gm_l3;
                gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t161) + 0));
                gm_tqsk8dt6ib3qq t163;
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t163 + 0), t160);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t163 + 4), t162);
                gm_l18 = t163;
            }
            void * t164 = ((uint8_t *)&gm_l18);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 106, 5);
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(((uint8_t *)t164 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t166 = t165;
            gm_l19 = t166;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t168 = gm_l20;
                uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t170 = (t168 < t169);
                if (!t170) goto lb167;
                {
                    uint64_t t173 = gm_l20;
                    uint64_t t174 = (uintptr_t)t173;
                    uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t174 * 4));
                    gm_l21 = t178;
                    {
                        void * t181 = gm_l0;
                        gm_t88rcucg1b7uk t182 = gm_l21;
                        uint8_t t183 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t181, t182);
                        if (gm_unw.kind) {
                            goto gm_ll171;
                        }
                        if (!t183) goto ie180;
                        {
                            {
                                void * t186 = gm_l0;
                                gm_t88rcucg1b7uk t187 = gm_l21;
                                gm_tl874t2atnlh3 t188 = gm_l4;
                                gm_t88rcucg1b7uk t189 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t186, t187, t188);
                                if (gm_unw.kind) {
                                    goto gm_ll171;
                                }
                            }
                        }
                        goto ix180;
ie180:;
ix180:;
                    }
                    goto me176;
mn177:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 106, 5);
me176:;
                }
lc167:;
                {
                    uint64_t t191 = gm_l20;
                    uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                    uint64_t t194 = r192;
                    gm_l20 = t194;
                }
            }
gm_ll171:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb167; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc167; }
            }
            goto gm_lad;
lb167:;
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
