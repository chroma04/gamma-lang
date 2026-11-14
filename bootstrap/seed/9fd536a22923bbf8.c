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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; gm_b16_8 f5; } gm_t08tkmlkm1sbu;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_ti71ll5knnvij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_tbbqefuhv2r8h f1; } gm_tpp7miti592nm;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t08tkmlkm1sbu) == 88 && _Alignof(gm_t08tkmlkm1sbu) == 8 && offsetof(gm_t08tkmlkm1sbu, f0) == 0 && offsetof(gm_t08tkmlkm1sbu, f1) == 16 && offsetof(gm_t08tkmlkm1sbu, f2) == 32 && offsetof(gm_t08tkmlkm1sbu, f3) == 48 && offsetof(gm_t08tkmlkm1sbu, f4) == 56 && offsetof(gm_t08tkmlkm1sbu, f5) == 72, "layout of `(string, string, string, u64, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_ti71ll5knnvij) == 72 && _Alignof(gm_ti71ll5knnvij) == 8 && offsetof(gm_ti71ll5knnvij, f0) == 0 && offsetof(gm_ti71ll5knnvij, f1) == 16 && offsetof(gm_ti71ll5knnvij, f2) == 32 && offsetof(gm_ti71ll5knnvij, f3) == 40 && offsetof(gm_ti71ll5knnvij, f4) == 56, "layout of `(string, []u8, usize, []u8, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpp7miti592nm) == 32 && _Alignof(gm_tpp7miti592nm) == 8 && offsetof(gm_tpp7miti592nm, f0) == 0 && offsetof(gm_tpp7miti592nm, f1) == 8, "layout of `(i: usize, x: Arm)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_adt_def_ni54he1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_calls_in_scan_qq1r0rb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conds_79r4f4d {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_first_llehmvr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_in_expr_6j2ajh7 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_710oev4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_match_expr_4m6qrp7 {
    gm_frame h;
    gm_tqetrgo0q24b5 l11; /* starts */
    gm_trqfvmjtt4sij l16; /* s */
    gm_trqfvmjtt4sij l23; /* s */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_match_switch_koqe5v7 {
    gm_frame h;
    gm_b16_8 l3; /* arms */
    gm_trqfvmjtt4sij l6; /* labels */
    gm_trqfvmjtt4sij l12; /* out */
    gm_tckdobunaulee l16; /* v */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pat_admits_86ni7ec {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pat_jfuov2m {
    gm_frame h;
    gm_tbci5ospk5nnn l13; /* lo */
    gm_tbci5ospk5nnn l14; /* hi */
    gm_tbci5ospk5nnn l35; /* vtag */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_scrutinee_addr_3fvmbme {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_calls_in_scan_qq1r0rb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_conds_79r4f4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_first_llehmvr(void *env, gm_node *cx, gm_node *inst, gm_t9805qtonbsag a0);
__attribute__((aligned(4))) void gm_f_format_076g3l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0r468he(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7nv0e8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_d3cmh7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_da6unq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gjujk09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mdlbfc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_oi7136m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_p3pvqcn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_snpkhcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_v9dnkq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void gm_f_in_expr_6j2ajh7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_0bsl1db(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3gi7f6b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7eujspv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8694lum(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ck79eul(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_h1r66ik(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_h995d88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ktpatuk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_njoe3l6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_nt2kiv2(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_o548q4u(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_u8da5pt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_lv_m2jtf0n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_match_expr_4m6qrp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_match_switch_koqe5v7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_pat_admits_86ni7ec(void * a0, gm_t9805qtonbsag a1);
__attribute__((aligned(4))) void gm_f_pat_jfuov2m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_reads_place_nji55rm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_scrutinee_addr_3fvmbme(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_str_equal_at_mshane1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b9o3mne(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jfgif5f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_upbce2g(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s73qv4ju7m615[18];
extern const uint8_t gm_sb8vbka031tie[21];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src8qgs75cskrcf[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_def_ni54he1 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    void * t5 = gm_f_adt_of_gej99vn(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 480, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 360));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 481, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 360), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)5ULL)));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_label_710oev4 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 184);
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_f1cbljc(t3, t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcmok80ho8hh01, 535, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 184);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 464, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 184);
        gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 465, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uint64_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
    F.ret = t3;
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3877pk7lv748 t1;
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    GM_ST(uint64_t, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_0gjpujh((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src0gtuh94b1fce, 291, 72);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_7lru3c2((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_src0gtuh94b1fce, 289, 60);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t6);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t7);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_in_scan_qq1r0rb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_calls_in_scan_qq1r0rb F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* et */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* r */
    uint64_t gm_l6; /* esz */
    gm_b16_8 gm_l7; /* xa */
    gm_b16_8 gm_l8; /* eq */
    GM_STACK_CHECK(&F, 448u);
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
        gm_t88rcucg1b7uk t5 = gm_l1;
        uint64_t t6 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        gm_b16_8 t9 = gm_l4;
        gm_b16_8 t10 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t10;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = (void *)(((uint8_t *)t12) + 1968);
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_b16_8 t18 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t13, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_t08tkmlkm1sbu t20;
        gm_b16_8 t21 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
        gm_b16_8 t22 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t22);
        gm_b16_8 t23 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 32, t23);
        uint64_t t24 = gm_l6;
        GM_ST(uint64_t, (uint8_t *)&t20 + 48, t24);
        gm_b16_8 t25 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 56, t25);
        gm_b16_8 t26 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 72, t26);
        gm_b0_1 t27 = gm_f_linef_0bsl1db((void *)NULL, cx, NULL, t19, &t20);
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
inline __attribute__((aligned(4))) void gm_f_conds_79r4f4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_conds_79r4f4d F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* cond */
    gm_b16_8 gm_l2; /* fail */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* b */
    gm_b16_8 gm_l7; /* v */
    void * gm_l8; /* p */
    void * gm_l9; /* value */
    gm_b16_8 gm_l10; /* a */
    GM_STACK_CHECK(&F, 691u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            {
                gm_b16_8 t4 = gm_l1;
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
                        gm_tlr9dp305htm7 t16 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 24));
                        gm_l5 = t16;
                        {
                            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t20 != ((uint8_t)0ULL)) goto mn19;
                            void * t21 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                            gm_l6 = t21;
                            {
                                void * t23 = gm_l0;
                                void * t24 = gm_l6;
                                gm_t5o0tpv5jka2l t25;
                                gm_f_expr_porbc9t((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                if (((((uint8_t *)&t25))[0] != 0)) {
                                    gm_tqgtd5gtgj65s t26;
                                    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                                    ((uint8_t *)&t26)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t26 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t25) + 8)));
                                    F.ret = t26;
                                    goto gm_fret;
                                }
                                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
                                gm_l7 = t27;
                            }
                            {
                                void * t28 = gm_l0;
                                gm_t6j6smj7qoctu t29;
                                gm_b16_8 t30 = gm_l7;
                                GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t30);
                                gm_b16_8 t31 = gm_l2;
                                GM_ST(gm_b16_8, (uint8_t *)&t29 + 16, t31);
                                gm_b0_1 t32 = gm_f_linef_u8da5pt((void *)NULL, cx, NULL, t28, &t29);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                            }
                            goto me18;
mn19:;
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t34 != ((uint8_t)1ULL)) goto mn33;
                            void * t35 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                            gm_l8 = t35;
                            void * t36 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                            gm_l9 = t36;
                            {
                                void * t38 = gm_l0;
                                void * t39 = gm_l9;
                                gm_t5o0tpv5jka2l t40;
                                gm_f_scrutinee_addr_3fvmbme((void *)&t40, (void *)NULL, cx, NULL, t38, t39, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                if (((((uint8_t *)&t40))[0] != 0)) {
                                    gm_tqgtd5gtgj65s t41;
                                    GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                                    ((uint8_t *)&t41)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t41 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t40) + 8)));
                                    F.ret = t41;
                                    goto gm_fret;
                                }
                                gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
                                gm_l10 = t42;
                            }
                            {
                                void * t43 = gm_l0;
                                void * t44 = gm_l8;
                                gm_b16_8 t45 = gm_l10;
                                gm_b16_8 t46 = gm_l2;
                                gm_tqgtd5gtgj65s t47;
                                gm_f_pat_jfuov2m((void *)&t47, (void *)NULL, cx, NULL, t43, t44, t45, t46);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                if (((((uint8_t *)&t47))[0] != 0)) {
                                    gm_tqgtd5gtgj65s t48;
                                    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                                    ((uint8_t *)&t48)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t48 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t47) + 8)));
                                    F.ret = t48;
                                    goto gm_fret;
                                }
                                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 8));
                            }
                            goto me18;
mn33:;
                            gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 6, 9);
me18:;
                        }
                        goto me14;
mn15:;
                        gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 5, 5);
me14:;
                    }
lc5:;
                    {
                        uint64_t t51 = gm_l4;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        uint64_t t54 = r52;
                        gm_l4 = t54;
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
    }
    gm_b0_1 t55;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t55);
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
inline __attribute__((aligned(4))) uint64_t gm_f_first_llehmvr(void *env, gm_node *cx, gm_node *inst, gm_t9805qtonbsag a0) {
    struct gm_fr_first_llehmvr F;
    gm_t9805qtonbsag gm_l0; /* k */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    gm_tbbqefuhv2r8h gm_l4; /* arm */
    GM_STACK_CHECK(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_match_switch_koqe5v7 *)((&F.h)->up))->l3));
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
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
                    gm_tpp7miti592nm t11;
                    uint64_t t12 = gm_l2;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 24), 24);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                    gm_l3 = t18;
                    gm_l4 = (*(gm_tbbqefuhv2r8h *)(((uint8_t *)&t11) + 8));
                    {
                        void * t21 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
                        gm_t9805qtonbsag t22 = gm_l0;
                        uint8_t t23 = gm_f_pat_admits_86ni7ec(t21, t22);
                        if (!t23) goto ie20;
                        {
                            uint64_t t25 = gm_l3;
                            uint8_t t26 = 0;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 62, 9);
me16:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l2;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l2 = t31;
                }
            }
