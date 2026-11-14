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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t1j70din2upc4;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t f7; uint8_t p8[3]; uint32_t f8; uint32_t f9; } gm_t98k64j4g45rk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t88rcucg1b7uk f2; uint32_t f3; gm_t88rcucg1b7uk f4; uint8_t pe[4]; } gm_t0h5lk09e9p5r;
_Static_assert(sizeof(gm_t1j70din2upc4) == 32 && _Alignof(gm_t1j70din2upc4) == 8 && offsetof(gm_t1j70din2upc4, f0) == 0 && offsetof(gm_t1j70din2upc4, f1) == 8 && offsetof(gm_t1j70din2upc4, f2) == 16 && offsetof(gm_t1j70din2upc4, f3) == 24, "layout of `(base: ^Node, index: u32, off: u64, packed: bool)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t98k64j4g45rk) == 40 && _Alignof(gm_t98k64j4g45rk) == 4 && offsetof(gm_t98k64j4g45rk, f0) == 0 && offsetof(gm_t98k64j4g45rk, f1) == 4 && offsetof(gm_t98k64j4g45rk, f2) == 8 && offsetof(gm_t98k64j4g45rk, f3) == 12 && offsetof(gm_t98k64j4g45rk, f4) == 16 && offsetof(gm_t98k64j4g45rk, f5) == 20 && offsetof(gm_t98k64j4g45rk, f6) == 24 && offsetof(gm_t98k64j4g45rk, f7) == 28 && offsetof(gm_t98k64j4g45rk, f8) == 32 && offsetof(gm_t98k64j4g45rk, f9) == 36, "layout of `VmLoop`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tdvcgvrf81sbd;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { void * f0; void * f1; gm_t9qkh0rsooc8d f2; uint8_t p3[7]; uint64_t f3; uint64_t f4; } gm_tp0fa5qv7rqnn;
_Static_assert(sizeof(gm_t0h5lk09e9p5r) == 40 && _Alignof(gm_t0h5lk09e9p5r) == 8 && offsetof(gm_t0h5lk09e9p5r, f0) == 0 && offsetof(gm_t0h5lk09e9p5r, f1) == 16 && offsetof(gm_t0h5lk09e9p5r, f2) == 24 && offsetof(gm_t0h5lk09e9p5r, f3) == 28 && offsetof(gm_t0h5lk09e9p5r, f4) == 32, "layout of `(host: []u8, addr: u64, whole: Ty, off: u32, fty: Ty)`");
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tbl8il92l97ut v1; } f1; } gm_t9b5r56q9jk9c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tdvcgvrf81sbd) == 24 && _Alignof(gm_tdvcgvrf81sbd) == 8 && offsetof(gm_tdvcgvrf81sbd, f0) == 0 && offsetof(gm_tdvcgvrf81sbd, f1) == 8 && offsetof(gm_tdvcgvrf81sbd, f2) == 12 && offsetof(gm_tdvcgvrf81sbd, f3) == 16, "layout of `(base: ^Node, index: u32, bit_off: u32, fty: Ty)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; uint64_t v1; uint64_t v2; gm_t0h5lk09e9p5r v3; } f1; } gm_tou12vuc19e0s;
_Static_assert(sizeof(gm_tp0fa5qv7rqnn) == 40 && _Alignof(gm_tp0fa5qv7rqnn) == 8 && offsetof(gm_tp0fa5qv7rqnn, f0) == 0 && offsetof(gm_tp0fa5qv7rqnn, f1) == 8 && offsetof(gm_tp0fa5qv7rqnn, f2) == 16 && offsetof(gm_tp0fa5qv7rqnn, f3) == 24 && offsetof(gm_tp0fa5qv7rqnn, f4) == 32, "layout of `(base: ^Node, index: ^Node, seq: SeqKind, esz: u64, len: u64)`");
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t9b5r56q9jk9c) == 32 && _Alignof(gm_t9b5r56q9jk9c) == 8 && offsetof(gm_t9b5r56q9jk9c, f0) == 0 && offsetof(gm_t9b5r56q9jk9c, f1) == 8, "layout of `Flow!(frame: ^Frame, label: u32)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tou12vuc19e0s v1; } f1; } gm_tl1r7a2fn1ev6;
_Static_assert(sizeof(gm_tou12vuc19e0s) == 48 && _Alignof(gm_tou12vuc19e0s) == 8 && offsetof(gm_tou12vuc19e0s, f0) == 0 && offsetof(gm_tou12vuc19e0s, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tl1r7a2fn1ev6) == 56 && _Alignof(gm_tl1r7a2fn1ev6) == 8 && offsetof(gm_tl1r7a2fn1ev6, f0) == 0 && offsetof(gm_tl1r7a2fn1ev6, f1) == 8, "layout of `Flow!Place`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_0fd5f52 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_whole_u4vfvvk {
    gm_frame h;
    gm_tdlro53ljvkl0 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ev_slice_i5uin87 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_label_dst_b5cat1e {
    gm_frame h;
    gm_t98k64j4g45rk l6; /* lp */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_local_addr_o51qq3k {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_local_place_gp2fins {
    gm_frame h;
    gm_tl1r7a2fn1ev6 ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_place_47rfqe5 {
    gm_frame h;
    gm_t0h5lk09e9p5r l15; /* y */
    gm_tp0fa5qv7rqnn l21; /* x */
    gm_tou12vuc19e0s l23; /* p */
    gm_tl1r7a2fn1ev6 ret;
};
struct gm_fr_read_local_n8sc5ua {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_read_place_fcc38bb {
    gm_frame h;
    gm_tou12vuc19e0s l2; /* p */
    gm_t0h5lk09e9p5r l8; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_target_bdf5i1e {
    gm_frame h;
    gm_t9b5r56q9jk9c ret;
};
struct gm_fr_set_local_ug1vgfu {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_write_place_pd5r6up {
    gm_frame h;
    gm_tou12vuc19e0s l2; /* p */
    gm_t0h5lk09e9p5r l8; /* x */
    gm_tcfa6l7reoosn ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_alloc_temp_9cmh124(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint64_t a3);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_bits_from_raw_el4tjdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, unsigned __int128 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits_raw_t6dqs65(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bits_whole_u4vfvvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint64_t a3, gm_t88rcucg1b7uk a4);
__attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ev_index_bie377m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_ev_slice_i5uin87(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4, void * a5, uint8_t a6, gm_t9qkh0rsooc8d a7, uint64_t a8, uint64_t a9, gm_b16_8 a10);
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_2hd15t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_3pa9rer(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_frame_up_ghkkuqg(void * a0, uint32_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_label_dst_b5cat1e(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_label_dst_b5cat1e_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_local_place_gp2fins(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) void gm_f_place_47rfqe5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_read_local_n8sc5ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_place_fcc38bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) void gm_f_resolve_target_bdf5i1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_te9hrk0u35rev a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_set_local_ug1vgfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_at_ktabiji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_write_place_pd5r6up(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
extern const uint8_t gm_s3hpe56o884a0[42];
extern const uint8_t gm_s718drvtpg9kd[49];
extern const uint8_t gm_sco971hnpq7nk[49];
extern const uint8_t gm_sevktc6u3mas1[52];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sgu2tengos987[41];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrvqeupiu1766[];
extern const uint8_t gm_sv0u97lrebfd4[41];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_0fd5f52 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint32_t t4 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t2, t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_b16_8 t6 = t5;
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t6)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        if (GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l2;
                        uint8_t t20 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3) {
    struct gm_fr_local_addr_o51qq3k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l4 = t4;
    }
    {
        uint64_t t6 = gm_l4;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l2;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6hq50r4rk7e6, 193, 8);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 64)) + t7 * 8));
        uint8_t t11 = (!(t10 == ((uint64_t)0ULL)));
        if (!t11) goto ie5;
        {
            uint64_t t13 = gm_l4;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l2;
            uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 64) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src6hq50r4rk7e6, 193, 36);
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 64)) + t14 * 8));
            gm_t833u93mr4iob t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t18 + 8), t17);
            gm_t833u93mr4iob t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t21 = gm_l0;
        gm_tl874t2atnlh3 t22 = gm_l1;
        void * t23 = gm_l2;
        uint32_t t24 = gm_l3;
        gm_f_local_alloc_qm9q4fo((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t21, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bits_whole_u4vfvvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint64_t a3, gm_t88rcucg1b7uk a4) {
    struct gm_fr_bits_whole_u4vfvvk F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* host */
    uint64_t gm_l3; /* addr */
    gm_t88rcucg1b7uk gm_l4; /* whole */
    gm_t88rcucg1b7uk gm_l5; /* u */
    uint64_t gm_l6; /* n */
    gm_t51b2en4qp02s gm_l7; /* b */
    GM_STACK_CHECK(&F, 795u);
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
        gm_t88rcucg1b7uk t3 = gm_l4;
        gm_tkn42u7na3t81 t4 = gm_f_bits_struct_ty_22ue20s(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_tkn42u7na3t81 t5 = t4;
        gm_t88rcucg1b7uk t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_t88rcucg1b7uk t8 = gm_l4;
            t6 = t8;
        }
        gm_l5 = t6;
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l5;
        uint64_t t11 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t12 = gm_f_us_cuv01fp(t11);
        gm_l6 = t12;
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t15 = (t14 > ((uint64_t)0ULL));
        if (!t15) goto ie13;
        {
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t19 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t21 = gm_l6;
            uint64_t t22 = (uintptr_t)t21;
            if (t20 > t22 || t22 > (uintptr_t)t18) gm_trap_slice(t20, t22, t18, gm_srcrvqeupiu1766, 158, 41);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t19 + t20 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t20));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            unsigned __int128 t26 = gm_f_get_uint_bq8c4b7(t17, t25);
            gm_tdlro53ljvkl0 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t27 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t27 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t27 + 40, 0);
            ((uint8_t *)&t27)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t27 + 16), t26);
            gm_tdlro53ljvkl0 t28 = t27;
            uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
            F.ret = t27;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
    }
    {
        void * t30 = gm_l0;
        gm_tl874t2atnlh3 t31 = gm_l1;
        uint64_t t32 = gm_l3;
        void * t33 = gm_l0;
        gm_t88rcucg1b7uk t34 = gm_l5;
        uint64_t t35 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t36 = ((uint8_t *)&gm_l7);
        uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t38 = gm_l6;
        uint64_t t39 = (uintptr_t)t38;
        if (t37 > t39 || t39 > (uintptr_t)16) gm_trap_slice(t37, t39, 16, gm_srcrvqeupiu1766, 160, 43);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t36 + t37 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t37));
        gm_tcfa6l7reoosn t41;
        gm_f_load_at_6u81jcc((void *)&t41, (void *)NULL, cx, NULL, t30, t31, t32, t35, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) {
            gm_t76123gfet73g t43 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t42) + 8));
            gm_tdlro53ljvkl0 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 40, 0);
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t44 + 16), t43);
            F.ret = t44;
            goto gm_fret;
        }
        gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 8));
    }
    {
        void * t46 = gm_l0;
        void * t47 = ((uint8_t *)&gm_l7);
        uint64_t t48 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t49 = gm_l6;
        uint64_t t50 = (uintptr_t)t49;
        if (t48 > t50 || t50 > (uintptr_t)16) gm_trap_slice(t48, t50, 16, gm_srcrvqeupiu1766, 161, 23);
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t47 + t48 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50 - t48));
        gm_b16_8 t52 = t51;
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
        unsigned __int128 t54 = gm_f_get_uint_bq8c4b7(t46, t53);
        gm_tdlro53ljvkl0 t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 40, 0);
        ((uint8_t *)&t55)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t55 + 16), t54);
        gm_tdlro53ljvkl0 t56 = t55;
        uint8_t t57 = ((((uint8_t *)&t56))[0] != 0);
        F.ret = t55;
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
__attribute__((aligned(4))) void gm_f_ev_slice_i5uin87(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4, void * a5, uint8_t a6, gm_t9qkh0rsooc8d a7, uint64_t a8, uint64_t a9, gm_b16_8 a10) {
    struct gm_fr_ev_slice_i5uin87 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    void * gm_l3; /* base */
    void * gm_l4; /* lo_n */
    void * gm_l5; /* hi_n */
    uint8_t gm_l6; /* inclusive */
    gm_t9qkh0rsooc8d gm_l7; /* seq */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* n_len */
    gm_b16_8 gm_l10; /* dst */
    uint64_t gm_l11; /* ptr */
    gm_tf5srh4mp4b3i gm_l12; /* len */
    gm_t2lqu8jsj511d gm_l13; /* mk */
    uint64_t gm_l14; /* addr */
    uint64_t gm_l15; /* addr */
    gm_b16_8 gm_l16; /* b */
    gm_t2lqu8jsj511d gm_l17; /* sp */
    uint64_t gm_l18; /* lo */
    void * gm_l19; /* x */
    uint64_t gm_l20; /* hi */
    void * gm_l21; /* x */
    uint64_t gm_l22; /* l */
    gm_b16_8 gm_l23; /* text */
    GM_STACK_CHECK(&F, 2561u);
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
    gm_l7 = a7;
    gm_l8 = a8;
    gm_l9 = a9;
    gm_l10 = a10;
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            gm_tf5srh4mp4b3i t3;
            memset(&t3, 0, sizeof t3);
            gm_l12 = t3;
        }
        {
            void * t4 = gm_l0;
            gm_t2lqu8jsj511d t5 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t4));
            gm_l13 = t5;
        }
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; default: goto ma11; }
ma7:;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (t14 != ((uint8_t)0ULL)) goto mn13;
            {
                void * t16 = gm_l0;
                gm_tbl8il92l97ut t17 = gm_l1;
                void * t18 = gm_l3;
                gm_tl1r7a2fn1ev6 t19;
                gm_f_place_47rfqe5((void *)&t19, (void *)NULL, cx, NULL, t16, t17, t18, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t20 = t19;
                if (((((uint8_t *)&t20))[0] != 0)) {
                    gm_t76123gfet73g t21 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t20) + 8));
                    gm_tcfa6l7reoosn t22;
                    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
                    ((uint8_t *)&t22)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t22 + 8), t21);
                    F.ret = t22;
                    goto gm_fret;
                }
                gm_tou12vuc19e0s t23 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t20) + 8));
                gm_tou12vuc19e0s t24 = t23;
                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
                if (t29 != ((uint8_t)1ULL)) goto pa28;
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t24) + 8));
                gm_l14 = t30;
                goto po27;
