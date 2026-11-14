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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { uint64_t f0; void * f1; gm_t88rcucg1b7uk f2; uint8_t pe[4]; } gm_tkmj998anon0r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
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
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { void * f0; void * f1; uint64_t f2; uint64_t f3; uint32_t f4; uint8_t p5[4]; gm_trqfvmjtt4sij f5; uint64_t f6; gm_trqfvmjtt4sij f7; uint32_t f8; uint8_t pe[4]; } gm_t72vjnq98v7qn;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkmj998anon0r) == 24 && _Alignof(gm_tkmj998anon0r) == 8 && offsetof(gm_tkmj998anon0r, f0) == 0 && offsetof(gm_tkmj998anon0r, f1) == 8 && offsetof(gm_tkmj998anon0r, f2) == 16, "layout of `PendingConst`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_toceffb697p0u f0; uint8_t f1; uint8_t pe[3]; } gm_trj4m8pa99qp7;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t72vjnq98v7qn) == 136 && _Alignof(gm_t72vjnq98v7qn) == 8 && offsetof(gm_t72vjnq98v7qn, f0) == 0 && offsetof(gm_t72vjnq98v7qn, f1) == 8 && offsetof(gm_t72vjnq98v7qn, f2) == 16 && offsetof(gm_t72vjnq98v7qn, f3) == 24 && offsetof(gm_t72vjnq98v7qn, f4) == 32 && offsetof(gm_t72vjnq98v7qn, f5) == 40 && offsetof(gm_t72vjnq98v7qn, f6) == 80 && offsetof(gm_t72vjnq98v7qn, f7) == 88 && offsetof(gm_t72vjnq98v7qn, f8) == 128, "layout of `Machine`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trj4m8pa99qp7) == 20 && _Alignof(gm_trj4m8pa99qp7) == 4 && offsetof(gm_trj4m8pa99qp7, f0) == 0 && offsetof(gm_trj4m8pa99qp7, f1) == 16, "layout of `?NeedsAt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t6evimt84lndq v0; void * v1; gm_tbgd14esf0jvi v2; } f1; } gm_tr7bagvonfqmg;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tr7bagvonfqmg) == 64 && _Alignof(gm_tr7bagvonfqmg) == 16 && offsetof(gm_tr7bagvonfqmg, f0) == 0 && offsetof(gm_tr7bagvonfqmg, f1) == 16, "layout of `EvalResult`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { void * f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_tti6qnvf9jg1p f4; gm_t88rcucg1b7uk f5; uint8_t p6[4]; gm_tnpifv20irnph f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_t9805qtonbsag f13; gm_trj4m8pa99qp7 f14; uint8_t p15[4]; gm_trqfvmjtt4sij f15; uint8_t f16; uint8_t p17[7]; gm_trqfvmjtt4sij f17; uint8_t f18; uint8_t p19[7]; uint64_t f19; gm_trqfvmjtt4sij f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; uint8_t f24; uint8_t p25[7]; gm_trqfvmjtt4sij f25; uint8_t f26; uint8_t p27[3]; gm_tkn42u7na3t81 f27; uint8_t p28[4]; gm_trqfvmjtt4sij f28; gm_t403eokj7ifg9 f29; gm_trqfvmjtt4sij f30; gm_trqfvmjtt4sij f31; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; uint8_t f38; uint8_t p39[7]; gm_trqfvmjtt4sij f39; uint8_t f40; uint8_t p41[7]; void * f41; } gm_tqgtfeqqnkjsa;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tqgtfeqqnkjsa) == 1304 && _Alignof(gm_tqgtfeqqnkjsa) == 8 && offsetof(gm_tqgtfeqqnkjsa, f0) == 0 && offsetof(gm_tqgtfeqqnkjsa, f1) == 8 && offsetof(gm_tqgtfeqqnkjsa, f2) == 16 && offsetof(gm_tqgtfeqqnkjsa, f3) == 56 && offsetof(gm_tqgtfeqqnkjsa, f4) == 96 && offsetof(gm_tqgtfeqqnkjsa, f5) == 168 && offsetof(gm_tqgtfeqqnkjsa, f6) == 176 && offsetof(gm_tqgtfeqqnkjsa, f7) == 192 && offsetof(gm_tqgtfeqqnkjsa, f8) == 232 && offsetof(gm_tqgtfeqqnkjsa, f9) == 272 && offsetof(gm_tqgtfeqqnkjsa, f10) == 280 && offsetof(gm_tqgtfeqqnkjsa, f11) == 320 && offsetof(gm_tqgtfeqqnkjsa, f12) == 360 && offsetof(gm_tqgtfeqqnkjsa, f13) == 400 && offsetof(gm_tqgtfeqqnkjsa, f14) == 408 && offsetof(gm_tqgtfeqqnkjsa, f15) == 432 && offsetof(gm_tqgtfeqqnkjsa, f16) == 472 && offsetof(gm_tqgtfeqqnkjsa, f17) == 480 && offsetof(gm_tqgtfeqqnkjsa, f18) == 520 && offsetof(gm_tqgtfeqqnkjsa, f19) == 528 && offsetof(gm_tqgtfeqqnkjsa, f20) == 536 && offsetof(gm_tqgtfeqqnkjsa, f21) == 576 && offsetof(gm_tqgtfeqqnkjsa, f22) == 616 && offsetof(gm_tqgtfeqqnkjsa, f23) == 656 && offsetof(gm_tqgtfeqqnkjsa, f24) == 696 && offsetof(gm_tqgtfeqqnkjsa, f25) == 704 && offsetof(gm_tqgtfeqqnkjsa, f26) == 744 && offsetof(gm_tqgtfeqqnkjsa, f27) == 748 && offsetof(gm_tqgtfeqqnkjsa, f28) == 760 && offsetof(gm_tqgtfeqqnkjsa, f29) == 800 && offsetof(gm_tqgtfeqqnkjsa, f30) == 920 && offsetof(gm_tqgtfeqqnkjsa, f31) == 960 && offsetof(gm_tqgtfeqqnkjsa, f32) == 1000 && offsetof(gm_tqgtfeqqnkjsa, f33) == 1040 && offsetof(gm_tqgtfeqqnkjsa, f34) == 1080 && offsetof(gm_tqgtfeqqnkjsa, f35) == 1120 && offsetof(gm_tqgtfeqqnkjsa, f36) == 1160 && offsetof(gm_tqgtfeqqnkjsa, f37) == 1200 && offsetof(gm_tqgtfeqqnkjsa, f38) == 1240 && offsetof(gm_tqgtfeqqnkjsa, f39) == 1248 && offsetof(gm_tqgtfeqqnkjsa, f40) == 1288 && offsetof(gm_tqgtfeqqnkjsa, f41) == 1296, "layout of `FnCx`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_array_length_f852tll {
    gm_frame h;
    void * l0; /* s */
    uint8_t l5; /* has_value_param */
    uint8_t l6; /* on_type */
    gm_t5e6kkuq21nig ret;
};
struct gm_fr_backtrace_name_fd5fgd3 {
    gm_frame h;
    gm_tdgfduqjheqdm l3; /* def */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_body_when_cond_2coms3p {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_check_const_expr_2ot4q9v {
    gm_frame h;
    gm_tqgtfeqqnkjsa l5; /* cx */
    void * ret;
};
struct gm_fr_check_consts_fsj0q4a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_declared_defaults_m12p6ao {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* f */
    gm_t5v0c8ia6a5mo l7; /* p */
    gm_b0_1 ret;
};
struct gm_fr_check_static_assert_s4f7i6i {
    gm_frame h;
    gm_t6evimt84lndq l5; /* v */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_compile_error_qlau792 {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* m */
    gm_b0_1 ret;
};
struct gm_fr_const_hook_2qtf05o {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_const_string_jdjj62s {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_const_value_fr4coi4 {
    gm_frame h;
    gm_tjcptv9sagfq8 l4; /* c */
    void * ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_default_const_k60bpu4 {
    gm_frame h;
    gm_tjcptv9sagfq8 l5; /* c */
    uint8_t ret;
};
struct gm_fr_depends_on_params_khui57h {
    gm_frame h;
    void * l0; /* s */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_escapes_0j96sk5 {
    gm_frame h;
    gm_t6evimt84lndq l0; /* v */
    gm_t6evimt84lndq l7; /* x */
    uint8_t ret;
};
struct gm_fr_eval_typed_m82ie8p {
    gm_frame h;
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_evaluate_ukvgal2 {
    gm_frame h;
    gm_t6evimt84lndq l3; /* v */
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_finish_const_tk4l20a {
    gm_frame h;
    gm_tjcptv9sagfq8 l2; /* c */
    gm_t6evimt84lndq l5; /* v */
    uint8_t ret;
};
struct gm_fr_fn_6qb25nt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_meq413i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n7mt18r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_install_db2gf1l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_site_8sr7vam {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_len_ty_nkbf2gl {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* i */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_const_b6dhg2g {
    gm_frame h;
    gm_t6evimt84lndq l11; /* v */
    gm_t6evimt84lndq l12; /* x */
    gm_t9805qtonbsag ret;
};
struct gm_fr_mark_vedqs3l {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_mentions_error_37knu7r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_non_constant_part_itk7b0v {
    gm_frame h;
    void * l0; /* s */
    gm_tkn42u7na3t81 l2; /* bad */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_one_expr_q6kn0hn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_trap_1kollts {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_static_assert_k8dt551 {
    gm_frame h;
    gm_t0mfir12leg2g l4; /* cond */
    gm_tc3oht4ksth9d l5; /* c */
    gm_tc3oht4ksth9d l6; /* c */
    gm_tc3oht4ksth9d l9; /* m */
    void * ret;
};
struct gm_fr_string_bytes_l9peo43 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t72vjnq98v7qn l2; /* m */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_type_of_expr_l8qm9vv {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visit_e3tpkkh {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_any_since_o1kpm84(void * a0, gm_csc7lc6jlkqkuq a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_length_f852tll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_backtrace_name_fd5fgd3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_body_when_cond_2coms3p(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_check_const_expr_2ot4q9v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_consts_fsj0q4a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_declared_defaults_m12p6ao(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_static_assert_s4f7i6i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3, gm_toceffb697p0u a4);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_compile_error_qlau792(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_hook_2qtf05o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_const_string_jdjj62s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_default_const_k60bpu4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) uint8_t gm_f_depends_on_params_khui57h(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_escapes_0j96sk5(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_escapes_0j96sk5_c(gm_t6evimt84lndq a0);
__attribute__((aligned(4))) void gm_f_eval_typed_m82ie8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_evaluate_ukvgal2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_finish_const_tk4l20a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_6qb25nt(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_n7mt18r(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_06lg16a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_3ahqitg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_80i8qff(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_civj829(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d6hu3gb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jru5qda(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uthdi26(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0);
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_len_expr_i134fvg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_len_ty_nkbf2gl(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_const_b6dhg2g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_machine_mv7ob92(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_prepare_0r9bic7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0rapspb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r5pdr8o(void * a0, gm_t2i8ne5ibd7v4 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_trap_1kollts(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_reset_dg0kphe(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) void gm_f_run_rq2eqmk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_static_assert_k8dt551(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_string_bytes_l9peo43(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_string_of_plna4gu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_of_expr_l8qm9vv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_usize_key_rs1gk1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_csd3p6j3fi7[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k8majipqr8n[40] __attribute__((aligned(8)));
extern uint8_t gm_ke6nl7d0rco[40] __attribute__((aligned(8)));
extern const uint8_t gm_s32428dkdndn7[141];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s88n2j7klshfh[11];
extern const uint8_t gm_s8ta4feq24fk9[12];
extern const uint8_t gm_s9gu0s8i5nn2u[9];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sbd6ket0ql5ab[9];
extern const uint8_t gm_sblodmj4vs4nd[24];
extern const uint8_t gm_sg12s7bns50dg[201];
extern const uint8_t gm_shcail1g86pp0[26];
extern const uint8_t gm_siic7p394dov6[13];
extern const uint8_t gm_slg1gdqpr4sdg[49];
extern const uint8_t gm_smsf0v36m37b1[66];
extern const uint8_t gm_sn5m7a373k04g[45];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const uint8_t gm_so0m8vf92uemo[66];
extern const uint8_t gm_sp5mk8jqu8dn0[70];
extern const uint8_t gm_sqk4m1hc1n4b5[49];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcstmd1b5da98l[];
extern const uint8_t gm_ss75kiikq7m3h[28];
extern const uint8_t gm_sta9bvnin7dlr[36];
extern const uint8_t gm_stgqtu1f7bco8[8];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 2008);
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_trlabqsjjp0ds t5;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 12), t4);
    gm_t9805qtonbsag t6 = gm_f_get_pk8ncss(t2, t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_depends_on_params_khui57h(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depends_on_params_khui57h F;
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l1;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_n7mt18r, (void *)&F.h, cx});
        gm_f_visit_e3tpkkh(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&t1));
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_count_omtt6qd F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 48));
    F.ret = t2;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_meq413i F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_visit_e3tpkkh *)((&F.h)->up))->l1));
        gm_f_visit_e3tpkkh(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_install_db2gf1l F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        GM_ST(void *, (((uint8_t *)t2) + 2168), ((void *)gm_f_const_hook_2qtf05o));
    }
    {
        void * t3 = gm_l0;
        GM_ST(void *, (((uint8_t *)t3) + 2176), ((void *)gm_f_array_length_f852tll));
    }
    {
        void * t4 = gm_l0;
        GM_ST(void *, (((uint8_t *)t4) + 2184), ((void *)gm_f_type_of_expr_l8qm9vv));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_site_8sr7vam F;
    void * gm_l0; /* p */
    void * gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        void * t8 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t9 != ((uint8_t)49ULL)) goto ie7;
        gm_tet9guhev5r4u t10 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t8) + 8) + 8));
        F.l2 = t10;
        {
            uint8_t t12 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 15) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_sa9e77lq9q04p, 15) == 0));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix7;
ie7:;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix7:;
    }
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
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_vedqs3l F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 0) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
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
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_non_constant_part_itk7b0v F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        gm_tkn42u7na3t81 t2;
        memset(&t2, 0, sizeof t2);
        F.l2 = t2;
    }
    {
        void * t3 = F.l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_closure t5 = ((gm_closure){(void *)gm_f_fn_hcjjkrm, (void *)&F.h, cx});
        gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t6 = F.l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_expr_q6kn0hn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcg82mujgottmf, 140, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_string_bytes_l9peo43(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_string_bytes_l9peo43 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_machine_mv7ob92((void *)&F.l2, (void *)NULL, cx, NULL, t2, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_string_of_plna4gu((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t3, &(*(gm_t6evimt84lndq *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visit_e3tpkkh F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        gm_closure t2 = F.l1;
        void * t3 = gm_l0;
        gm_closure t4 = t2;
        gm_node *ccx5 = t4.cx;
        void * t6 = t4.code;
        gm_b0_1 t7;
        if (GM_PTR_TAGGED(t6)) {
            ((void (*)(void *))GM_PTR_UNTAG(t6))(t3);
        } else {
            t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l0;
        gm_closure t9 = ((gm_closure){(void *)gm_f_fn_meq413i, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t8, t9);
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
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_length_f852tll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_array_length_f852tll F;
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* errors */
    void * gm_l4; /* ex */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint8_t gm_l8; /* known */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t2i8ne5ibd7v4 gm_l11; /* le */
    uint32_t gm_l12; /* id */
    void * gm_l13; /* cell */
    unsigned __int128 gm_l14; /* n */
    GM_STACK_CHECK(&F, 1246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = F.l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        uint64_t t5 = gm_f_error_count_omtt6qd(t4);
        gm_l3 = t5;
    }
    {
        void * t6 = F.l0;
        uint64_t t7 = gm_l1;
        void * t8 = gm_l2;
        void * t9 = F.l0;
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
        ((uint8_t *)&t12)[4] = 1;
        void * t13 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t6, t7, t8, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t14;
        if ((GM_LD(void *, ((uint8_t *)&t13)) != 0)) {
            void * t15 = GM_LD(void *, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_t5e6kkuq21nig t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t16 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)2ULL));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l4 = t14;
    }
    {
        uint8_t t19 = 0;
        void * t20 = F.l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 8));
        uint64_t t23 = gm_f_error_count_omtt6qd(t22);
        uint64_t t24 = gm_l3;
        uint8_t t25 = (!(t23 == t24));
        if (!t25) {
            void * t26 = F.l0;
            void * t27 = (void *)(((uint8_t *)t26) + 8);
            void * t28 = gm_l4;
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + 0));
            uint8_t t30 = gm_f_is_error_8k27gd1(t27, GM_CONV(gm_csguankko4lmhd, t29));
            t19 = t30;
        } else t19 = 1;
        if (!t19) goto ie18;
        {
            gm_t5e6kkuq21nig t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t32 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)2ULL));
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        F.l5 = ((uint8_t)0);
    }
    {
        F.l6 = ((uint8_t)0);
    }
    {
        void * t34 = gm_l4;
        gm_closure t35 = ((gm_closure){(void *)gm_f_fn_6qb25nt, (void *)&F.h, cx});
        gm_f_visit_e3tpkkh(t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t37 = F.l6;
        if (!t37) goto ie36;
        uint8_t t38 = F.l5;
        uint8_t t39 = (uint8_t)!t38;
        if (!t39) goto ie36;
        {
            void * t41 = F.l0;
            void * t42 = gm_l2;
            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s32428dkdndn7));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(140));
            gm_b0_1 t45 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t41, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t5e6kkuq21nig t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
            uint8_t t47 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        uint8_t t49 = F.l5;
        if (!t49) goto ie48;
        {
            void * t51 = F.l0;
            void * t52 = gm_l4;
            gm_tkn42u7na3t81 t53 = gm_f_len_ty_nkbf2gl((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t54;
            if (((((uint8_t *)&t53))[4] != 0)) {
                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t53) + 0));
                t54 = t55;
            } else {
                {
                    void * t57 = F.l0;
                    void * t58 = gm_l2;
                    gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
                    gm_b16_8 t60;
                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sg12s7bns50dg));
                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(200));
                    gm_b0_1 t61 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t57, t59, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_t5e6kkuq21nig t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)2ULL));
                uint8_t t63 = 0;
                F.ret = t62;
                goto gm_fret;
            }
            gm_l7 = t54;
        }
        {
            void * t65 = F.l0;
            void * t66 = (void *)(((uint8_t *)t65) + 8);
            gm_t88rcucg1b7uk t67 = gm_l7;
            gm_t53at80pfdus3 t68;
            gm_f_kind_jcnt4hm((void *)&t68, t66, GM_CONV(gm_csguankko4lmhd, t67));
            uint8_t t69;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
            if (t72 != ((uint8_t)22ULL)) goto mn71;
            t69 = ((uint8_t)1);
            goto me70;
mn71:;
            t69 = ((uint8_t)0);
            goto me70;
mn73:;
            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 48, 12);
me70:;
            if (!t69) goto ie64;
            {
                gm_l8 = ((uint8_t)0);
            }
            {
                {
                    void * t76 = F.l0;
                    gm_b16_8 t77 = GM_LD(gm_b16_8, ((((uint8_t *)t76) + 2192) + 0));
                    gm_l9 = t77;
                }
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t79 = gm_l10;
                        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint8_t t81 = (t79 < t80);
                        if (!t81) goto lb78;
                        {
                            uint64_t t84 = gm_l10;
                            uint64_t t85 = (uintptr_t)t84;
                            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            gm_t2i8ne5ibd7v4 t89 = GM_LD(gm_t2i8ne5ibd7v4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t85 * 16));
                            gm_l11 = t89;
                            {
                                gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 0));
                                gm_t88rcucg1b7uk t93 = gm_l7;
                                uint8_t t94 = (gm_eq9m6mvtmgde(((uint8_t *)&t92), ((uint8_t *)&t93)));
                                if (!t94) goto ie91;
                                gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l11) + 4));
                                void * t96 = gm_l2;
                                gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                                uint8_t t98 = (gm_eqff85ac10o3(((uint8_t *)&t95), ((uint8_t *)&t97)));
                                if (!t98) goto ie91;
                                {
                                    gm_l8 = ((uint8_t)1);
                                }
                                goto ix91;
ie91:;
ix91:;
                            }
                            goto me87;
mn88:;
                            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 50, 13);
me87:;
                        }
lc78:;
                        {
                            uint64_t t101 = gm_l10;
                            uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                            uint64_t t104 = r102;
                            gm_l10 = t104;
                        }
                    }
lb78:;
                }
            }
            {
                uint8_t t106 = gm_l8;
                uint8_t t107 = (uint8_t)!t106;
                if (!t107) goto ie105;
                {
                    void * t109 = F.l0;
                    void * t110 = (void *)(((uint8_t *)t109) + 2192);
                    gm_t88rcucg1b7uk t111 = gm_l7;
                    void * t112 = gm_l2;
                    gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t112) + 72));
                    gm_t2i8ne5ibd7v4 t114;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t114 + 0), t111);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t114 + 4), t113);
                    gm_tftb1odc739t5 t115 = gm_f_push_r5pdr8o(t110, t114);
                    if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcstmd1b5da98l, 53, 28);
                    gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                }
                goto ix105;