lb5:;
        }
    }
    {
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_match_switch_koqe5v7 *)((&F.h)->up))->l3) + 8);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_in_expr_6j2ajh7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3) {
    struct gm_fr_in_expr_6j2ajh7 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* elem */
    void * gm_l2; /* container */
    uint8_t gm_l3; /* negated */
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* c */
    gm_b16_8 gm_l6; /* ca */
    gm_b16_8 gm_l7; /* r */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* et */
    gm_t88rcucg1b7uk gm_l10; /* et */
    gm_b16_8 gm_l11; /* p */
    gm_b16_8 gm_l12; /* l */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* offs */
    gm_t88rcucg1b7uk gm_l15; /* et */
    gm_b16_8 gm_l16; /* xa */
    gm_b16_8 gm_l17; /* eq */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    void * gm_l21; /* def */
    uint8_t gm_l22; /* incl */
    gm_b16_8 gm_l23; /* offs */
    gm_t88rcucg1b7uk gm_l24; /* lt */
    gm_b16_8 gm_l25; /* lo */
    gm_b16_8 gm_l26; /* hi */
    gm_b16_8 gm_l27; /* op */
    gm_b16_8 gm_l28; /* ge */
    gm_b16_8 gm_l29; /* below */
    GM_STACK_CHECK(&F, 2776u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_t5o0tpv5jka2l t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t5 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t4) + 8)));
            F.ret = t5;
            goto gm_fret;
        }
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        gm_t5o0tpv5jka2l t9;
        gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_t5o0tpv5jka2l t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t9) + 8)));
            F.ret = t10;
            goto gm_fret;
        }
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l2;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_b16_8 t15 = gm_l5;
        gm_b16_8 t16 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t16;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        gm_t88rcucg1b7uk t21 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
        gm_b16_8 t23 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t17, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t23;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        void * t27 = gm_l2;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
        gm_t53at80pfdus3 t29;
        gm_f_kind_ghvav22((void *)&t29, t26, GM_CONV(gm_csguankko4lmhd, t28));
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        switch (t37) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; case ((uint8_t)7ULL): goto ma35; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma35; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma31; case ((uint8_t)12ULL): goto ma33; case ((uint8_t)13ULL): goto ma35; case ((uint8_t)14ULL): goto ma35; case ((uint8_t)15ULL): goto ma35; case ((uint8_t)16ULL): goto ma35; case ((uint8_t)17ULL): goto ma34; case ((uint8_t)18ULL): goto ma35; case ((uint8_t)19ULL): goto ma35; case ((uint8_t)20ULL): goto ma35; case ((uint8_t)21ULL): goto ma35; case ((uint8_t)22ULL): goto ma35; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma35; case ((uint8_t)25ULL): goto ma35; case ((uint8_t)26ULL): goto ma35; default: goto ma35; }
ma31:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t39 != ((uint8_t)11ULL)) goto mn38;
        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)&t29) + 8) + 0));
        gm_l8 = t40;
        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t29) + 8) + 8));
        gm_l9 = t41;
        void * t42 = gm_l0;
        gm_t88rcucg1b7uk t43 = gm_l9;
        uint64_t t44 = gm_l8;
        gm_t4ce0jd3tdro4 t45;
        GM_ST(uint64_t, ((uint8_t *)&t45 + 0), t44);
        gm_tabb710d9mq2e t46;
        gm_f_format_1amnh72((void *)&t46, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_src8qgs75cskrcf, 188, 52);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_b16_8 t49 = gm_l6;
        gm_b16_8 t50 = gm_l4;
        gm_b16_8 t51 = gm_l7;
        gm_b0_1 t52 = gm_f_calls_in_scan_qq1r0rb((void *)NULL, cx, NULL, t42, t43, t48, t49, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me30;
mn38:;
ma32:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t54 != ((uint8_t)10ULL)) goto mn53;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t29) + 8) + 4));
        gm_l10 = t55;
        {
            gm_b16_8 t57 = gm_l6;
            gm_b16_8 t58 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t58;
        }
        {
            void * t59 = gm_l0;
            gm_b16_8 t60 = gm_l6;
            gm_b16_8 t61 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t61;
        }
        {
            void * t62 = gm_l0;
            gm_t88rcucg1b7uk t63 = gm_l10;
            gm_b16_8 t64 = gm_l12;
            gm_b16_8 t65 = gm_l11;
            gm_tnpifv20irnph t66;
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
            gm_tabb710d9mq2e t67;
            gm_f_format_da6unq6((void *)&t67, (void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_src8qgs75cskrcf, 192, 37);
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_b16_8 t70 = gm_l4;
            gm_b16_8 t71 = gm_l7;
            gm_b0_1 t72 = gm_f_calls_in_scan_qq1r0rb((void *)NULL, cx, NULL, t62, t63, t64, t69, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me30;
mn53:;
ma33:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t74 != ((uint8_t)12ULL)) goto mn73;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((((uint8_t *)&t29) + 8) + 0));
        gm_l13 = t75;
        {
            void * t77 = gm_l0;
            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
            void * t79 = (void *)(((uint8_t *)t78) + 8);
            void * t80 = gm_l2;
            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 0));
            gm_b16_8 t82 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t79, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t82;
        }
        {
            void * t83 = gm_l1;
            gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t83) + 0));
            gm_l15 = t84;
        }
        {
            void * t85 = gm_l0;
            gm_t88rcucg1b7uk t86 = gm_l15;
            gm_b16_8 t87 = gm_l4;
            gm_b16_8 t88 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t85, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t88;
        }
        {
            void * t89 = gm_l0;
            void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
            void * t91 = (void *)(((uint8_t *)t90) + 1968);
            void * t92 = gm_l0;
            void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
            void * t94 = (void *)(((uint8_t *)t93) + 8);
            gm_t88rcucg1b7uk t95 = gm_l15;
            gm_b16_8 t96 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t91, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t96;
        }
        {
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                gm_l18 = t98;
            }
            {
                for (;;) {
                    uint64_t t100 = gm_l19;
                    uint64_t t101 = gm_l18;
                    uint8_t t102 = (t100 < t101);
                    if (!t102) goto lb99;
                    {
                        uint64_t t105 = gm_l19;
                        uint64_t t108 = GM_LD(uint64_t, ((uint8_t *)&t105));
                        gm_l20 = t108;
                        {
                            void * t110 = gm_l0;
                            gm_ti71ll5knnvij t111;
                            gm_b16_8 t112 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
                            gm_b16_8 t113 = gm_l6;
                            GM_ST(gm_b16_8, (uint8_t *)&t111 + 16, t113);
                            uint64_t t114 = gm_l20;
                            uint64_t t115 = (uintptr_t)t114;
                            uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            if (t115 >= (uintptr_t)(t116)) gm_trap_index(t115, t116, gm_src8qgs75cskrcf, 200, 66);
                            uint64_t t117 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t115 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t111 + 32, t117);
                            gm_b16_8 t118 = gm_l16;
                            GM_ST(gm_b16_8, (uint8_t *)&t111 + 40, t118);
                            gm_b16_8 t119 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t111 + 56, t119);
                            gm_b0_1 t120 = gm_f_linef_o548q4u((void *)NULL, cx, NULL, t110, &t111);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                        }
                        goto me106;
mn107:;
                        gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 199, 13);
me106:;
                    }
lc99:;
                    {
                        uint64_t t122 = gm_l19;
                        uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                        uint64_t t125 = r123;
                        gm_l19 = t125;
                    }
                }
gm_ll103:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
                }
                goto gm_lad;
lb99:;
            }
        }
        goto me30;
mn73:;
ma34:;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t127 != ((uint8_t)17ULL)) goto mn126;
        {
            void * t129 = gm_l0;
            void * t130 = gm_l2;
            gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t130) + 0));
            void * t132 = gm_f_adt_def_ni54he1(t129, GM_CONV(gm_csguankko4lmhd, t131));
            gm_l21 = t132;
        }
        {
            gm_l22 = ((uint8_t)0);
        }
        {
            void * t134 = gm_l21;
            uint8_t t135 = (((GM_LD(uintptr_t, (((uint8_t *)t134) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)t134) + 0)), gm_seue7mq07v0oc, 5) == 0));
            if (!t135) goto ie133;
            {
                gm_l22 = ((uint8_t)0);
            }
            goto ix133;
ie133:;
            void * t138 = gm_l21;
            uint8_t t139 = (((GM_LD(uintptr_t, (((uint8_t *)t138) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)t138) + 0)), gm_s43cq4krigs2b, 9) == 0));
            if (!t139) goto ie137;
            {
                gm_l22 = ((uint8_t)1);
            }
            goto ix137;
ie137:;
            {
                void * t142 = gm_l2;
                gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
                gm_b16_8 t144;
                GM_ST(void *, (uint8_t *)&t144, (void *)(gm_s73qv4ju7m615));
                GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(17));
                gm_t08olq97rscn3 t145;
                gm_f_unsupported_531q51r((void *)&t145, (void *)NULL, cx, NULL, t143, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t145);
                goto gm_fdone;
            }