pa28:;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
                if (t32 != ((uint8_t)2ULL)) goto pa31;
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&t24) + 8));
                gm_l14 = t33;
                goto po27;
pa31:;
                goto mn26;
po27:;
                {
                    uint64_t t35 = gm_l14;
                    gm_l11 = t35;
                }
                goto me25;
mn26:;
                void * t37 = gm_l0;
                void * t38 = gm_l2;
                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 112));
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sv0u97lrebfd4));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(40));
                gm_t76123gfet73g t41;
                gm_f_trap_qh84l9d((void *)&t41, (void *)NULL, cx, NULL, t37, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                ((uint8_t *)&t42)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t42 + 8), t41);
                gm_tcfa6l7reoosn t43 = t42;
                uint8_t t44 = ((((uint8_t *)&t43))[0] != 0);
                F.ret = t42;
                goto gm_fret;
                goto me25;
mn36:;
                gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 206, 13);
me25:;
            }
            {
                uint64_t t45 = gm_l9;
                gm_tf5srh4mp4b3i t46;
                GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t46), t45);
                ((uint8_t *)&t46)[8] = 1;
                gm_l12 = t46;
            }
            goto me6;
mn13:;
ma8:;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (t48 != ((uint8_t)1ULL)) goto mn47;
            {
                void * t50 = gm_l0;
                void * t51 = gm_l3;
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 104));
                gm_b16_8 t53 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t50, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t53;
            }
            {
                void * t54 = gm_l0;
                gm_tbl8il92l97ut t55 = gm_l1;
                void * t56 = gm_l3;
                gm_b16_8 t57 = gm_l16;
                gm_tcfa6l7reoosn t58;
                gm_f_ev_25jfdar((void *)&t58, (void *)NULL, cx, NULL, t54, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t59 = t58;
                if (((((uint8_t *)&t59))[0] != 0)) {
                    gm_t76123gfet73g t60 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t59) + 8));
                    gm_tcfa6l7reoosn t61;
                    GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                    ((uint8_t *)&t61)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t61 + 8), t60);
                    F.ret = t61;
                    goto gm_fret;
                }
                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 8));
            }
            {
                void * t63 = gm_l0;
                gm_b16_8 t64 = gm_l16;
                gm_b16_8 t65 = t64;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                gm_t2lqu8jsj511d t67 = gm_f_slice_parts_1se5hu4(t63, t66);
                gm_l17 = t67;
            }
            {
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 0));
                gm_l11 = t68;
            }
            {
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                gm_tf5srh4mp4b3i t70;
                GM_ST(uint8_t, (uint8_t *)&t70 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t70 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t70 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t70), t69);
                ((uint8_t *)&t70)[8] = 1;
                gm_l12 = t70;
            }
            goto me6;
mn47:;
ma9:;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (t72 != ((uint8_t)2ULL)) goto mn71;
            {
                void * t74 = gm_l0;
                gm_tbl8il92l97ut t75 = gm_l1;
                void * t76 = gm_l3;
                gm_t833u93mr4iob t77;
                gm_f_ew_c5kil07((void *)&t77, (void *)NULL, cx, NULL, t74, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t78 = t77;
                if (((((uint8_t *)&t78))[0] != 0)) {
                    gm_t76123gfet73g t79 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t78) + 8));
                    gm_tcfa6l7reoosn t80;
                    GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                    ((uint8_t *)&t80)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t80 + 8), t79);
                    F.ret = t80;
                    goto gm_fret;
                }
                uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&t78) + 8));
                gm_l11 = t81;
            }
            goto me6;
mn71:;
ma10:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (t83 != ((uint8_t)3ULL)) goto mn82;
            {
                void * t85 = gm_l0;
                gm_tbl8il92l97ut t86 = gm_l1;
                void * t87 = gm_l3;
                gm_t833u93mr4iob t88;
                gm_f_ew_c5kil07((void *)&t88, (void *)NULL, cx, NULL, t85, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t89 = t88;
                if (((((uint8_t *)&t89))[0] != 0)) {
                    gm_t76123gfet73g t90 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t89) + 8));
                    gm_tcfa6l7reoosn t91;
                    GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                    ((uint8_t *)&t91)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t91 + 8), t90);
                    F.ret = t91;
                    goto gm_fret;
                }
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&t89) + 8));
                gm_l11 = t92;
            }
            {
                uint64_t t93 = gm_l9;
                gm_tf5srh4mp4b3i t94;
                GM_ST(uint8_t, (uint8_t *)&t94 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t94 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t94 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t94), t93);
                ((uint8_t *)&t94)[8] = 1;
                gm_l12 = t94;
            }
            goto me6;
mn82:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 204, 5);
me6:;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            void * t96 = gm_l4;
            void * t97 = t96;
            if (!(GM_LD(void *, ((uint8_t *)&t97)) != 0)) goto ie95;
            void * t98 = GM_LD(void *, ((uint8_t *)&t97));
            gm_l19 = t98;
            {
                void * t100 = gm_l0;
                gm_tbl8il92l97ut t101 = gm_l1;
                void * t102 = gm_l19;
                gm_t833u93mr4iob t103;
                gm_f_ev_index_bie377m((void *)&t103, (void *)NULL, cx, NULL, t100, t101, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t104 = t103;
                if (((((uint8_t *)&t104))[0] != 0)) {
                    gm_t76123gfet73g t105 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t104) + 8));
                    gm_tcfa6l7reoosn t106;
                    GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                    ((uint8_t *)&t106)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t106 + 8), t105);
                    F.ret = t106;
                    goto gm_fret;
                }
                uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&t104) + 8));
                gm_l18 = t107;
            }
            goto ix95;
ie95:;
ix95:;
        }
        {
            gm_tf5srh4mp4b3i t108 = gm_l12;
            gm_tf5srh4mp4b3i t109 = t108;
            uint64_t t110;
            if (((((uint8_t *)&t109))[8] != 0)) {
                uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&t109) + 0));
                t110 = t111;
            } else {
                t110 = ((uint64_t)0ULL);
            }
            gm_l20 = t110;
        }
        {
            void * t113 = gm_l5;
            void * t114 = t113;
            if (!(GM_LD(void *, ((uint8_t *)&t114)) != 0)) goto ie112;
            void * t115 = GM_LD(void *, ((uint8_t *)&t114));
            gm_l21 = t115;
            {
                void * t117 = gm_l0;
                gm_tbl8il92l97ut t118 = gm_l1;
                void * t119 = gm_l21;
                gm_t833u93mr4iob t120;
                gm_f_ev_index_bie377m((void *)&t120, (void *)NULL, cx, NULL, t117, t118, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t121 = t120;
                if (((((uint8_t *)&t121))[0] != 0)) {
                    gm_t76123gfet73g t122 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t121) + 8));
                    gm_tcfa6l7reoosn t123;
                    GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                    ((uint8_t *)&t123)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t123 + 8), t122);
                    F.ret = t123;
                    goto gm_fret;
                }
                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&t121) + 8));
                gm_l20 = t124;
            }
            goto ix112;
ie112:;
ix112:;
        }
        {
            uint8_t t126 = gm_l6;
            if (!t126) goto ie125;
            {
                uint64_t t128 = gm_l20;
                uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                uint64_t t131 = o130 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r129;
                gm_l20 = t131;
            }
            goto ix125;
ie125:;
ix125:;
        }
        {
            void * t132 = gm_l0;
            gm_t2lqu8jsj511d t133 = gm_l13;
            gm_f_release_jn5kqdi(t132, GM_CONV(gm_csc7lc6jlkqkuq, t133));
        }
        {
            uint8_t t135 = 0;
            uint64_t t136 = gm_l18;
            uint64_t t137 = gm_l20;
            uint8_t t138 = (t136 > t137);
            if (!t138) {
                uint8_t t139 = 0;
                uint8_t t140;
                if (((((uint8_t *)&gm_l12))[8] != 0)) goto mn142;
                t140 = ((uint8_t)0);
                goto me141;
mn142:;
                t140 = ((uint8_t)1);
                goto me141;
mn143:;
                gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 231, 20);
me141:;
                if (t140) {
                    uint64_t t144 = gm_l20;
                    gm_tf5srh4mp4b3i t145 = gm_l12;
                    gm_tf5srh4mp4b3i t146 = t145;
                    uint64_t t147;
                    if (((((uint8_t *)&t146))[8] != 0)) {
                        uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&t146) + 0));
                        t147 = t148;
                    } else {
                        t147 = ((uint64_t)0ULL);
                    }
                    uint8_t t149 = (t144 > t147);
                    t139 = t149;
                } else t139 = 0;
                t135 = t139;
            } else t135 = 1;
            if (!t135) goto ie134;
            {
                gm_b16_8 t151;
                if (!((((uint8_t *)&gm_l12))[8] != 0)) goto ie152;
                uint64_t t153 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                gm_l22 = t153;
                uint64_t t155 = gm_l22;
                gm_t4ce0jd3tdro4 t156;
                GM_ST(uint64_t, ((uint8_t *)&t156 + 0), t155);
                gm_tabb710d9mq2e t157;
                gm_f_format_1amnh72((void *)&t157, (void *)NULL, cx, NULL, t156);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t158 = t157;
                if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 8, gm_srcrvqeupiu1766, 232, 31);
                gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&t158) + 8));
                gm_b16_8 t160 = t159;
                gm_b16_8 t161 = t160;
                gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                t151 = t162;
                goto ix152;
ie152:;
                gm_b16_8 t164;
                GM_ST(void *, (uint8_t *)&t164, (void *)(gm_sfqk3rro4g7t0));
                GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(1));
                gm_b16_8 t165 = t164;
                t151 = t165;
ix152:;
                gm_l23 = t151;
            }
            {
                void * t166 = gm_l0;
                void * t167 = gm_l2;
                gm_tl874t2atnlh3 t168 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t167) + 112));
                gm_tbccvikg6b96e t169;
                uint64_t t170 = gm_l18;
                GM_ST(uint64_t, (uint8_t *)&t169 + 0, t170);
                uint64_t t171 = gm_l20;
                GM_ST(uint64_t, (uint8_t *)&t169 + 8, t171);
                gm_b16_8 t172 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t169 + 16, t172);
                gm_tabb710d9mq2e t173;
                gm_f_format_3pa9rer((void *)&t173, (void *)NULL, cx, NULL, &t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t174 = t173;
                if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 8, gm_srcrvqeupiu1766, 233, 32);
                gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&t174) + 8));
                gm_b16_8 t176 = t175;
                gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                gm_t76123gfet73g t178;
                gm_f_trap_qh84l9d((void *)&t178, (void *)NULL, cx, NULL, t166, t168, t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t179;
                GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                ((uint8_t *)&t179)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t179 + 8), t178);
                gm_tcfa6l7reoosn t180 = t179;
                uint8_t t181 = ((((uint8_t *)&t180))[0] != 0);
                F.ret = t179;
                goto gm_fret;
            }
            goto ix134;
ie134:;
ix134:;
        }
        {
            void * t182 = gm_l0;
            gm_b16_8 t183 = gm_l10;
            uint64_t t184 = gm_l11;
            uint64_t t185 = gm_l18;
            uint64_t t186 = gm_l8;
            uint64_t r187; bool o188 = __builtin_mul_overflow(t185, t186, &r187);
            if (o188) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 235, 29);
            uint64_t t189 = r187;
            uint64_t r190; bool o191 = __builtin_add_overflow(t184, t189, &r190);
            uint64_t t192 = r190;
            uint64_t t193 = gm_l20;
            uint64_t t194 = gm_l18;
            uint64_t r195; bool o196 = __builtin_sub_overflow(t193, t194, &r195);
            if (o196) gm_trap("integer overflow in `-`", gm_srcrvqeupiu1766, 235, 39);
            uint64_t t197 = r195;
            gm_f_put_slice_ko14loi(t182, t183, t192, t197);
        }
    }
    gm_b0_1 t198;
    gm_tcfa6l7reoosn t199;
    GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t199 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t199 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t199 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t199 + 24, 0);
    ((uint8_t *)&t199)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t199 + 8), t198);
    gm_tcfa6l7reoosn t200 = t199;
    F.ret = t200;
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
__attribute__((aligned(4))) void gm_f_label_dst_b5cat1e(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_label_dst_b5cat1e F;
    void * gm_l0; /* fr */
    uint32_t gm_l1; /* label */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* code */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l7; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 573u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 136) + 0) + 8);
        gm_l2 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcrvqeupiu1766, 169, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 136) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcrvqeupiu1766, 170, 12);
                uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 136) + 0)) + t15 * 24) + 0));
                uint32_t t19 = gm_l1;
                uint8_t t20 = ((t18 == t19));
                if (!t20) goto ie13;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t t23 = (uintptr_t)t22;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 136) + 0) + 8);
                    if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcrvqeupiu1766, 170, 55);
                    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 136) + 0)) + t23 * 24) + 8));
                    gm_th2lqbo77pvsc t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t27 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[16] = 1;
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
ix13:;
            }
lc4:;
        }