ie105:;
ix105:;
            }
            goto ix64;
ie64:;
ix64:;
        }
        {
            gm_t88rcucg1b7uk t117 = gm_l7;
            gm_t5e6kkuq21nig t118;
            GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t118 + 12, 0);
            GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t118 + 8), t117);
            uint8_t t119 = 0;
            F.ret = t118;
            goto gm_fret;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        void * t120 = F.l0;
        gm_tjcptv9sagfq8 t121;
        memset((uint8_t *)&t121 + 73, 0, 7);
        gm_b16_8 t122;
        GM_ST(void *, (uint8_t *)&t122, (void *)(gm_s9gu0s8i5nn2u));
        GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t121 + 0, t122);
        void * t123 = gm_l2;
        gm_tl874t2atnlh3 t124 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t123) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t121 + 16, t124);
        void * t125 = gm_l4;
        gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t125) + 0));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t121 + 28, t126);
        uint64_t t127 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t121 + 32, t127);
        void * t128 = gm_l2;
        GM_ST(void *, (uint8_t *)&t121 + 40, t128);
        void * t129 = F.l0;
        void * t130 = (void *)(((uint8_t *)t129) + 8);
        gm_t88rcucg1b7uk t131 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t132 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t132), t131);
        ((uint8_t *)&t132)[4] = 1;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t121 + 48, t132);
        void * t133 = F.l0;
        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)t133) + 2312));
        GM_ST(uint64_t, (uint8_t *)&t121 + 56, t134);
        void * t135 = gm_l4;
        GM_ST(void *, (uint8_t *)&t121 + 64, t135);
        gm_t9qkh0rsooc8d t136;
        GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t121 + 72, t136);
        GM_ST(void *, (uint8_t *)&t121 + 80, ((void *)0));
        uint32_t t137 = gm_f_add_const_8u8b1ja((void *)NULL, cx, NULL, t120, &t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t137;
    }
    {
        void * t139 = F.l0;
        uint32_t t140 = gm_l12;
        uint8_t t141 = gm_f_finish_const_tk4l20a((void *)NULL, cx, NULL, t139, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t142 = (uint8_t)!t141;
        if (!t142) goto ie138;
        {
            gm_t5e6kkuq21nig t144;
            GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t144 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)2ULL));
            uint8_t t145 = 0;
            F.ret = t144;
            goto gm_fret;
        }
        goto ix138;
ie138:;
ix138:;
    }
    {
        uint32_t t146 = gm_l12;
        uint64_t t147 = (uint64_t)t146;
        uint64_t t148 = (uintptr_t)t147;
        void * t149 = F.l0;
        uint64_t t150 = GM_LD(uintptr_t, ((((uint8_t *)t149) + 1968) + 0) + 8);
        if (t148 >= (uintptr_t)(t150)) gm_trap_index(t148, t150, gm_srcstmd1b5da98l, 59, 13);
        void * t151 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t149) + 1968) + 0)) + t148 * 88) + 80));
        void * t152;
        if ((GM_LD(void *, ((uint8_t *)&t151)) != 0)) {
            void * t153 = GM_LD(void *, (((uint8_t *)&t151) + 0));
            t152 = t153;
        } else {
            gm_t5e6kkuq21nig t154;
            GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)2ULL));
            uint8_t t155 = 0;
            F.ret = t154;
            goto gm_fret;
        }
        gm_l13 = t152;
    }
    {
        void * t157 = gm_l13;
        uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t157) + 0) + 0));
        if (t158 != ((uint8_t)0ULL)) goto ie156;
        unsigned __int128 t159 = GM_LD(unsigned __int128, ((((uint8_t *)t157) + 0) + 16));
        gm_l14 = t159;
        {
            unsigned __int128 t161 = gm_l14;
            uint64_t t162 = (uint64_t)t161;
            gm_t5e6kkuq21nig t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t163 + 8), t162);
            uint8_t t164 = 0;
            F.ret = t163;
            goto gm_fret;
        }
        goto ix156;
ie156:;
        {
            gm_t5e6kkuq21nig t166;
            GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t166 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)2ULL));
            uint8_t t167 = 0;
            F.ret = t166;
            goto gm_fret;
        }