ix137:;
ix133:;
        }
        {
            void * t146 = gm_l0;
            void * t147 = GM_LD(void *, (((uint8_t *)t146) + 0));
            void * t148 = (void *)(((uint8_t *)t147) + 8);
            void * t149 = gm_l2;
            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t149) + 0));
            gm_b16_8 t151 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t148, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t151;
        }
        {
            void * t152 = gm_l1;
            gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t152) + 0));
            gm_l24 = t153;
        }
        {
            void * t154 = gm_l0;
            gm_t88rcucg1b7uk t155 = gm_l24;
            gm_t3877pk7lv748 t156;
            gm_b16_8 t157 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t156 + 0, t157);
            uint64_t t158 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_src8qgs75cskrcf, 215, 59);
            uint64_t t160 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t158 * 8));
            GM_ST(uint64_t, (uint8_t *)&t156 + 16, t160);
            gm_tabb710d9mq2e t161;
            gm_f_format_gjujk09((void *)&t161, (void *)NULL, cx, NULL, &t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 8, gm_src8qgs75cskrcf, 215, 31);
            gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 8));
            gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
            gm_b16_8 t164 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t154, t155, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t164;
        }
        {
            void * t165 = gm_l0;
            gm_t88rcucg1b7uk t166 = gm_l24;
            gm_t3877pk7lv748 t167;
            gm_b16_8 t168 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t168);
            uint64_t t169 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t169 >= (uintptr_t)(t170)) gm_trap_index(t169, t170, gm_src8qgs75cskrcf, 216, 59);
            uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t169 * 8));
            GM_ST(uint64_t, (uint8_t *)&t167 + 16, t171);
            gm_tabb710d9mq2e t172;
            gm_f_format_gjujk09((void *)&t172, (void *)NULL, cx, NULL, &t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 8, gm_src8qgs75cskrcf, 216, 31);
            gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t172) + 8));
            gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
            gm_b16_8 t175 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t165, t166, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t175;
        }
        {
            gm_b16_8 t176;
            uint8_t t178 = gm_l22;
            if (!t178) goto ie177;
            gm_b16_8 t180;
            GM_ST(void *, (uint8_t *)&t180, (void *)(gm_s4g3cajt15dmi));
            GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(2));
            gm_b16_8 t181 = t180;
            t176 = t181;
            goto ix177;
ie177:;
            gm_b16_8 t183;
            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(1));
            gm_b16_8 t184 = t183;
            t176 = t184;
ix177:;
            gm_l27 = t176;
        }
        {
            gm_t6j6smj7qoctu t185;
            gm_b16_8 t186 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t185 + 0, t186);
            gm_b16_8 t187 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t185 + 16, t187);
            gm_tabb710d9mq2e t188;
            gm_f_format_d3cmh7d((void *)&t188, (void *)NULL, cx, NULL, &t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 8, gm_src8qgs75cskrcf, 218, 19);
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t188) + 8));
            gm_l28 = t189;
        }
        {
            gm_tfeab279c16rr t190;
            gm_b16_8 t191 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t191);
            gm_b16_8 t192 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t190 + 16, t192);
            gm_b16_8 t193 = gm_l26;
            GM_ST(gm_b16_8, (uint8_t *)&t190 + 32, t193);
            gm_tabb710d9mq2e t194;
            gm_f_format_mdlbfc1((void *)&t194, (void *)NULL, cx, NULL, &t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 8, gm_src8qgs75cskrcf, 219, 22);
            gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&t194) + 8));
            gm_l29 = t195;
        }
        {
            void * t196 = gm_l0;
            gm_tfeab279c16rr t197;
            gm_b16_8 t198 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t198);
            gm_b16_8 t199 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t197 + 16, t199);
            gm_b16_8 t200 = gm_l29;
            GM_ST(gm_b16_8, (uint8_t *)&t197 + 32, t200);
            gm_b0_1 t201 = gm_f_linef_nt2kiv2((void *)NULL, cx, NULL, t196, &t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me30;
mn126:;
ma35:;
        void * t203 = gm_l2;
        gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t203) + 72));
        gm_b16_8 t205;
        GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s73qv4ju7m615));
        GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(17));
        gm_t08olq97rscn3 t206;
        gm_f_unsupported_531q51r((void *)&t206, (void *)NULL, cx, NULL, t204, t205);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t206);
        goto gm_fdone;
        goto me30;
mn202:;
ma36:;
        gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 187, 5);
me30:;
    }
    {
        void * t207 = gm_l0;
        void * t208 = gm_l0;
        void * t209 = GM_LD(void *, (((uint8_t *)t208) + 0));
        void * t210 = (void *)(((uint8_t *)t209) + 8);
        gm_t88rcucg1b7uk t211 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t210);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t212;
        gm_b16_8 t213;
        uint8_t t215 = gm_l3;
        if (!t215) goto ie214;
        gm_b16_8 t217;
        GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sf9a2kg3eo1gs));
        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(1));
        gm_b16_8 t218 = t217;
        t213 = t218;
        goto ix214;
ie214:;
        gm_b16_8 t220;
        GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(0));
        gm_b16_8 t221 = t220;
        t213 = t221;
ix214:;
        GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
        gm_b16_8 t222 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t222);
        gm_tabb710d9mq2e t223;
        gm_f_format_snpkhcp((void *)&t223, (void *)NULL, cx, NULL, &t212);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 8, gm_src8qgs75cskrcf, 224, 38);
        gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&t223) + 8));
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
        gm_b16_8 t226 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t207, t211, t225);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t226);
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
__attribute__((aligned(4))) void gm_f_match_expr_4m6qrp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_match_expr_4m6qrp7 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l3; /* arms */
    uint8_t gm_l4; /* guarded */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l7; /* arm */
    gm_b16_8 gm_l8; /* a */
    gm_th2lqbo77pvsc gm_l9; /* res */
    gm_b16_8 gm_l10; /* end */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_tbbqefuhv2r8h gm_l15; /* arm */
    gm_b16_8 gm_l17; /* next */
    void * gm_l18; /* g */
    gm_b16_8 gm_l19; /* gv */
    gm_b16_8 gm_l20; /* bv */
    gm_b16_8 gm_l21; /* r */
    gm_b16_8 gm_l22; /* loc */
    gm_b16_8 gm_l24; /* r */
    GM_STACK_CHECK(&F, 1980u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tbbqefuhv2r8h t15 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 24));
                    gm_l7 = t15;
                    {
                        void * t18 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        uint8_t t19;
                        if ((GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto mn21;
                        t19 = ((uint8_t)0);
                        goto me20;
mn21:;
                        t19 = ((uint8_t)1);
                        goto me20;
mn22:;
                        gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 22, 12);
me20:;
                        if (!t19) goto ie17;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 21, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l6;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l6 = t28;
                }
            }
lb4:;
        }
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l2;
        uint8_t t31 = gm_l4;
        uint8_t t32 = (uint8_t)!t31;
        gm_t5o0tpv5jka2l t33;
        gm_f_scrutinee_addr_3fvmbme((void *)&t33, (void *)NULL, cx, NULL, t29, t30, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t33))[0] != 0)) {
            gm_t5o0tpv5jka2l t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            ((uint8_t *)&t34)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t34 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t33) + 8)));
            F.ret = t34;
            goto gm_fret;
        }
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_l8 = t35;
    }
    {
        gm_th2lqbo77pvsc t36;
        memset(&t36, 0, sizeof t36);
        gm_l9 = t36;
    }
    {
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        void * t41 = gm_l1;
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 0));
        uint8_t t43 = gm_f_is_never_in5ftma(t40, GM_CONV(gm_csguankko4lmhd, t42));
        uint8_t t44 = (uint8_t)!t43;
        if (!t44) goto ie37;
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
        void * t47 = (void *)(((uint8_t *)t46) + 8);
        void * t48 = gm_l1;
        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t48) + 0));
        uint8_t t50 = gm_f_is_unit_17uennd(t47, GM_CONV(gm_csguankko4lmhd, t49));
        uint8_t t51 = (uint8_t)!t50;
        if (!t51) goto ie37;
        {
            void * t53 = gm_l0;
            void * t54 = gm_l1;
            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
            gm_b16_8 t56 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t53, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t56);
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        void * t57 = gm_l0;
        uint32_t t58 = gm_f_fresh_5kokjmg(t57);
        gm_t88rcucg1b7uk t59;
        GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t58);
        gm_tabb710d9mq2e t60;
        gm_f_format_7nv0e8m((void *)&t60, (void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_src8qgs75cskrcf, 27, 12);
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
        gm_l10 = t61;
    }
    {
        void * t62 = gm_l0;
        void * t63 = gm_l2;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + 0));
        gm_b16_8 t65 = gm_l8;
        gm_b16_8 t66 = gm_l3;
        gm_f_match_switch_koqe5v7((void *)&F.l11, (void *)NULL, cx, NULL, t62, t64, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t68 = gm_l3;
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
                    gm_tpp7miti592nm t76;
                    uint64_t t77 = gm_l13;
                    GM_ST(uint64_t, (uint8_t *)&t76 + 0, t77);
                    uint64_t t78 = gm_l13;
                    uint64_t t79 = (uintptr_t)t78;
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    memcpy((uint8_t *)&t76 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t79 * 24), 24);
                    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&t76) + 0));
                    gm_l14 = t83;
                    gm_l15 = (*(gm_tbbqefuhv2r8h *)(((uint8_t *)&t76) + 8));
                    {
                        if (!((((uint8_t *)&F.l11))[40] != 0)) goto ie85;
                        gm_trqfvmjtt4sij t86 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
                        F.l16 = t86;
                        {
                            void * t88 = gm_l0;
                            uint64_t t89 = gm_l14;
                            uint64_t t90 = (uintptr_t)t89;
                            uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                            if (t90 >= (uintptr_t)(t91)) gm_trap_index(t90, t91, gm_src8qgs75cskrcf, 30, 35);
                            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t90 * 16));
                            gm_b0_1 t93 = gm_f_label_710oev4((void *)NULL, cx, NULL, t88, t92);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                        }
                        goto ix85;