lb4:;
    }
    {
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 16));
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 56));
        void * t32 = t31;
        void * t33;
        if ((GM_LD(void *, ((uint8_t *)&t32)) != 0)) {
            void * t34 = GM_LD(void *, (((uint8_t *)&t32) + 0));
            t33 = t34;
        } else {
            gm_th2lqbo77pvsc t35;
            memset(&t35, 0, sizeof t35);
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        gm_l3 = t33;
    }
    {
        {
            void * t38 = gm_l3;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 128));
            gm_l4 = t39;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t41 = gm_l5;
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    uint64_t t46 = gm_l5;
                    uint64_t t47 = (uintptr_t)t46;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t98k64j4g45rk t51 = GM_LD(gm_t98k64j4g45rk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t47 * 40));
                    F.l6 = t51;
                    {
                        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 28));
                        if (!t54) goto ie53;
                        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 0));
                        uint32_t t56 = gm_l1;
                        uint8_t t57 = ((t55 == t56));
                        if (!t57) goto ie53;
                        {
                            uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 32));
                            uint64_t t60 = (uint64_t)t59;
                            gm_l7 = t60;
                        }
                        {
                            void * t61 = gm_l0;
                            uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)t61) + 48) + 8);
                            void * t63 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t61) + 48)));
                            uint64_t t64 = gm_l7;
                            uint64_t t65 = (uintptr_t)t64;
                            uint64_t t66 = gm_l7;
                            uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 36));
                            uint64_t t68 = (uint64_t)t67;
                            uint64_t r69; bool o70 = __builtin_add_overflow(t66, t68, &r69);
                            if (o70) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 176, 32);
                            uint64_t t71 = r69;
                            uint64_t t72 = (uintptr_t)t71;
                            if (t65 > t72 || t72 > (uintptr_t)t62) gm_trap_slice(t65, t72, t62, gm_srcrvqeupiu1766, 176, 20);
                            gm_b16_8 t73;
                            GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t63 + t65 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72 - t65));
                            gm_th2lqbo77pvsc t74;
                            GM_ST(uint8_t, (uint8_t *)&t74 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t74 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t74), t73);
                            ((uint8_t *)&t74)[16] = 1;
                            uint8_t t75 = 0;
                            F.ret = t74;
                            goto gm_fret;
                        }
                        goto ix53;
ie53:;
ix53:;
                    }
                    goto me49;
mn50:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 173, 5);
me49:;
                }
lc40:;
                {
                    uint64_t t77 = gm_l5;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l5 = t80;
                }
            }
lb40:;
        }
    }
    {
        gm_th2lqbo77pvsc t81;
        memset(&t81, 0, sizeof t81);
        uint8_t t82 = 0;
        F.ret = t81;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_label_dst_b5cat1e_c(void * a0, uint32_t a1) { gm_th2lqbo77pvsc gm_r; gm_f_label_dst_b5cat1e(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_local_place_gp2fins(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, uint8_t a4) {
    struct gm_fr_local_place_gp2fins F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint8_t gm_l4; /* need_addr */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* off */
    GM_STACK_CHECK(&F, 1084u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l3;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l5 = t4;
    }
    {
        uint64_t t6 = gm_l5;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l2;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srcrvqeupiu1766, 31, 8);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 64)) + t7 * 8));
        uint8_t t11 = (!(t10 == ((uint64_t)0ULL)));
        if (!t11) goto ie5;
        {
            uint64_t t13 = gm_l5;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l2;
            uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 64) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrvqeupiu1766, 31, 41);
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 64)) + t14 * 8));
            gm_tou12vuc19e0s t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t18 + 8), t17);
            gm_tl1r7a2fn1ev6 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t19 + 8), t18);
            gm_tl1r7a2fn1ev6 t20 = t19;
            uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t23 = 0;
        uint8_t t24 = gm_l4;
        if (!t24) {
            uint64_t t25 = gm_l5;
            uint64_t t26 = (uintptr_t)t25;
            void * t27 = gm_l2;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 16));
            uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 120) + 8);
            if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_srcrvqeupiu1766, 32, 21);
            uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 120)) + t26 * 1));
            t23 = t30;
        } else t23 = 1;
        if (!t23) goto ie22;
        {
            void * t32 = gm_l0;
            gm_tl874t2atnlh3 t33 = gm_l1;
            void * t34 = gm_l2;
            uint32_t t35 = gm_l3;
            gm_t833u93mr4iob t36;
            gm_f_local_addr_o51qq3k((void *)&t36, (void *)NULL, cx, NULL, t32, t33, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) {
                gm_t76123gfet73g t38 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t37) + 8));
                gm_tl1r7a2fn1ev6 t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t39 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t39 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t39 + 48, 0);
                ((uint8_t *)&t39)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t39 + 8), t38);
                F.ret = t39;
                goto gm_fret;
            }
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&t37) + 8));
            gm_tou12vuc19e0s t41;
            memset(&t41, 0, sizeof t41);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t41 + 8), t40);
            gm_tl1r7a2fn1ev6 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            ((uint8_t *)&t42)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t42 + 8), t41);
            gm_tl1r7a2fn1ev6 t43 = t42;
            uint8_t t44 = ((((uint8_t *)&t43))[0] != 0);
            F.ret = t42;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        uint64_t t45 = gm_l5;
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = gm_l2;
        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 16));
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 72) + 8);
        if (t46 >= (uintptr_t)(t49)) gm_trap_index(t46, t49, gm_srcrvqeupiu1766, 33, 15);
        uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 72)) + t46 * 8));
        uint64_t t51 = gm_f_us_cuv01fp(t50);
        gm_l6 = t51;
    }
    {
        void * t52 = gm_l2;
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t52) + 48));
        gm_b16_8 t54 = t53;
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&t54) + 8);
        void * t56 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t54)));
        uint64_t t57 = gm_l6;
        uint64_t t58 = (uintptr_t)t57;
        uint64_t t59 = gm_l6;
        uint64_t t60 = gm_l5;
        uint64_t t61 = (uintptr_t)t60;
        void * t62 = gm_l2;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 16));
        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)t63) + 88) + 8);
        if (t61 >= (uintptr_t)(t64)) gm_trap_index(t61, t64, gm_srcrvqeupiu1766, 34, 41);
        uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t63) + 88)) + t61 * 8));
        uint64_t t66 = gm_f_us_cuv01fp(t65);
        uint64_t r67; bool o68 = __builtin_add_overflow(t59, t66, &r67);
        if (o68) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 34, 32);
        uint64_t t69 = r67;
        uint64_t t70 = (uintptr_t)t69;
        if (t58 > t70 || t70 > (uintptr_t)t55) gm_trap_slice(t58, t70, t55, gm_srcrvqeupiu1766, 34, 18);
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t56 + t58 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(t70 - t58));
        gm_tou12vuc19e0s t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t71);
        gm_tl1r7a2fn1ev6 t73;
        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
        ((uint8_t *)&t73)[0] = 0;
        GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t73 + 8), t72);
        gm_tl1r7a2fn1ev6 t74 = t73;
        uint8_t t75 = ((((uint8_t *)&t74))[0] != 0);
        F.ret = t73;
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
__attribute__((aligned(4))) void gm_f_place_47rfqe5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3) {
    struct gm_fr_place_47rfqe5 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    uint8_t gm_l3; /* need_addr */
    uint32_t gm_l4; /* l */
    uint32_t gm_l5; /* levels */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* fr */
    uint64_t gm_l8; /* decl */
    gm_tl874t2atnlh3 gm_l9; /* span */
    void * gm_l10; /* x */
    gm_tdvcgvrf81sbd gm_l11; /* x */
    gm_b16_8 gm_l12; /* h */
    uint64_t gm_l13; /* addr */
    uint64_t gm_l14; /* addr */
    gm_t1j70din2upc4 gm_l16; /* x */
    gm_b16_8 gm_l17; /* h */
    uint64_t gm_l18; /* off */
    uint64_t gm_l19; /* addr */
    uint64_t gm_l20; /* addr */
    uint64_t gm_l22; /* esz */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l25; /* h */
    uint64_t gm_l26; /* off */
    uint64_t gm_l27; /* addr */
    uint64_t gm_l28; /* addr */
    gm_t51b2en4qp02s gm_l29; /* b */
    gm_b16_8 gm_l30; /* sb */
    gm_t2lqu8jsj511d gm_l31; /* sp */
    uint64_t gm_l32; /* i */
    uint64_t gm_l33; /* p */
    uint64_t gm_l34; /* i */
    uint64_t gm_l35; /* p */
    uint64_t gm_l36; /* i */
    gm_b16_8 gm_l37; /* b */
    GM_STACK_CHECK(&F, 9527u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma12; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma12; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma12; case ((uint8_t)59ULL): goto ma12; case ((uint8_t)60ULL): goto ma12; case ((uint8_t)61ULL): goto ma12; case ((uint8_t)62ULL): goto ma12; default: goto ma12; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t16 != ((uint8_t)6ULL)) goto mn15;
        uint32_t t17 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l4 = t17;
        void * t18 = gm_l0;
        void * t19 = gm_l2;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 112));
        void * t21 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        uint32_t t22 = gm_l4;
        uint8_t t23 = gm_l3;
        gm_f_local_place_gp2fins((void *)&(*(gm_tl1r7a2fn1ev6 *)gm_out), (void *)NULL, cx, NULL, t18, t20, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn15:;
ma5:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t25 != ((uint8_t)7ULL)) goto mn24;
        uint32_t t26 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l5 = t26;
        uint32_t t27 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 4));
        gm_l6 = t27;
        {
            void * t29 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            uint32_t t30 = gm_l5;
            void * t31 = gm_f_frame_up_ghkkuqg(t29, t30);
            void * t32 = t31;
            void * t33;
            if ((GM_LD(void *, ((uint8_t *)&t32)) != 0)) {
                void * t34 = GM_LD(void *, (((uint8_t *)&t32) + 0));
                t33 = t34;
            } else {
                void * t35 = gm_l0;
                void * t36 = gm_l2;
                gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 112));
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sco971hnpq7nk));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(48));
                gm_t76123gfet73g t39;
                gm_f_trap_qh84l9d((void *)&t39, (void *)NULL, cx, NULL, t35, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 48, 0);
                ((uint8_t *)&t40)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t40 + 8), t39);
                gm_tl1r7a2fn1ev6 t41 = t40;
                uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
                F.ret = t40;
                goto gm_fret;
            }
            gm_l7 = t33;
        }
        {
            void * t43 = gm_l0;
            void * t44 = gm_l2;
            gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t44) + 112));
            void * t46 = gm_l7;
            uint32_t t47 = gm_l6;
            uint8_t t48 = gm_l3;
            gm_f_local_place_gp2fins((void *)&(*(gm_tl1r7a2fn1ev6 *)gm_out), (void *)NULL, cx, NULL, t43, t45, t46, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn24:;
ma6:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t50 != ((uint8_t)8ULL)) goto mn49;
        uint64_t t51 = GM_LD(uint64_t, ((((uint8_t *)t2) + 0) + 8));
        gm_l8 = t51;
        void * t52 = gm_l0;
        void * t53 = gm_l2;
        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 112));
        uint64_t t55 = gm_l8;
        gm_t833u93mr4iob t56;
        gm_f_global_addr_9an4270((void *)&t56, (void *)NULL, cx, NULL, t52, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t57 = t56;
        if (((((uint8_t *)&t57))[0] != 0)) {
            gm_t76123gfet73g t58 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t57) + 8));
            gm_tl1r7a2fn1ev6 t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 48, 0);
            ((uint8_t *)&t59)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t59 + 8), t58);
            F.ret = t59;
            goto gm_fret;
        }
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&t57) + 8));
        gm_tou12vuc19e0s t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
        GM_ST(uint64_t, ((uint8_t *)&t61 + 8), t60);
        gm_tl1r7a2fn1ev6 t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t62 + 8), t61);
        gm_tl1r7a2fn1ev6 t63 = t62;
        uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
        F.ret = t62;
        goto gm_fret;
        goto me3;
mn49:;
ma7:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t66 != ((uint8_t)4ULL)) goto mn65;
        gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 0) + 8));
        gm_l9 = t67;
        void * t68 = gm_l0;
        gm_tl874t2atnlh3 t69 = gm_l9;
        void * t70 = gm_l2;
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t70) + 96));
        void * t72 = gm_l2;
        gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 112));
        gm_t833u93mr4iob t74;
        gm_f_const_addr_heqn3dl((void *)&t74, (void *)NULL, cx, NULL, t68, t69, t71, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t75 = t74;
        if (((((uint8_t *)&t75))[0] != 0)) {
            gm_t76123gfet73g t76 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t75) + 8));
            gm_tl1r7a2fn1ev6 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 48, 0);
            ((uint8_t *)&t77)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t77 + 8), t76);
            F.ret = t77;
            goto gm_fret;
        }
        uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&t75) + 8));
        gm_tou12vuc19e0s t79;
        memset(&t79, 0, sizeof t79);
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)1ULL));
        GM_ST(uint64_t, ((uint8_t *)&t79 + 8), t78);
        gm_tl1r7a2fn1ev6 t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
        ((uint8_t *)&t80)[0] = 0;
        GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t80 + 8), t79);
        gm_tl1r7a2fn1ev6 t81 = t80;
        uint8_t t82 = ((((uint8_t *)&t81))[0] != 0);
        F.ret = t80;
        goto gm_fret;
        goto me3;
mn65:;
ma8:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t84 != ((uint8_t)9ULL)) goto mn83;
        void * t85 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l10 = t85;
        void * t86 = gm_l0;
        gm_tbl8il92l97ut t87 = gm_l1;
        void * t88 = gm_l10;
        gm_t833u93mr4iob t89;
        gm_f_ew_c5kil07((void *)&t89, (void *)NULL, cx, NULL, t86, t87, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t90 = t89;
        if (((((uint8_t *)&t90))[0] != 0)) {
            gm_t76123gfet73g t91 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t90) + 8));
            gm_tl1r7a2fn1ev6 t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 48, 0);
            ((uint8_t *)&t92)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t92 + 8), t91);
            F.ret = t92;
            goto gm_fret;
        }
        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&t90) + 8));
        gm_tou12vuc19e0s t94;
        memset(&t94, 0, sizeof t94);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)1ULL));
        GM_ST(uint64_t, ((uint8_t *)&t94 + 8), t93);
        gm_tl1r7a2fn1ev6 t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
        ((uint8_t *)&t95)[0] = 0;
        GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t95 + 8), t94);
        gm_tl1r7a2fn1ev6 t96 = t95;
        uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
        F.ret = t95;
        goto gm_fret;
        goto me3;