ix156:;
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
__attribute__((aligned(4))) void gm_f_backtrace_name_fd5fgd3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_backtrace_name_fd5fgd3 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* sig */
    gm_th2lqbo77pvsc gm_l4; /* parent */
    uint32_t gm_l5; /* p */
    uint8_t gm_l6; /* clause */
    uint8_t gm_l7; /* special */
    gm_b16_8 gm_l8; /* p */
    GM_STACK_CHECK(&F, 817u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcstmd1b5da98l, 202, 13);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136);
        gm_l2 = t7;
    }
    {
        uint8_t t9 = 0;
        void * t10 = gm_l2;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 32));
        uint64_t t12 = (uintptr_t)t11;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 200) + 0) + 8);
        if (t12 >= (uintptr_t)(t15)) gm_trap_index(t12, t15, gm_srcstmd1b5da98l, 203, 8);
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 200) + 0)) + t12 * 184) + 112));
        if (!t16) {
            void * t17 = gm_l2;
            uint8_t t18 = (((GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)t17) + 0)), gm_stgqtu1f7bco8, 7) == 0));
            t9 = t18;
        } else t9 = 1;
        if (!t9) goto ie8;
        {
            gm_th2lqbo77pvsc t20;
            memset(&t20, 0, sizeof t20);
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t22 = gm_l0;
        uint32_t t23 = gm_l1;
        gm_f_def_of_m512sbu((void *)&F.l3, t22, t23);
    }
    {
        gm_th2lqbo77pvsc t24;
        memset(&t24, 0, sizeof t24);
        gm_l4 = t24;
    }
    {
        if (!(((((uint8_t *)&F.l3) + 48))[4] != 0)) goto ie25;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 48));
        gm_l5 = t26;
        {
            void * t28 = gm_l0;
            uint32_t t29 = gm_l5;
            gm_f_backtrace_name_fd5fgd3((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), (void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        uint8_t t30 = ((GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0) + 0) == ((uint8_t)2ULL)));
        gm_l6 = t30;
    }
    {
        uint8_t t31 = 0;
        void * t32 = gm_l2;
        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)t32) + 0) + 8);
        uint8_t t34 = (t33 > ((uint64_t)0ULL));
        if (t34) {
            uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
            void * t36 = gm_l2;
            uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
            if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcstmd1b5da98l, 208, 37);
            uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)) + t35 * 1));
            uint8_t t39 = ((t38 == ((uint8_t)60ULL)));
            t31 = t39;
        } else t31 = 0;
        gm_l7 = t31;
    }
    {
        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie40;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l8 = t41;
        {
            uint8_t t44 = gm_l6;
            if (!t44) goto ie43;
            uint8_t t45 = gm_l7;
            if (!t45) goto ie43;
            {
                gm_t6j6smj7qoctu t47;
                void * t48 = gm_l2;
                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t47 + 0, t49);
                gm_b16_8 t50 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t47 + 16, t50);
                gm_tabb710d9mq2e t51;
                gm_f_format_civj829((void *)&t51, (void *)NULL, cx, NULL, &t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srcstmd1b5da98l, 210, 40);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t53);
                goto gm_fdone;
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            uint8_t t55 = gm_l6;
            if (!t55) goto ie54;
            {
                gm_t6j6smj7qoctu t57;
                void * t58 = gm_l2;
                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)t58) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t59);
                gm_b16_8 t60 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t60);
                gm_tabb710d9mq2e t61;
                gm_f_format_jru5qda((void *)&t61, (void *)NULL, cx, NULL, &t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srcstmd1b5da98l, 211, 28);
                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t63);
                goto gm_fdone;
            }
            goto ix54;
ie54:;
ix54:;
        }
        {
            uint8_t t65 = ((GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0) + 0) == ((uint8_t)1ULL)));
            if (!t65) goto ie64;
            uint8_t t66 = gm_l7;
            if (!t66) goto ie64;
            {
                gm_t6j6smj7qoctu t68;
                void * t69 = gm_l2;
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t69) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t70);
                gm_b16_8 t71 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t71);
                gm_tabb710d9mq2e t72;
                gm_f_format_civj829((void *)&t72, (void *)NULL, cx, NULL, &t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcstmd1b5da98l, 212, 54);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t74);
                goto gm_fdone;
            }
            goto ix64;
ie64:;
ix64:;
        }
        goto ix40;
ie40:;
        uint8_t t76 = gm_l6;
        if (!t76) goto ie75;
        {
            void * t78 = gm_l2;
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)t78) + 0));
            gm_tnpifv20irnph t80;
            GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
            gm_tabb710d9mq2e t81;
            gm_f_format_d6hu3gb((void *)&t81, (void *)NULL, cx, NULL, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srcstmd1b5da98l, 214, 16);
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t83);
            goto gm_fdone;
        }
        goto ix75;
ie75:;
ix75:;
ix40:;
    }
    {
        void * t84 = gm_l2;
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)t84) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t85);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_body_when_cond_2coms3p(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_body_when_cond_2coms3p F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* cond */
    void * gm_l3; /* ex */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[4] = 1;
        void * t9 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t2, t3, t4, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_tqs5p4n2tq19v t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l3;
        uint8_t t17 = gm_f_depends_on_params_khui57h(t15, t16);
        if (!t17) goto ie14;
        {
            void * t19 = gm_l0;
            void * t20 = gm_l2;
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_smsf0v36m37b1));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(65));
            gm_b0_1 t23 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t19, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tqs5p4n2tq19v t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = gm_l3;
        void * t29 = gm_l0;
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 2312));
        gm_tjoi8o798ne9k t31;
        gm_f_evaluate_ukvgal2((void *)&t31, (void *)NULL, cx, NULL, t27, t28, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t31))[48] != 0)) goto ie26;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t32 != ((uint8_t)2ULL)) goto ie26;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t31) + 16));
        gm_l4 = t33;
        {
            uint8_t t35 = gm_l4;
            gm_tqs5p4n2tq19v t36;
            GM_ST(uint8_t, ((uint8_t *)&t36), t35);
            ((uint8_t *)&t36)[1] = 1;
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto ix26;
ie26:;
        {
            gm_tqs5p4n2tq19v t39;
            memset(&t39, 0, sizeof t39);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
ix26:;
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
__attribute__((aligned(4))) void * gm_f_check_const_expr_2ot4q9v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_check_const_expr_2ot4q9v F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    uint64_t gm_l4; /* errors */
    void * gm_l6; /* ex */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 772u);
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
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        uint64_t t5 = gm_f_error_count_omtt6qd(t4);
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t11 = gm_f_empty_effects_r0iv3pv();
        gm_f_fn_cx_hgvq15e((void *)&F.l5, (void *)NULL, cx, NULL, t6, t7, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l5) + 1240), ((uint8_t)1));
    }
    {
        void * t12 = (void *)((uint8_t *)&F.l5);
        void * t13 = gm_l2;
        gm_tkn42u7na3t81 t14 = gm_l3;
        void * t15 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t15;
    }
    {
        if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie16;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
        gm_l7 = t17;
        {
            void * t19 = (void *)((uint8_t *)&F.l5);
            void * t20 = gm_l6;
            gm_t88rcucg1b7uk t21 = gm_l7;
            void * t22 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t22;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)&F.l5) + 192) + 0) + 8);
        uint8_t t25 = (t24 > ((uint64_t)0ULL));
        if (!t25) goto ie23;
        {
            void * t27 = (void *)((uint8_t *)&F.l5);
            uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)&F.l5) + 192) + 0) + 8);
            if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srcstmd1b5da98l, 123, 46);
            gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l5) + 192) + 0)) + t28 * 40) + 24));
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_slg1gdqpr4sdg));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(48));
            gm_b0_1 t32 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t27, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t33 = (void *)((uint8_t *)&F.l5);
        void * t34 = gm_l6;
        gm_b0_1 t35 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 8));
        uint64_t t40 = gm_f_error_count_omtt6qd(t39);
        uint64_t t41 = gm_l4;
        uint8_t t42 = (!(t40 == t41));
        if (!t42) goto ie36;
        {
            uint8_t t44 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)&F.l5) + 16) + 0) + 8);
        uint8_t t47 = ((t46 == ((uint64_t)0ULL)));
        if (!t47) goto ie45;
        {
            void * t49 = gm_l6;
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        void * t51 = gm_l0;
        gm_t9qkh0rsooc8d t52;
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s88n2j7klshfh));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(10));
        void * t54 = gm_l6;
        gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
        uint64_t t56 = gm_l1;
        gm_b16_8 t57 = gm_f_empty_slice_he774nl();
        void * t58 = gm_l6;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 0));
        gm_tnpifv20irnph t60 = gm_f_empty_effects_r0iv3pv();
        uint32_t t61 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t51, t52, t53, t55, t56, t57, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t61;
    }
    {
        uint32_t t62 = gm_l8;
        uint64_t t63 = (uint64_t)t62;
        uint64_t t64 = (uintptr_t)t63;
        void * t65 = gm_l0;
        uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 384) + 0) + 8);
        if (t64 >= (uintptr_t)(t66)) gm_trap_index(t64, t66, gm_srcstmd1b5da98l, 130, 5);
        uint8_t *t67 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 384) + 0)) + t64 * 64);
        memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 57, 0, 7);
        gm_t9qkh0rsooc8d t68;
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
        gm_b16_8 t69 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 16) + 0));
        void * t71 = gm_l6;
        gm_t9805qtonbsag t72;
        memset(&t72, 0, sizeof t72);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 0, t68);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 8, t69);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 24, t70);
        GM_ST(void *, (uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 40, t71);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 48, t72);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)t67) + 56, ((uint8_t)0));
    }
    {
        void * t73 = gm_l6;
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + 0));
        gm_to76jli3bqddq t75;
        uint32_t t76 = gm_l8;
        gm_tsth9qu1dv0g3 t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t77 + 8), t76);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t75 + 0, t77);
        gm_b16_8 t78 = gm_f_empty_slice_g3tddrb_2();
        GM_ST(gm_b16_8, (uint8_t *)&t75 + 40, t78);
        gm_teti411eujvf4 t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t79 + 8), t75);
        void * t80 = gm_l6;
        gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + 72));
        void * t82 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t74, &t79, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t83 = 0;
        F.ret = t82;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_consts_fsj0q4a(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_consts_fsj0q4a F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    gm_tkmj998anon0r gm_l4; /* c */
    GM_STACK_CHECK(&F, 312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 464) + 0) + 8);
            gm_l1 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = gm_l1;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 464) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcstmd1b5da98l, 260, 14);
                        gm_tkmj998anon0r t20 = GM_LD(gm_tkmj998anon0r, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 464) + 0)) + t17 * 24));
                        gm_l4 = t20;
                    }
                    {
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 0));
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 200) + 0) + 8);
                        if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_srcstmd1b5da98l, 261, 12);
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 200) + 0)) + t23 * 184) + 112));
                        if (!t27) goto ie21;
                        {
                            goto lc5;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    {
                        void * t30 = gm_l0;
                        void * t31 = (void *)(((uint8_t *)t30) + 8);
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                        uint8_t t33 = gm_f_has_param_u9t0efs(t31, GM_CONV(gm_csguankko4lmhd, t32));
                        if (!t33) goto ie29;
                        {
                            void * t35 = gm_l0;
                            void * t36 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
                            gm_b16_8 t38;
                            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sta9bvnin7dlr));
                            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(35));
                            gm_b0_1 t39 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t35, t37, t38);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            goto lc5;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    {
                        {
                            void * t41 = gm_l0;
                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 0));
                            void * t43 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                            gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                            uint8_t t45 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t41, t42, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 259, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t47 = gm_l2;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l2 = t50;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_declared_defaults_m12p6ao(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_declared_defaults_m12p6ao F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l8; /* d */
    GM_STACK_CHECK(&F, 555u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 344) + 0) + 8);
            gm_l1 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = gm_l1;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 344) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcstmd1b5da98l, 274, 14);
                        gm_tqqhoivahirr0 t20 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 344) + 0)) + t17 * 136));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = 0;
                        uint8_t t23;
                        if ((((((uint8_t *)&F.l4) + 96))[24] != 0)) goto mn25;
                        t23 = ((uint8_t)0);
                        goto me24;
mn25:;
                        t23 = ((uint8_t)1);
                        goto me24;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 275, 12);
me24:;
                        if (!t23) {
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                            uint64_t t28 = (uintptr_t)t27;
                            void * t29 = gm_l0;
                            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 200) + 0) + 8);
                            if (t28 >= (uintptr_t)(t31)) gm_trap_index(t28, t31, gm_srcstmd1b5da98l, 275, 38);
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 200) + 0)) + t28 * 184) + 112));
                            t22 = t32;
                        } else t22 = 1;
                        if (!t22) goto ie21;
                        {
                            goto lc5;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    {
                        {
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
                            gm_l5 = t35;
                        }
                        {
                            gm_l6 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t37 = gm_l6;
                                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                uint8_t t39 = (t37 < t38);
                                if (!t39) goto lb36;
                                {
                                    uint64_t t42 = gm_l6;
                                    uint64_t t43 = (uintptr_t)t42;
                                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    gm_t5v0c8ia6a5mo t47 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t43 * 56));
                                    F.l7 = t47;
                                    {
                                        void * t49 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
                                        void * t50;
                                        if ((GM_LD(void *, ((uint8_t *)&t49)) != 0)) {
                                            void * t51 = GM_LD(void *, (((uint8_t *)&t49) + 0));
                                            t50 = t51;
                                        } else {
                                            goto lc36;
                                        }
                                        gm_l8 = t50;
                                    }
                                    {
                                        uint8_t t53 = 0;
                                        uint8_t t54 = 0;
                                        void * t55 = (void *)((uint8_t *)&F.l7);
                                        uint8_t t56 = gm_f_is_site_8sr7vam(t55);
                                        if (!t56) {
                                            void * t57 = gm_l0;
                                            void * t58 = (void *)(((uint8_t *)t57) + 8);
                                            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                                            uint8_t t60 = gm_f_has_param_u9t0efs(t58, GM_CONV(gm_csguankko4lmhd, t59));
                                            t54 = t60;
                                        } else t54 = 1;
                                        if (!t54) {
                                            uint8_t t61;
                                            if ((((((uint8_t *)&F.l7) + 40))[4] != 0)) goto mn63;
                                            t61 = ((uint8_t)0);
                                            goto me62;
mn63:;
                                            t61 = ((uint8_t)1);
                                            goto me62;
mn64:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 278, 61);
me62:;
                                            t53 = t61;
                                        } else t53 = 1;
                                        if (!t53) goto ie52;
                                        {
                                            goto lc36;
                                        }
                                        goto ix52;
ie52:;
ix52:;
                                    }
                                    {
                                        {
                                            void * t67 = gm_l0;
                                            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                                            void * t69 = gm_l8;
                                            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                                            uint8_t t71 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t67, t68, t69, t70);
                                            if (gm_unw.kind) {
                                                goto gm_ll40;
                                            }
                                        }
                                    }
                                    goto me45;