ie85:;
ix85:;
                    }
                    {
                        void * t94 = gm_l0;
                        uint32_t t95 = gm_f_fresh_5kokjmg(t94);
                        gm_t88rcucg1b7uk t96;
                        GM_ST(uint32_t, ((uint8_t *)&t96 + 0), t95);
                        gm_tabb710d9mq2e t97;
                        gm_f_format_v9dnkq5((void *)&t97, (void *)NULL, cx, NULL, t96);
                        if (gm_unw.kind) {
                            goto gm_ll74;
                        }
                        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_src8qgs75cskrcf, 31, 17);
                        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
                        gm_l17 = t98;
                    }
                    {
                        void * t99 = gm_l0;
                        void * t100 = GM_LD(void *, (((uint8_t *)&gm_l15) + 0));
                        gm_b16_8 t101 = gm_l8;
                        gm_b16_8 t102 = gm_l17;
                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                        gm_tqgtd5gtgj65s t104;
                        gm_f_pat_jfuov2m((void *)&t104, (void *)NULL, cx, NULL, t99, t100, t101, t103);
                        if (gm_unw.kind) {
                            goto gm_ll74;
                        }
                        if (((((uint8_t *)&t104))[0] != 0)) {
                            gm_t5o0tpv5jka2l t105;
                            GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                            ((uint8_t *)&t105)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t105 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t104) + 8)));
                            F.ret = t105;
                            goto gm_fret;
                        }
                        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 8));
                    }
                    {
                        void * t108 = GM_LD(void *, (((uint8_t *)&gm_l15) + 8));
                        if (!(GM_LD(void *, ((uint8_t *)&t108)) != 0)) goto ie107;
                        void * t109 = GM_LD(void *, ((uint8_t *)&t108));
                        gm_l18 = t109;
                        {
                            void * t111 = gm_l0;
                            void * t112 = gm_l18;
                            gm_t5o0tpv5jka2l t113;
                            gm_f_expr_porbc9t((void *)&t113, (void *)NULL, cx, NULL, t111, t112);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                            if (((((uint8_t *)&t113))[0] != 0)) {
                                gm_t5o0tpv5jka2l t114;
                                GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                                ((uint8_t *)&t114)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t114 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t113) + 8)));
                                F.ret = t114;
                                goto gm_fret;
                            }
                            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
                            gm_l19 = t115;
                        }
                        {
                            void * t116 = gm_l0;
                            gm_t6j6smj7qoctu t117;
                            gm_b16_8 t118 = gm_l19;
                            GM_ST(gm_b16_8, (uint8_t *)&t117 + 0, t118);
                            gm_b16_8 t119 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t117 + 16, t119);
                            gm_b0_1 t120 = gm_f_linef_3gi7f6b((void *)NULL, cx, NULL, t116, &t117);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                        }
                        goto ix107;
ie107:;
ix107:;
                    }
                    {
                        void * t121 = gm_l0;
                        void * t122 = GM_LD(void *, (((uint8_t *)&gm_l15) + 16));
                        gm_t5o0tpv5jka2l t123;
                        gm_f_expr_porbc9t((void *)&t123, (void *)NULL, cx, NULL, t121, t122);
                        if (gm_unw.kind) {
                            goto gm_ll74;
                        }
                        if (((((uint8_t *)&t123))[0] != 0)) {
                            gm_t5o0tpv5jka2l t124;
                            GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                            ((uint8_t *)&t124)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t124 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t123) + 8)));
                            F.ret = t124;
                            goto gm_fret;
                        }
                        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                        gm_l20 = t125;
                    }
                    {
                        if (!((((uint8_t *)&gm_l9))[16] != 0)) goto ie126;
                        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                        gm_l21 = t127;
                        {
                            void * t130 = gm_l0;
                            void * t131 = GM_LD(void *, (((uint8_t *)t130) + 0));
                            void * t132 = (void *)(((uint8_t *)t131) + 8);
                            void * t133 = GM_LD(void *, (((uint8_t *)&gm_l15) + 16));
                            gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t133) + 0));
                            uint8_t t135 = gm_f_is_never_in5ftma(t132, GM_CONV(gm_csguankko4lmhd, t134));
                            uint8_t t136 = (uint8_t)!t135;
                            if (!t136) goto ie129;
                            {
                                void * t138 = gm_l0;
                                gm_t6j6smj7qoctu t139;
                                gm_b16_8 t140 = gm_l21;
                                GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
                                gm_b16_8 t141 = gm_l20;
                                GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t141);
                                gm_b0_1 t142 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t138, &t139);
                                if (gm_unw.kind) {
                                    goto gm_ll74;
                                }
                            }
                            goto ix129;
ie129:;
ix129:;
                        }
                        goto ix126;
ie126:;
ix126:;
                    }
                    {
                        void * t143 = gm_l0;
                        gm_b16_8 t144 = gm_l10;
                        gm_tnpifv20irnph t145;
                        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t144);
                        gm_b0_1 t146 = gm_f_linef_h995d88((void *)NULL, cx, NULL, t143, t145);
                        if (gm_unw.kind) {
                            goto gm_ll74;
                        }
                    }
                    {
                        void * t147 = gm_l0;
                        gm_b16_8 t148 = gm_l17;
                        gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                        gm_b0_1 t150 = gm_f_label_710oev4((void *)NULL, cx, NULL, t147, t149);
                        if (gm_unw.kind) {
                            goto gm_ll74;
                        }
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 29, 5);
me81:;
                }
lc70:;
                {
                    uint64_t t152 = gm_l13;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    uint64_t t155 = r153;
                    gm_l13 = t155;
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
        void * t156 = gm_l0;
        void * t157 = gm_l1;
        gm_b16_8 t158 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t158;
    }
    {
        if (!((((uint8_t *)&F.l11))[40] != 0)) goto ie159;
        gm_trqfvmjtt4sij t160 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
        F.l23 = t160;
        {
            void * t162 = gm_l0;
            uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t164 = (uintptr_t)t163;
            uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&F.l23) + 0) + 8);
            if (t164 >= (uintptr_t)(t165)) gm_trap_index(t164, t165, gm_src8qgs75cskrcf, 45, 31);
            gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l23) + 0)) + t164 * 16));
            gm_b0_1 t167 = gm_f_label_710oev4((void *)NULL, cx, NULL, t162, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix159;
ie159:;
ix159:;
    }
    {
        void * t168 = gm_l0;
        gm_b16_8 t169 = gm_l22;
        gm_tnpifv20irnph t170;
        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 0), t169);
        gm_b0_1 t171 = gm_f_linef_8694lum((void *)NULL, cx, NULL, t168, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t172 = gm_l0;
        gm_b16_8 t173 = gm_l10;
        gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
        gm_b0_1 t175 = gm_f_label_710oev4((void *)NULL, cx, NULL, t172, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l9))[16] != 0)) goto ie176;
        gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
        gm_l24 = t177;
        {
            gm_b16_8 t179 = gm_l24;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t179);
            goto gm_fdone;
        }
        goto ix176;
ie176:;
ix176:;
    }
    {
        void * t180 = gm_l0;
        void * t181 = gm_l1;
        gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t181) + 0));
        gm_b16_8 t183 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t180, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t183);
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
__attribute__((aligned(4))) void gm_f_match_switch_koqe5v7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_match_switch_koqe5v7 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* a */
    gm_t88rcucg1b7uk gm_l4; /* tag */
    gm_b16_8 gm_l5; /* variants */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_closure gm_l9; /* first */
    gm_b16_8 gm_l10; /* offs */
    gm_b16_8 gm_l11; /* tv */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* k */
    GM_STACK_CHECK(&F, 1451u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&F.l3) + 8);
        uint8_t t5 = (t4 < ((uint64_t)3ULL));
        if (!t5) {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l1;
            gm_t53at80pfdus3 t10;
            gm_f_kind_ghvav22((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t11;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
            if (t14 != ((uint8_t)17ULL)) goto mn13;
            t11 = ((uint8_t)0);
            goto me12;
mn13:;
            t11 = ((uint8_t)1);
            goto me12;
mn15:;
            gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 56, 24);
me12:;
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_tqetrgo0q24b5 t17;
            memset(&t17, 0, sizeof t17);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l1;
        void * t22 = gm_f_adt_def_ni54he1(t20, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 24) + 0));
        if (t23 != ((uint8_t)1ULL)) goto ie19;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t22) + 24) + 8) + 0));
        gm_l4 = t24;
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((((uint8_t *)t22) + 24) + 8) + 8));
        gm_l5 = t25;
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            void * t30 = (void *)(((uint8_t *)t29) + 8);
            gm_t88rcucg1b7uk t31 = gm_l4;
            uint64_t t32 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t33 = (t32 > ((uint64_t)8ULL));
            if (!t33) goto ie27;
            {
                gm_tqetrgo0q24b5 t35;
                memset(&t35, 0, sizeof t35);
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&F.l3) + 8);
                uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                if (o40) gm_trap("integer overflow in `+`", gm_src8qgs75cskrcf, 60, 17);
                uint64_t t41 = r39;
                gm_l7 = t41;
            }
            {
                for (;;) {
                    uint64_t t43 = gm_l8;
                    uint64_t t44 = gm_l7;
                    uint8_t t45 = (t43 < t44);
                    if (!t45) goto lb42;
                    {
                        uint64_t t48 = gm_l8;
                        {
                            void * t52 = (void *)((uint8_t *)&F.l6);
                            void * t53 = gm_l0;
                            uint32_t t54 = gm_f_fresh_5kokjmg(t53);
                            gm_t88rcucg1b7uk t55;
                            GM_ST(uint32_t, ((uint8_t *)&t55 + 0), t54);
                            gm_tabb710d9mq2e t56;
                            gm_f_format_oi7136m((void *)&t56, (void *)NULL, cx, NULL, t55);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_src8qgs75cskrcf, 60, 44);
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                            gm_tftb1odc739t5 t59 = gm_f_push_3ef746b(t52, t58);
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_src8qgs75cskrcf, 60, 32);
                            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                        }
                        goto me49;
mn50:;
                        gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 60, 5);