mn83:;
ma9:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t99 != ((uint8_t)11ULL)) goto mn98;
        gm_tdvcgvrf81sbd t100 = GM_LD(gm_tdvcgvrf81sbd, ((((uint8_t *)t2) + 0) + 8));
        gm_l11 = t100;
        {
            void * t102 = gm_l0;
            gm_tbl8il92l97ut t103 = gm_l1;
            void * t104 = GM_LD(void *, (((uint8_t *)&gm_l11) + 0));
            gm_tl1r7a2fn1ev6 t105;
            gm_f_place_47rfqe5((void *)&t105, (void *)NULL, cx, NULL, t102, t103, t104, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl1r7a2fn1ev6 t106 = t105;
            if (((((uint8_t *)&t106))[0] != 0)) {
                gm_t76123gfet73g t107 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t106) + 8));
                gm_tl1r7a2fn1ev6 t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t108 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t108 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t108 + 48, 0);
                ((uint8_t *)&t108)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t108 + 8), t107);
                F.ret = t108;
                goto gm_fret;
            }
            gm_tou12vuc19e0s t109 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t106) + 8));
            gm_tou12vuc19e0s t110 = t109;
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t110) + 0));
            switch (t116) { case ((uint8_t)0ULL): goto ma112; case ((uint8_t)1ULL): goto ma113; case ((uint8_t)2ULL): goto ma113; case ((uint8_t)3ULL): goto ma114; default: goto ma115; }
ma112:;
            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t110) + 0));
            if (t118 != ((uint8_t)0ULL)) goto mn117;
            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
            gm_l12 = t119;
            gm_t0h5lk09e9p5r t120;
            memset((uint8_t *)&t120 + 36, 0, 4);
            gm_b16_8 t121 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
            GM_ST(uint64_t, (uint8_t *)&t120 + 16, ((uint64_t)0ULL));
            void * t122 = GM_LD(void *, (((uint8_t *)&gm_l11) + 0));
            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t122) + 96));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t120 + 24, t123);
            uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 12));
            GM_ST(uint32_t, (uint8_t *)&t120 + 28, t124);
            gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 16));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t120 + 32, t125);
            gm_tou12vuc19e0s t126;
            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t0h5lk09e9p5r, ((uint8_t *)&t126 + 8), t120);
            gm_tl1r7a2fn1ev6 t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
            ((uint8_t *)&t127)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t127 + 8), t126);
            gm_tl1r7a2fn1ev6 t128 = t127;
            uint8_t t129 = ((((uint8_t *)&t128))[0] != 0);
            F.ret = t127;
            goto gm_fret;
            goto me111;
mn117:;
ma113:;
            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t110) + 0));
            if (t133 != ((uint8_t)1ULL)) goto pa132;
            uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&t110) + 8));
            gm_l13 = t134;
            goto po131;
pa132:;
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&t110) + 0));
            if (t136 != ((uint8_t)2ULL)) goto pa135;
            uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&t110) + 8));
            gm_l13 = t137;
            goto po131;
pa135:;
            goto mn130;
po131:;
            gm_t0h5lk09e9p5r t138;
            memset((uint8_t *)&t138 + 36, 0, 4);
            gm_b16_8 t139 = gm_f_empty_slice_ijtp9lo();
            GM_ST(gm_b16_8, (uint8_t *)&t138 + 0, t139);
            uint64_t t140 = gm_l13;
            GM_ST(uint64_t, (uint8_t *)&t138 + 16, t140);
            void * t141 = GM_LD(void *, (((uint8_t *)&gm_l11) + 0));
            gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t141) + 96));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t138 + 24, t142);
            uint32_t t143 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 12));
            GM_ST(uint32_t, (uint8_t *)&t138 + 28, t143);
            gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 16));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t138 + 32, t144);
            gm_tou12vuc19e0s t145;
            GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t0h5lk09e9p5r, ((uint8_t *)&t145 + 8), t138);
            gm_tl1r7a2fn1ev6 t146;
            GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
            ((uint8_t *)&t146)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t146 + 8), t145);
            gm_tl1r7a2fn1ev6 t147 = t146;
            uint8_t t148 = ((((uint8_t *)&t147))[0] != 0);
            F.ret = t146;
            goto gm_fret;
            goto me111;
mn130:;
ma114:;
            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&t110) + 0));
            if (t150 != ((uint8_t)3ULL)) goto mn149;
            gm_t0h5lk09e9p5r t151 = GM_LD(gm_t0h5lk09e9p5r, (((uint8_t *)&t110) + 8));
            F.l15 = t151;
            gm_t0h5lk09e9p5r t152;
            memset((uint8_t *)&t152 + 36, 0, 4);
            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t152 + 0, t153);
            uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
            GM_ST(uint64_t, (uint8_t *)&t152 + 16, t154);
            gm_t88rcucg1b7uk t155 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 24));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t152 + 24, t155);
            uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)&F.l15) + 28));
            uint32_t t157 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 12));
            uint32_t r158; bool o159 = __builtin_add_overflow(t156, t157, &r158);
            if (o159) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 53, 67);
            uint32_t t160 = r158;
            GM_ST(uint32_t, (uint8_t *)&t152 + 28, t160);
            gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 16));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t152 + 32, t161);
            gm_tou12vuc19e0s t162;
            GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t0h5lk09e9p5r, ((uint8_t *)&t162 + 8), t152);
            gm_tl1r7a2fn1ev6 t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
            ((uint8_t *)&t163)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t163 + 8), t162);
            gm_tl1r7a2fn1ev6 t164 = t163;
            uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
            F.ret = t163;
            goto gm_fret;
            goto me111;
mn149:;
ma115:;
            gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 50, 13);
me111:;
        }
        goto me3;
mn98:;
ma10:;
        uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t167 != ((uint8_t)10ULL)) goto mn166;
        gm_t1j70din2upc4 t168 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t2) + 0) + 8));
        gm_l16 = t168;
        {
            void * t170 = gm_l0;
            gm_tbl8il92l97ut t171 = gm_l1;
            void * t172 = GM_LD(void *, (((uint8_t *)&gm_l16) + 0));
            uint8_t t173 = gm_l3;
            gm_tl1r7a2fn1ev6 t174;
            gm_f_place_47rfqe5((void *)&t174, (void *)NULL, cx, NULL, t170, t171, t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl1r7a2fn1ev6 t175 = t174;
            if (((((uint8_t *)&t175))[0] != 0)) {
                gm_t76123gfet73g t176 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t175) + 8));
                gm_tl1r7a2fn1ev6 t177;
                GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t177 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t177 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t177 + 48, 0);
                ((uint8_t *)&t177)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t177 + 8), t176);
                F.ret = t177;
                goto gm_fret;
            }
            gm_tou12vuc19e0s t178 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t175) + 8));
            gm_tou12vuc19e0s t179 = t178;
            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
            switch (t186) { case ((uint8_t)0ULL): goto ma181; case ((uint8_t)1ULL): goto ma182; case ((uint8_t)2ULL): goto ma183; case ((uint8_t)3ULL): goto ma184; default: goto ma185; }
ma181:;
            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
            if (t188 != ((uint8_t)0ULL)) goto mn187;
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
            gm_l17 = t189;
            {
                uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 16));
                uint64_t t192 = gm_f_us_cuv01fp(t191);
                gm_l18 = t192;
            }
            {
                gm_b16_8 t193 = gm_l17;
                gm_b16_8 t194 = t193;
                uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&t194) + 8);
                void * t196 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t194)));
                uint64_t t197 = gm_l18;
                uint64_t t198 = (uintptr_t)t197;
                uint64_t t199 = gm_l18;
                void * t200 = gm_l2;
                uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)t200) + 104));
                uint64_t t202 = gm_f_us_cuv01fp(t201);
                uint64_t r203; bool o204 = __builtin_add_overflow(t199, t202, &r203);
                if (o204) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 60, 41);
                uint64_t t205 = r203;
                uint64_t t206 = (uintptr_t)t205;
                if (t198 > t206 || t206 > (uintptr_t)t195) gm_trap_slice(t198, t206, t195, gm_srcrvqeupiu1766, 60, 34);
                gm_b16_8 t207;
                GM_ST(void *, (uint8_t *)&t207, (void *)(((uint8_t *)t196 + t198 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(t206 - t198));
                gm_tou12vuc19e0s t208;
                GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t208 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t208 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t208 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t208 + 8), t207);
                gm_tl1r7a2fn1ev6 t209;
                GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                ((uint8_t *)&t209)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t209 + 8), t208);
                gm_tl1r7a2fn1ev6 t210 = t209;
                uint8_t t211 = ((((uint8_t *)&t210))[0] != 0);
                F.ret = t209;
                goto gm_fret;
            }
            goto me180;
mn187:;
ma182:;
            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
            if (t213 != ((uint8_t)1ULL)) goto mn212;
            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&t179) + 8));
            gm_l19 = t214;
            gm_tl1r7a2fn1ev6 t215;
            uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 24));
            if (!t217) goto ie216;
            uint64_t t219 = gm_l19;
            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 16));
            uint64_t r221; bool o222 = __builtin_add_overflow(t219, t220, &r221);
            if (o222) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 62, 60);
            uint64_t t223 = r221;
            gm_tou12vuc19e0s t224;
            memset(&t224, 0, sizeof t224);
            GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)2ULL));
            GM_ST(uint64_t, ((uint8_t *)&t224 + 8), t223);
            gm_tou12vuc19e0s t225 = t224;
            gm_tl1r7a2fn1ev6 t226;
            GM_ST(uint8_t, (uint8_t *)&t226 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t226 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
            ((uint8_t *)&t226)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t226 + 8), t225);
            t215 = t226;
            goto ix216;
ie216:;
            uint64_t t228 = gm_l19;
            uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 16));
            uint64_t r230; bool o231 = __builtin_add_overflow(t228, t229, &r230);
            if (o231) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 62, 88);
            uint64_t t232 = r230;
            gm_tou12vuc19e0s t233;
            memset(&t233, 0, sizeof t233);
            GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t233 + 8), t232);
            gm_tou12vuc19e0s t234 = t233;
            gm_tl1r7a2fn1ev6 t235;
            GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
            ((uint8_t *)&t235)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t235 + 8), t234);
            t215 = t235;
ix216:;
            gm_tl1r7a2fn1ev6 t236 = t215;
            uint8_t t237 = ((((uint8_t *)&t236))[0] != 0);
            F.ret = t215;
            goto gm_fret;
            goto me180;
mn212:;
ma183:;
            uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
            if (t239 != ((uint8_t)2ULL)) goto mn238;
            uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&t179) + 8));
            gm_l20 = t240;
            uint64_t t241 = gm_l20;
            uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 16));
            uint64_t r243; bool o244 = __builtin_add_overflow(t241, t242, &r243);
            if (o244) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 63, 49);
            uint64_t t245 = r243;
            gm_tou12vuc19e0s t246;
            memset(&t246, 0, sizeof t246);
            GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)2ULL));
            GM_ST(uint64_t, ((uint8_t *)&t246 + 8), t245);
            gm_tl1r7a2fn1ev6 t247;
            GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
            ((uint8_t *)&t247)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t247 + 8), t246);
            gm_tl1r7a2fn1ev6 t248 = t247;
            uint8_t t249 = ((((uint8_t *)&t248))[0] != 0);
            F.ret = t247;
            goto gm_fret;
            goto me180;
mn238:;
ma184:;
            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
            if (t251 != ((uint8_t)3ULL)) goto mn250;
            void * t252 = gm_l0;
            void * t253 = gm_l2;
            gm_tl874t2atnlh3 t254 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t253) + 112));
            gm_b16_8 t255;
            GM_ST(void *, (uint8_t *)&t255, (void *)(gm_sgu2tengos987));
            GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(40));
            gm_t76123gfet73g t256;
            gm_f_trap_qh84l9d((void *)&t256, (void *)NULL, cx, NULL, t252, t254, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl1r7a2fn1ev6 t257;
            GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t257 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t257 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t257 + 48, 0);
            ((uint8_t *)&t257)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t257 + 8), t256);
            gm_tl1r7a2fn1ev6 t258 = t257;
            uint8_t t259 = ((((uint8_t *)&t258))[0] != 0);
            F.ret = t257;
            goto gm_fret;
            goto me180;
mn250:;
ma185:;
            gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 57, 13);
me180:;
        }
        goto me3;
mn166:;
ma11:;
        uint8_t t261 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t261 != ((uint8_t)12ULL)) goto mn260;
        gm_tp0fa5qv7rqnn t262 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t2) + 0) + 8));
        F.l21 = t262;
        {
            uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 24));
            gm_l22 = t264;
        }
        {
            uint8_t t271 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 16) + 0));
            switch (t271) { case ((uint8_t)0ULL): goto ma266; case ((uint8_t)1ULL): goto ma267; case ((uint8_t)2ULL): goto ma268; case ((uint8_t)3ULL): goto ma269; default: goto ma270; }
