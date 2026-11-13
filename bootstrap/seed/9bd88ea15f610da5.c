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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csmp0r3q7nu934 { uint32_t f0; uint64_t f1; void * f2; } gm_csmp0r3q7nu934;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct gm_cs2js9o3uvfb1h { gm_cseahvg8v7p0vs f0; uint64_t f1; } gm_cs2js9o3uvfb1h;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct gm_csedogq9e4rp5r { uint32_t f0; gm_cseahvg8v7p0vs f1; } gm_csedogq9e4rp5r;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
typedef struct gm_cskqtm7tpd0qif { gm_csfq64m1m1rt6m f0; uint64_t f1; } gm_cskqtm7tpd0qif;
_Static_assert(sizeof(gm_csmp0r3q7nu934) == 24 && _Alignof(gm_csmp0r3q7nu934) == 8 && offsetof(gm_csmp0r3q7nu934, f0) == 0 && offsetof(gm_csmp0r3q7nu934, f1) == 8 && offsetof(gm_csmp0r3q7nu934, f2) == 16, "layout of `PendingBody`");
typedef struct gm_csuqtrgc8st3af { uint64_t f0; uint64_t f1; gm_cseahvg8v7p0vs f2; } gm_csuqtrgc8st3af;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint64_t f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0nqnmillkbs6;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t3jim1gvsdr87;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_tm98ap4112hee f0; uint64_t f1; } gm_t5nmvq97ncshv;
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_tg23pq9a29gh1;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint32_t f3; } gm_thrm96fhhdpt5;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; } gm_tn96msocmj81h;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; uint64_t f1; } gm_trevnmecc5pjt;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
_Static_assert(sizeof(gm_cs2js9o3uvfb1h) == 24 && _Alignof(gm_cs2js9o3uvfb1h) == 8 && offsetof(gm_cs2js9o3uvfb1h, f0) == 0 && offsetof(gm_cs2js9o3uvfb1h, f1) == 16, "layout of `Shielded`");
_Static_assert(sizeof(gm_csedogq9e4rp5r) == 16 && _Alignof(gm_csedogq9e4rp5r) == 4 && offsetof(gm_csedogq9e4rp5r, f0) == 0 && offsetof(gm_csedogq9e4rp5r, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_cskqtm7tpd0qif) == 16 && _Alignof(gm_cskqtm7tpd0qif) == 8 && offsetof(gm_cskqtm7tpd0qif, f0) == 0 && offsetof(gm_cskqtm7tpd0qif, f1) == 8, "layout of `Frame`");
_Static_assert(sizeof(gm_csuqtrgc8st3af) == 32 && _Alignof(gm_csuqtrgc8st3af) == 8 && offsetof(gm_csuqtrgc8st3af, f0) == 0 && offsetof(gm_csuqtrgc8st3af, f1) == 8 && offsetof(gm_csuqtrgc8st3af, f2) == 16, "layout of `Edge`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0nqnmillkbs6) == 32 && _Alignof(gm_t0nqnmillkbs6) == 8 && offsetof(gm_t0nqnmillkbs6, f0) == 0 && offsetof(gm_t0nqnmillkbs6, f1) == 8 && offsetof(gm_t0nqnmillkbs6, f2) == 16, "layout of `Edge`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3jim1gvsdr87) == 40 && _Alignof(gm_t3jim1gvsdr87) == 8 && offsetof(gm_t3jim1gvsdr87, f0) == 0 && offsetof(gm_t3jim1gvsdr87, f1) == 16 && offsetof(gm_t3jim1gvsdr87, f2) == 32, "layout of `DocComment`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5nmvq97ncshv) == 16 && _Alignof(gm_t5nmvq97ncshv) == 8 && offsetof(gm_t5nmvq97ncshv, f0) == 0 && offsetof(gm_t5nmvq97ncshv, f1) == 8, "layout of `Frame`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_b16_8 f2; } gm_t8qqrlhb610a2;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg23pq9a29gh1) == 64 && _Alignof(gm_tg23pq9a29gh1) == 8 && offsetof(gm_tg23pq9a29gh1, f0) == 0 && offsetof(gm_tg23pq9a29gh1, f1) == 16 && offsetof(gm_tg23pq9a29gh1, f2) == 32 && offsetof(gm_tg23pq9a29gh1, f3) == 48 && offsetof(gm_tg23pq9a29gh1, f4) == 56, "layout of `(string, string, string, u64, u64)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thrm96fhhdpt5) == 40 && _Alignof(gm_thrm96fhhdpt5) == 8 && offsetof(gm_thrm96fhhdpt5, f0) == 0 && offsetof(gm_thrm96fhhdpt5, f1) == 16 && offsetof(gm_thrm96fhhdpt5, f2) == 32 && offsetof(gm_thrm96fhhdpt5, f3) == 36, "layout of `(string, string, u32, u32)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tn96msocmj81h) == 136 && _Alignof(gm_tn96msocmj81h) == 8 && offsetof(gm_tn96msocmj81h, f0) == 0 && offsetof(gm_tn96msocmj81h, f1) == 16 && offsetof(gm_tn96msocmj81h, f2) == 32 && offsetof(gm_tn96msocmj81h, f3) == 48 && offsetof(gm_tn96msocmj81h, f4) == 64 && offsetof(gm_tn96msocmj81h, f5) == 72 && offsetof(gm_tn96msocmj81h, f6) == 88 && offsetof(gm_tn96msocmj81h, f7) == 104 && offsetof(gm_tn96msocmj81h, f8) == 120, "layout of `(string, string, string, string, u64, string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trevnmecc5pjt) == 24 && _Alignof(gm_trevnmecc5pjt) == 8 && offsetof(gm_trevnmecc5pjt, f0) == 0 && offsetof(gm_trevnmecc5pjt, f1) == 16, "layout of `Shielded`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; gm_tc70a9dbbmgkg f3; uint8_t f4; uint8_t pe[7]; } gm_tsnno3vacsa4v;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t8qqrlhb610a2) == 56 && _Alignof(gm_t8qqrlhb610a2) == 8 && offsetof(gm_t8qqrlhb610a2, f0) == 0 && offsetof(gm_t8qqrlhb610a2, f1) == 16 && offsetof(gm_t8qqrlhb610a2, f2) == 40, "layout of `Choice`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_tsnno3vacsa4v) == 48 && _Alignof(gm_tsnno3vacsa4v) == 8 && offsetof(gm_tsnno3vacsa4v, f0) == 0 && offsetof(gm_tsnno3vacsa4v, f1) == 8 && offsetof(gm_tsnno3vacsa4v, f2) == 9 && offsetof(gm_tsnno3vacsa4v, f3) == 16 && offsetof(gm_tsnno3vacsa4v, f4) == 40, "layout of `Frame`");
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { gm_t6ok5tvcjc200 f0; gm_t8qqrlhb610a2 f1; } gm_tv2l6796tk7ol;
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_tv2l6796tk7ol) == 96 && _Alignof(gm_tv2l6796tk7ol) == 8 && offsetof(gm_tv2l6796tk7ol, f0) == 0 && offsetof(gm_tv2l6796tk7ol, f1) == 40, "layout of `(key: Key, value: Choice)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t f6; uint8_t p7[3]; uint64_t f7; gm_t9805qtonbsag f8; uint8_t f9; uint8_t f10; uint8_t p11[2]; gm_tkn42u7na3t81 f11; uint8_t p12[4]; gm_t403eokj7ifg9 f12; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; } gm_tjhr0bdm69ios;
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
_Static_assert(sizeof(gm_tjhr0bdm69ios) == 384 && _Alignof(gm_tjhr0bdm69ios) == 8 && offsetof(gm_tjhr0bdm69ios, f0) == 0 && offsetof(gm_tjhr0bdm69ios, f1) == 40 && offsetof(gm_tjhr0bdm69ios, f2) == 48 && offsetof(gm_tjhr0bdm69ios, f3) == 64 && offsetof(gm_tjhr0bdm69ios, f4) == 104 && offsetof(gm_tjhr0bdm69ios, f5) == 144 && offsetof(gm_tjhr0bdm69ios, f6) == 148 && offsetof(gm_tjhr0bdm69ios, f7) == 152 && offsetof(gm_tjhr0bdm69ios, f8) == 160 && offsetof(gm_tjhr0bdm69ios, f9) == 168 && offsetof(gm_tjhr0bdm69ios, f10) == 169 && offsetof(gm_tjhr0bdm69ios, f11) == 172 && offsetof(gm_tjhr0bdm69ios, f12) == 184 && offsetof(gm_tjhr0bdm69ios, f13) == 304 && offsetof(gm_tjhr0bdm69ios, f14) == 344, "layout of `Frame`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
struct gm_fr_clear_ob36n1n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_ogqmkj3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_oe972vj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_19cnq5l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1b5knd6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_24d87sh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2rkvkft {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3lb41jd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6pjjq7g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_70qeg3n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a84p703 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e669g2g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eifqll9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fqd7l7g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kcikdv8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mo0ba23 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nkvsd0r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s9iufar {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_u4kvl49 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vsb2rda {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_86kk1bu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_88khgsi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_89e90o6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_89j9i66 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_8nlen85 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_8r7pelg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_o00nc4s {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_o444f8n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ol3ji66 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oo6l3qa {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_opmir2c {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_push_83inprj {
    gm_frame h;
    gm_tnljbma3s3h16 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_849ebnd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_86t0l9s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_88e5hkq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_89d0k90 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_89v6mq9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8cklqk5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8cr0sgp {
    gm_frame h;
    gm_tjcptv9sagfq8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8h1r7s6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8h1r7s6_2 {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8h1r7s6_3 {
    gm_frame h;
    gm_tsnno3vacsa4v l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8isgrsr {
    gm_frame h;
    gm_t7rh4gj19r55v l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8quniiv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o022ckl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o6f0em6 {
    gm_frame h;
    gm_t3jim1gvsdr87 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o6sr5eu {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ola4v1l {
    gm_frame h;
    gm_tb07pp6njg0bk l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_80s6ctu {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_83amts0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_83g20im {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_850bd8k {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8bl3rpf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8bt91lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8d45hjk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8f62bij {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8fvt97b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8l7j2k6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8mal8et {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o08qncv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o2oupjk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ohmrmof {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_okcqb9e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ontu3di {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_osbhhdi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_our9l6p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remove_om8q0q2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_remove_os7rp0v {
    gm_frame h;
    gm_tv2l6796tk7ol l3; /* x */
    gm_tv2l6796tk7ol ret;
};
struct gm_fr_reserve_8hbo1q9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reserve_8vqecvd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_truncate_ov4pgoq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_80k6mh9 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_815rgbg {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_894ocpq {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8cmm737 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8gmb252 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8ijr07s {
    gm_frame h;
    void * l0; /* l */
    gm_tg23pq9a29gh1 l1; /* args */
    uint8_t l2; /* failed */
    gm_tg23pq9a29gh1 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8jvglca {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8o29aab {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8p0rdr2 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8qoqt62 {
    gm_frame h;
    void * l0; /* l */
    gm_tfeab279c16rr l1; /* args */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8tfqc0p {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_o50khi1 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_oig1hhm {
    gm_frame h;
    void * l0; /* l */
    gm_thrm96fhhdpt5 l1; /* args */
    uint8_t l2; /* failed */
    gm_thrm96fhhdpt5 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_okup0j2 {
    gm_frame h;
    void * l0; /* l */
    gm_tud7oovfvnf43 l1; /* args */
    uint8_t l2; /* failed */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_oouhb7d {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_otf2npe {
    gm_frame h;
    void * l0; /* l */
    gm_tn96msocmj81h l1; /* args */
    uint8_t l2; /* failed */
    gm_tn96msocmj81h l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_otk7ft7 {
    gm_frame h;
    void * l0; /* l */
    gm_tfeab279c16rr l1; /* args */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void gm_f_clear_ob36n1n(void * a0);
__attribute__((aligned(4))) void gm_f_clear_ogqmkj3(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_0lkule6(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4l4svrs(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_738b90k(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_aiebmtn(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cpdaeui(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ft2q3st(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gu7qoap(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0iglrj(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lgqbbc4(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppj8ikt(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ure7n5c(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vndn3a4(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_oe972vj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_19cnq5l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_1b5knd6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_24d87sh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_2rkvkft(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_3lb41jd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_6pjjq7g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_70qeg3n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_a84p703(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_e669g2g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_eifqll9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_fqd7l7g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_kcikdv8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo0ba23(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_nkvsd0r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_s9iufar(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_u4kvl49(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsb2rda(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_list_86kk1bu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_88khgsi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_89e90o6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_89j9i66(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_8nlen85(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_o00nc4s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_o444f8n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_oo6l3qa(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_td8hfgklo8dil gm_f_pop_8sodrk9_c(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_83inprj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_83inprj_c(void * a0, gm_tnljbma3s3h16 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_849ebnd(void * a0, gm_tqs5p4n2tq19v a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_86t0l9s(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_86t0l9s_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_88e5hkq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_88e5hkq_c(void * a0, gm_t3877pk7lv748 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89v6mq9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89v6mq9_c(void * a0, gm_csuqtrgc8st3af a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cklqk5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cklqk5_c(void * a0, gm_t5hdnnftkup6m a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cr0sgp_c(void * a0, gm_tjcptv9sagfq8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6(void * a0, gm_cskqtm7tpd0qif a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_2_c(void * a0, gm_tjhr0bdm69ios a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_3_c(void * a0, gm_tsnno3vacsa4v a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8isgrsr(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8isgrsr_c(void * a0, gm_t7rh4gj19r55v a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8quniiv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8quniiv_c(void * a0, gm_cs2js9o3uvfb1h a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl_c(void * a0, gm_csmp0r3q7nu934 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6f0em6_c(void * a0, gm_t3jim1gvsdr87 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6sr5eu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh_c(void * a0, gm_t5v0c8ia6a5mo a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ola4v1l(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ola4v1l_c(void * a0, gm_tb07pp6njg0bk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_13c41om(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_15b7omr(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4l3ibqn(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mifc6m(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jtvbud(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_78ppoki(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_80s6ctu(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_83amts0(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_83g20im(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_850bd8k(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bl3rpf(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bt91lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8d45hjk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8f62bij(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8fvt97b(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8mal8et(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c2r0eo7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cmj1khh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_efufsip(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gpvm1qb(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mgis1jm(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o2oupjk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ohmrmof(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_okcqb9e(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ontu3di(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_osbhhdi(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_our9l6p(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pm9nn7e(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v1tfl0k(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vl9m9ao(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_remove_om8q0q2(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_os7rp0v(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tv2l6796tk7ol gm_f_remove_os7rp0v_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_8hbo1q9(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_8vqecvd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_80k6mh9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_815rgbg(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_894ocpq(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8cmm737(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252_c(void * a0, gm_tvaas22dl64e0 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s_c(void * a0, gm_tg23pq9a29gh1 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8jvglca(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8jvglca_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8p0rdr2(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62_c(void * a0, gm_tfeab279c16rr a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8tfqc0p(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_o50khi1(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm_c(void * a0, gm_thrm96fhhdpt5 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2_c(void * a0, gm_tud7oovfvnf43 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe_c(void * a0, gm_tn96msocmj81h a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7_c(void * a0, gm_tfeab279c16rr a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1lh34nabv9g2[5];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sc559v8etqgda[19];
extern const uint8_t gm_scedkbh0j6d78[6];
extern const uint8_t gm_schn9bp2k5kqo[47];
extern const uint8_t gm_sci406m9tkvjp[32];
extern const uint8_t gm_sdrhpr2s6028d[6];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_si2ks4lbjbojr[22];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjqjrfqdmp9ao[7];
extern const uint8_t gm_skem4050p6ec1[13];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skk0hh6be703h[20];
extern const uint8_t gm_skp02jnee3dfj[35];
extern const uint8_t gm_sksh6i7obnq40[8];
extern const uint8_t gm_sl1a3p7pocs82[15];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_sm732n1u78vvc[16];
extern const uint8_t gm_sm77ovo2kgv5k[42];
extern const uint8_t gm_sm7s66sg1itc1[4];
extern const uint8_t gm_smdiuajagl2mn[5];
extern const uint8_t gm_smlifs2umbvsq[8];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smpg2ulm74cim[9];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_so25i3en68imc[27];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_spm4139orpkvm[10];
extern const uint8_t gm_sq800qhfmpfk7[39];
extern const uint8_t gm_sqjlosotosjfp[18];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss7iv2kssvf80[7];
extern const uint8_t gm_stqvtvp1hc371[12];
extern const uint8_t gm_sv8s0ogr3iflj[8];
extern const uint8_t gm_svkdb6kaja59r[7];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty9oqmmiu8qc;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
            gm_td8hfgklo8dil t8;
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
        gm_t66j1im9ip128 t17 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t66j1im9ip128 t29 = gm_l2;
        gm_td8hfgklo8dil t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 36, 0);
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[32] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_ob36n1n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ob36n1n F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 72u);
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
inline __attribute__((aligned(4))) void gm_f_clear_ogqmkj3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ogqmkj3 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 72u);
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_oe972vj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_oe972vj F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 361u);
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
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 77, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_efufsip(t14, t15, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) {
                gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t18) + 1));
                gm_tftb1odc739t5 t20;
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t19);
                F.ret = t20;
                goto gm_fret;
            }
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = ((uint8_t *)t24);
            uint64_t t26 = gm_l2;
            uint64_t t27 = (uintptr_t)t26;
            uint64_t t28 = gm_l2;
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
            if (o31) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 81, 36);
            uint64_t t32 = r30;
            uint64_t t33 = (uintptr_t)t32;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t25 + t27 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t27));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = gm_l1;
            gm_b16_8 t37 = t36;
            if (GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t35)), GM_LD(void *, ((uint8_t *)&t37)), GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) * 40);
        }
ix4:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = ((uint8_t *)t40);
        uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t43 = gm_l2;
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r45; bool o46 = __builtin_add_overflow(t43, t44, &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 83, 30);
        uint64_t t47 = r45;
        uint64_t t48 = (uintptr_t)t47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t41 + t42 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t42));
        GM_ST(gm_b16_8, (((uint8_t *)t38) + 0), t49);
    }
    {
        gm_b0_1 t50;
        gm_tftb1odc739t5 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t51 + 1), t50);
        gm_tftb1odc739t5 t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_19cnq5l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_19cnq5l F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8cmm737 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8cmm737 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8cmm737 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1b5knd6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1b5knd6 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8p0rdr2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8p0rdr2 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8p0rdr2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_24d87sh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_24d87sh F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_otf2npe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_otf2npe *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_otf2npe *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2rkvkft(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2rkvkft F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_okup0j2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_okup0j2 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_okup0j2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3lb41jd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3lb41jd F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_otk7ft7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_otk7ft7 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_otk7ft7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6pjjq7g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6pjjq7g F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8ijr07s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8ijr07s *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8ijr07s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_70qeg3n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_70qeg3n F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_o50khi1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_o50khi1 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_o50khi1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a84p703(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a84p703 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8o29aab *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8o29aab *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8o29aab *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e669g2g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e669g2g F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8tfqc0p *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8tfqc0p *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8tfqc0p *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eifqll9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eifqll9 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8gmb252 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8gmb252 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8gmb252 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fqd7l7g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fqd7l7g F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_oig1hhm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_oig1hhm *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_oig1hhm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kcikdv8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kcikdv8 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_oouhb7d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_oouhb7d *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_oouhb7d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo0ba23(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mo0ba23 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_815rgbg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_815rgbg *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_815rgbg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nkvsd0r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nkvsd0r F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8qoqt62 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8qoqt62 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8qoqt62 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s9iufar(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_s9iufar F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_80k6mh9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_80k6mh9 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_80k6mh9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u4kvl49(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_u4kvl49 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_8jvglca *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_8jvglca *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_8jvglca *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsb2rda(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vsb2rda F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_894ocpq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_894ocpq *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_894ocpq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) void gm_f_list_86kk1bu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_86kk1bu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j0iglrj();
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
inline __attribute__((aligned(4))) void gm_f_list_88khgsi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_88khgsi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qm5tp1g();
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
inline __attribute__((aligned(4))) void gm_f_list_89e90o6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_89e90o6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4l4svrs();
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
inline __attribute__((aligned(4))) void gm_f_list_8nlen85(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_8nlen85 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_gu7qoap();
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
inline __attribute__((aligned(4))) void gm_f_list_o444f8n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_o444f8n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_cpdaeui();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ol3ji66 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_evi8iki();
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
inline __attribute__((aligned(4))) void gm_f_list_oo6l3qa(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oo6l3qa F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_aiebmtn();
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
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_opmir2c F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8rccll4();
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
extern __attribute__((aligned(4))) void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_td8hfgklo8dil gm_f_pop_8sodrk9_c(void * a0) { gm_td8hfgklo8dil gm_r; gm_f_pop_8sodrk9(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_83inprj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_83inprj F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_dijpe9t();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_cmj1khh(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tnljbma3s3h16 t32 = GM_LD(gm_tnljbma3s3h16, ((uint8_t *)a1));
        GM_ST(gm_tnljbma3s3h16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 64), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_83inprj_c(void * a0, gm_tnljbma3s3h16 a1) { return gm_f_push_83inprj(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_849ebnd(void * a0, gm_tqs5p4n2tq19v a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_849ebnd F;
    void * gm_l0; /* l */
    gm_tqs5p4n2tq19v gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 269u);
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
            gm_b16_8 t11 = gm_f_empty_slice_e8istsh();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_4l3ibqn(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 2)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tqs5p4n2tq19v t32 = gm_l1;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 2), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_86t0l9s(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_86t0l9s F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_fklmfmo();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_mgis1jm(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t6j6smj7qoctu t32 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_86t0l9s_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_push_86t0l9s(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_88e5hkq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_88e5hkq F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_aiebmtn();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_v1tfl0k(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t3877pk7lv748 t32 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a1));
        GM_ST(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_88e5hkq_c(void * a0, gm_t3877pk7lv748 a1) { return gm_f_push_88e5hkq(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_89d0k90 F;
    void * gm_l0; /* l */
    gm_t9d4sf0vqn7dt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t9d4sf0vqn7dt, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_j6dmic1();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_5jtvbud(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t9d4sf0vqn7dt t32 = gm_l1;
        GM_ST(gm_t9d4sf0vqn7dt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 16), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89v6mq9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_89v6mq9 F;
    void * gm_l0; /* l */
    gm_t0nqnmillkbs6 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_738b90k();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_c2r0eo7(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t0nqnmillkbs6 t32 = GM_LD(gm_t0nqnmillkbs6, ((uint8_t *)a1));
        GM_ST(gm_t0nqnmillkbs6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89v6mq9_c(void * a0, gm_csuqtrgc8st3af a1) { return gm_f_push_89v6mq9(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cklqk5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8cklqk5 F;
    void * gm_l0; /* l */
    gm_t5hdnnftkup6m gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_0lkule6();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_gpvm1qb(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t5hdnnftkup6m t32 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a1));
        GM_ST(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cklqk5_c(void * a0, gm_t5hdnnftkup6m a1) { return gm_f_push_8cklqk5(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8cr0sgp F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 353u);
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
            gm_b16_8 t11 = gm_f_empty_slice_8nf04m8();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_15b7omr(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tjcptv9sagfq8 t32 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)a1));
        GM_ST(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 88), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cr0sgp_c(void * a0, gm_tjcptv9sagfq8 a1) { return gm_f_push_8cr0sgp(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6(void * a0, gm_cskqtm7tpd0qif a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8h1r7s6 F;
    void * gm_l0; /* l */
    gm_t5nmvq97ncshv gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t5nmvq97ncshv, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_p5lo8ed();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_r51vrc8(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t5nmvq97ncshv t32 = gm_l1;
        GM_ST(gm_t5nmvq97ncshv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 16), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8h1r7s6_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 265u);
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
            gm_b16_8 t11 = gm_f_empty_slice_p5lo8ed_2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_r51vrc8_2(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 384)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        memmove(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 384), ((uint8_t *)a1), 384);
    }
    {
        void * t32 = gm_l0;
        gm_b16_8 t33 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t32) + 0), t33);
    }
    {
        gm_b0_1 t34;
        gm_tftb1odc739t5 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        ((uint8_t *)&t35)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t35 + 1), t34);
        gm_tftb1odc739t5 t36 = t35;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_2_c(void * a0, gm_tjhr0bdm69ios a1) { return gm_f_push_8h1r7s6_2(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8h1r7s6_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_p5lo8ed_3();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_r51vrc8_3(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tsnno3vacsa4v t32 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)a1));
        GM_ST(gm_tsnno3vacsa4v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 48), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_3_c(void * a0, gm_tsnno3vacsa4v a1) { return gm_f_push_8h1r7s6_3(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8isgrsr(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8isgrsr F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 409u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_ure7n5c();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_pm9nn7e(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t7rh4gj19r55v t32 = F.l1;
        GM_ST(gm_t7rh4gj19r55v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 144), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8isgrsr_c(void * a0, gm_t7rh4gj19r55v a1) { return gm_f_push_8isgrsr(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8quniiv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8quniiv F;
    void * gm_l0; /* l */
    gm_trevnmecc5pjt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_lgqbbc4();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_83g20im(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_trevnmecc5pjt t32 = GM_LD(gm_trevnmecc5pjt, ((uint8_t *)a1));
        GM_ST(gm_trevnmecc5pjt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8quniiv_c(void * a0, gm_cs2js9o3uvfb1h a1) { return gm_f_push_8quniiv(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o022ckl F;
    void * gm_l0; /* l */
    gm_t5r2drn9pjqkt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_kgfehvr();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_4mifc6m(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t5r2drn9pjqkt t32 = GM_LD(gm_t5r2drn9pjqkt, ((uint8_t *)a1));
        GM_ST(gm_t5r2drn9pjqkt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl_c(void * a0, gm_csmp0r3q7nu934 a1) { return gm_f_push_o022ckl(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o6f0em6 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_hc7thq6();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_13c41om(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t3jim1gvsdr87 t32 = GM_LD(gm_t3jim1gvsdr87, ((uint8_t *)a1));
        GM_ST(gm_t3jim1gvsdr87, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6f0em6_c(void * a0, gm_t3jim1gvsdr87 a1) { return gm_f_push_o6f0em6(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6sr5eu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o6sr5eu F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 297u);
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
            gm_b16_8 t11 = gm_f_empty_slice_ft2q3st();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_vl9m9ao(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_b16_8 t32 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 16), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_he774nl();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_7sj41hq(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t5v0c8ia6a5mo t32 = F.l1;
        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 56), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh_c(void * a0, gm_t5v0c8ia6a5mo a1) { return gm_f_push_o8u01sh(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ola4v1l(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ola4v1l F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 489u);
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
            gm_b16_8 t11 = gm_f_empty_slice_f6g91fe();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_78ppoki(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 224)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tb07pp6njg0bk t32 = GM_LD(gm_tb07pp6njg0bk, ((uint8_t *)a1));
        GM_ST(gm_tb07pp6njg0bk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 224), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ola4v1l_c(void * a0, gm_tb07pp6njg0bk a1) { return gm_f_push_ola4v1l(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_80s6ctu(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_80s6ctu F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_83amts0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_83amts0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_83g20im(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_83g20im F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_850bd8k(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_850bd8k F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bl3rpf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8bl3rpf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bt91lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8bt91lk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8d45hjk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8d45hjk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8f62bij(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8f62bij F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8fvt97b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8fvt97b F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8l7j2k6 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8mal8et(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8mal8et F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o08qncv F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o2oupjk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o2oupjk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)160ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ohmrmof(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ohmrmof F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_okcqb9e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_okcqb9e F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ontu3di(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ontu3di F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_osbhhdi(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_osbhhdi F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_our9l6p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_our9l6p F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_remove_om8q0q2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_om8q0q2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* x */
    GM_STACK_CHECK(&F, 417u);
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
        uint64_t t4 = gm_l1;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srcrgc9bkquf6nr, 104, 10);
        uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t6) + 0)) + t5 * 8));
        gm_l3 = t8;
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
        uint64_t t12 = gm_l1;
        uint64_t t13 = (uintptr_t)t12;
        uint64_t t14 = gm_l2;
        uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 105, 28);
        uint64_t t17 = r15;
        uint64_t t18 = (uintptr_t)t17;
        if (t13 > t18 || t18 > (uintptr_t)t10) gm_trap_slice(t13, t18, t10, gm_srcrgc9bkquf6nr, 105, 17);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t11 + t13 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t13));
        gm_b16_8 t20 = t19;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 0) + 8);
        void * t23 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 0)));
        uint64_t t24 = gm_l1;
        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
        if (o26) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 105, 44);
        uint64_t t27 = r25;
        uint64_t t28 = (uintptr_t)t27;
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        if (t28 > t30 || t30 > (uintptr_t)t22) gm_trap_slice(t28, t30, t22, gm_srcrgc9bkquf6nr, 105, 36);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t23 + t28 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t28));
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_b16_8 t34 = t33;
        if (GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t20)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) * 8);
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)));
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 106, 26);
        uint64_t t43 = r41;
        uint64_t t44 = (uintptr_t)t43;
        if (t39 > t44 || t44 > (uintptr_t)t37) gm_trap_slice(t39, t44, t37, gm_srcrgc9bkquf6nr, 106, 15);
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t38 + t39 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t45);
    }
    {
        uint64_t t46 = gm_l3;
        uint8_t t47 = 0;
        F.ret = t46;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_remove_os7rp0v(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_os7rp0v F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 472u);
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
        uint64_t t4 = gm_l1;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srcrgc9bkquf6nr, 104, 10);
        gm_tv2l6796tk7ol t8 = GM_LD(gm_tv2l6796tk7ol, ((uint8_t *)GM_LD(void *, (((uint8_t *)t6) + 0)) + t5 * 96));
        F.l3 = t8;
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
        uint64_t t12 = gm_l1;
        uint64_t t13 = (uintptr_t)t12;
        uint64_t t14 = gm_l2;
        uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 105, 28);
        uint64_t t17 = r15;
        uint64_t t18 = (uintptr_t)t17;
        if (t13 > t18 || t18 > (uintptr_t)t10) gm_trap_slice(t13, t18, t10, gm_srcrgc9bkquf6nr, 105, 17);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t11 + t13 * 96)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t13));
        gm_b16_8 t20 = t19;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 0) + 8);
        void * t23 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 0)));
        uint64_t t24 = gm_l1;
        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
        if (o26) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 105, 44);
        uint64_t t27 = r25;
        uint64_t t28 = (uintptr_t)t27;
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        if (t28 > t30 || t30 > (uintptr_t)t22) gm_trap_slice(t28, t30, t22, gm_srcrgc9bkquf6nr, 105, 36);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t23 + t28 * 96)));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t28));
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_b16_8 t34 = t33;
        if (GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t20)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) * 96);
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)));
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 106, 26);
        uint64_t t43 = r41;
        uint64_t t44 = (uintptr_t)t43;
        if (t39 > t44 || t44 > (uintptr_t)t37) gm_trap_slice(t39, t44, t37, gm_srcrgc9bkquf6nr, 106, 15);
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t38 + t39 * 96)));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t45);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 96);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tv2l6796tk7ol gm_f_remove_os7rp0v_c(void * a0, uint64_t a1) { gm_tv2l6796tk7ol gm_r; gm_f_remove_os7rp0v(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_8hbo1q9(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_8hbo1q9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 159u);
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
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 18, 8);
        uint64_t t10 = r8;
        uint64_t t11 = gm_l1;
        uint8_t t12 = (t10 >= t11);
        if (!t12) goto ie4;
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = gm_l1;
        gm_b16_8 t20 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_tftb1odc739t5 t23 = gm_f_regrow_2onqfca(t18, t19, t22);
        gm_tftb1odc739t5 t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_8vqecvd(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_8vqecvd F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 159u);
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
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 18, 8);
        uint64_t t10 = r8;
        uint64_t t11 = gm_l1;
        uint8_t t12 = (t10 >= t11);
        if (!t12) goto ie4;
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = gm_l1;
        gm_b16_8 t20 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_tftb1odc739t5 t23 = gm_f_regrow_0jmnnpk(t18, t19, t22);
        gm_tftb1odc739t5 t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ov4pgoq F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 113u);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 1)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_80k6mh9(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_80k6mh9 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_s9iufar, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_skem4050p6ec1, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_si2ks4lbjbojr, 21)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_815rgbg(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_815rgbg F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_mo0ba23, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sasea0sobfkrs, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s6t4r831mukdr, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_894ocpq(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_894ocpq F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_vsb2rda, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_ss7iv2kssvf80, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8cmm737(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8cmm737 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_19cnq5l, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sjeopa747clpb, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8gmb252 F;
    gm_tvaas22dl64e0 gm_l1; /* args */
    gm_tvaas22dl64e0 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tvaas22dl64e0 t3 = GM_LD(gm_tvaas22dl64e0, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_eifqll9, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            uint16_t t7 = GM_LD(uint16_t, (((uint8_t *)&gm_l3) + 16));
            uint16_t t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty9oqmmiu8qc, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252_c(void * a0, gm_tvaas22dl64e0 a1) { return gm_f_write_8gmb252(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8ijr07s F;
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tg23pq9a29gh1 t3 = GM_LD(gm_tg23pq9a29gh1, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_6pjjq7g, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 48));
            uint64_t t12 = t11;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 56));
            uint64_t t14 = t13;
            gm_closure t15 = t4;
            gm_node *ccx16 = t15.cx;
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sm732n1u78vvc, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_spm4139orpkvm, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_skfdo9qggtrbp, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_tyb2nno7n67r, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sqrh6i8djv6b4, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_tyb2nno7n67r, ((uint8_t *)&t14), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_snr5o5tak6405, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t18 = F.l2;
        if (!t18) goto ie17;
        {
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t21;
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
            gm_tftb1odc739t5 t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_b0_1 t24;
        gm_tftb1odc739t5 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        ((uint8_t *)&t25)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t25 + 1), t24);
        gm_tftb1odc739t5 t26 = t25;
        uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
        F.ret = t25;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s_c(void * a0, gm_tg23pq9a29gh1 a1) { return gm_f_write_8ijr07s(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8jvglca(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8jvglca F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_u4kvl49, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8jvglca_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_8jvglca(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8o29aab F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_a84p703, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_som0q53jqvjh3, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_scedkbh0j6d78, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_smdiuajagl2mn, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_8o29aab(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8p0rdr2(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8p0rdr2 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_1b5knd6, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sjqjrfqdmp9ao, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8qoqt62 F;
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_nkvsd0r, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            gm_closure t11 = t4;
            gm_node *ccx12 = t11.cx;
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_svkdb6kaja59r, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t14 = F.l2;
        if (!t14) goto ie13;
        {
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
            gm_tftb1odc739t5 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b0_1 t20;
        gm_tftb1odc739t5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 1), t20);
        gm_tftb1odc739t5 t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62_c(void * a0, gm_tfeab279c16rr a1) { return gm_f_write_8qoqt62(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8tfqc0p(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8tfqc0p F;
    gm_t2lqu8jsj511d gm_l1; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_e669g2g, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_skk0hh6be703h, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tyb2nno7n67r, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_smlifs2umbvsq, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tyb2nno7n67r, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sq800qhfmpfk7, 38)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_o50khi1(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_o50khi1 F;
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_70qeg3n, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_skp02jnee3dfj, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_tyb2nno7n67r, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sqjlosotosjfp, 17)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_oig1hhm F;
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_thrm96fhhdpt5 t3 = GM_LD(gm_thrm96fhhdpt5, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_fqd7l7g, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 32));
            uint32_t t10 = t9;
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 36));
            uint32_t t12 = t11;
            gm_closure t13 = t4;
            gm_node *ccx14 = t13.cx;
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_s1lh34nabv9g2, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty467d5cm1d5, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty467d5cm1d5, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t16 = F.l2;
        if (!t16) goto ie15;
        {
            gm_t9qkh0rsooc8d t18;
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t19;
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
            gm_tftb1odc739t5 t20 = t19;
            uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_b0_1 t22;
        gm_tftb1odc739t5 t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t23 + 1), t22);
        gm_tftb1odc739t5 t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm_c(void * a0, gm_thrm96fhhdpt5 a1) { return gm_f_write_oig1hhm(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_okup0j2 F;
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_2rkvkft, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t14 = t13;
            gm_closure t15 = t4;
            gm_node *ccx16 = t15.cx;
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_tybihgc69jv3, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sc559v8etqgda, 18)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t14), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sdrhpr2s6028d, 5)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t18 = F.l2;
        if (!t18) goto ie17;
        {
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t21;
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
            gm_tftb1odc739t5 t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_b0_1 t24;
        gm_tftb1odc739t5 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        ((uint8_t *)&t25)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t25 + 1), t24);
        gm_tftb1odc739t5 t26 = t25;
        uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
        F.ret = t25;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2_c(void * a0, gm_tud7oovfvnf43 a1) { return gm_f_write_okup0j2(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_oouhb7d F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_kcikdv8, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_oouhb7d(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_otf2npe F;
    GM_STACK_CHECK(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tn96msocmj81h t3 = GM_LD(gm_tn96msocmj81h, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_24d87sh, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t12 = t11;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 64));
            uint64_t t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 72));
            gm_b16_8 t16 = t15;
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 88));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 104));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 120));
            gm_b16_8 t22 = t21;
            gm_closure t23 = t4;
            gm_node *ccx24 = t23.cx;
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_secrjke11f3ph, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_sm7s66sg1itc1, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_som0q53jqvjh3, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_schn9bp2k5kqo, 46)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_sm77ovo2kgv5k, 41)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_tyb2nno7n67r, ((uint8_t *)&t14), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_sl1a3p7pocs82, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_smpg2ulm74cim, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t18), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t20), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_so25i3en68imc, 26)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t23.code, t23.env, ccx24, &gm_ty0iqiubfm7e, ((uint8_t *)&t22), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t23.code, t23.env, ccx24, gm_sci406m9tkvjp, 31)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t26 = F.l2;
        if (!t26) goto ie25;
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t29;
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 1), t28);
            gm_tftb1odc739t5 t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        gm_b0_1 t32;
        gm_tftb1odc739t5 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        ((uint8_t *)&t33)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t33 + 1), t32);
        gm_tftb1odc739t5 t34 = t33;
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t33;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe_c(void * a0, gm_tn96msocmj81h a1) { return gm_f_write_otf2npe(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_otk7ft7 F;
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_3lb41jd, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            gm_closure t11 = t4;
            gm_node *ccx12 = t11.cx;
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_sksh6i7obnq40, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_sv8s0ogr3iflj, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_stqvtvp1hc371, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t14 = F.l2;
        if (!t14) goto ie13;
        {
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
            gm_tftb1odc739t5 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b0_1 t20;
        gm_tftb1odc739t5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 1), t20);
        gm_tftb1odc739t5 t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7_c(void * a0, gm_tfeab279c16rr a1) { return gm_f_write_otk7ft7(a0, &a1); }