me49:;
                    }
lc42:;
                    {
                        uint64_t t62 = gm_l8;
                        uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                        uint64_t t65 = r63;
                        gm_l8 = t65;
                    }
                }
gm_ll46:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb42; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc42; }
                }
                goto gm_lad;
lb42:;
            }
        }
        {
            gm_closure t66 = ((gm_closure){(void *)gm_f_first_llehmvr, (void *)&F.h, cx});
            gm_l9 = t66;
        }
        {
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
            void * t69 = (void *)(((uint8_t *)t68) + 8);
            gm_t88rcucg1b7uk t70 = gm_l1;
            gm_b16_8 t71 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t71;
        }
        {
            void * t72 = gm_l0;
            gm_t88rcucg1b7uk t73 = gm_l4;
            gm_t3877pk7lv748 t74;
            gm_b16_8 t75 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t75);
            uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_src8qgs75cskrcf, 68, 51);
            uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t76 * 8));
            GM_ST(uint64_t, (uint8_t *)&t74 + 16, t78);
            gm_tabb710d9mq2e t79;
            gm_f_format_gjujk09((void *)&t79, (void *)NULL, cx, NULL, &t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_src8qgs75cskrcf, 68, 24);
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
            gm_b16_8 t82 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t72, t73, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t82;
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t83 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t84 = gm_l11;
            gm_tnpifv20irnph t85;
            GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
            gm_tftb1odc739t5 t86 = gm_f_write_b9o3mne(t83, t85);
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_src8qgs75cskrcf, 70, 5);
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        }
        {
            {
                gm_b16_8 t89 = gm_l5;
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                gm_l13 = t90;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t92 = gm_l14;
                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t94 = (t92 < t93);
                    if (!t94) goto lb91;
                    {
                        gm_t3l8idb2m6c1r t97;
                        uint64_t t98 = gm_l14;
                        GM_ST(uint64_t, (uint8_t *)&t97 + 0, t98);
                        uint64_t t99 = gm_l14;
                        uint64_t t100 = (uintptr_t)t99;
                        uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        memcpy((uint8_t *)&t97 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t100 * 72), 72);
                        uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&t97) + 0));
                        gm_l15 = t104;
                        F.l16 = (*(gm_tckdobunaulee *)(((uint8_t *)&t97) + 8));
                        {
                            void * t106 = (void *)((uint8_t *)&F.l12);
                            gm_t6j6smj7qoctu t107;
                            void * t108 = gm_l0;
                            gm_t88rcucg1b7uk t109 = gm_l4;
                            gm_b16_8 t110 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t108, t109, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l16) + 24)));
                            if (gm_unw.kind) {
                                goto gm_ll95;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t110);
                            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_b16_8 t112 = t111;
                            gm_closure t113 = gm_l9;
                            uint64_t t114 = gm_l15;
                            uint32_t t115 = (uint32_t)t114;
                            gm_t9805qtonbsag t116;
                            GM_ST(uint8_t, (uint8_t *)&t116 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t116 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t116), t115);
                            ((uint8_t *)&t116)[4] = 1;
                            gm_closure t117 = t113;
                            gm_node *ccx118 = t117.cx;
                            void * t119 = t117.code;
                            uint64_t t120;
                            if (GM_PTR_TAGGED(t119)) {
                                t120 = ((uint64_t (*)(gm_t9805qtonbsag))GM_PTR_UNTAG(t119))(t116);
                            } else {
                                t120 = ((uint64_t (*)(void *, gm_node *, gm_node *, gm_t9805qtonbsag))t119)(t117.env, ccx118, NULL, t116);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll95;
                            }
                            uint64_t t121 = (uintptr_t)t120;
                            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&t112) + 8);
                            if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_src8qgs75cskrcf, 71, 95);
                            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t112)) + t121 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t123);
                            gm_tftb1odc739t5 t124 = gm_f_write_jfgif5f(t106, &t107);
                            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_src8qgs75cskrcf, 71, 39);
                            gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
                        }
                        goto me102;
mn103:;
                        gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 71, 5);
me102:;
                    }
lc91:;
                    {
                        uint64_t t127 = gm_l14;
                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                        uint64_t t130 = r128;
                        gm_l14 = t130;
                    }
                }
gm_ll95:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb91; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc91; }
                }
                goto gm_lad;
lb91:;
            }
        }
        {
            void * t131 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t133 = t132;
            gm_closure t134 = gm_l9;
            gm_t9805qtonbsag t135;
            memset(&t135, 0, sizeof t135);
            gm_closure t136 = t134;
            gm_node *ccx137 = t136.cx;
            void * t138 = t136.code;
            uint64_t t139;
            if (GM_PTR_TAGGED(t138)) {
                t139 = ((uint64_t (*)(gm_t9805qtonbsag))GM_PTR_UNTAG(t138))(t135);
            } else {
                t139 = ((uint64_t (*)(void *, gm_node *, gm_node *, gm_t9805qtonbsag))t138)(t136.env, ccx137, NULL, t135);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t140 = (uintptr_t)t139;
            uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&t133) + 8);
            if (t140 >= (uintptr_t)(t141)) gm_trap_index(t140, t141, gm_src8qgs75cskrcf, 72, 39);
            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t133)) + t140 * 16));
            gm_tnpifv20irnph t143;
            GM_ST(gm_b16_8, ((uint8_t *)&t143 + 0), t142);
            gm_tftb1odc739t5 t144 = gm_f_write_upbce2g(t131, t143);
            if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_src8qgs75cskrcf, 72, 5);
            gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
        }
        {
            void * t146 = gm_l0;
            gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
            gm_b0_1 t149 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t146, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            const uint8_t *t150 = ((uint8_t *)&F.l6);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
            ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
            memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t150, 40);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
        {
            gm_tqetrgo0q24b5 t152;
            memset(&t152, 0, sizeof t152);
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
        }
ix19:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_pat_admits_86ni7ec(void * a0, gm_t9805qtonbsag a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pat_admits_86ni7ec F;
    void * gm_l0; /* p */
    gm_t9805qtonbsag gm_l1; /* k */
    uint32_t gm_l2; /* v */
    uint32_t gm_l3; /* x */
    gm_b16_8 gm_l4; /* alts */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l7; /* q */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)8ULL)) goto mn9;
    uint32_t t11 = GM_LD(uint32_t, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l2 = t11;
    uint8_t t12;
    if (!((((uint8_t *)&gm_l1))[4] != 0)) goto ie13;
    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_l3 = t14;
    uint32_t t16 = gm_l2;
    uint32_t t17 = gm_l3;
    uint8_t t18 = ((t16 == t17));
    uint8_t t19 = t18;
    t12 = t19;
    goto ix13;
ie13:;
    uint8_t t21 = ((uint8_t)0);
    t12 = t21;
ix13:;
    t2 = t12;
    goto me3;
mn9:;
ma5:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)10ULL)) goto mn22;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)t1) + 8) + 8));
    gm_l4 = t24;
    {
        {
            gm_b16_8 t27 = gm_l4;
            gm_l5 = t27;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t29 = gm_l6;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    uint64_t t34 = gm_l6;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    void * t37 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t35 * 8));
                    void * t40 = GM_LD(void *, ((uint8_t *)&t37));
                    gm_l7 = t40;
                    {
                        void * t43 = gm_l7;
                        gm_t9805qtonbsag t44 = gm_l1;
                        uint8_t t45 = gm_f_pat_admits_86ni7ec(t43, t44);
                        if (!t45) goto ie42;
                        {
                            uint8_t t47 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 81, 9);
me38:;
                }
lc28:;
                {
                    uint64_t t49 = gm_l6;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l6 = t52;
                }
            }
lb28:;
        }
    }
    uint8_t t53 = ((uint8_t)0);
    t2 = t53;
    goto me3;
mn22:;
ma6:;
    t2 = ((uint8_t)1);
    goto me3;