ma266:;
            uint8_t t273 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 16) + 0));
            if (t273 != ((uint8_t)0ULL)) goto mn272;
            {
                void * t275 = gm_l0;
                gm_tbl8il92l97ut t276 = gm_l1;
                void * t277 = GM_LD(void *, (((uint8_t *)&F.l21) + 0));
                uint8_t t278 = gm_l3;
                gm_tl1r7a2fn1ev6 t279;
                gm_f_place_47rfqe5((void *)&t279, (void *)NULL, cx, NULL, t275, t276, t277, t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t280 = t279;
                if (((((uint8_t *)&t280))[0] != 0)) {
                    gm_t76123gfet73g t281 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t280) + 8));
                    gm_tl1r7a2fn1ev6 t282;
                    GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t282 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t282 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t282 + 48, 0);
                    ((uint8_t *)&t282)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t282 + 8), t281);
                    F.ret = t282;
                    goto gm_fret;
                }
                gm_tou12vuc19e0s t283 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t280) + 8));
                F.l23 = t283;
            }
            {
                void * t284 = gm_l0;
                gm_tbl8il92l97ut t285 = gm_l1;
                void * t286 = GM_LD(void *, (((uint8_t *)&F.l21) + 8));
                gm_t833u93mr4iob t287;
                gm_f_ev_index_bie377m((void *)&t287, (void *)NULL, cx, NULL, t284, t285, t286);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t288 = t287;
                if (((((uint8_t *)&t288))[0] != 0)) {
                    gm_t76123gfet73g t289 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t288) + 8));
                    gm_tl1r7a2fn1ev6 t290;
                    GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t290 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t290 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t290 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t290 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t290 + 48, 0);
                    ((uint8_t *)&t290)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t290 + 8), t289);
                    F.ret = t290;
                    goto gm_fret;
                }
                uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&t288) + 8));
                gm_l24 = t291;
            }
            {
                uint64_t t293 = gm_l24;
                uint64_t t294 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 32));
                uint8_t t295 = (t293 >= t294);
                if (!t295) goto ie292;
                {
                    void * t297 = gm_l0;
                    void * t298 = gm_l2;
                    gm_tl874t2atnlh3 t299 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t298) + 112));
                    uint64_t t300 = gm_l24;
                    uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 32));
                    gm_t2lqu8jsj511d t302;
                    GM_ST(uint64_t, ((uint8_t *)&t302 + 0), t300);
                    GM_ST(uint64_t, ((uint8_t *)&t302 + 8), t301);
                    gm_tabb710d9mq2e t303;
                    gm_f_format_2hd15t9((void *)&t303, (void *)NULL, cx, NULL, t302);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t304 = t303;
                    if (((((uint8_t *)&t304))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t304) + 8, gm_srcrvqeupiu1766, 73, 60);
                    gm_b16_8 t305 = GM_LD(gm_b16_8, (((uint8_t *)&t304) + 8));
                    gm_b16_8 t306 = t305;
                    gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)&t306));
                    gm_t76123gfet73g t308;
                    gm_f_trap_qh84l9d((void *)&t308, (void *)NULL, cx, NULL, t297, t299, t307);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl1r7a2fn1ev6 t309;
                    GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t309 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t309 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t309 + 48, 0);
                    ((uint8_t *)&t309)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t309 + 8), t308);
                    gm_tl1r7a2fn1ev6 t310 = t309;
                    uint8_t t311 = ((((uint8_t *)&t310))[0] != 0);
                    F.ret = t309;
                    goto gm_fret;
                }
                goto ix292;
ie292:;
ix292:;
            }
            {
                uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                switch (t318) { case ((uint8_t)0ULL): goto ma313; case ((uint8_t)1ULL): goto ma314; case ((uint8_t)2ULL): goto ma315; case ((uint8_t)3ULL): goto ma316; default: goto ma317; }
ma313:;
                uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                if (t320 != ((uint8_t)0ULL)) goto mn319;
                gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                gm_l25 = t321;
                {
                    uint64_t t323 = gm_l24;
                    uint64_t t324 = gm_l22;
                    uint64_t r325; bool o326 = __builtin_mul_overflow(t323, t324, &r325);
                    if (o326) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 76, 39);
                    uint64_t t327 = r325;
                    uint64_t t328 = gm_f_us_cuv01fp(t327);
                    gm_l26 = t328;
                }
                {
                    gm_b16_8 t329 = gm_l25;
                    gm_b16_8 t330 = t329;
                    uint64_t t331 = GM_LD(uintptr_t, ((uint8_t *)&t330) + 8);
                    void * t332 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t330)));
                    uint64_t t333 = gm_l26;
                    uint64_t t334 = (uintptr_t)t333;
                    uint64_t t335 = gm_l26;
                    uint64_t t336 = gm_l22;
                    uint64_t t337 = gm_f_us_cuv01fp(t336);
                    uint64_t r338; bool o339 = __builtin_add_overflow(t335, t337, &r338);
                    if (o339) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 77, 49);
                    uint64_t t340 = r338;
                    uint64_t t341 = (uintptr_t)t340;
                    if (t334 > t341 || t341 > (uintptr_t)t331) gm_trap_slice(t334, t341, t331, gm_srcrvqeupiu1766, 77, 42);
                    gm_b16_8 t342;
                    GM_ST(void *, (uint8_t *)&t342, (void *)(((uint8_t *)t332 + t334 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(t341 - t334));
                    gm_tou12vuc19e0s t343;
                    GM_ST(uint8_t, (uint8_t *)&t343 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t343 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t343 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t343 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t343 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t343 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t343 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t343 + 8), t342);
                    gm_tl1r7a2fn1ev6 t344;
                    GM_ST(uint8_t, (uint8_t *)&t344 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t344 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t344 + 4, 0);
                    ((uint8_t *)&t344)[0] = 0;
                    GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t344 + 8), t343);
                    gm_tl1r7a2fn1ev6 t345 = t344;
                    uint8_t t346 = ((((uint8_t *)&t345))[0] != 0);
                    F.ret = t344;
                    goto gm_fret;
                }
                goto me312;
mn319:;
ma314:;
                uint8_t t348 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                if (t348 != ((uint8_t)1ULL)) goto mn347;
                uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                gm_l27 = t349;
                uint64_t t350 = gm_l27;
                uint64_t t351 = gm_l24;
                uint64_t t352 = gm_l22;
                uint64_t r353; bool o354 = __builtin_mul_overflow(t351, t352, &r353);
                if (o354) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 79, 58);
                uint64_t t355 = r353;
                uint64_t r356; bool o357 = __builtin_add_overflow(t350, t355, &r356);
                if (o357) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 79, 51);
                uint64_t t358 = r356;
                gm_tou12vuc19e0s t359;
                memset(&t359, 0, sizeof t359);
                GM_ST(uint8_t, ((uint8_t *)&t359 + 0), ((uint8_t)1ULL));
                GM_ST(uint64_t, ((uint8_t *)&t359 + 8), t358);
                gm_tl1r7a2fn1ev6 t360;
                GM_ST(uint8_t, (uint8_t *)&t360 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t360 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t360 + 4, 0);
                ((uint8_t *)&t360)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t360 + 8), t359);
                gm_tl1r7a2fn1ev6 t361 = t360;
                uint8_t t362 = ((((uint8_t *)&t361))[0] != 0);
                F.ret = t360;
                goto gm_fret;
                goto me312;
mn347:;
ma315:;
                uint8_t t364 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                if (t364 != ((uint8_t)2ULL)) goto mn363;
                uint64_t t365 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                gm_l28 = t365;
                uint64_t t366 = gm_l28;
                uint64_t t367 = gm_l24;
                uint64_t t368 = gm_l22;
                uint64_t r369; bool o370 = __builtin_mul_overflow(t367, t368, &r369);
                if (o370) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 80, 64);
                uint64_t t371 = r369;
                uint64_t r372; bool o373 = __builtin_add_overflow(t366, t371, &r372);
                if (o373) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 80, 57);
                uint64_t t374 = r372;
                gm_tou12vuc19e0s t375;
                memset(&t375, 0, sizeof t375);
                GM_ST(uint8_t, ((uint8_t *)&t375 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t375 + 8), t374);
                gm_tl1r7a2fn1ev6 t376;
                GM_ST(uint8_t, (uint8_t *)&t376 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t376 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t376 + 4, 0);
                ((uint8_t *)&t376)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t376 + 8), t375);
                gm_tl1r7a2fn1ev6 t377 = t376;
                uint8_t t378 = ((((uint8_t *)&t377))[0] != 0);
                F.ret = t376;
                goto gm_fret;
                goto me312;
mn363:;
ma316:;
                uint8_t t380 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0));
                if (t380 != ((uint8_t)3ULL)) goto mn379;
                void * t381 = gm_l0;
                void * t382 = gm_l2;
                gm_tl874t2atnlh3 t383 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t382) + 112));
                gm_b16_8 t384;
                GM_ST(void *, (uint8_t *)&t384, (void *)(gm_s3hpe56o884a0));
                GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(41));
                gm_t76123gfet73g t385;
                gm_f_trap_qh84l9d((void *)&t385, (void *)NULL, cx, NULL, t381, t383, t384);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t386;
                GM_ST(uint8_t, (uint8_t *)&t386 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t386 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t386 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t386 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t386 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t386 + 48, 0);
                ((uint8_t *)&t386)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t386 + 8), t385);
                gm_tl1r7a2fn1ev6 t387 = t386;
                uint8_t t388 = ((((uint8_t *)&t387))[0] != 0);
                F.ret = t386;
                goto gm_fret;
                goto me312;
mn379:;
ma317:;
                gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 74, 21);
me312:;
            }
            goto me265;
mn272:;
ma267:;
            uint8_t t390 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 16) + 0));
            if (t390 != ((uint8_t)1ULL)) goto mn389;
            {
            }
            {
                void * t392 = ((uint8_t *)&gm_l29);
                uint64_t t393 = (uintptr_t)((uint64_t)0ULL);
                void * t394 = gm_l0;
                uint64_t t395 = GM_LD(uint64_t, (((uint8_t *)t394) + 176));
                uint64_t r396; bool o397 = __builtin_mul_overflow(((uint64_t)2ULL), t395, &r396);
                if (o397) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 86, 35);
                uint64_t t398 = r396;
                uint64_t t399 = gm_f_us_cuv01fp(t398);
                uint64_t t400 = (uintptr_t)t399;
                if (t393 > t400 || t400 > (uintptr_t)16) gm_trap_slice(t393, t400, 16, gm_srcrvqeupiu1766, 86, 27);
                gm_b16_8 t401;
                GM_ST(void *, (uint8_t *)&t401, (void *)(((uint8_t *)t392 + t393 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t401 + 8, (uintptr_t)(t400 - t393));
                gm_l30 = t401;
            }
            {
                void * t402 = gm_l0;
                gm_tbl8il92l97ut t403 = gm_l1;
                void * t404 = GM_LD(void *, (((uint8_t *)&F.l21) + 0));
                gm_b16_8 t405 = gm_l30;
                gm_tcfa6l7reoosn t406;
                gm_f_ev_25jfdar((void *)&t406, (void *)NULL, cx, NULL, t402, t403, t404, t405);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t407 = t406;
                if (((((uint8_t *)&t407))[0] != 0)) {
                    gm_t76123gfet73g t408 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t407) + 8));
                    gm_tl1r7a2fn1ev6 t409;
                    GM_ST(uint8_t, (uint8_t *)&t409 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t409 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t409 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 48, 0);
                    ((uint8_t *)&t409)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t409 + 8), t408);
                    F.ret = t409;
                    goto gm_fret;
                }
                gm_b0_1 t410 = GM_LD(gm_b0_1, (((uint8_t *)&t407) + 8));
            }
            {
                void * t411 = gm_l0;
                gm_b16_8 t412 = gm_l30;
                gm_b16_8 t413 = t412;
                gm_b16_8 t414 = GM_LD(gm_b16_8, ((uint8_t *)&t413));
                gm_t2lqu8jsj511d t415 = gm_f_slice_parts_1se5hu4(t411, t414);
                gm_l31 = t415;
            }
            {
                void * t416 = gm_l0;
                gm_tbl8il92l97ut t417 = gm_l1;
                void * t418 = GM_LD(void *, (((uint8_t *)&F.l21) + 8));
                gm_t833u93mr4iob t419;
                gm_f_ev_index_bie377m((void *)&t419, (void *)NULL, cx, NULL, t416, t417, t418);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t420 = t419;
                if (((((uint8_t *)&t420))[0] != 0)) {
                    gm_t76123gfet73g t421 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t420) + 8));
                    gm_tl1r7a2fn1ev6 t422;
                    GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t422 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t422 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t422 + 48, 0);
                    ((uint8_t *)&t422)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t422 + 8), t421);
                    F.ret = t422;
                    goto gm_fret;
                }
                uint64_t t423 = GM_LD(uint64_t, (((uint8_t *)&t420) + 8));
                gm_l32 = t423;
            }
            {
                uint64_t t425 = gm_l32;
                uint64_t t426 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                uint8_t t427 = (t425 >= t426);
                if (!t427) goto ie424;
                {
                    void * t429 = gm_l0;
                    void * t430 = gm_l2;
                    gm_tl874t2atnlh3 t431 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t430) + 112));
                    uint64_t t432 = gm_l32;
                    uint64_t t433 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                    gm_t2lqu8jsj511d t434;
                    GM_ST(uint64_t, ((uint8_t *)&t434 + 0), t432);
                    GM_ST(uint64_t, ((uint8_t *)&t434 + 8), t433);
                    gm_tabb710d9mq2e t435;
                    gm_f_format_2hd15t9((void *)&t435, (void *)NULL, cx, NULL, t434);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t436 = t435;
                    if (((((uint8_t *)&t436))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t436) + 8, gm_srcrvqeupiu1766, 90, 61);
                    gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)&t436) + 8));
                    gm_b16_8 t438 = t437;
                    gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&t438));
                    gm_t76123gfet73g t440;
                    gm_f_trap_qh84l9d((void *)&t440, (void *)NULL, cx, NULL, t429, t431, t439);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl1r7a2fn1ev6 t441;
                    GM_ST(uint8_t, (uint8_t *)&t441 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t441 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t441 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t441 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t441 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t441 + 48, 0);
                    ((uint8_t *)&t441)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t441 + 8), t440);
                    gm_tl1r7a2fn1ev6 t442 = t441;
                    uint8_t t443 = ((((uint8_t *)&t442))[0] != 0);
                    F.ret = t441;
                    goto gm_fret;
                }
                goto ix424;