mn46:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 276, 9);
me45:;
                                }
lc36:;
                                {
                                    uint64_t t73 = gm_l6;
                                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                                    uint64_t t76 = r74;
                                    gm_l6 = t76;
                                }
                            }
gm_ll40:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
                            }
                            goto gm_ll9;
lb36:;
                        }
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 273, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t78 = gm_l2;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l2 = t81;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_static_assert_s4f7i6i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3, gm_toceffb697p0u a4) {
    struct gm_fr_check_static_assert_s4f7i6i F;
    void * gm_l0; /* s */
    void * gm_l1; /* ex */
    gm_th2lqbo77pvsc gm_l2; /* msg */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_toceffb697p0u gm_l4; /* instance */
    gm_b16_8 gm_l6; /* t */
    gm_b16_8 gm_l7; /* text */
    gm_tl874t2atnlh3 gm_l9; /* at */
    GM_STACK_CHECK(&F, 526u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 2312));
        gm_tjoi8o798ne9k t6;
        gm_f_evaluate_ukvgal2((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6evimt84lndq t7;
        if (((((uint8_t *)&t6))[48] != 0)) {
            t7 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t6) + 0));
        } else {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        F.l5 = t7;
    }
    {
        uint8_t t11 = (((GM_LD(uint8_t, ((uint8_t *)&F.l5) + 0) == ((uint8_t)2ULL)) && ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 16)) != 0) == (((uint8_t)0) != 0))));
        if (!t11) goto ie10;
        {
            gm_b16_8 t13;
            if (!((((uint8_t *)&gm_l2))[16] != 0)) goto ie14;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_l6 = t15;
            gm_b16_8 t17 = gm_l6;
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            t13 = t19;
            goto ix14;
ie14:;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_siic7p394dov6));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(12));
            gm_b16_8 t22 = t21;
            t13 = t22;
ix14:;
            gm_l7 = t13;
        }
        {
            gm_tl874t2atnlh3 t23 = gm_l3;
            gm_b16_8 t24 = gm_l7;
            gm_tnpifv20irnph t25;
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
            gm_tabb710d9mq2e t26;
            gm_f_format_3ahqitg((void *)&t26, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcstmd1b5da98l, 372, 31);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t23, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&gm_l4))[12] != 0)) goto ie29;
            gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l4));
            gm_l9 = t30;
            {
                void * t32 = (void *)((uint8_t *)&F.l8);
                gm_tl874t2atnlh3 t33 = gm_l9;
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_shcail1g86pp0));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(25));
                gm_f_add_label_50fj7vv(t32, GM_CONV(gm_cseahvg8v7p0vs, t33), t34);
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            void * t35 = gm_l0;
            gm_b0_1 t36 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t35, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix10;
ie10:;
ix10:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_compile_error_qlau792(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_compile_error_qlau792 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_th2lqbo77pvsc gm_l4; /* msg */
    gm_b16_8 gm_l6; /* t */
    GM_STACK_CHECK(&F, 350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie3;
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srcstmd1b5da98l, 393, 43);
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 144) + 0) + 0));
        if (t8 != ((uint8_t)0ULL)) goto ie3;
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 144) + 0) + 8));
        F.l5 = t9;
        {
            void * t11 = gm_l0;
            uint64_t t12 = gm_l1;
            void * t13 = (void *)((uint8_t *)&F.l5);
            gm_f_const_string_jdjj62s((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), (void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie14;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l6 = t15;
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l3;
            gm_b16_8 t19 = gm_l6;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_b0_1 t21 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t17, t18, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix14;
ie14:;
        {
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = gm_l3;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sqk4m1hc1n4b5));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(48));
            gm_b0_1 t26 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix14:;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_hook_2qtf05o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_const_hook_2qtf05o F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    gm_b16_8 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint64_t gm_l6; /* errors */
    void * gm_l7; /* ex */
    gm_t88rcucg1b7uk gm_l8; /* bad */
    uint32_t gm_l9; /* id */
    GM_STACK_CHECK(&F, 1039u);
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
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        uint64_t t5 = gm_f_error_count_omtt6qd(t4);
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        void * t8 = gm_l2;
        gm_tkn42u7na3t81 t9 = gm_l3;
        void * t10 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t6, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t11;
        if ((GM_LD(void *, ((uint8_t *)&t10)) != 0)) {
            void * t12 = GM_LD(void *, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            gm_t9805qtonbsag t13;
            memset(&t13, 0, sizeof t13);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l7 = t11;
    }
    {
        uint8_t t16 = 0;
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
        uint64_t t20 = gm_f_error_count_omtt6qd(t19);
        uint64_t t21 = gm_l6;
        uint8_t t22 = (!(t20 == t21));
        if (!t22) {
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            void * t25 = gm_l7;
            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + 0));
            uint8_t t27 = gm_f_is_error_8k27gd1(t24, GM_CONV(gm_csguankko4lmhd, t26));
            t16 = t27;
        } else t16 = 1;
        if (!t16) goto ie15;
        {
            gm_t9805qtonbsag t29;
            memset(&t29, 0, sizeof t29);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_b16_8 t32 = gm_l4;
        gm_t6j6smj7qoctu t33;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s9gu0s8i5nn2u));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 0, t34);
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s33hvuvcdjs1b));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(10));
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 16, t35);
        uint8_t t36 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t33) + 0, ((uint8_t *)&t32))) t36 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t33) + 16, ((uint8_t *)&t32))) t36 = 1;
        uint8_t t37 = (uint8_t)(t36);
        if (!t37) goto ie31;
        void * t38 = gm_l0;
        void * t39 = gm_l7;
        uint8_t t40 = gm_f_depends_on_params_khui57h(t38, t39);
        if (!t40) goto ie31;
        {
            void * t42 = gm_l0;
            void * t43 = gm_l2;
            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sn5m7a373k04g));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(44));
            gm_b0_1 t46 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t42, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9805qtonbsag t47;
            memset(&t47, 0, sizeof t47);
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        gm_b16_8 t50 = gm_l4;
        gm_t0uuhaljsf410 t51;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sna4vk4o059d0));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s9gu0s8i5nn2u));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t53);
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s33hvuvcdjs1b));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(10));
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 32, t54);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sbd6ket0ql5ab));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 48, t55);
        uint8_t t56 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t51) + 0, ((uint8_t *)&t50))) t56 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t51) + 16, ((uint8_t *)&t50))) t56 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t51) + 32, ((uint8_t *)&t50))) t56 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t51) + 48, ((uint8_t *)&t50))) t56 = 1;
        uint8_t t57 = (uint8_t)(!t56);
        if (!t57) goto ie49;
        {
            void * t60 = gm_l0;
            void * t61 = gm_l7;
            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t61) + 0));
            gm_tkn42u7na3t81 t63 = gm_f_non_constant_part_itk7b0v(t60, GM_CONV(gm_csguankko4lmhd, t62));
            if (!((((uint8_t *)&t63))[4] != 0)) goto ie59;
            gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t63));
            gm_l8 = t64;
            {
                void * t66 = gm_l0;
                void * t67 = gm_l2;
                gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
                gm_t6j6smj7qoctu t69;
                void * t70 = gm_l0;
                void * t71 = gm_l7;
                gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
                gm_b16_8 t73 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t70, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t69 + 0, t73);
                void * t74 = gm_l0;
                gm_t88rcucg1b7uk t75 = gm_l8;
                gm_b16_8 t76 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t69 + 16, t76);
                gm_tabb710d9mq2e t77;
                gm_f_format_j4sks15((void *)&t77, (void *)NULL, cx, NULL, &t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcstmd1b5da98l, 107, 29);
                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                gm_b0_1 t80 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t66, t68, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t9805qtonbsag t81;
                memset(&t81, 0, sizeof t81);
                uint8_t t82 = 0;
                F.ret = t81;
                goto gm_fret;
            }
            goto ix59;
ie59:;
ix59:;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        void * t83 = gm_l0;
        gm_tjcptv9sagfq8 t84;
        memset((uint8_t *)&t84 + 73, 0, 7);
        gm_b16_8 t85 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t84 + 0, t85);
        gm_tl874t2atnlh3 t86 = gm_l5;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t84 + 16, t86);
        void * t87 = gm_l7;
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t87) + 0));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t84 + 28, t88);
        uint64_t t89 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t84 + 32, t89);
        void * t90 = gm_l2;
        GM_ST(void *, (uint8_t *)&t84 + 40, t90);
        gm_tkn42u7na3t81 t91 = gm_l3;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t84 + 48, t91);
        void * t92 = gm_l0;
        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t92) + 2312));
        GM_ST(uint64_t, (uint8_t *)&t84 + 56, t93);
        void * t94 = gm_l7;
        GM_ST(void *, (uint8_t *)&t84 + 64, t94);
        gm_t9qkh0rsooc8d t95;
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t84 + 72, t95);
        GM_ST(void *, (uint8_t *)&t84 + 80, ((void *)0));
        uint32_t t96 = gm_f_add_const_8u8b1ja((void *)NULL, cx, NULL, t83, &t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t96;
    }
    {
        gm_t9805qtonbsag t97;
        void * t99 = gm_l0;
        uint32_t t100 = gm_l9;
        uint8_t t101 = gm_f_finish_const_tk4l20a((void *)NULL, cx, NULL, t99, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t101) goto ie98;
        uint32_t t103 = gm_l9;
        uint32_t t104 = t103;
        gm_t9805qtonbsag t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t105), t104);
        ((uint8_t *)&t105)[4] = 1;
        t97 = t105;
        goto ix98;
ie98:;
        gm_t9805qtonbsag t107;
        memset(&t107, 0, sizeof t107);
        gm_t9805qtonbsag t108 = t107;
        t97 = t108;