mn54:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 78, 51);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_pat_jfuov2m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_pat_jfuov2m F;
    void * gm_l0; /* fg */
    void * gm_l1; /* p */
    gm_b16_8 gm_l2; /* addr */
    gm_b16_8 gm_l3; /* fail */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint32_t gm_l5; /* l */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* lv */
    void * gm_l8; /* c */
    gm_b16_8 gm_l9; /* bytes */
    gm_b16_8 gm_l10; /* cv */
    gm_b16_8 gm_l11; /* v */
    gm_b16_8 gm_l12; /* eq */
    uint8_t gm_l15; /* inclusive */
    gm_b16_8 gm_l16; /* v */
    gm_b16_8 gm_l17; /* l */
    gm_b16_8 gm_l18; /* h */
    gm_b16_8 gm_l19; /* op */
    gm_b16_8 gm_l20; /* below */
    gm_b16_8 gm_l21; /* above */
    gm_b16_8 gm_l22; /* present */
    void * gm_l23; /* q */
    gm_b16_8 gm_l24; /* present */
    void * gm_l25; /* q */
    void * gm_l26; /* q */
    gm_b16_8 gm_l27; /* bad */
    uint8_t gm_l28; /* want_err */
    uint64_t gm_l29; /* off */
    uint32_t gm_l30; /* variant */
    void * gm_l31; /* payload */
    void * gm_l32; /* def */
    gm_t88rcucg1b7uk gm_l33; /* tag */
    gm_b16_8 gm_l34; /* variants */
    gm_b16_8 gm_l36; /* offs */
    gm_b16_8 gm_l37; /* tv */
    gm_b16_8 gm_l38; /* want */
    void * gm_l39; /* q */
    gm_b16_8 gm_l40; /* fs */
    gm_b16_8 gm_l41; /* offs */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l44; /* f */
    gm_b16_8 gm_l45; /* alts */
    gm_b16_8 gm_l46; /* ok */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    void * gm_l49; /* alt */
    gm_b16_8 gm_l50; /* next */
    GM_STACK_CHECK(&F, 4489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l1;
            gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
            gm_l4 = t4;
        }
        {
            void * t5 = gm_l1;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            switch (t18) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma16; default: goto ma17; }
ma7:;
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t20 != ((uint8_t)0ULL)) goto mn19;
            goto me6;
mn19:;
ma8:;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t23 != ((uint8_t)1ULL)) goto mn22;
            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
            gm_l5 = t24;
            {
                void * t26 = gm_l0;
                gm_t88rcucg1b7uk t27 = gm_l4;
                gm_b16_8 t28 = gm_l2;
                gm_b16_8 t29 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t26, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t29;
            }
            {
                void * t30 = gm_l0;
                uint32_t t31 = gm_l5;
                gm_b16_8 t32 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t32;
            }
            {
                void * t33 = gm_l0;
                gm_t6j6smj7qoctu t34;
                gm_b16_8 t35 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
                gm_b16_8 t36 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t36);
                gm_b0_1 t37 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t33, &t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me6;
mn22:;
ma9:;
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t39 != ((uint8_t)2ULL)) goto mn38;
            void * t40 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
            gm_l8 = t40;
            {
                void * t43 = gm_l8;
                uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t43) + 8) + 0));
                if (t44 != ((uint8_t)5ULL)) goto ie42;
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((((uint8_t *)t43) + 8) + 8));
                gm_l9 = t45;
                void * t46 = gm_l0;
                void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
                void * t48 = (void *)(((uint8_t *)t47) + 8);
                gm_t88rcucg1b7uk t49 = gm_l4;
                gm_t53at80pfdus3 t50;
                gm_f_kind_ghvav22((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
                uint8_t t51;
                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
                if (t54 != ((uint8_t)10ULL)) goto mn53;
                t51 = ((uint8_t)1);
                goto me52;
mn53:;
                t51 = ((uint8_t)0);
                goto me52;
mn55:;
                gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 110, 42);
me52:;
                if (!t51) goto ie42;
                {
                    void * t57 = gm_l0;
                    gm_t6j6smj7qoctu t58;
                    void * t59 = gm_l0;
                    gm_b16_8 t60 = gm_l2;
                    gm_b16_8 t61 = gm_l9;
                    gm_b16_8 t62 = gm_f_str_equal_at_mshane1((void *)NULL, cx, NULL, t59, t60, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t58 + 0, t62);
                    gm_b16_8 t63 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&t58 + 16, t63);
                    gm_b0_1 t64 = gm_f_linef_u8da5pt((void *)NULL, cx, NULL, t57, &t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t65;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t65);
                    goto gm_fdone;
                }
                goto ix42;
ie42:;
ix42:;
            }
            {
                void * t66 = gm_l0;
                void * t67 = gm_l8;
                gm_t5o0tpv5jka2l t68;
                gm_f_expr_porbc9t((void *)&t68, (void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t68))[0] != 0)) {
                    gm_tqgtd5gtgj65s t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                    ((uint8_t *)&t69)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t69 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t68) + 8)));
                    F.ret = t69;
                    goto gm_fret;
                }
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                gm_l10 = t70;
            }
            {
                void * t71 = gm_l0;
                gm_t88rcucg1b7uk t72 = gm_l4;
                gm_b16_8 t73 = gm_l2;
                gm_b16_8 t74 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t71, t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t74;
            }
            {
                void * t75 = gm_l0;
                gm_t88rcucg1b7uk t76 = gm_l4;
                gm_b16_8 t77 = gm_l11;
                gm_b16_8 t78 = gm_l10;
                gm_b16_8 t79 = gm_f_equal_hcuag9d((void *)NULL, cx, NULL, t75, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t79;
            }
            {
                void * t80 = gm_l0;
                gm_t6j6smj7qoctu t81;
                gm_b16_8 t82 = gm_l12;
                GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t82);
                gm_b16_8 t83 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t81 + 16, t83);
                gm_b0_1 t84 = gm_f_linef_u8da5pt((void *)NULL, cx, NULL, t80, &t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me6;
mn38:;
ma10:;
            uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t86 != ((uint8_t)3ULL)) goto mn85;
            gm_tbci5ospk5nnn t87 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t5) + 8) + 8) + 0));
            F.l13 = t87;
            gm_tbci5ospk5nnn t88 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t5) + 8) + 8) + 48));
            F.l14 = t88;
            uint8_t t89 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 8) + 96));
            gm_l15 = t89;
            {
                void * t91 = gm_l0;
                gm_t88rcucg1b7uk t92 = gm_l4;
                gm_b16_8 t93 = gm_l2;
                gm_b16_8 t94 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t91, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t94;
            }
            {
                void * t95 = gm_l0;
                gm_t88rcucg1b7uk t96 = gm_l4;
                gm_b16_8 t97 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t95, t96, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t97;
            }
            {
                void * t98 = gm_l0;
                gm_t88rcucg1b7uk t99 = gm_l4;
                gm_b16_8 t100 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t98, t99, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t100;
            }
            {
                gm_b16_8 t101;
                uint8_t t103 = gm_l15;
                if (!t103) goto ie102;
                gm_b16_8 t105;
                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(1));
                gm_b16_8 t106 = t105;
                t101 = t106;
                goto ix102;
ie102:;
                gm_b16_8 t108;
                GM_ST(void *, (uint8_t *)&t108, (void *)(gm_s02ru83e3fffc));
                GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(2));
                gm_b16_8 t109 = t108;
                t101 = t109;
ix102:;
                gm_l19 = t101;
            }
            {
                gm_t6j6smj7qoctu t110;
                gm_b16_8 t111 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t111);
                gm_b16_8 t112 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t112);
                gm_tabb710d9mq2e t113;
                gm_f_format_p3pvqcn((void *)&t113, (void *)NULL, cx, NULL, &t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_src8qgs75cskrcf, 124, 22);
                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
                gm_l20 = t114;
            }
            {
                gm_tfeab279c16rr t115;
                gm_b16_8 t116 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t115 + 0, t116);
                gm_b16_8 t117 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t115 + 16, t117);
                gm_b16_8 t118 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t115 + 32, t118);
                gm_tabb710d9mq2e t119;
                gm_f_format_mdlbfc1((void *)&t119, (void *)NULL, cx, NULL, &t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 8, gm_src8qgs75cskrcf, 125, 22);
                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
                gm_l21 = t120;
            }
            {
                void * t121 = gm_l0;
                gm_tfeab279c16rr t122;
                gm_b16_8 t123 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t122 + 0, t123);
                gm_b16_8 t124 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t122 + 16, t124);
                gm_b16_8 t125 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t122 + 32, t125);
                gm_b0_1 t126 = gm_f_linef_7eujspv((void *)NULL, cx, NULL, t121, &t122);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me6;
mn85:;
ma11:;
            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t128 != ((uint8_t)4ULL)) goto mn127;
            {
                void * t130 = gm_l0;
                gm_t88rcucg1b7uk t131 = gm_l4;
                gm_b16_8 t132 = gm_l2;
                gm_b16_8 t133 = gm_f_opt_present_joptd3k((void *)NULL, cx, NULL, t130, t131, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t133;
            }
            {
                void * t134 = gm_l0;
                gm_t6j6smj7qoctu t135;
                gm_b16_8 t136 = gm_l22;
                GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
                gm_b16_8 t137 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t135 + 16, t137);
                gm_b0_1 t138 = gm_f_linef_ck79eul((void *)NULL, cx, NULL, t134, &t135);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me6;
mn127:;
ma12:;
            uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t140 != ((uint8_t)5ULL)) goto mn139;
            void * t141 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
            gm_l23 = t141;
            {
                void * t143 = gm_l0;
                gm_t88rcucg1b7uk t144 = gm_l4;
                gm_b16_8 t145 = gm_l2;
                gm_b16_8 t146 = gm_f_opt_present_joptd3k((void *)NULL, cx, NULL, t143, t144, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t146;
            }
            {
                void * t147 = gm_l0;
                gm_t6j6smj7qoctu t148;
                gm_b16_8 t149 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t148 + 0, t149);
                gm_b16_8 t150 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t148 + 16, t150);
                gm_b0_1 t151 = gm_f_linef_u8da5pt((void *)NULL, cx, NULL, t147, &t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t152 = gm_l0;
                void * t153 = gm_l23;
                gm_b16_8 t154 = gm_l2;
                gm_b16_8 t155 = gm_l3;
                gm_tqgtd5gtgj65s t156;
                gm_f_pat_jfuov2m((void *)&t156, (void *)NULL, cx, NULL, t152, t153, t154, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t156))[0] != 0)) {
                    gm_tqgtd5gtgj65s t157;
                    GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                    ((uint8_t *)&t157)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t157 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t156) + 8)));
                    F.ret = t157;
                    goto gm_fret;
                }
                gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 8));
            }
            goto me6;