ie424:;
ix424:;
            }
            {
                uint64_t t444 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 0));
                uint64_t t445 = gm_l32;
                uint64_t t446 = gm_l22;
                uint64_t r447; bool o448 = __builtin_mul_overflow(t445, t446, &r447);
                if (o448) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 91, 43);
                uint64_t t449 = r447;
                uint64_t r450; bool o451 = __builtin_add_overflow(t444, t449, &r450);
                uint64_t t452 = r450;
                gm_tou12vuc19e0s t453;
                memset(&t453, 0, sizeof t453);
                GM_ST(uint8_t, ((uint8_t *)&t453 + 0), ((uint8_t)1ULL));
                GM_ST(uint64_t, ((uint8_t *)&t453 + 8), t452);
                gm_tl1r7a2fn1ev6 t454;
                GM_ST(uint8_t, (uint8_t *)&t454 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t454 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t454 + 4, 0);
                ((uint8_t *)&t454)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t454 + 8), t453);
                gm_tl1r7a2fn1ev6 t455 = t454;
                uint8_t t456 = ((((uint8_t *)&t455))[0] != 0);
                F.ret = t454;
                goto gm_fret;
            }
            goto me265;
mn389:;
ma268:;
            uint8_t t458 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 16) + 0));
            if (t458 != ((uint8_t)2ULL)) goto mn457;
            {
                void * t460 = gm_l0;
                gm_tbl8il92l97ut t461 = gm_l1;
                void * t462 = GM_LD(void *, (((uint8_t *)&F.l21) + 0));
                gm_t833u93mr4iob t463;
                gm_f_ew_c5kil07((void *)&t463, (void *)NULL, cx, NULL, t460, t461, t462);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t464 = t463;
                if (((((uint8_t *)&t464))[0] != 0)) {
                    gm_t76123gfet73g t465 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t464) + 8));
                    gm_tl1r7a2fn1ev6 t466;
                    GM_ST(uint8_t, (uint8_t *)&t466 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t466 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t466 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t466 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t466 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t466 + 48, 0);
                    ((uint8_t *)&t466)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t466 + 8), t465);
                    F.ret = t466;
                    goto gm_fret;
                }
                uint64_t t467 = GM_LD(uint64_t, (((uint8_t *)&t464) + 8));
                gm_l33 = t467;
            }
            {
                void * t468 = gm_l0;
                gm_tbl8il92l97ut t469 = gm_l1;
                void * t470 = GM_LD(void *, (((uint8_t *)&F.l21) + 8));
                gm_t833u93mr4iob t471;
                gm_f_ev_index_bie377m((void *)&t471, (void *)NULL, cx, NULL, t468, t469, t470);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t472 = t471;
                if (((((uint8_t *)&t472))[0] != 0)) {
                    gm_t76123gfet73g t473 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t472) + 8));
                    gm_tl1r7a2fn1ev6 t474;
                    GM_ST(uint8_t, (uint8_t *)&t474 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t474 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t474 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t474 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t474 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t474 + 48, 0);
                    ((uint8_t *)&t474)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t474 + 8), t473);
                    F.ret = t474;
                    goto gm_fret;
                }
                uint64_t t475 = GM_LD(uint64_t, (((uint8_t *)&t472) + 8));
                gm_l34 = t475;
            }
            {
                uint64_t t476 = gm_l33;
                uint64_t t477 = gm_l34;
                uint64_t t478 = gm_l22;
                uint64_t r479; bool o480 = __builtin_mul_overflow(t477, t478, &r479);
                uint64_t t481 = r479;
                uint64_t r482; bool o483 = __builtin_add_overflow(t476, t481, &r482);
                uint64_t t484 = r482;
                gm_tou12vuc19e0s t485;
                memset(&t485, 0, sizeof t485);
                GM_ST(uint8_t, ((uint8_t *)&t485 + 0), ((uint8_t)1ULL));
                GM_ST(uint64_t, ((uint8_t *)&t485 + 8), t484);
                gm_tl1r7a2fn1ev6 t486;
                GM_ST(uint8_t, (uint8_t *)&t486 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t486 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t486 + 4, 0);
                ((uint8_t *)&t486)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t486 + 8), t485);
                gm_tl1r7a2fn1ev6 t487 = t486;
                uint8_t t488 = ((((uint8_t *)&t487))[0] != 0);
                F.ret = t486;
                goto gm_fret;
            }
            goto me265;
mn457:;
ma269:;
            uint8_t t490 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 16) + 0));
            if (t490 != ((uint8_t)3ULL)) goto mn489;
            {
                void * t492 = gm_l0;
                gm_tbl8il92l97ut t493 = gm_l1;
                void * t494 = GM_LD(void *, (((uint8_t *)&F.l21) + 0));
                gm_t833u93mr4iob t495;
                gm_f_ew_c5kil07((void *)&t495, (void *)NULL, cx, NULL, t492, t493, t494);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t496 = t495;
                if (((((uint8_t *)&t496))[0] != 0)) {
                    gm_t76123gfet73g t497 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t496) + 8));
                    gm_tl1r7a2fn1ev6 t498;
                    GM_ST(uint8_t, (uint8_t *)&t498 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t498 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t498 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t498 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t498 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t498 + 48, 0);
                    ((uint8_t *)&t498)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t498 + 8), t497);
                    F.ret = t498;
                    goto gm_fret;
                }
                uint64_t t499 = GM_LD(uint64_t, (((uint8_t *)&t496) + 8));
                gm_l35 = t499;
            }
            {
                void * t500 = gm_l0;
                gm_tbl8il92l97ut t501 = gm_l1;
                void * t502 = GM_LD(void *, (((uint8_t *)&F.l21) + 8));
                gm_t833u93mr4iob t503;
                gm_f_ev_index_bie377m((void *)&t503, (void *)NULL, cx, NULL, t500, t501, t502);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t504 = t503;
                if (((((uint8_t *)&t504))[0] != 0)) {
                    gm_t76123gfet73g t505 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t504) + 8));
                    gm_tl1r7a2fn1ev6 t506;
                    GM_ST(uint8_t, (uint8_t *)&t506 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t506 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t506 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t506 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t506 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t506 + 48, 0);
                    ((uint8_t *)&t506)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t506 + 8), t505);
                    F.ret = t506;
                    goto gm_fret;
                }
                uint64_t t507 = GM_LD(uint64_t, (((uint8_t *)&t504) + 8));
                gm_l36 = t507;
            }
            {
                uint64_t t509 = gm_l36;
                uint64_t t510 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 32));
                uint8_t t511 = (t509 >= t510);
                if (!t511) goto ie508;
                {
                    void * t513 = gm_l0;
                    void * t514 = gm_l2;
                    gm_tl874t2atnlh3 t515 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t514) + 112));
                    uint64_t t516 = gm_l36;
                    uint64_t t517 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 32));
                    gm_t2lqu8jsj511d t518;
                    GM_ST(uint64_t, ((uint8_t *)&t518 + 0), t516);
                    GM_ST(uint64_t, ((uint8_t *)&t518 + 8), t517);
                    gm_tabb710d9mq2e t519;
                    gm_f_format_2hd15t9((void *)&t519, (void *)NULL, cx, NULL, t518);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t520 = t519;
                    if (((((uint8_t *)&t520))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t520) + 8, gm_srcrvqeupiu1766, 101, 60);
                    gm_b16_8 t521 = GM_LD(gm_b16_8, (((uint8_t *)&t520) + 8));
                    gm_b16_8 t522 = t521;
                    gm_b16_8 t523 = GM_LD(gm_b16_8, ((uint8_t *)&t522));
                    gm_t76123gfet73g t524;
                    gm_f_trap_qh84l9d((void *)&t524, (void *)NULL, cx, NULL, t513, t515, t523);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl1r7a2fn1ev6 t525;
                    GM_ST(uint8_t, (uint8_t *)&t525 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t525 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t525 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t525 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t525 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t525 + 48, 0);
                    ((uint8_t *)&t525)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t525 + 8), t524);
                    gm_tl1r7a2fn1ev6 t526 = t525;
                    uint8_t t527 = ((((uint8_t *)&t526))[0] != 0);
                    F.ret = t525;
                    goto gm_fret;
                }
                goto ix508;
ie508:;
ix508:;
            }
            {
                uint64_t t528 = gm_l35;
                uint64_t t529 = gm_l36;
                uint64_t t530 = gm_l22;
                uint64_t r531; bool o532 = __builtin_mul_overflow(t529, t530, &r531);
                if (o532) gm_trap("integer overflow in `*`", gm_srcrvqeupiu1766, 102, 38);
                uint64_t t533 = r531;
                uint64_t r534; bool o535 = __builtin_add_overflow(t528, t533, &r534);
                uint64_t t536 = r534;
                gm_tou12vuc19e0s t537;
                memset(&t537, 0, sizeof t537);
                GM_ST(uint8_t, ((uint8_t *)&t537 + 0), ((uint8_t)1ULL));
                GM_ST(uint64_t, ((uint8_t *)&t537 + 8), t536);
                gm_tl1r7a2fn1ev6 t538;
                GM_ST(uint8_t, (uint8_t *)&t538 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t538 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t538 + 4, 0);
                ((uint8_t *)&t538)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t538 + 8), t537);
                gm_tl1r7a2fn1ev6 t539 = t538;
                uint8_t t540 = ((((uint8_t *)&t539))[0] != 0);
                F.ret = t538;
                goto gm_fret;
            }
            goto me265;
mn489:;
ma270:;
            gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 69, 13);
me265:;
        }
        goto me3;
mn260:;
ma12:;
        {
            void * t543 = gm_l0;
            void * t544 = gm_l2;
            uint64_t t545 = GM_LD(uint64_t, (((uint8_t *)t544) + 104));
            gm_b16_8 t546 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t543, t545);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l37 = t546;
        }
        {
            void * t547 = gm_l0;
            gm_tbl8il92l97ut t548 = gm_l1;
            void * t549 = gm_l2;
            gm_b16_8 t550 = gm_l37;
            gm_tcfa6l7reoosn t551;
            gm_f_ev_25jfdar((void *)&t551, (void *)NULL, cx, NULL, t547, t548, t549, t550);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t552 = t551;
            if (((((uint8_t *)&t552))[0] != 0)) {
                gm_t76123gfet73g t553 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t552) + 8));
                gm_tl1r7a2fn1ev6 t554;
                GM_ST(uint8_t, (uint8_t *)&t554 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t554 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t554 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t554 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t554 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t554 + 48, 0);
                ((uint8_t *)&t554)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t554 + 8), t553);
                F.ret = t554;
                goto gm_fret;
            }
            gm_b0_1 t555 = GM_LD(gm_b0_1, (((uint8_t *)&t552) + 8));
        }
        {
            uint8_t t557 = gm_l3;
            uint8_t t558 = (uint8_t)!t557;
            if (!t558) goto ie556;
            {
                gm_b16_8 t560 = gm_l37;
                gm_tou12vuc19e0s t561;
                GM_ST(uint8_t, (uint8_t *)&t561 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t561 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t561 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t561 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t561 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t561 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t561 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t561 + 8), t560);
                gm_tl1r7a2fn1ev6 t562;
                GM_ST(uint8_t, (uint8_t *)&t562 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t562 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t562 + 4, 0);
                ((uint8_t *)&t562)[0] = 0;
                GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t562 + 8), t561);
                gm_tl1r7a2fn1ev6 t563 = t562;
                uint8_t t564 = ((((uint8_t *)&t563))[0] != 0);
                F.ret = t562;
                goto gm_fret;
            }
            goto ix556;
ie556:;
ix556:;
        }
        {
            void * t565 = gm_l0;
            void * t566 = gm_l2;
            gm_tl874t2atnlh3 t567 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t566) + 112));
            gm_b16_8 t568 = gm_l37;
            gm_b16_8 t569 = t568;
            gm_b16_8 t570 = GM_LD(gm_b16_8, ((uint8_t *)&t569));
            void * t571 = gm_l0;
            void * t572 = gm_l2;
            gm_t88rcucg1b7uk t573 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t572) + 96));
            uint64_t t574 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t571, t573);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t575;
            gm_f_alloc_temp_9cmh124((void *)&t575, (void *)NULL, cx, NULL, t565, t567, t570, t574);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t576 = t575;
            if (((((uint8_t *)&t576))[0] != 0)) {
                gm_t76123gfet73g t577 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t576) + 8));
                gm_tl1r7a2fn1ev6 t578;
                GM_ST(uint8_t, (uint8_t *)&t578 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t578 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t578 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t578 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t578 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t578 + 48, 0);
                ((uint8_t *)&t578)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t578 + 8), t577);
                F.ret = t578;
                goto gm_fret;
            }
            uint64_t t579 = GM_LD(uint64_t, (((uint8_t *)&t576) + 8));
            gm_tou12vuc19e0s t580;
            memset(&t580, 0, sizeof t580);
            GM_ST(uint8_t, ((uint8_t *)&t580 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t580 + 8), t579);
            gm_tl1r7a2fn1ev6 t581;
            GM_ST(uint8_t, (uint8_t *)&t581 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t581 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t581 + 4, 0);
            ((uint8_t *)&t581)[0] = 0;
            GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t581 + 8), t580);
            gm_tl1r7a2fn1ev6 t582 = t581;
            uint8_t t583 = ((((uint8_t *)&t582))[0] != 0);
            F.ret = t581;
            goto gm_fret;
        }
        goto me3;