ix98:;
        uint8_t t109 = 0;
        F.ret = t97;
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
inline __attribute__((aligned(4))) void gm_f_const_string_jdjj62s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_const_string_jdjj62s F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* ex */
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[4] = 1;
        void * t9 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t2, t3, t4, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l3;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 2312));
        gm_tjoi8o798ne9k t18;
        gm_f_evaluate_ukvgal2((void *)&t18, (void *)NULL, cx, NULL, t14, t15, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6evimt84lndq t19;
        if (((((uint8_t *)&t18))[48] != 0)) {
            t19 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t18) + 0));
        } else {
            gm_th2lqbo77pvsc t20;
            memset(&t20, 0, sizeof t20);
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        F.l4 = t19;
    }
    {
        void * t22 = gm_l0;
        gm_f_string_bytes_l9peo43((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t22, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)));
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
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_const_value_fr4coi4 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l3; /* id */
    void * gm_l5; /* ex */
    GM_STACK_CHECK(&F, 465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t9805qtonbsag t5 = gm_f_const_id_2mlh0og(t2, GM_CONV(gm_cseahvg8v7p0vs, t3), GM_CONV(gm_csguankko4lmhd, t4));
        uint32_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        uint32_t t9 = gm_l3;
        uint64_t t10 = (uint64_t)t9;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 1968) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcstmd1b5da98l, 223, 10);
        gm_tjcptv9sagfq8 t14 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 1968) + 0)) + t11 * 88));
        F.l4 = t14;
    }
    {
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 72) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; default: goto ma18; }
ma16:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 72) + 0));
        if (t22 != ((uint8_t)2ULL)) goto mn21;
        void * t23 = GM_LD(void *, (((uint8_t *)&F.l4) + 80));
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me15;
mn21:;
ma17:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 72) + 0));
        if (t26 != ((uint8_t)0ULL)) goto mn25;
        {
            uint32_t t28 = gm_l3;
            uint64_t t29 = (uint64_t)t28;
            uint64_t t30 = (uintptr_t)t29;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 1968) + 0) + 8);
            if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_srcstmd1b5da98l, 227, 13);
            gm_t9qkh0rsooc8d t33;
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 1968) + 0)) + t30 * 88) + 72), t33);
        }
        {
            void * t34 = gm_l0;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
            void * t36 = GM_LD(void *, (((uint8_t *)&F.l4) + 40));
            gm_tkn42u7na3t81 t37 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l4) + 48));
            void * t38 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t34, t35, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t39;
            if ((GM_LD(void *, ((uint8_t *)&t38)) != 0)) {
                void * t40 = GM_LD(void *, (((uint8_t *)&t38) + 0));
                t39 = t40;
            } else {
                {
                    uint32_t t42 = gm_l3;
                    uint64_t t43 = (uint64_t)t42;
                    uint64_t t44 = (uintptr_t)t43;
                    void * t45 = gm_l0;
                    uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 1968) + 0) + 8);
                    if (t44 >= (uintptr_t)(t46)) gm_trap_index(t44, t46, gm_srcstmd1b5da98l, 229, 17);
                    gm_t9qkh0rsooc8d t47;
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t45) + 1968) + 0)) + t44 * 88) + 72), t47);
                }
                uint8_t t48 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l5 = t39;
        }
        {
            uint32_t t49 = gm_l3;
            uint64_t t50 = (uint64_t)t49;
            uint64_t t51 = (uintptr_t)t50;
            void * t52 = gm_l0;
            uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 1968) + 0) + 8);
            if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_srcstmd1b5da98l, 232, 13);
            void * t54 = gm_l5;
            GM_ST(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 1968) + 0)) + t51 * 88) + 64), t54);
        }
        {
            void * t56 = gm_l0;
            uint32_t t57 = gm_l3;
            uint8_t t58 = gm_f_finish_const_tk4l20a((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = (uint8_t)!t58;
            if (!t59) goto ie55;
            {
                uint8_t t61 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            goto ix55;
ie55:;
ix55:;
        }
        {
            uint32_t t62 = gm_l3;
            uint64_t t63 = (uint64_t)t62;
            uint64_t t64 = (uintptr_t)t63;
            void * t65 = gm_l0;
            uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 1968) + 0) + 8);
            if (t64 >= (uintptr_t)(t66)) gm_trap_index(t64, t66, gm_srcstmd1b5da98l, 234, 20);
            void * t67 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 1968) + 0)) + t64 * 88) + 80));
            uint8_t t68 = 0;
            F.ret = t67;
            goto gm_fret;
        }
        goto me15;
mn25:;
ma18:;
        uint8_t t70 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me15;
mn69:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 224, 5);
me15:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_default_const_k60bpu4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_default_const_k60bpu4 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 375u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t4) + 72));
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_t9805qtonbsag t7 = gm_f_const_id_2mlh0og(t3, GM_CONV(gm_cseahvg8v7p0vs, t5), GM_CONV(gm_csguankko4lmhd, t6));
        if (!((((uint8_t *)&t7))[4] != 0)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&t7));
        gm_l4 = t8;
        {
            uint32_t t10 = gm_l4;
            uint64_t t11 = (uint64_t)t10;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 1968) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcstmd1b5da98l, 245, 14);
            gm_tjcptv9sagfq8 t15 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 1968) + 0)) + t12 * 88));
            F.l5 = t15;
        }
        {
            uint8_t t17 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 72) + 0) == ((uint8_t)0ULL)));
            if (!t17) goto ie16;
            {
                void * t19 = gm_l0;
                void * t20 = gm_l2;
                gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
                gm_t88rcucg1b7uk t22 = gm_l3;
                void * t23 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t19, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t24;
                if ((GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto mn26;
                t24 = ((uint8_t)0);
                goto me25;
mn26:;
                t24 = ((uint8_t)1);
                goto me25;
mn27:;
                gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 246, 43);
me25:;
                uint8_t t28 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            uint8_t t30 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 72) + 0) == ((uint8_t)1ULL)));
            if (!t30) goto ie29;
            {
                void * t32 = gm_l0;
                void * t33 = gm_l2;
                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sp5mk8jqu8dn0));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(69));
                gm_b0_1 t36 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t32, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t37 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            uint8_t t38 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 72) + 0) == ((uint8_t)2ULL)));
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        void * t42 = gm_l2;
        gm_t88rcucg1b7uk t43 = gm_l3;
        gm_tkn42u7na3t81 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44), t43);
        ((uint8_t *)&t44)[4] = 1;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sna4vk4o059d0));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(9));
        void * t46 = gm_l2;
        gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 72));
        gm_t9805qtonbsag t48 = gm_f_const_hook_2qtf05o((void *)NULL, cx, NULL, t40, t41, t42, t44, t45, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t49;
        if (((((uint8_t *)&t48))[4] != 0)) goto mn51;
        t49 = ((uint8_t)0);
        goto me50;
mn51:;
        t49 = ((uint8_t)1);
        goto me50;
mn52:;
        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 253, 12);
me50:;
        uint8_t t53 = 0;
        F.ret = t49;
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
extern __attribute__((aligned(4))) uint8_t gm_f_depends_on_params_khui57h(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_escapes_0j96sk5(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_escapes_0j96sk5 F;
    gm_tkaapcii86udk gm_l1; /* pl */
    gm_to6glrduves6o gm_l2; /* sl */
    void * gm_l3; /* a */
    uint8_t gm_l4; /* found */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l8; /* p */
    void * gm_l9; /* p */
    void * gm_l10; /* p */
    void * gm_l11; /* payload */
    void * gm_l12; /* p */
    GM_STACK_CHECK(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma3; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; default: goto ma8; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t12 != ((uint8_t)4ULL)) goto mn11;
    gm_tkaapcii86udk t13 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l0) + 16));
    gm_l1 = t13;
    void * t14 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 48));
    uint8_t t16 = (uint8_t)!t15;
    t1 = t16;
    goto me2;
mn11:;
ma4:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t18 != ((uint8_t)5ULL)) goto mn17;
    gm_to6glrduves6o t19 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l0) + 16));
    gm_l2 = t19;
    void * t20 = GM_LD(void *, ((((uint8_t *)&gm_l2) + 0) + 0));
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 48));
    uint8_t t22 = (uint8_t)!t21;
    t1 = t22;
    goto me2;
mn17:;
ma5:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t24 != ((uint8_t)3ULL)) goto mn23;
    void * t25 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
    gm_l3 = t25;
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            void * t28 = gm_l3;
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 0));
            gm_l5 = t29;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t31 = gm_l6;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l6;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t6evimt84lndq t41 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t37 * 48));
                    F.l7 = t41;
                    {
                        uint8_t t44 = gm_f_escapes_0j96sk5(&(*(gm_t6evimt84lndq *)((uint8_t *)&F.l7)));
                        if (!t44) goto ie43;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        {
                            goto lb30;
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 155, 9);
me39:;
                }
lc30:;
                {
                    uint64_t t47 = gm_l6;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l6 = t50;
                }
            }
lb30:;
        }
    }
    uint8_t t51 = gm_l4;
    uint8_t t52 = t51;
    t1 = t52;
    goto me2;
mn23:;
ma6:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t56 != ((uint8_t)7ULL)) goto pa55;
    void * t57 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
    gm_l8 = t57;
    goto po54;
pa55:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t59 != ((uint8_t)8ULL)) goto pa58;
    void * t60 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
    gm_l8 = t60;
    goto po54;
pa58:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t62 != ((uint8_t)9ULL)) goto pa61;
    void * t63 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
    gm_l8 = t63;
    goto po54;
pa61:;
    goto mn53;
po54:;
    void * t64 = gm_l8;
    uint8_t t65 = gm_f_escapes_0j96sk5(&(*(gm_t6evimt84lndq *)((uint8_t *)t64)));
    t1 = t65;
    goto me2;
mn53:;
ma7:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t67 != ((uint8_t)10ULL)) goto mn66;
    void * t68 = GM_LD(void *, ((((uint8_t *)&F.l0) + 16) + 8));
    gm_l11 = t68;
    uint8_t t69;
    void * t71 = gm_l11;
    if (!(GM_LD(void *, ((uint8_t *)&t71)) != 0)) goto ie70;
    void * t72 = GM_LD(void *, ((uint8_t *)&t71));
    gm_l12 = t72;
    void * t74 = gm_l12;
    uint8_t t75 = gm_f_escapes_0j96sk5(&(*(gm_t6evimt84lndq *)((uint8_t *)t74)));
    uint8_t t76 = t75;
    t69 = t76;
    goto ix70;
ie70:;
    uint8_t t78 = ((uint8_t)0);
    t69 = t78;
ix70:;
    t1 = t69;
    goto me2;
mn66:;
ma8:;
    t1 = ((uint8_t)0);
    goto me2;
mn79:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 150, 36);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_escapes_0j96sk5_c(gm_t6evimt84lndq a0) { return gm_f_escapes_0j96sk5(&a0); }
inline __attribute__((aligned(4))) void gm_f_eval_typed_m82ie8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_eval_typed_m82ie8p F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* ex */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_l3;
        gm_tkn42u7na3t81 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6), t5);
        ((uint8_t *)&t6)[4] = 1;
        void * t7 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t2, t3, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t8;
        if ((GM_LD(void *, ((uint8_t *)&t7)) != 0)) {
            void * t9 = GM_LD(void *, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_tjoi8o798ne9k t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l4 = t8;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l4;
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 2312));
        gm_f_evaluate_ukvgal2((void *)&(*(gm_tjoi8o798ne9k *)gm_out), (void *)NULL, cx, NULL, t12, t13, t15);
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
inline __attribute__((aligned(4))) void gm_f_evaluate_ukvgal2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2) {
    struct gm_fr_evaluate_ukvgal2 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* budget */
    void * gm_l4; /* t */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l6; /* what */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_mentions_error_37knu7r(t5, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) {
            void * t9 = gm_l0;
            void * t10 = gm_l1;
            uint8_t t11 = gm_f_prepare_0r9bic7((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = (uint8_t)!t11;
            t3 = t12;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_tjoi8o798ne9k t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = gm_l1;
        uint64_t t18 = gm_l2;
        gm_tr7bagvonfqmg t19;
        gm_f_run_rq2eqmk((void *)&t19, (void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma24; default: goto ma25; }
ma21:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t28 != ((uint8_t)0ULL)) goto mn27;
        F.l3 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t19) + 16));
        {
            uint8_t t31 = gm_f_escapes_0j96sk5(&(*(gm_t6evimt84lndq *)((uint8_t *)&F.l3)));
            uint8_t t32 = (uint8_t)!t31;
            if (!t32) goto ie30;
            {
                const uint8_t *t34 = ((uint8_t *)&F.l3);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 52, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 56, 0);
                ((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out))[48] = 1;
                memmove((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 0, t34, 48);
                goto gm_fdone;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t35 = gm_l0;
            void * t36 = gm_l1;
            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
            gm_b16_8 t38 = GM_LD(gm_b16_8, gm_csd3p6j3fi7);
            gm_tnpifv20irnph t39;
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
            gm_tabb710d9mq2e t40;
            gm_f_format_uthdi26((void *)&t40, (void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcstmd1b5da98l, 176, 29);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_b0_1 t43 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t35, t37, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me20;
mn27:;
ma22:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t45 != ((uint8_t)1ULL)) goto mn44;
        void * t46 = GM_LD(void *, (((uint8_t *)&t19) + 16));
        gm_l4 = t46;
        void * t47 = gm_l0;
        void * t48 = gm_l1;
        void * t49 = gm_l4;
        gm_b0_1 t50 = gm_f_report_trap_1kollts((void *)NULL, cx, NULL, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me20;
mn44:;
ma23:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t52 != ((uint8_t)2ULL)) goto mn51;
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&t19) + 16) + 0));
        gm_l5 = t53;
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((((uint8_t *)&t19) + 16) + 16));
        gm_l6 = t54;
        void * t55 = gm_l0;
        gm_tl874t2atnlh3 t56 = gm_l5;
        gm_b16_8 t57 = gm_l6;
        gm_b0_1 t58 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t55, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me20;
mn51:;
ma24:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t60 != ((uint8_t)3ULL)) goto mn59;
        goto me20;