mn139:;
ma13:;
            uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t162 != ((uint8_t)6ULL)) goto pa161;
            void * t163 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
            gm_l25 = t163;
            goto po160;
pa161:;
            uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t165 != ((uint8_t)7ULL)) goto pa164;
            void * t166 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
            gm_l25 = t166;
            goto po160;
pa164:;
            goto mn159;
po160:;
            {
                void * t168 = gm_l0;
                gm_t88rcucg1b7uk t169 = gm_l4;
                gm_b16_8 t170 = gm_l2;
                gm_b16_8 t171 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t168, t169, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t171;
            }
            {
                void * t172 = gm_l1;
                uint8_t t173;
                uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t172) + 8) + 0));
                if (t176 != ((uint8_t)7ULL)) goto mn175;
                t173 = ((uint8_t)1);
                goto me174;
mn175:;
                t173 = ((uint8_t)0);
                goto me174;
mn177:;
                gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 139, 25);
me174:;
                gm_l28 = t173;
            }
            {
                void * t178 = gm_l0;
                gm_tfeab279c16rr t179;
                gm_b16_8 t180;
                uint8_t t182 = gm_l28;
                if (!t182) goto ie181;
                gm_b16_8 t184;
                GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sf9a2kg3eo1gs));
                GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(1));
                gm_b16_8 t185 = t184;
                t180 = t185;
                goto ix181;
ie181:;
                gm_b16_8 t187;
                GM_ST(void *, (uint8_t *)&t187, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(0));
                gm_b16_8 t188 = t187;
                t180 = t188;
ix181:;
                GM_ST(gm_b16_8, (uint8_t *)&t179 + 0, t180);
                gm_b16_8 t189 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t179 + 16, t189);
                gm_b16_8 t190 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t179 + 32, t190);
                gm_b0_1 t191 = gm_f_linef_h1r66ik((void *)NULL, cx, NULL, t178, &t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t192 = (uintptr_t)((uint64_t)1ULL);
                void * t193 = gm_l0;
                void * t194 = GM_LD(void *, (((uint8_t *)t193) + 0));
                void * t195 = (void *)(((uint8_t *)t194) + 8);
                gm_t88rcucg1b7uk t196 = gm_l4;
                gm_b16_8 t197 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t195, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&t197) + 8);
                if (t192 >= (uintptr_t)(t198)) gm_trap_index(t192, t198, gm_src8qgs75cskrcf, 141, 20);
                uint64_t t199 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t197)) + t192 * 8));
                gm_l29 = t199;
            }
            {
                void * t200 = gm_l0;
                void * t201 = gm_l25;
                gm_t3877pk7lv748 t202;
                gm_b16_8 t203 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t202 + 0, t203);
                uint64_t t204 = gm_l29;
                GM_ST(uint64_t, (uint8_t *)&t202 + 16, t204);
                gm_tabb710d9mq2e t205;
                gm_f_format_gjujk09((void *)&t205, (void *)NULL, cx, NULL, &t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_src8qgs75cskrcf, 142, 27);
                gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
                gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
                gm_b16_8 t208 = gm_l3;
                gm_tqgtd5gtgj65s t209;
                gm_f_pat_jfuov2m((void *)&t209, (void *)NULL, cx, NULL, t200, t201, t207, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t209))[0] != 0)) {
                    gm_tqgtd5gtgj65s t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                    ((uint8_t *)&t210)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t210 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t209) + 8)));
                    F.ret = t210;
                    goto gm_fret;
                }
                gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t209) + 8));
            }
            goto me6;
mn159:;
ma14:;
            uint8_t t213 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t213 != ((uint8_t)8ULL)) goto mn212;
            uint32_t t214 = GM_LD(uint32_t, (((((uint8_t *)t5) + 8) + 8) + 0));
            gm_l30 = t214;
            void * t215 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
            gm_l31 = t215;
            {
                void * t217 = gm_l0;
                gm_t88rcucg1b7uk t218 = gm_l4;
                void * t219 = gm_f_adt_def_ni54he1(t217, GM_CONV(gm_csguankko4lmhd, t218));
                gm_l32 = t219;
            }
            {
                void * t221 = gm_l32;
                uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t221) + 24) + 0));
                if (t222 != ((uint8_t)1ULL)) goto ie220;
                gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t221) + 24) + 8) + 0));
                gm_l33 = t223;
                gm_b16_8 t224 = GM_LD(gm_b16_8, (((((uint8_t *)t221) + 24) + 8) + 8));
                gm_l34 = t224;
                {
                    uint32_t t226 = gm_l30;
                    uint64_t t227 = (uint64_t)t226;
                    uint64_t t228 = (uintptr_t)t227;
                    uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    if (t228 >= (uintptr_t)(t229)) gm_trap_index(t228, t229, gm_src8qgs75cskrcf, 147, 21);
                    gm_tbci5ospk5nnn t230 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t228 * 72) + 24));
                    F.l35 = t230;
                }
                {
                    void * t231 = gm_l0;
                    void * t232 = GM_LD(void *, (((uint8_t *)t231) + 0));
                    void * t233 = (void *)(((uint8_t *)t232) + 8);
                    gm_t88rcucg1b7uk t234 = gm_l4;
                    gm_b16_8 t235 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t233, t234);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l36 = t235;
                }
                {
                    void * t236 = gm_l0;
                    gm_t88rcucg1b7uk t237 = gm_l33;
                    gm_t3877pk7lv748 t238;
                    gm_b16_8 t239 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t238 + 0, t239);
                    uint64_t t240 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                    if (t240 >= (uintptr_t)(t241)) gm_trap_index(t240, t241, gm_src8qgs75cskrcf, 149, 62);
                    uint64_t t242 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t240 * 8));
                    GM_ST(uint64_t, (uint8_t *)&t238 + 16, t242);
                    gm_tabb710d9mq2e t243;
                    gm_f_format_gjujk09((void *)&t243, (void *)NULL, cx, NULL, &t238);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t243))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t243) + 8, gm_src8qgs75cskrcf, 149, 32);
                    gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&t243) + 8));
                    gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                    gm_b16_8 t246 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t236, t237, t245);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l37 = t246;
                }
                {
                    void * t247 = gm_l0;
                    gm_t88rcucg1b7uk t248 = gm_l33;
                    gm_b16_8 t249 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t247, t248, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l35)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l38 = t249;
                }
                {
                    void * t250 = gm_l0;
                    gm_tfeab279c16rr t251;
                    gm_b16_8 t252 = gm_l37;
                    GM_ST(gm_b16_8, (uint8_t *)&t251 + 0, t252);
                    gm_b16_8 t253 = gm_l38;
                    GM_ST(gm_b16_8, (uint8_t *)&t251 + 16, t253);
                    gm_b16_8 t254 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&t251 + 32, t254);
                    gm_b0_1 t255 = gm_f_linef_ktpatuk((void *)NULL, cx, NULL, t250, &t251);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t257 = gm_l31;
                    if (!(GM_LD(void *, ((uint8_t *)&t257)) != 0)) goto ie256;
                    void * t258 = GM_LD(void *, ((uint8_t *)&t257));
                    gm_l39 = t258;
                    {
                        void * t260 = gm_l0;
                        void * t261 = gm_l39;
                        gm_t3877pk7lv748 t262;
                        gm_b16_8 t263 = gm_l2;
                        GM_ST(gm_b16_8, (uint8_t *)&t262 + 0, t263);
                        uint64_t t264 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        if (t264 >= (uintptr_t)(t265)) gm_trap_index(t264, t265, gm_src8qgs75cskrcf, 152, 75);
                        uint64_t t266 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t264 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t262 + 16, t266);
                        gm_tabb710d9mq2e t267;
                        gm_f_format_gjujk09((void *)&t267, (void *)NULL, cx, NULL, &t262);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 8, gm_src8qgs75cskrcf, 152, 45);
                        gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
                        gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
                        gm_b16_8 t270 = gm_l3;
                        gm_tqgtd5gtgj65s t271;
                        gm_f_pat_jfuov2m((void *)&t271, (void *)NULL, cx, NULL, t260, t261, t269, t270);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t271))[0] != 0)) {
                            gm_tqgtd5gtgj65s t272;
                            GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
                            ((uint8_t *)&t272)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t272 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t271) + 8)));
                            F.ret = t272;
                            goto gm_fret;
                        }
                        gm_b0_1 t273 = GM_LD(gm_b0_1, (((uint8_t *)&t271) + 8));
                    }
                    goto ix256;
ie256:;
ix256:;
                }
                goto ix220;
ie220:;
                {
                    void * t275 = gm_l1;
                    gm_tl874t2atnlh3 t276 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t275) + 120));
                    gm_b16_8 t277;
                    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_sb8vbka031tie));
                    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(20));
                    gm_t08olq97rscn3 t278;
                    gm_f_unsupported_531q51r((void *)&t278, (void *)NULL, cx, NULL, t276, t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t278);
                    goto gm_fdone;
                }