mn541:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 40, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_read_local_n8sc5ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4) {
    struct gm_fr_read_local_n8sc5ua F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    gm_b16_8 gm_l4; /* dst */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* off */
    GM_STACK_CHECK(&F, 505u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            uint32_t t3 = gm_l3;
            uint64_t t4 = (uint64_t)t3;
            uint64_t t5 = gm_f_us_cuv01fp(t4);
            gm_l5 = t5;
        }
        {
            uint64_t t7 = gm_l5;
            uint64_t t8 = (uintptr_t)t7;
            void * t9 = gm_l2;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 64) + 8);
            if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcrvqeupiu1766, 15, 8);
            uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 64)) + t8 * 8));
            uint8_t t12 = (!(t11 == ((uint64_t)0ULL)));
            if (!t12) goto ie6;
            {
                void * t14 = gm_l0;
                gm_tl874t2atnlh3 t15 = gm_l1;
                uint64_t t16 = gm_l5;
                uint64_t t17 = (uintptr_t)t16;
                void * t18 = gm_l2;
                uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 64) + 8);
                if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcrvqeupiu1766, 15, 52);
                uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 64)) + t17 * 8));
                uint64_t t21 = gm_l5;
                uint64_t t22 = (uintptr_t)t21;
                void * t23 = gm_l2;
                void * t24 = GM_LD(void *, (((uint8_t *)t23) + 16));
                uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 104) + 8);
                if (t22 >= (uintptr_t)(t25)) gm_trap_index(t22, t25, gm_srcrvqeupiu1766, 15, 63);
                uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t24) + 104)) + t22 * 8));
                gm_b16_8 t27 = gm_l4;
                gm_f_load_at_6u81jcc((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t14, t15, t20, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t28 = gm_l5;
            uint64_t t29 = (uintptr_t)t28;
            void * t30 = gm_l2;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 16));
            uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)t31) + 72) + 8);
            if (t29 >= (uintptr_t)(t32)) gm_trap_index(t29, t32, gm_srcrvqeupiu1766, 16, 15);
            uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t31) + 72)) + t29 * 8));
            uint64_t t34 = gm_f_us_cuv01fp(t33);
            gm_l6 = t34;
        }
        {
            gm_b16_8 t35 = gm_l4;
            void * t36 = gm_l2;
            uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 48) + 8);
            void * t38 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 48)));
            uint64_t t39 = gm_l6;
            uint64_t t40 = (uintptr_t)t39;
            uint64_t t41 = gm_l6;
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t r43; bool o44 = __builtin_add_overflow(t41, t42, &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 17, 33);
            uint64_t t45 = r43;
            uint64_t t46 = (uintptr_t)t45;
            if (t40 > t46 || t46 > (uintptr_t)t37) gm_trap_slice(t40, t46, t37, gm_srcrvqeupiu1766, 17, 19);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t38 + t40 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t40));
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_f_copy_from_f7j6p05(t35, t49);
        }
    }
    gm_b0_1 t50;
    gm_tcfa6l7reoosn t51;
    GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t51 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
    ((uint8_t *)&t51)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t51 + 8), t50);
    gm_tcfa6l7reoosn t52 = t51;
    F.ret = t52;
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
__attribute__((aligned(4))) void gm_f_read_place_fcc38bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_read_place_fcc38bb F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* dst */
    gm_b16_8 gm_l5; /* h */
    uint64_t gm_l6; /* addr */
    uint64_t gm_l7; /* addr */
    unsigned __int128 gm_l9; /* whole */
    unsigned __int128 gm_l10; /* raw */
    uint32_t gm_l11; /* w */
    GM_STACK_CHECK(&F, 1118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; default: goto ma8; }
ma4:;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t11 != ((uint8_t)0ULL)) goto mn10;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l5 = t12;
            gm_b16_8 t13 = gm_l4;
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
            uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t18 = (uintptr_t)t17;
            if (t16 > t18 || t18 > (uintptr_t)t14) gm_trap_slice(t16, t18, t14, gm_srcrvqeupiu1766, 117, 35);
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t15 + t16 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_f_copy_from_f7j6p05(t13, t21);
            goto me3;
mn10:;
ma5:;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t23 != ((uint8_t)1ULL)) goto mn22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
            gm_l6 = t24;
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l1;
            uint64_t t27 = gm_l6;
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l3;
            uint64_t t30 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t31 = gm_l4;
            gm_tcfa6l7reoosn t32;
            gm_f_load_at_6u81jcc((void *)&t32, (void *)NULL, cx, NULL, t25, t26, t27, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t76123gfet73g t34 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t33) + 8));
                gm_tcfa6l7reoosn t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t35 + 8), t34);
                F.ret = t35;
                goto gm_fret;
            }
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 8));
            goto me3;
mn22:;
ma6:;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t38 != ((uint8_t)2ULL)) goto mn37;
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
            gm_l7 = t39;
            void * t40 = gm_l0;
            gm_tl874t2atnlh3 t41 = gm_l1;
            uint64_t t42 = gm_l7;
            gm_b16_8 t43 = gm_l4;
            gm_tcfa6l7reoosn t44;
            gm_f_load_at_6u81jcc((void *)&t44, (void *)NULL, cx, NULL, t40, t41, t42, ((uint64_t)1ULL), t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t45 = t44;
            if (((((uint8_t *)&t45))[0] != 0)) {
                gm_t76123gfet73g t46 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t45) + 8));
                gm_tcfa6l7reoosn t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t47 + 8), t46);
                F.ret = t47;
                goto gm_fret;
            }
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 8));
            goto me3;
mn37:;
ma7:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t50 != ((uint8_t)3ULL)) goto mn49;
            gm_t0h5lk09e9p5r t51 = GM_LD(gm_t0h5lk09e9p5r, (((uint8_t *)a2) + 8));
            F.l8 = t51;
            {
                void * t53 = gm_l0;
                gm_tl874t2atnlh3 t54 = gm_l1;
                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
                gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 24));
                gm_tdlro53ljvkl0 t58;
                gm_f_bits_whole_u4vfvvk((void *)&t58, (void *)NULL, cx, NULL, t53, t54, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tdlro53ljvkl0 t59 = t58;
                if (((((uint8_t *)&t59))[0] != 0)) {
                    gm_t76123gfet73g t60 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t59) + 16));
                    gm_tcfa6l7reoosn t61;
                    GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                    ((uint8_t *)&t61)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t61 + 8), t60);
                    F.ret = t61;
                    goto gm_fret;
                }
                unsigned __int128 t62 = GM_LD(unsigned __int128, (((uint8_t *)&t59) + 16));
                gm_l9 = t62;
            }
            {
                unsigned __int128 t63 = gm_l9;
                uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 28));
                unsigned __int128 t65 = (unsigned __int128)t64;
                unsigned __int128 n66 = (unsigned __int128)t65;
                if (n66 >= 128) gm_trap_shift((uint64_t)n66, "u128", gm_srcrvqeupiu1766, 122, 20);
                unsigned __int128 t67 = (unsigned __int128)(t63 >> (int)n66);
                gm_l10 = t67;
            }
            {
                void * t68 = gm_l0;
                gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 32));
                uint32_t t70 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t70;
            }
            {
                uint32_t t72 = gm_l11;
                uint8_t t73 = (t72 < ((uint32_t)128ULL));
                if (!t73) goto ie71;
                {
                    unsigned __int128 t75 = gm_l10;
                    uint32_t t76 = gm_l11;
                    unsigned __int128 t77 = (unsigned __int128)t76;
                    unsigned __int128 n78 = (unsigned __int128)t77;
                    if (n78 >= 128) gm_trap_shift((uint64_t)n78, "u128", gm_srcrvqeupiu1766, 124, 34);
                    unsigned __int128 r79 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n78);
                    bool o80 = (unsigned __int128)(r79 >> (int)n78) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                    if (o80) gm_trap("integer overflow in `<<`", gm_srcrvqeupiu1766, 124, 34);
                    unsigned __int128 t81 = r79;
                    unsigned __int128 r82; bool o83 = __builtin_sub_overflow(t81, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r82);
                    if (o83) gm_trap("integer overflow in `-`", gm_srcrvqeupiu1766, 124, 33);
                    unsigned __int128 t84 = r82;
                    unsigned __int128 t85 = (unsigned __int128)(t75 & t84);
                    gm_l10 = t85;
                }
                goto ix71;
ie71:;
ix71:;
            }
            {
                void * t86 = gm_l0;
                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 32));
                unsigned __int128 t88 = gm_l10;
                gm_b16_8 t89 = gm_l4;
                gm_b0_1 t90 = gm_f_bits_from_raw_el4tjdh((void *)NULL, cx, NULL, t86, t87, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me3;
mn49:;
ma8:;
            gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 116, 5);
me3:;
        }
    }
    gm_b0_1 t91;
    gm_tcfa6l7reoosn t92;
    GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t92 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t92 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t92 + 24, 0);
    ((uint8_t *)&t92)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t92 + 8), t91);
    gm_tcfa6l7reoosn t93 = t92;
    F.ret = t93;
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
inline __attribute__((aligned(4))) void gm_f_resolve_target_bdf5i1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_te9hrk0u35rev a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_resolve_target_bdf5i1e F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_te9hrk0u35rev gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* label */
    void * gm_l6; /* fr */
    gm_tbl8il92l97ut gm_l7; /* x */
    GM_STACK_CHECK(&F, 658u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t4 != ((uint8_t)0ULL)) goto mn3;
        uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 4) + 0));
        gm_l4 = t5;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 4) + 4));
        gm_l5 = t6;
        {
            void * t8 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            uint32_t t9 = gm_l4;
            void * t10 = gm_f_frame_up_ghkkuqg(t8, t9);
            void * t11 = t10;
            void * t12;
            if ((GM_LD(void *, ((uint8_t *)&t11)) != 0)) {
                void * t13 = GM_LD(void *, (((uint8_t *)&t11) + 0));
                t12 = t13;
            } else {
                void * t14 = gm_l0;
                gm_tl874t2atnlh3 t15 = gm_l3;
                gm_b16_8 t16;
                GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s718drvtpg9kd));
                GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(48));
                gm_t76123gfet73g t17;
                gm_f_trap_qh84l9d((void *)&t17, (void *)NULL, cx, NULL, t14, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9b5r56q9jk9c t18;
                GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                ((uint8_t *)&t18)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t18 + 8), t17);
                gm_t9b5r56q9jk9c t19 = t18;
                uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
                F.ret = t18;
                goto gm_fret;
            }
            gm_l6 = t12;
        }
        {
            void * t21 = gm_l6;
            uint32_t t22 = gm_l5;
            gm_tbl8il92l97ut t23;
            GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t23 + 0), t21);
            GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t22);
            gm_t9b5r56q9jk9c t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
            ((uint8_t *)&t24)[0] = 0;
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t24 + 8), t23);
            gm_t9b5r56q9jk9c t25 = t24;
            uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto me2;
mn3:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t28 != ((uint8_t)1ULL)) goto mn27;
        {
            void * t30 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_t5lf9i3n1r3co t31 = GM_LD(gm_t5lf9i3n1r3co, (((uint8_t *)t30) + 216));
            gm_t5lf9i3n1r3co t32 = t31;
            gm_tbl8il92l97ut t33;
            if (((((uint8_t *)&t32))[16] != 0)) {
                gm_tbl8il92l97ut t34 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t32) + 0));
                t33 = t34;
            } else {
                void * t35 = gm_l0;
                gm_tl874t2atnlh3 t36 = gm_l3;
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sevktc6u3mas1));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(51));
                gm_t76123gfet73g t38;
                gm_f_trap_qh84l9d((void *)&t38, (void *)NULL, cx, NULL, t35, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9b5r56q9jk9c t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                ((uint8_t *)&t39)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t39 + 8), t38);
                gm_t9b5r56q9jk9c t40 = t39;
                uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
                F.ret = t39;
                goto gm_fret;
            }
            gm_l7 = t33;
        }
        {
            void * t42 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
            uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
            gm_tbl8il92l97ut t44;
            GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t44 + 0), t42);
            GM_ST(uint32_t, ((uint8_t *)&t44 + 8), t43);
            gm_t9b5r56q9jk9c t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
            ((uint8_t *)&t45)[0] = 0;
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t45 + 8), t44);
            gm_t9b5r56q9jk9c t46 = t45;
            uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
            F.ret = t45;
            goto gm_fret;
        }
        goto me2;
mn27:;
        gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 184, 5);