mn59:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 173, 5);
me20:;
    }
    {
        gm_tjoi8o798ne9k t62;
        memset(&t62, 0, sizeof t62);
        uint8_t t63 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_finish_const_tk4l20a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_finish_const_tk4l20a F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    void * gm_l3; /* ex */
    gm_t9qkh0rsooc8d gm_l4; /* state */
    GM_STACK_CHECK(&F, 470u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1968) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcstmd1b5da98l, 137, 10);
        gm_tjcptv9sagfq8 t7 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1968) + 0)) + t4 * 88));
        F.l2 = t7;
    }
    {
        void * t8 = GM_LD(void *, (((uint8_t *)&F.l2) + 64));
        void * t9;
        if ((GM_LD(void *, ((uint8_t *)&t8)) != 0)) {
            void * t10 = GM_LD(void *, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1968) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcstmd1b5da98l, 139, 5);
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1968) + 0)) + t14 * 88) + 72), t17);
    }
    {
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)3ULL));
        gm_l4 = t18;
    }
    {
        void * t20 = gm_l0;
        void * t21 = gm_l3;
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 56));
        gm_tjoi8o798ne9k t23;
        gm_f_evaluate_ukvgal2((void *)&t23, (void *)NULL, cx, NULL, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t23))[48] != 0)) goto ie19;
        F.l5 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t23) + 0));
        {
            uint32_t t25 = gm_l1;
            uint64_t t26 = (uint64_t)t25;
            uint64_t t27 = (uintptr_t)t26;
            void * t28 = gm_l0;
            uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 1968) + 0) + 8);
            if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srcstmd1b5da98l, 142, 9);
            uint8_t *t30 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 1968) + 0)) + t27 * 88) + 80);
            gm_tjoi8o798ne9k t31;
            memset((uint8_t *)&t31 + 49, 0, 15);
            memcpy((uint8_t *)&t31 + 0, ((uint8_t *)&F.l5), 48);
            GM_ST(uint8_t, (uint8_t *)&t31 + 48, ((uint8_t)1));
            gm_tq7s98ihonkju t32 = gm_f_new_15vqrvd((void *)NULL, cx, NULL, &t31, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ke6nl7d0rco)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcstmd1b5da98l, 142, 42);
            void * t33 = GM_LD(void *, (((uint8_t *)&t32) + 8));
            GM_ST(void *, t30, t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
            gm_l4 = t34;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        uint32_t t35 = gm_l1;
        uint64_t t36 = (uint64_t)t35;
        uint64_t t37 = (uintptr_t)t36;
        void * t38 = gm_l0;
        uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 1968) + 0) + 8);
        if (t37 >= (uintptr_t)(t39)) gm_trap_index(t37, t39, gm_srcstmd1b5da98l, 145, 5);
        gm_t9qkh0rsooc8d t40 = gm_l4;
        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t38) + 1968) + 0)) + t37 * 88) + 72), t40);
    }
    {
        uint8_t t41 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)2ULL)));
        uint8_t t42 = 0;
        F.ret = t41;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6qb25nt(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_6qb25nt F;
    void * gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t7 != ((uint8_t)9ULL)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 35, 12);
me5:;
        if (!t4) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_array_length_f852tll *)((&F.h)->up))->l5), ((uint8_t)1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t11 = 0;
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_array_length_f852tll *)((&F.h)->up))->l0));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
        uint8_t t16 = gm_f_has_param_u9t0efs(t13, GM_CONV(gm_csguankko4lmhd, t15));
        if (!t16) {
            void * t17 = gm_l0;
            uint8_t t18;
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma21; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma24; case ((uint8_t)17ULL): goto ma24; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma24; case ((uint8_t)23ULL): goto ma24; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma24; case ((uint8_t)26ULL): goto ma24; case ((uint8_t)27ULL): goto ma24; case ((uint8_t)28ULL): goto ma24; case ((uint8_t)29ULL): goto ma24; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma24; case ((uint8_t)32ULL): goto ma24; case ((uint8_t)33ULL): goto ma24; case ((uint8_t)34ULL): goto ma24; case ((uint8_t)35ULL): goto ma24; case ((uint8_t)36ULL): goto ma24; case ((uint8_t)37ULL): goto ma24; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma24; case ((uint8_t)41ULL): goto ma24; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma24; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma24; case ((uint8_t)46ULL): goto ma24; case ((uint8_t)47ULL): goto ma24; case ((uint8_t)48ULL): goto ma24; case ((uint8_t)49ULL): goto ma24; case ((uint8_t)50ULL): goto ma24; case ((uint8_t)51ULL): goto ma24; case ((uint8_t)52ULL): goto ma24; case ((uint8_t)53ULL): goto ma24; case ((uint8_t)54ULL): goto ma24; case ((uint8_t)55ULL): goto ma24; case ((uint8_t)56ULL): goto ma24; case ((uint8_t)57ULL): goto ma24; case ((uint8_t)58ULL): goto ma24; default: goto ma24; }
ma20:;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t28 != ((uint8_t)10ULL)) goto mn27;
            t18 = ((uint8_t)1);
            goto me19;
mn27:;
ma21:;
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t30 != ((uint8_t)11ULL)) goto mn29;
            t18 = ((uint8_t)1);
            goto me19;
mn29:;
ma22:;
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t32 != ((uint8_t)12ULL)) goto mn31;
            t18 = ((uint8_t)1);
            goto me19;
mn31:;
ma23:;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t34 != ((uint8_t)13ULL)) goto mn33;
            t18 = ((uint8_t)1);
            goto me19;
mn33:;
ma24:;
            t18 = ((uint8_t)0);
            goto me19;
mn35:;
ma25:;
            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 36, 37);
me19:;
            t11 = t18;
        } else t11 = 1;
        if (!t11) goto ie10;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_array_length_f852tll *)((&F.h)->up))->l6), ((uint8_t)1));
        }
        goto ix10;
ie10:;
ix10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n7mt18r(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_n7mt18r F;
    void * gm_l0; /* x */
    gm_t88rcucg1b7uk gm_l1; /* of */
    GM_STACK_CHECK_SHALLOW(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_depends_on_params_khui57h *)((&F.h)->up))->l0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_has_param_u9t0efs(t5, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) {
            void * t9 = gm_l0;
            uint8_t t10;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            switch (t18) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma16; case ((uint8_t)15ULL): goto ma16; case ((uint8_t)16ULL): goto ma16; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma16; case ((uint8_t)22ULL): goto ma16; case ((uint8_t)23ULL): goto ma16; case ((uint8_t)24ULL): goto ma16; case ((uint8_t)25ULL): goto ma16; case ((uint8_t)26ULL): goto ma16; case ((uint8_t)27ULL): goto ma16; case ((uint8_t)28ULL): goto ma16; case ((uint8_t)29ULL): goto ma16; case ((uint8_t)30ULL): goto ma16; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma16; case ((uint8_t)33ULL): goto ma16; case ((uint8_t)34ULL): goto ma16; case ((uint8_t)35ULL): goto ma16; case ((uint8_t)36ULL): goto ma16; case ((uint8_t)37ULL): goto ma16; case ((uint8_t)38ULL): goto ma16; case ((uint8_t)39ULL): goto ma16; case ((uint8_t)40ULL): goto ma16; case ((uint8_t)41ULL): goto ma16; case ((uint8_t)42ULL): goto ma16; case ((uint8_t)43ULL): goto ma16; case ((uint8_t)44ULL): goto ma16; case ((uint8_t)45ULL): goto ma16; case ((uint8_t)46ULL): goto ma16; case ((uint8_t)47ULL): goto ma16; case ((uint8_t)48ULL): goto ma16; case ((uint8_t)49ULL): goto ma16; case ((uint8_t)50ULL): goto ma16; case ((uint8_t)51ULL): goto ma16; case ((uint8_t)52ULL): goto ma16; case ((uint8_t)53ULL): goto ma16; case ((uint8_t)54ULL): goto ma16; case ((uint8_t)55ULL): goto ma16; case ((uint8_t)56ULL): goto ma16; case ((uint8_t)57ULL): goto ma16; case ((uint8_t)58ULL): goto ma16; default: goto ma16; }
ma12:;
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t20 != ((uint8_t)9ULL)) goto mn19;
            t10 = ((uint8_t)1);
            goto me11;
mn19:;
ma13:;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t22 != ((uint8_t)12ULL)) goto mn21;
            t10 = ((uint8_t)1);
            goto me11;
mn21:;
ma14:;
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t24 != ((uint8_t)13ULL)) goto mn23;
            t10 = ((uint8_t)1);
            goto me11;
mn23:;
ma15:;
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t26 != ((uint8_t)11ULL)) goto mn25;
            t10 = ((uint8_t)1);
            goto me11;
mn25:;
ma16:;
            t10 = ((uint8_t)0);
            goto me11;
mn27:;
ma17:;
            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 412, 37);
me11:;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_depends_on_params_khui57h *)((&F.h)->up))->l2), ((uint8_t)1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t30 = gm_l0;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
        if (t31 != ((uint8_t)10ULL)) goto ie29;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t30) + 8) + 8) + 0));
        gm_l1 = t32;
        void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_depends_on_params_khui57h *)((&F.h)->up))->l0));
        void * t34 = (void *)(((uint8_t *)t33) + 8);
        gm_t88rcucg1b7uk t35 = gm_l1;
        uint8_t t36 = gm_f_has_param_u9t0efs(t34, GM_CONV(gm_csguankko4lmhd, t35));
        if (!t36) goto ie29;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_depends_on_params_khui57h *)((&F.h)->up))->l2), ((uint8_t)1));
        }
        goto ix29;
ie29:;
ix29:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_len_ty_nkbf2gl(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_len_ty_nkbf2gl F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* p */
    uint64_t gm_l4; /* n */
    void * gm_l5; /* x */
    gm_ts6oqppjh0ba3 gm_l6; /* op */
    void * gm_l7; /* lhs */
    void * gm_l8; /* rhs */
    gm_t9qkh0rsooc8d gm_l9; /* lop */
    gm_t88rcucg1b7uk gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* b */
    GM_STACK_CHECK(&F, 730u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)9ULL)) goto mn11;
        uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t13;
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        uint32_t t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t18), t17);
        ((uint8_t *)&t18)[4] = 1;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)0ULL)) goto mn20;
        gm_tbci5ospk5nnn t22 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t2) + 8) + 8));
        F.l3 = t22;
        {
            gm_tf5srh4mp4b3i t24 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
            uint64_t t25;
            if (((((uint8_t *)&t24))[8] != 0)) {
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t24) + 0));
                t25 = t26;
            } else {
                gm_tkn42u7na3t81 t27;
                memset(&t27, 0, sizeof t27);
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
            gm_l4 = t25;
        }
        {
            void * t29 = gm_l0;
            void * t30 = (void *)(((uint8_t *)t29) + 8);
            void * t31 = gm_l0;
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            uint64_t t33 = gm_l4;
            gm_t428ue2u3hf5g t34;
            gm_f_usize_key_rs1gk1r((void *)&t34, (void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t35 + 8), t34);
            gm_t88rcucg1b7uk t36 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t30, &t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37), t36);
            ((uint8_t *)&t37)[4] = 1;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto me3;
mn20:;
ma6:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)32ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
        if (t41 != ((uint8_t)0ULL)) goto mn39;
        void * t42 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t42;
        gm_tkn42u7na3t81 t43;
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        void * t47 = gm_l5;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 0));
        uint8_t t49 = gm_f_is_signed_integer_s66djm7(t46, GM_CONV(gm_csguankko4lmhd, t48));
        if (!t49) goto ie44;
        gm_tkn42u7na3t81 t51;
        memset(&t51, 0, sizeof t51);
        gm_tkn42u7na3t81 t52 = t51;
        t43 = t52;
        goto ix44;
ie44:;
        void * t54 = gm_l0;
        void * t55 = gm_l5;
        gm_tkn42u7na3t81 t56 = gm_f_len_ty_nkbf2gl((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t57 = t56;
        t43 = t57;
ix44:;
        uint8_t t58 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me3;
mn39:;
ma7:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)30ULL)) goto mn59;
        gm_ts6oqppjh0ba3 t61 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t61;
        void * t62 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t62;
        void * t63 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l8 = t63;
        {
            void * t66 = gm_l0;
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            void * t68 = gm_l1;
            gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 0));
            gm_t53at80pfdus3 t70;
            gm_f_kind_jcnt4hm((void *)&t70, t67, GM_CONV(gm_csguankko4lmhd, t69));
            uint8_t t71 = (!(GM_LD(uint8_t, ((uint8_t *)&t70) + 0) == ((uint8_t)1ULL)));
            if (!t71) goto ie65;
            {
                gm_tkn42u7na3t81 t73;
                memset(&t73, 0, sizeof t73);
                uint8_t t74 = 0;
                F.ret = t73;
                goto gm_fret;
            }
            goto ix65;
ie65:;
ix65:;
        }
        {
            gm_t9qkh0rsooc8d t75;
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            switch (t89) { case ((uint8_t)0ULL): goto ma77; case ((uint8_t)1ULL): goto ma78; case ((uint8_t)2ULL): goto ma79; case ((uint8_t)3ULL): goto ma80; case ((uint8_t)4ULL): goto ma81; case ((uint8_t)5ULL): goto ma82; case ((uint8_t)6ULL): goto ma83; case ((uint8_t)7ULL): goto ma84; case ((uint8_t)8ULL): goto ma85; case ((uint8_t)9ULL): goto ma86; case ((uint8_t)10ULL): goto ma87; case ((uint8_t)11ULL): goto ma87; case ((uint8_t)12ULL): goto ma87; case ((uint8_t)13ULL): goto ma87; default: goto ma87; }
ma77:;
            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t91 != ((uint8_t)0ULL)) goto mn90;
            uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 1) + 0));
            if (t92 != ((uint8_t)0ULL)) goto mn90;
            gm_t9qkh0rsooc8d t93;
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)0ULL));
            t75 = t93;
            goto me76;