ix220:;
            }
            goto me6;
mn212:;
ma15:;
            uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t280 != ((uint8_t)9ULL)) goto mn279;
            gm_b16_8 t281 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 8) + 8));
            gm_l40 = t281;
            {
                void * t283 = gm_l0;
                void * t284 = GM_LD(void *, (((uint8_t *)t283) + 0));
                void * t285 = (void *)(((uint8_t *)t284) + 8);
                gm_t88rcucg1b7uk t286 = gm_l4;
                gm_b16_8 t287 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t285, t286);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t287;
            }
            {
                {
                    gm_b16_8 t289 = gm_l40;
                    gm_l42 = t289;
                }
                {
                    gm_l43 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t291 = gm_l43;
                        uint64_t t292 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                        uint8_t t293 = (t291 < t292);
                        if (!t293) goto lb290;
                        {
                            uint64_t t296 = gm_l43;
                            uint64_t t297 = (uintptr_t)t296;
                            uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                            gm_ttggq7ng4kp3p t301 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t297 * 16));
                            gm_l44 = t301;
                            {
                                void * t303 = gm_l0;
                                void * t304 = GM_LD(void *, (((uint8_t *)&gm_l44) + 8));
                                gm_t3877pk7lv748 t305;
                                gm_b16_8 t306 = gm_l2;
                                GM_ST(gm_b16_8, (uint8_t *)&t305 + 0, t306);
                                uint32_t t307 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 0));
                                uint64_t t308 = (uint64_t)t307;
                                uint64_t t309 = (uintptr_t)t308;
                                uint64_t t310 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                if (t309 >= (uintptr_t)(t310)) gm_trap_index(t309, t310, gm_src8qgs75cskrcf, 157, 65);
                                uint64_t t311 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t309 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t305 + 16, t311);
                                gm_tabb710d9mq2e t312;
                                gm_f_format_gjujk09((void *)&t312, (void *)NULL, cx, NULL, &t305);
                                if (gm_unw.kind) {
                                    goto gm_ll294;
                                }
                                if (((((uint8_t *)&t312))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t312) + 8, gm_src8qgs75cskrcf, 157, 35);
                                gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&t312) + 8));
                                gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                                gm_b16_8 t315 = gm_l3;
                                gm_tqgtd5gtgj65s t316;
                                gm_f_pat_jfuov2m((void *)&t316, (void *)NULL, cx, NULL, t303, t304, t314, t315);
                                if (gm_unw.kind) {
                                    goto gm_ll294;
                                }
                                if (((((uint8_t *)&t316))[0] != 0)) {
                                    gm_tqgtd5gtgj65s t317;
                                    GM_ST(uint8_t, (uint8_t *)&t317 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t317 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t317 + 4, 0);
                                    ((uint8_t *)&t317)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t317 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t316) + 8)));
                                    F.ret = t317;
                                    goto gm_fret;
                                }
                                gm_b0_1 t318 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 8));
                            }
                            goto me299;
mn300:;
                            gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 156, 13);
me299:;
                        }
lc290:;
                        {
                            uint64_t t320 = gm_l43;
                            uint64_t r321; bool o322 = __builtin_add_overflow(t320, ((uint64_t)1ULL), &r321);
                            uint64_t t323 = r321;
                            gm_l43 = t323;
                        }
                    }
gm_ll294:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb290; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc290; }
                    }
                    goto gm_lad;
lb290:;
                }
            }
            goto me6;
mn279:;
ma16:;
            uint8_t t325 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
            if (t325 != ((uint8_t)10ULL)) goto mn324;
            gm_b16_8 t326 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 8) + 8));
            gm_l45 = t326;
            {
                void * t328 = gm_l0;
                uint32_t t329 = gm_f_fresh_5kokjmg(t328);
                gm_t88rcucg1b7uk t330;
                GM_ST(uint32_t, ((uint8_t *)&t330 + 0), t329);
                gm_tabb710d9mq2e t331;
                gm_f_format_076g3l5((void *)&t331, (void *)NULL, cx, NULL, t330);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t331))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t331) + 8, gm_src8qgs75cskrcf, 161, 19);
                gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&t331) + 8));
                gm_l46 = t332;
            }
            {
                {
                    gm_b16_8 t334 = gm_l45;
                    gm_l47 = t334;
                }
                {
                    gm_l48 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t336 = gm_l48;
                        uint64_t t337 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                        uint8_t t338 = (t336 < t337);
                        if (!t338) goto lb335;
                        {
                            uint64_t t341 = gm_l48;
                            uint64_t t342 = (uintptr_t)t341;
                            uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                            void * t344 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t342 * 8));
                            void * t347 = GM_LD(void *, ((uint8_t *)&t344));
                            gm_l49 = t347;
                            {
                                void * t349 = gm_l0;
                                uint32_t t350 = gm_f_fresh_5kokjmg(t349);
                                gm_t88rcucg1b7uk t351;
                                GM_ST(uint32_t, ((uint8_t *)&t351 + 0), t350);
                                gm_tabb710d9mq2e t352;
                                gm_f_format_0r468he((void *)&t352, (void *)NULL, cx, NULL, t351);
                                if (gm_unw.kind) {
                                    goto gm_ll339;
                                }
                                if (((((uint8_t *)&t352))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t352) + 8, gm_src8qgs75cskrcf, 163, 25);
                                gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)&t352) + 8));
                                gm_l50 = t353;
                            }
                            {
                                void * t354 = gm_l0;
                                void * t355 = gm_l49;
                                gm_b16_8 t356 = gm_l2;
                                gm_b16_8 t357 = gm_l50;
                                gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&t357));
                                gm_tqgtd5gtgj65s t359;
                                gm_f_pat_jfuov2m((void *)&t359, (void *)NULL, cx, NULL, t354, t355, t356, t358);
                                if (gm_unw.kind) {
                                    goto gm_ll339;
                                }
                                if (((((uint8_t *)&t359))[0] != 0)) {
                                    gm_tqgtd5gtgj65s t360;
                                    GM_ST(uint8_t, (uint8_t *)&t360 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t360 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t360 + 4, 0);
                                    ((uint8_t *)&t360)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t360 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t359) + 8)));
                                    F.ret = t360;
                                    goto gm_fret;
                                }
                                gm_b0_1 t361 = GM_LD(gm_b0_1, (((uint8_t *)&t359) + 8));
                            }
                            {
                                void * t362 = gm_l0;
                                gm_b16_8 t363 = gm_l46;
                                gm_tnpifv20irnph t364;
                                GM_ST(gm_b16_8, ((uint8_t *)&t364 + 0), t363);
                                gm_b0_1 t365 = gm_f_linef_h995d88((void *)NULL, cx, NULL, t362, t364);
                                if (gm_unw.kind) {
                                    goto gm_ll339;
                                }
                            }
                            {
                                void * t366 = gm_l0;
                                gm_b16_8 t367 = gm_l50;
                                gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&t367));
                                gm_b0_1 t369 = gm_f_label_710oev4((void *)NULL, cx, NULL, t366, t368);
                                if (gm_unw.kind) {
                                    goto gm_ll339;
                                }
                            }
                            goto me345;
mn346:;
                            gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 162, 13);
me345:;
                        }
lc335:;
                        {
                            uint64_t t371 = gm_l48;
                            uint64_t r372; bool o373 = __builtin_add_overflow(t371, ((uint64_t)1ULL), &r372);
                            uint64_t t374 = r372;
                            gm_l48 = t374;
                        }
                    }
gm_ll339:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb335; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc335; }
                    }
                    goto gm_lad;
lb335:;
                }
            }
            {
                void * t375 = gm_l0;
                gm_b16_8 t376 = gm_l3;
                gm_tnpifv20irnph t377;
                GM_ST(gm_b16_8, ((uint8_t *)&t377 + 0), t376);
                gm_b0_1 t378 = gm_f_linef_njoe3l6((void *)NULL, cx, NULL, t375, t377);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t379 = gm_l0;
                gm_b16_8 t380 = gm_l46;
                gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
                gm_b0_1 t382 = gm_f_label_710oev4((void *)NULL, cx, NULL, t379, t381);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me6;
mn324:;
ma17:;
            gm_trap("no match arm matched the scrutinee", gm_src8qgs75cskrcf, 102, 5);
me6:;
        }
    }
    gm_b0_1 t383;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t383);
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
inline __attribute__((aligned(4))) void gm_f_scrutinee_addr_3fvmbme(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_scrutinee_addr_3fvmbme F;
    void * gm_l0; /* fg */
    void * gm_l1; /* value */
    uint8_t gm_l2; /* in_place */
    gm_b16_8 gm_l3; /* v */
    GM_STACK_CHECK(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l2;
        if (!t3) goto ie2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        gm_t5v8n6qrn4rh4 t9;
        gm_f_repr_53quqki((void *)&t9, (void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = ((GM_LD(uint8_t, ((uint8_t *)&t9) + 0) == ((uint8_t)6ULL)));
        if (!t10) goto ie2;
        void * t11 = gm_l0;
        void * t12 = gm_l1;
        uint8_t t13 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t13) goto ie2;
        {
            void * t15 = gm_l0;
            void * t16 = gm_l1;
            gm_f_place_addr_7ji15ot((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l1;
        gm_t5o0tpv5jka2l t19;
        gm_f_expr_porbc9t((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_t5o0tpv5jka2l t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t20 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t19) + 8)));
            F.ret = t20;
            goto gm_fret;
        }
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_l3 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
        gm_b16_8 t25 = gm_l3;
        gm_b16_8 t26 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t22, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t26);
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