me2:;
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
__attribute__((aligned(4))) void gm_f_set_local_ug1vgfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4) {
    struct gm_fr_set_local_ug1vgfu F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    gm_b16_8 gm_l4; /* data */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* off */
    GM_STACK_CHECK(&F, 715u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            uint32_t t3 = gm_l3;
            uint64_t t4 = (uint64_t)t3;
            uint64_t t5 = gm_f_us_cuv01fp(t4);
            gm_l5 = t5;
        }
        {
            uint64_t t7 = gm_l5;
            uint64_t t8 = (uintptr_t)t7;
            void * t9 = gm_l2;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 64) + 8);
            if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcrvqeupiu1766, 22, 8);
            uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 64)) + t8 * 8));
            uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
            if (!t12) goto ie6;
            uint64_t t13 = gm_l5;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l2;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 16));
            uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 120) + 8);
            if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srcrvqeupiu1766, 22, 27);
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t16) + 120)) + t14 * 1));
            if (!t18) goto ie6;
            {
                {
                    void * t21 = gm_l0;
                    gm_tl874t2atnlh3 t22 = gm_l1;
                    void * t23 = gm_l2;
                    uint32_t t24 = gm_l3;
                    gm_t833u93mr4iob t25;
                    gm_f_local_addr_o51qq3k((void *)&t25, (void *)NULL, cx, NULL, t21, t22, t23, t24);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t833u93mr4iob t26 = t25;
                    if (((((uint8_t *)&t26))[0] != 0)) {
                        gm_t76123gfet73g t27 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t26) + 8));
                        gm_tcfa6l7reoosn t28;
                        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                        ((uint8_t *)&t28)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), t27);
                        F.ret = t28;
                        goto gm_fret;
                    }
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&t26) + 8));
                }
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t31 = gm_l5;
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l2;
            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 64) + 8);
            if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srcrvqeupiu1766, 23, 8);
            uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 64)) + t32 * 8));
            uint8_t t36 = (!(t35 == ((uint64_t)0ULL)));
            if (!t36) goto ie30;
            {
                void * t38 = gm_l0;
                gm_tl874t2atnlh3 t39 = gm_l1;
                uint64_t t40 = gm_l5;
                uint64_t t41 = (uintptr_t)t40;
                void * t42 = gm_l2;
                uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)t42) + 64) + 8);
                if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_srcrvqeupiu1766, 23, 53);
                uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t42) + 64)) + t41 * 8));
                uint64_t t45 = gm_l5;
                uint64_t t46 = (uintptr_t)t45;
                void * t47 = gm_l2;
                void * t48 = GM_LD(void *, (((uint8_t *)t47) + 16));
                uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 104) + 8);
                if (t46 >= (uintptr_t)(t49)) gm_trap_index(t46, t49, gm_srcrvqeupiu1766, 23, 64);
                uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 104)) + t46 * 8));
                gm_b16_8 t51 = gm_l4;
                gm_f_store_at_ktabiji((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t38, t39, t44, t50, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            uint64_t t52 = gm_l5;
            uint64_t t53 = (uintptr_t)t52;
            void * t54 = gm_l2;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 16));
            uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)t55) + 72) + 8);
            if (t53 >= (uintptr_t)(t56)) gm_trap_index(t53, t56, gm_srcrvqeupiu1766, 24, 15);
            uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t55) + 72)) + t53 * 8));
            uint64_t t58 = gm_f_us_cuv01fp(t57);
            gm_l6 = t58;
        }
        {
            void * t59 = gm_l2;
            uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 48) + 8);
            void * t61 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t59) + 48)));
            uint64_t t62 = gm_l6;
            uint64_t t63 = (uintptr_t)t62;
            uint64_t t64 = gm_l6;
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t r66; bool o67 = __builtin_add_overflow(t64, t65, &r66);
            if (o67) gm_trap("integer overflow in `+`", gm_srcrvqeupiu1766, 25, 19);
            uint64_t t68 = r66;
            uint64_t t69 = (uintptr_t)t68;
            if (t63 > t69 || t69 > (uintptr_t)t60) gm_trap_slice(t63, t69, t60, gm_srcrvqeupiu1766, 25, 5);
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t61 + t63 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69 - t63));
            gm_b16_8 t71 = gm_l4;
            gm_f_copy_from_f7j6p05(t70, t71);
        }
    }
    gm_b0_1 t72;
    gm_tcfa6l7reoosn t73;
    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t73 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t73 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t73 + 24, 0);
    ((uint8_t *)&t73)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t73 + 8), t72);
    gm_tcfa6l7reoosn t74 = t73;
    F.ret = t74;
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
__attribute__((aligned(4))) void gm_f_write_place_pd5r6up(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_write_place_pd5r6up F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* data */
    gm_b16_8 gm_l5; /* h */
    uint64_t gm_l6; /* addr */
    uint64_t gm_l7; /* addr */
    unsigned __int128 gm_l9; /* whole */
    uint32_t gm_l10; /* w */
    unsigned __int128 gm_l11; /* mask */
    unsigned __int128 gm_l12; /* raw */
    unsigned __int128 gm_l13; /* v */
    gm_t88rcucg1b7uk gm_l14; /* u */
    uint64_t gm_l15; /* n */
    gm_t51b2en4qp02s gm_l16; /* b */
    GM_STACK_CHECK(&F, 1803u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; default: goto ma8; }
ma4:;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t11 != ((uint8_t)0ULL)) goto mn10;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l5 = t12;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
            uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t17 = (uintptr_t)t16;
            if (t15 > t17 || t17 > (uintptr_t)t13) gm_trap_slice(t15, t17, t13, gm_srcrvqeupiu1766, 132, 21);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t14 + t15 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(t17 - t15));
            gm_b16_8 t19 = gm_l4;
            gm_f_copy_from_f7j6p05(t18, t19);
            goto me3;
mn10:;
ma5:;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t21 != ((uint8_t)1ULL)) goto mn20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
            gm_l6 = t22;
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = gm_l1;
            uint64_t t25 = gm_l6;
            void * t26 = gm_l0;
            gm_t88rcucg1b7uk t27 = gm_l3;
            uint64_t t28 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t29 = gm_l4;
            gm_tcfa6l7reoosn t30;
            gm_f_store_at_ktabiji((void *)&t30, (void *)NULL, cx, NULL, t23, t24, t25, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) {
                gm_t76123gfet73g t32 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t31) + 8));
                gm_tcfa6l7reoosn t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                ((uint8_t *)&t33)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t33 + 8), t32);
                F.ret = t33;
                goto gm_fret;
            }
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
            goto me3;
mn20:;
ma6:;
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t36 != ((uint8_t)2ULL)) goto mn35;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
            gm_l7 = t37;
            void * t38 = gm_l0;
            gm_tl874t2atnlh3 t39 = gm_l1;
            uint64_t t40 = gm_l7;
            gm_b16_8 t41 = gm_l4;
            gm_tcfa6l7reoosn t42;
            gm_f_store_at_ktabiji((void *)&t42, (void *)NULL, cx, NULL, t38, t39, t40, ((uint64_t)1ULL), t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t43 = t42;
            if (((((uint8_t *)&t43))[0] != 0)) {
                gm_t76123gfet73g t44 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t43) + 8));
                gm_tcfa6l7reoosn t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                ((uint8_t *)&t45)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t45 + 8), t44);
                F.ret = t45;
                goto gm_fret;
            }
            gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 8));
            goto me3;
mn35:;
ma7:;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t48 != ((uint8_t)3ULL)) goto mn47;
            gm_t0h5lk09e9p5r t49 = GM_LD(gm_t0h5lk09e9p5r, (((uint8_t *)a2) + 8));
            F.l8 = t49;
            {
                void * t51 = gm_l0;
                gm_tl874t2atnlh3 t52 = gm_l1;
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 24));
                gm_tdlro53ljvkl0 t56;
                gm_f_bits_whole_u4vfvvk((void *)&t56, (void *)NULL, cx, NULL, t51, t52, t53, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tdlro53ljvkl0 t57 = t56;
                if (((((uint8_t *)&t57))[0] != 0)) {
                    gm_t76123gfet73g t58 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t57) + 16));
                    gm_tcfa6l7reoosn t59;
                    GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                    ((uint8_t *)&t59)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t59 + 8), t58);
                    F.ret = t59;
                    goto gm_fret;
                }
                unsigned __int128 t60 = GM_LD(unsigned __int128, (((uint8_t *)&t57) + 16));
                gm_l9 = t60;
            }
            {
                void * t61 = gm_l0;
                gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 32));
                uint32_t t63 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t61, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t63;
            }
            {
                unsigned __int128 t64;
                uint32_t t66 = gm_l10;
                uint8_t t67 = (t66 >= ((uint32_t)128ULL));
                if (!t67) goto ie65;
                unsigned __int128 t69 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                unsigned __int128 t70 = t69;
                t64 = t70;
                goto ix65;
ie65:;
                uint32_t t72 = gm_l10;
                unsigned __int128 t73 = (unsigned __int128)t72;
                unsigned __int128 n74 = (unsigned __int128)t73;
                if (n74 >= 128) gm_trap_shift((uint64_t)n74, "u128", gm_srcrvqeupiu1766, 138, 55);
                unsigned __int128 r75 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n74);
                bool o76 = (unsigned __int128)(r75 >> (int)n74) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                if (o76) gm_trap("integer overflow in `<<`", gm_srcrvqeupiu1766, 138, 55);
                unsigned __int128 t77 = r75;
                unsigned __int128 r78; bool o79 = __builtin_sub_overflow(t77, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r78);
                if (o79) gm_trap("integer overflow in `-`", gm_srcrvqeupiu1766, 138, 54);
                unsigned __int128 t80 = r78;
                unsigned __int128 t81 = t80;
                t64 = t81;
ix65:;
                uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 28));
                unsigned __int128 t83 = (unsigned __int128)t82;
                unsigned __int128 n84 = (unsigned __int128)t83;
                if (n84 >= 128) gm_trap_shift((uint64_t)n84, "u128", gm_srcrvqeupiu1766, 138, 21);
                unsigned __int128 r85 = (unsigned __int128)((unsigned __int128)t64 << (int)n84);
                bool o86 = (unsigned __int128)(r85 >> (int)n84) != t64;
                if (o86) gm_trap("integer overflow in `<<`", gm_srcrvqeupiu1766, 138, 21);
                unsigned __int128 t87 = r85;
                gm_l11 = t87;
            }
            {
                void * t88 = gm_l0;
                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 32));
                gm_b16_8 t90 = gm_l4;
                unsigned __int128 t91 = gm_f_bits_raw_t6dqs65((void *)NULL, cx, NULL, t88, t89, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t91;
            }
            {
                unsigned __int128 t92 = gm_l9;
                unsigned __int128 t93 = gm_l11;
                unsigned __int128 t94 = (unsigned __int128)~t93;
                unsigned __int128 t95 = (unsigned __int128)(t92 & t94);
                unsigned __int128 t96 = gm_l12;
                uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 28));
                unsigned __int128 t98 = (unsigned __int128)t97;
                unsigned __int128 n99 = (unsigned __int128)t98;
                if (n99 >= 128) gm_trap_shift((uint64_t)n99, "u128", gm_srcrvqeupiu1766, 140, 38);
                unsigned __int128 r100 = (unsigned __int128)((unsigned __int128)t96 << (int)n99);
                bool o101 = (unsigned __int128)(r100 >> (int)n99) != t96;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcrvqeupiu1766, 140, 38);
                unsigned __int128 t102 = r100;
                unsigned __int128 t103 = gm_l11;
                unsigned __int128 t104 = (unsigned __int128)(t102 & t103);
                unsigned __int128 t105 = (unsigned __int128)(t95 | t104);
                gm_l13 = t105;
            }
            {
                void * t106 = gm_l0;
                gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 24));
                gm_tkn42u7na3t81 t108 = gm_f_bits_struct_ty_22ue20s(t106, GM_CONV(gm_csguankko4lmhd, t107));
                gm_tkn42u7na3t81 t109 = t108;
                gm_t88rcucg1b7uk t110;
                if (((((uint8_t *)&t109))[4] != 0)) {
                    gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t109) + 0));
                    t110 = t111;
                } else {
                    gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 24));
                    t110 = t112;
                }
                gm_l14 = t110;
            }
            {
                void * t113 = gm_l0;
                gm_t88rcucg1b7uk t114 = gm_l14;
                uint64_t t115 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t113, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t116 = gm_f_us_cuv01fp(t115);
                gm_l15 = t116;
            }
            {
                uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                uint8_t t119 = (t118 > ((uint64_t)0ULL));
                if (!t119) goto ie117;
                {
                    void * t121 = gm_l0;
                    uint64_t t122 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                    void * t123 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)));
                    uint64_t t124 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t125 = gm_l15;
                    uint64_t t126 = (uintptr_t)t125;
                    if (t124 > t126 || t126 > (uintptr_t)t122) gm_trap_slice(t124, t126, t122, gm_srcrvqeupiu1766, 144, 28);
                    gm_b16_8 t127;
                    GM_ST(void *, (uint8_t *)&t127, (void *)(((uint8_t *)t123 + t124 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(t126 - t124));
                    unsigned __int128 t128 = gm_l13;
                    gm_f_put_uint_cddin99(t121, t127, t128);
                }
                goto ix117;
ie117:;
                {
                }
                {
                    void * t130 = gm_l0;
                    void * t131 = ((uint8_t *)&gm_l16);
                    uint64_t t132 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t133 = gm_l15;
                    uint64_t t134 = (uintptr_t)t133;
                    if (t132 > t134 || t134 > (uintptr_t)16) gm_trap_slice(t132, t134, 16, gm_srcrvqeupiu1766, 147, 28);
                    gm_b16_8 t135;
                    GM_ST(void *, (uint8_t *)&t135, (void *)(((uint8_t *)t131 + t132 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(t134 - t132));
                    unsigned __int128 t136 = gm_l13;
                    gm_f_put_uint_cddin99(t130, t135, t136);
                }
                {
                    void * t137 = gm_l0;
                    gm_tl874t2atnlh3 t138 = gm_l1;
                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
                    void * t140 = gm_l0;
                    gm_t88rcucg1b7uk t141 = gm_l14;
                    uint64_t t142 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t140, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t143 = ((uint8_t *)&gm_l16);
                    uint64_t t144 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t145 = gm_l15;
                    uint64_t t146 = (uintptr_t)t145;
                    if (t144 > t146 || t146 > (uintptr_t)16) gm_trap_slice(t144, t146, 16, gm_srcrvqeupiu1766, 148, 58);
                    gm_b16_8 t147;
                    GM_ST(void *, (uint8_t *)&t147, (void *)(((uint8_t *)t143 + t144 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(t146 - t144));
                    gm_b16_8 t148 = t147;
                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                    gm_tcfa6l7reoosn t150;
                    gm_f_store_at_ktabiji((void *)&t150, (void *)NULL, cx, NULL, t137, t138, t139, t142, t149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t151 = t150;
                    if (((((uint8_t *)&t151))[0] != 0)) {
                        gm_t76123gfet73g t152 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t151) + 8));
                        gm_tcfa6l7reoosn t153;
                        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
                        ((uint8_t *)&t153)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t153 + 8), t152);
                        F.ret = t153;
                        goto gm_fret;
                    }
                    gm_b0_1 t154 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 8));
                }
ix117:;
            }
            goto me3;
mn47:;
ma8:;
            gm_trap("no match arm matched the scrutinee", gm_srcrvqeupiu1766, 131, 5);
me3:;
        }
    }
    gm_b0_1 t155;
    gm_tcfa6l7reoosn t156;
    GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t156 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t156 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t156 + 24, 0);
    ((uint8_t *)&t156)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t156 + 8), t155);
    gm_tcfa6l7reoosn t157 = t156;
    F.ret = t157;
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