mn90:;
ma78:;
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t95 != ((uint8_t)1ULL)) goto mn94;
            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 1) + 0));
            if (t96 != ((uint8_t)0ULL)) goto mn94;
            gm_t9qkh0rsooc8d t97;
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)1ULL));
            t75 = t97;
            goto me76;
mn94:;
ma79:;
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t99 != ((uint8_t)2ULL)) goto mn98;
            uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 1) + 0));
            if (t100 != ((uint8_t)0ULL)) goto mn98;
            gm_t9qkh0rsooc8d t101;
            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)2ULL));
            t75 = t101;
            goto me76;
mn98:;
ma80:;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t103 != ((uint8_t)3ULL)) goto mn102;
            uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)&gm_l6) + 1) + 0));
            if (t104 != ((uint8_t)0ULL)) goto mn102;
            gm_t9qkh0rsooc8d t105;
            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)5ULL));
            t75 = t105;
            goto me76;
mn102:;
ma81:;
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t107 != ((uint8_t)4ULL)) goto mn106;
            gm_t9qkh0rsooc8d t108;
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)3ULL));
            t75 = t108;
            goto me76;
mn106:;
ma82:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t110 != ((uint8_t)5ULL)) goto mn109;
            gm_t9qkh0rsooc8d t111;
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)4ULL));
            t75 = t111;
            goto me76;
mn109:;
ma83:;
            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t113 != ((uint8_t)6ULL)) goto mn112;
            gm_t9qkh0rsooc8d t114;
            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)6ULL));
            t75 = t114;
            goto me76;
mn112:;
ma84:;
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t116 != ((uint8_t)7ULL)) goto mn115;
            gm_t9qkh0rsooc8d t117;
            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)7ULL));
            t75 = t117;
            goto me76;
mn115:;
ma85:;
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t119 != ((uint8_t)8ULL)) goto mn118;
            gm_t9qkh0rsooc8d t120;
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)8ULL));
            t75 = t120;
            goto me76;
mn118:;
ma86:;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (t122 != ((uint8_t)9ULL)) goto mn121;
            gm_t9qkh0rsooc8d t123;
            GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)9ULL));
            t75 = t123;
            goto me76;
mn121:;
ma87:;
            gm_tkn42u7na3t81 t125;
            memset(&t125, 0, sizeof t125);
            uint8_t t126 = 0;
            F.ret = t125;
            goto gm_fret;
            goto me76;
mn124:;
ma88:;
            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 76, 32);
me76:;
            gm_l9 = t75;
        }
        {
            void * t127 = gm_l0;
            void * t128 = gm_l7;
            gm_tkn42u7na3t81 t129 = gm_f_len_ty_nkbf2gl((void *)NULL, cx, NULL, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t130;
            if (((((uint8_t *)&t129))[4] != 0)) {
                gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t129) + 0));
                t130 = t131;
            } else {
                gm_tkn42u7na3t81 t132;
                memset(&t132, 0, sizeof t132);
                uint8_t t133 = 0;
                F.ret = t132;
                goto gm_fret;
            }
            gm_l10 = t130;
        }
        {
            void * t134 = gm_l0;
            void * t135 = gm_l8;
            gm_tkn42u7na3t81 t136 = gm_f_len_ty_nkbf2gl((void *)NULL, cx, NULL, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t137;
            if (((((uint8_t *)&t136))[4] != 0)) {
                gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t136) + 0));
                t137 = t138;
            } else {
                gm_tkn42u7na3t81 t139;
                memset(&t139, 0, sizeof t139);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            gm_l11 = t137;
        }
        {
            void * t141 = gm_l0;
            void * t142 = (void *)(((uint8_t *)t141) + 8);
            gm_t9qkh0rsooc8d t143 = gm_l9;
            gm_t88rcucg1b7uk t144 = gm_l10;
            gm_t88rcucg1b7uk t145 = gm_l11;
            gm_t88rcucg1b7uk t146 = gm_f_len_expr_i134fvg((void *)NULL, cx, NULL, t142, t143, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t147), t146);
            ((uint8_t *)&t147)[4] = 1;
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
        goto me3;
mn59:;
ma8:;
        gm_tkn42u7na3t81 t150;
        memset(&t150, 0, sizeof t150);
        uint8_t t151 = 0;
        F.ret = t150;
        goto gm_fret;
        goto me3;
mn149:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 67, 5);
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_const_b6dhg2g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_local_const_b6dhg2g F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* init */
    gm_tkn42u7na3t81 gm_l3; /* annot */
    gm_b16_8 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_t2lqu8jsj511d gm_l6; /* m */
    void * gm_l7; /* checked */
    uint8_t gm_l8; /* ok */
    void * gm_l9; /* ex */
    gm_t2lqu8jsj511d gm_l10; /* m2 */
    void * gm_l13; /* t */
    gm_tl874t2atnlh3 gm_l14; /* at */
    gm_b16_8 gm_l15; /* what */
    uint32_t gm_l16; /* id */
    GM_STACK_CHECK(&F, 1453u);
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
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_in_generic_scope_rvb92ab(t3);
        if (!t4) goto ie2;
        {
            gm_t9805qtonbsag t6;
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
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
        gm_t2lqu8jsj511d t11 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_vedqs3l(t10));
        gm_l6 = t11;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = gm_l1;
        void * t14 = gm_l2;
        gm_tkn42u7na3t81 t15 = gm_l3;
        void * t16 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t12, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t16;
    }
    {
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 8));
        gm_t2lqu8jsj511d t21 = gm_l6;
        gm_b16_8 t22 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        uint8_t t23 = gm_f_any_since_o1kpm84(t20, GM_CONV(gm_csc7lc6jlkqkuq, t21), t22);
        if (!t23) goto ie17;
        {
            gm_t9805qtonbsag t25;
            memset(&t25, 0, sizeof t25);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 8));
        uint64_t t30 = gm_f_error_count_omtt6qd(t29);
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint8_t t32 = ((t30 == t31));
        gm_l8 = t32;
    }
    {
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 8));
        gm_t2lqu8jsj511d t36 = gm_l6;
        gm_f_reset_dg0kphe(t35, GM_CONV(gm_csc7lc6jlkqkuq, t36));
    }
    {
        void * t37 = gm_l7;
        void * t38;
        if ((GM_LD(void *, ((uint8_t *)&t37)) != 0)) {
            void * t39 = GM_LD(void *, (((uint8_t *)&t37) + 0));
            t38 = t39;
        } else {
            gm_t9805qtonbsag t40;
            memset(&t40, 0, sizeof t40);
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        gm_l9 = t38;
    }
    {
        uint8_t t43 = 0;
        uint8_t t44 = 0;
        uint8_t t45 = gm_l8;
        uint8_t t46 = (uint8_t)!t45;
        if (!t46) {
            void * t47 = gm_l0;
            void * t48 = (void *)(((uint8_t *)t47) + 8);
            void * t49 = gm_l9;
            gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t49) + 0));
            uint8_t t51 = gm_f_is_error_8k27gd1(t48, GM_CONV(gm_csguankko4lmhd, t50));
            t44 = t51;
        } else t44 = 1;
        if (!t44) {
            void * t52 = gm_l0;
            void * t53 = gm_l9;
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 0));
            gm_tkn42u7na3t81 t55 = gm_f_non_constant_part_itk7b0v(t52, GM_CONV(gm_csguankko4lmhd, t54));
            uint8_t t56;
            if (((((uint8_t *)&t55))[4] != 0)) goto mn58;
            t56 = ((uint8_t)0);
            goto me57;
mn58:;
            t56 = ((uint8_t)1);
            goto me57;
mn59:;
            gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 299, 43);
me57:;
            t43 = t56;
        } else t43 = 1;
        if (!t43) goto ie42;
        {
            gm_t9805qtonbsag t61;
            memset(&t61, 0, sizeof t61);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 8));
        gm_t2lqu8jsj511d t66 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_vedqs3l(t65));
        gm_l10 = t66;
    }
    {
        void * t68 = gm_l0;
        void * t69 = gm_l9;
        uint8_t t70 = gm_f_prepare_0r9bic7((void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = (uint8_t)!t70;
        if (!t71) goto ie67;
        {
            void * t74 = gm_l0;
            void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 8));
            gm_t2lqu8jsj511d t77 = gm_l10;
            gm_b16_8 t78 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
            uint8_t t79 = gm_f_any_since_o1kpm84(t76, GM_CONV(gm_csc7lc6jlkqkuq, t77), t78);
            uint8_t t80 = (uint8_t)!t79;
            if (!t80) goto ie73;
            {
                void * t82 = gm_l0;
                void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
                void * t84 = GM_LD(void *, (((uint8_t *)t83) + 8));
                gm_t2lqu8jsj511d t85 = gm_l10;
                gm_f_reset_dg0kphe(t84, GM_CONV(gm_csc7lc6jlkqkuq, t85));
            }
            goto ix73;
ie73:;
ix73:;
        }
        {
            gm_t9805qtonbsag t86;
            memset(&t86, 0, sizeof t86);
            uint8_t t87 = 0;
            F.ret = t86;
            goto gm_fret;
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
        memset(&F.l11, 0, sizeof F.l11);
        GM_ST(uint8_t, (uint8_t *)&F.l11 + 0, ((uint8_t)6ULL));
    }
    {
        void * t88 = gm_l0;
        void * t89 = gm_l9;
        void * t90 = gm_l0;
        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t90) + 2312));
        gm_tr7bagvonfqmg t92;
        gm_f_run_rq2eqmk((void *)&t92, (void *)NULL, cx, NULL, t88, t89, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&t92) + 0));
        switch (t99) { case ((uint8_t)0ULL): goto ma94; case ((uint8_t)1ULL): goto ma95; case ((uint8_t)2ULL): goto ma96; case ((uint8_t)3ULL): goto ma97; default: goto ma98; }
ma94:;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t92) + 0));
        if (t101 != ((uint8_t)0ULL)) goto mn100;
        F.l12 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t92) + 16));
        {
            uint8_t t104 = gm_f_escapes_0j96sk5(&(*(gm_t6evimt84lndq *)((uint8_t *)&F.l12)));
            if (!t104) goto ie103;
            {
                void * t106 = gm_l0;
                void * t107 = gm_l9;
                gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 72));
                gm_b16_8 t109 = GM_LD(gm_b16_8, gm_csd3p6j3fi7);
                gm_tnpifv20irnph t110;
                GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t109);
                gm_tabb710d9mq2e t111;
                gm_f_format_uthdi26((void *)&t111, (void *)NULL, cx, NULL, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcstmd1b5da98l, 310, 34);
                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                gm_b0_1 t114 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t106, t108, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t9805qtonbsag t115;
                memset(&t115, 0, sizeof t115);
                uint8_t t116 = 0;
                F.ret = t115;
                goto gm_fret;
            }
            goto ix103;
ie103:;
ix103:;
        }
        {
            gm_t6evimt84lndq t117 = F.l12;
            F.l11 = t117;
        }
        goto me93;
mn100:;
ma95:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t92) + 0));
        if (t119 != ((uint8_t)1ULL)) goto mn118;
        void * t120 = GM_LD(void *, (((uint8_t *)&t92) + 16));
        gm_l13 = t120;
        {
            void * t123 = gm_l13;
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 0));
            gm_b16_8 t125;
            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_s8ta4feq24fk9));
            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(11));
            uint8_t t126 = gm_f_contains_gkki4kq(t124, t125);
            uint8_t t127 = (uint8_t)!t126;
            if (!t127) goto ie122;
            {
                void * t129 = gm_l0;
                void * t130 = gm_l9;
                void * t131 = gm_l13;
                gm_b0_1 t132 = gm_f_report_trap_1kollts((void *)NULL, cx, NULL, t129, t130, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix122;
ie122:;
ix122:;
        }
        {
            gm_t9805qtonbsag t133;
            memset(&t133, 0, sizeof t133);
            uint8_t t134 = 0;
            F.ret = t133;
            goto gm_fret;
        }
        goto me93;
mn118:;
ma96:;
        uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&t92) + 0));
        if (t136 != ((uint8_t)2ULL)) goto mn135;
        gm_tl874t2atnlh3 t137 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&t92) + 16) + 0));
        gm_l14 = t137;
        gm_b16_8 t138 = GM_LD(gm_b16_8, ((((uint8_t *)&t92) + 16) + 16));
        gm_l15 = t138;
        {
            void * t140 = gm_l0;
            gm_tl874t2atnlh3 t141 = gm_l14;
            gm_b16_8 t142 = gm_l15;
            gm_b0_1 t143 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t140, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9805qtonbsag t144;
            memset(&t144, 0, sizeof t144);
            uint8_t t145 = 0;
            F.ret = t144;
            goto gm_fret;
        }
        goto me93;
mn135:;
ma97:;
        uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&t92) + 0));
        if (t147 != ((uint8_t)3ULL)) goto mn146;
        gm_t9805qtonbsag t148;
        memset(&t148, 0, sizeof t148);
        uint8_t t149 = 0;
        F.ret = t148;
        goto gm_fret;
        goto me93;
mn146:;
ma98:;
        gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 307, 5);
me93:;
    }
    {
        void * t150 = gm_l0;
        gm_tjcptv9sagfq8 t151;
        memset((uint8_t *)&t151 + 73, 0, 7);
        gm_b16_8 t152 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t151 + 0, t152);
        gm_tl874t2atnlh3 t153 = gm_l5;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t151 + 16, t153);
        void * t154 = gm_l9;
        gm_t88rcucg1b7uk t155 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t154) + 0));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t151 + 28, t155);
        uint64_t t156 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t151 + 32, t156);
        void * t157 = gm_l2;
        GM_ST(void *, (uint8_t *)&t151 + 40, t157);
        gm_tkn42u7na3t81 t158 = gm_l3;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t151 + 48, t158);
        void * t159 = gm_l0;
        uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)t159) + 2312));
        GM_ST(uint64_t, (uint8_t *)&t151 + 56, t160);
        void * t161 = gm_l9;
        GM_ST(void *, (uint8_t *)&t151 + 64, t161);
        gm_t9qkh0rsooc8d t162;
        GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t151 + 72, t162);
        GM_ST(void *, (uint8_t *)&t151 + 80, ((void *)0));
        uint32_t t163 = gm_f_add_const_8u8b1ja((void *)NULL, cx, NULL, t150, &t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t163;
    }
    {
        uint32_t t164 = gm_l16;
        uint64_t t165 = (uint64_t)t164;
        uint64_t t166 = (uintptr_t)t165;
        void * t167 = gm_l0;
        uint64_t t168 = GM_LD(uintptr_t, ((((uint8_t *)t167) + 1968) + 0) + 8);
        if (t166 >= (uintptr_t)(t168)) gm_trap_index(t166, t168, gm_srcstmd1b5da98l, 326, 5);
        uint8_t *t169 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t167) + 1968) + 0)) + t166 * 88) + 80);
        gm_tjoi8o798ne9k t170;
        memset((uint8_t *)&t170 + 49, 0, 15);
        memcpy((uint8_t *)&t170 + 0, ((uint8_t *)&F.l11), 48);
        GM_ST(uint8_t, (uint8_t *)&t170 + 48, ((uint8_t)1));
        gm_tq7s98ihonkju t171 = gm_f_new_15vqrvd((void *)NULL, cx, NULL, &t170, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8majipqr8n)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 8, gm_srcstmd1b5da98l, 326, 38);
        void * t172 = GM_LD(void *, (((uint8_t *)&t171) + 8));
        GM_ST(void *, t169, t172);
    }
    {
        uint32_t t173 = gm_l16;
        gm_t9805qtonbsag t174;
        GM_ST(uint8_t, (uint8_t *)&t174 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t174 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t174), t173);
        ((uint8_t *)&t174)[4] = 1;
        uint8_t t175 = 0;
        F.ret = t174;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_report_trap_1kollts(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_report_trap_1kollts F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l2; /* t */
    uint64_t gm_l4; /* shown */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t9d4sf0vqn7dt gm_l7; /* c */
    gm_b16_8 gm_l8; /* name */
    gm_b16_8 gm_l9; /* what */
    GM_STACK_CHECK(&F, 1023u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 72));
        void * t4 = gm_l2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
        gm_tnpifv20irnph t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        gm_tabb710d9mq2e t7;
        gm_f_format_uthdi26((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcstmd1b5da98l, 187, 29);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_f_error_s6hn871((void *)&F.l3, (void *)NULL, cx, NULL, t3, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l2;
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 16));
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t14 = (!gm_eqff85ac10o3(((uint8_t *)&t12), ((uint8_t *)&t13)));
        if (!t14) goto ie10;
        void * t15 = gm_l2;
        gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 16));
        void * t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 72));
        uint8_t t19 = (!gm_eqff85ac10o3(((uint8_t *)&t16), ((uint8_t *)&t18)));
        if (!t19) goto ie10;
        {
            void * t21 = (void *)((uint8_t *)&F.l3);
            void * t22 = gm_l2;
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 16));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_ss75kiikq7m3h));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(27));
            gm_f_add_label_50fj7vv(t21, GM_CONV(gm_cseahvg8v7p0vs, t23), t24);
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_l4 = ((uint64_t)12ULL);
    }
    {
        {
            void * t26 = gm_l2;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((((uint8_t *)t26) + 32) + 0));
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&t27) + 8);
            void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t27)));
            uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
            void * t31 = gm_l2;
            uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 32) + 0) + 8);
            uint64_t t33 = gm_l4;
            uint64_t t34 = (t32 < t33 ? t32 : t33);
            uint64_t t35 = (uintptr_t)t34;
            if (t30 > t35 || t35 > (uintptr_t)t28) gm_trap_slice(t30, t35, t28, gm_srcstmd1b5da98l, 191, 14);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t29 + t30 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t30));
            gm_l5 = t36;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l6;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l6;
                    uint64_t t44 = (uintptr_t)t43;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t9d4sf0vqn7dt t48 = GM_LD(gm_t9d4sf0vqn7dt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t44 * 16));
                    gm_l7 = t48;
                    {
                        uint8_t t51 = 0;
                        gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 4));
                        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        uint8_t t54 = (gm_eqff85ac10o3(((uint8_t *)&t52), ((uint8_t *)&t53)));
                        if (!t54) {
                            gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 4));
                            void * t56 = gm_l1;
                            gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 72));
                            uint8_t t58 = (gm_eqff85ac10o3(((uint8_t *)&t55), ((uint8_t *)&t57)));
                            t51 = t58;
                        } else t51 = 1;
                        if (!t51) goto ie50;
                        {
                            goto lc37;
                        }
                        goto ix50;
ie50:;
ix50:;
                    }
                    {
                        gm_b16_8 t60;
                        void * t62 = gm_l0;
                        uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
                        gm_th2lqbo77pvsc t64;
                        gm_f_backtrace_name_fd5fgd3((void *)&t64, (void *)NULL, cx, NULL, t62, t63);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        if (!((((uint8_t *)&t64))[16] != 0)) goto ie61;
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                        gm_l8 = t65;
                        gm_b16_8 t67 = gm_l8;
                        gm_tnpifv20irnph t68;
                        GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
                        gm_tabb710d9mq2e t69;
                        gm_f_format_h4jcodi((void *)&t69, (void *)NULL, cx, NULL, t68);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcstmd1b5da98l, 193, 64);
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
                        gm_b16_8 t71 = t70;
                        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                        t60 = t72;
                        goto ix61;
ie61:;
                        gm_b16_8 t74;
                        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sblodmj4vs4nd));
                        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(23));
                        gm_b16_8 t75 = t74;
                        t60 = t75;
ix61:;
                        gm_l9 = t60;
                    }
                    {
                        void * t76 = (void *)((uint8_t *)&F.l3);
                        gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 4));
                        gm_b16_8 t78 = gm_l9;
                        gm_tnpifv20irnph t79;
                        GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t78);
                        gm_tabb710d9mq2e t80;
                        gm_f_format_80i8qff((void *)&t80, (void *)NULL, cx, NULL, t79);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srcstmd1b5da98l, 194, 29);
                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                        gm_f_add_label_50fj7vv(t76, GM_CONV(gm_cseahvg8v7p0vs, t77), t82);
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_srcstmd1b5da98l, 191, 5);
me46:;
                }
lc37:;
                {
                    uint64_t t84 = gm_l6;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                    uint64_t t87 = r85;
                    gm_l6 = t87;
                }
            }
gm_ll41:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
            }
            goto gm_lad;
lb37:;
        }
    }
    {
        void * t89 = gm_l2;
        uint64_t t90 = GM_LD(uintptr_t, ((((uint8_t *)t89) + 32) + 0) + 8);
        uint64_t t91 = gm_l4;
        uint8_t t92 = (t90 > t91);
        if (!t92) goto ie88;
        {
            void * t94 = (void *)((uint8_t *)&F.l3);
            void * t95 = gm_l2;
            uint64_t t96 = GM_LD(uintptr_t, ((((uint8_t *)t95) + 32) + 0) + 8);
            uint64_t t97 = gm_l4;
            uint64_t r98; bool o99 = __builtin_sub_overflow(t96, t97, &r98);
            if (o99) gm_trap("integer overflow in `-`", gm_srcstmd1b5da98l, 196, 89);
            uint64_t t100 = r98;
            gm_t4ce0jd3tdro4 t101;
            GM_ST(uint64_t, ((uint8_t *)&t101 + 0), t100);
            gm_tabb710d9mq2e t102;
            gm_f_format_06lg16a((void *)&t102, (void *)NULL, cx, NULL, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srcstmd1b5da98l, 196, 47);
            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
            gm_f_add_note_46v502s(t94, t104);
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        void * t105 = gm_l0;
        gm_b0_1 t106 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t105, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l3)));
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
__attribute__((aligned(4))) void * gm_f_static_assert_k8dt551(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_static_assert_k8dt551 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l7; /* ex */
    gm_th2lqbo77pvsc gm_l8; /* msg */
    uint32_t gm_l10; /* id */
    GM_STACK_CHECK(&F, 1051u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t0mfir12leg2g t2;
        memset(&t2, 0, sizeof t2);
        F.l4 = t2;
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie3;
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srcstmd1b5da98l, 349, 43);
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 144) + 0) + 0));
        if (t8 != ((uint8_t)0ULL)) goto ie3;
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 144) + 0) + 8));
        F.l5 = t9;
        {
            const uint8_t *t11 = ((uint8_t *)&F.l5);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l4)) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l4)) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l4)) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l4)))[88] = 1;
            memmove((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l4)) + 0, t11, 88);
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_t0mfir12leg2g t12 = F.l4;
        gm_tc3oht4ksth9d t13;
        if (((((uint8_t *)&t12))[88] != 0)) {
            t13 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t12) + 0));
        } else {
            {
                void * t15 = gm_l0;
                gm_tl874t2atnlh3 t16 = gm_l3;
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_so0m8vf92uemo));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(65));
                gm_b0_1 t18 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t15, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t19 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        F.l6 = t13;
    }
    {
        void * t20 = gm_l0;
        uint64_t t21 = gm_l1;
        void * t22 = (void *)((uint8_t *)&F.l6);
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_t88rcucg1b7uk t25 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[4] = 1;
        void * t27 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t20, t21, t22, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l7 = t28;
    }
    {
        gm_th2lqbo77pvsc t31;
        memset(&t31, 0, sizeof t31);
        gm_l8 = t31;
    }
    {
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t34 = (t33 > ((uint64_t)1ULL));
        if (!t34) goto ie32;
        uint64_t t35 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srcstmd1b5da98l, 356, 43);
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t35 * 144) + 0) + 0));
        if (t37 != ((uint8_t)0ULL)) goto ie32;
        gm_tc3oht4ksth9d t38 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t35 * 144) + 0) + 8));
        F.l9 = t38;
        {
            void * t40 = gm_l0;
            uint64_t t41 = gm_l1;
            void * t42 = (void *)((uint8_t *)&F.l9);
            gm_f_const_string_jdjj62s((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l8)), (void *)NULL, cx, NULL, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t44 = gm_l0;
        void * t45 = gm_l7;
        uint8_t t46 = gm_f_depends_on_params_khui57h(t44, t45);
        if (!t46) goto ie43;
        {
            void * t48 = gm_l0;
            uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 2232) + 0) + 8);
            uint32_t t50 = (uint32_t)t49;
            gm_l10 = t50;
        }
        {
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 2232);
            gm_tftb1odc739t5 t53 = gm_f_push_0rapspb(t52, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l8)));
            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcstmd1b5da98l, 359, 9);
            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
        }
        {
            void * t55 = gm_l0;
            void * t56 = (void *)(((uint8_t *)t55) + 8);
            gm_t88rcucg1b7uk t57 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt8o5kebou62r t58;
            memset((uint8_t *)&t58 + 12, 0, 4);
            uint32_t t59 = gm_l10;
            gm_tvdg94bk7aa22 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)80ULL));
            GM_ST(uint32_t, ((uint8_t *)&t60 + 4), t59);
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t58 + 0, t60);
            void * t61 = gm_l7;
            gm_b16_8 t62 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t58 + 16, t62);
            gm_teti411eujvf4 t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t63 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t63 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t63 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t63 + 8), t58);
            gm_tl874t2atnlh3 t64 = gm_l3;
            void * t65 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t57, &t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t67 = gm_l0;
        void * t68 = gm_l7;
        gm_tl874t2atnlh3 t69 = gm_l3;
        gm_toceffb697p0u t70;
        memset(&t70, 0, sizeof t70);
        gm_b0_1 t71 = gm_f_check_static_assert_s4f7i6i((void *)NULL, cx, NULL, t67, t68, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l8)), t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t72 = 0;
        F.ret = ((void *)0);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_of_expr_l8qm9vv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_type_of_expr_l8qm9vv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* ex */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_tkn42u7na3t81 t5;
        memset(&t5, 0, sizeof t5);
        void * t6 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t7;
        if ((GM_LD(void *, ((uint8_t *)&t6)) != 0)) {
            void * t8 = GM_LD(void *, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 8);
            gm_t88rcucg1b7uk t11 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        void * t13 = gm_l3;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
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
