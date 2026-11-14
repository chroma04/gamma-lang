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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_terja4da2tj6r;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_terja4da2tj6r) == 24 && _Alignof(gm_terja4da2tj6r) == 8 && offsetof(gm_terja4da2tj6r, f0) == 0 && offsetof(gm_terja4da2tj6r, f1) == 8 && offsetof(gm_terja4da2tj6r, f2) == 16, "layout of `(start: usize, end: usize, outline: bool)`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { uint64_t f0; gm_tj64ui6kruni6 f1; } gm_t19us6e77elqj;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
typedef struct { uint64_t f0; gm_trqfvmjtt4sij f1; } gm_tmupb6k0asuti;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t19us6e77elqj) == 40 && _Alignof(gm_t19us6e77elqj) == 8 && offsetof(gm_t19us6e77elqj, f0) == 0 && offsetof(gm_t19us6e77elqj, f1) == 8, "layout of `(i: usize, x: RunExit)`");
typedef struct { gm_tmupb6k0asuti f0; uint8_t f1; uint8_t pe[7]; } gm_tj6rff10m69j7;
_Static_assert(sizeof(gm_tmupb6k0asuti) == 48 && _Alignof(gm_tmupb6k0asuti) == 8 && offsetof(gm_tmupb6k0asuti, f0) == 0 && offsetof(gm_tmupb6k0asuti, f1) == 8, "layout of `(i: usize, x: List((name: string, def: string)))`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_tj6rff10m69j7) == 56 && _Alignof(gm_tj6rff10m69j7) == 8 && offsetof(gm_tj6rff10m69j7, f0) == 0 && offsetof(gm_tj6rff10m69j7, f1) == 48, "layout of `?Run`");
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_any_outlined_7juvsf3 {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* x */
    uint8_t ret;
};
struct gm_fr_block_outlines_na0uhok {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* weights */
    gm_trqfvmjtt4sij l5; /* runs */
    uint8_t ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5c5er62 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qc81hdv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_lines_99v0bhd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_egshfcm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nnsuab7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_frame_name_stddmgg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_jump_pvnkvme {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_1imoitn {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6sh7i1h {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_long_body_qgvuu1c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_outline_run_cjmhkvl {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* saved_out */
    gm_tqgtd5gtgj65s l9; /* r */
    gm_tmupb6k0asuti l10; /* run */
    gm_trqfvmjtt4sij l11; /* body */
    gm_trqfvmjtt4sij l14; /* params */
    gm_trqfvmjtt4sij l15; /* args */
    gm_trqfvmjtt4sij l22; /* attrs */
    gm_trqfvmjtt4sij l24; /* text */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_outline_stmts_4hhf3mf {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_outlines_2kvbvit {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_03sdim1 {
    gm_frame h;
    gm_tmupb6k0asuti l2; /* x */
    gm_tj6rff10m69j7 ret;
};
struct gm_fr_runs_of_adlemfk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_size_attrs_abk1l3m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_stmt_list_d9ovq7d {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* weights */
    gm_trqfvmjtt4sij l9; /* runs */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_stmt_weight_21p4irs {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_weight_nu7ub4o {
    gm_frame h;
    uint32_t l1; /* cap */
    uint32_t l2; /* n */
    uint32_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_any_outlined_7juvsf3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_block_outlines_na0uhok(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_qdhd62f(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5c5er62(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qc81hdv(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_egshfcm(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_nnsuab7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_tag_okd90ep(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a09tbhs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hc3p1hh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i5sf3rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ti4oun0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_j9naoa6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_njoe3l6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_q6e8rn6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vepvkou(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) void gm_f_list_1imoitn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6sh7i1h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_long_body_qgvuu1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_outline_run_cjmhkvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_outline_stmts_4hhf3mf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_03sdim1(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_849ebnd(void * a0, gm_tqs5p4n2tq19v a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ai20sgs(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bs5eajd(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kcrf85d(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_runs_of_adlemfk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_stmt_block_5vkbn0n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_list_d9ovq7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_stmt_weight_21p4irs(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_weight_nu7ub4o(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2j0ojke(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1);
extern uint8_t gm_c9pef0g9fng[4] __attribute__((aligned(4)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cega6s9dl3o[4] __attribute__((aligned(4)));
extern uint8_t gm_crhue63gorj[4] __attribute__((aligned(4)));
extern uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
extern uint8_t gm_cvpsnk4jni3[4] __attribute__((aligned(4)));
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s1s3m1fet79ge[7];
extern const uint8_t gm_s205esok2u8pk[4];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s5bohs6opthjm[24];
extern const uint8_t gm_s6db4pm3k5olv[3];
extern const uint8_t gm_s84k1uo35omt8[10];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdvlm6i5s2j37[23];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sj53lcu5ne531[20];
extern const uint8_t gm_sjl4446pn0dup[3];
extern const uint8_t gm_slnuijmj5p1vk[19];
extern const uint8_t gm_sp1g56u3ibrtp[23];
extern const uint8_t gm_sq9pusqm0kq2f[7];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3m73omiakl12[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_std40ghjh6qib[13];
extern const uint8_t gm_suqchf5vh2hqp[5];
extern const uint8_t gm_sura2ofp16u7l[16];
extern const uint8_t gm_suuphrufuvh4m[12];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5v8n6qrn4rh4 t5 = t4;
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)6ULL)));
    if (t6) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (t9 > ((uint64_t)16ULL));
        t1 = t10;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5c5er62(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5c5er62 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 16)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qc81hdv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qc81hdv F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_lines_99v0bhd F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 2));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 184);
            gm_b16_8 t9 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tftb1odc739t5 t11 = gm_f_write_cdouvhd(t8, t10);
            gm_tftb1odc739t5 t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcmok80ho8hh01, 430, 31);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            gm_closure t6 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
            void * t7 = gm_l0;
            gm_closure t8 = t6;
            gm_node *ccx9 = t8.cx;
            void * t10 = t8.code;
            uint8_t t11;
            if (GM_PTR_TAGGED(t10)) {
                t11 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t10))(t7);
            } else {
                t11 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t10)(t8.env, ccx9, NULL, t7);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t11);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_nnsuab7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_nnsuab7 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = gm_f_any_outlined_7juvsf3((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_name_stddmgg F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t9805qtonbsag t3;
    memset(&t3, 0, sizeof t3);
    gm_b16_8 t4 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t1, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_tabb710d9mq2e t6;
    gm_f_format_5erjuiq((void *)&t6, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcht195tfrfuqo, 167, 60);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 452, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 344));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 453, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 344), t13);
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
inline __attribute__((aligned(4))) void gm_f_list_1imoitn(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1imoitn F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5c5er62();
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
inline __attribute__((aligned(4))) void gm_f_list_6sh7i1h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6sh7i1h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qc81hdv();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_03sdim1(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_03sdim1 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
            gm_tj6rff10m69j7 t8;
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
        gm_tmupb6k0asuti t17 = GM_LD(gm_tmupb6k0asuti, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
        F.l2 = t17;
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_tmupb6k0asuti t29 = F.l2;
        gm_tj6rff10m69j7 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_tmupb6k0asuti, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[48] = 1;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_size_attrs_abk1l3m F;
    uint32_t gm_l0; /* names */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, gm_cvpsnk4jni3);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sp1g56u3ibrtp));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(22));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, gm_ctq6t8frtor);
        uint8_t t12 = (t10 > t11);
        if (!t12) goto ie9;
        {
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_suuphrufuvh4m));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(11));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(0));
        uint8_t t17 = 0;
        F.ret = t16;
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
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
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
inline __attribute__((aligned(4))) uint32_t gm_f_weight_nu7ub4o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_weight_nu7ub4o F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        F.l2 = ((uint32_t)1ULL);
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_egshfcm, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t4 = F.l2;
        uint32_t t5 = F.l1;
        uint32_t t6 = (t4 < t5 ? t4 : t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_any_outlined_7juvsf3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_any_outlined_7juvsf3 F;
    void * gm_l0; /* e */
    void * gm_l1; /* blk */
    GM_STACK_CHECK(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma4; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)46ULL)) goto mn9;
        void * t11 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t11;
        void * t13 = gm_l1;
        uint8_t t14 = gm_f_block_outlines_na0uhok((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t14) goto ie12;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
        goto me3;
mn9:;
ma5:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)48ULL)) goto mn17;
        gm_t710lsrq1m8gk t19 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l2 = t19;
        void * t21 = GM_LD(void *, (((uint8_t *)&F.l2) + 24));
        uint8_t t22 = gm_f_block_outlines_na0uhok((void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t22) goto ie20;
        {
            uint8_t t24 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
        goto me3;
mn17:;
ma6:;
        goto me3;
mn25:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 178, 5);
me3:;
    }
    {
        void * t27 = gm_l0;
        gm_closure t28 = ((gm_closure){((void *)gm_f_fn_nnsuab7), NULL, cx});
        uint8_t t29 = gm_f_any_child_ffc8h1k(t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = 0;
        F.ret = t29;
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
__attribute__((aligned(4))) uint8_t gm_f_block_outlines_na0uhok(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_block_outlines_na0uhok F;
    void * gm_l0; /* blk */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_terja4da2tj6r gm_l8; /* r */
    GM_STACK_CHECK(&F, 415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
            gm_l2 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = gm_l2;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        uint64_t t19 = gm_l4;
                        uint64_t t20 = (uintptr_t)t19;
                        void * t21 = gm_l0;
                        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 0) + 8);
                        if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_src3m73omiakl12, 190, 59);
                        void * t23 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 0)) + t20 * 32);
                        uint32_t t24 = GM_LD(uint32_t, gm_cega6s9dl3o);
                        uint32_t r25; bool o26 = __builtin_add_overflow(t24, ((uint32_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 190, 73);
                        uint32_t t27 = r25;
                        uint32_t t28 = gm_f_stmt_weight_21p4irs(t23, t27);
                        gm_tftb1odc739t5 t29 = gm_f_push_im9d4ro(t18, t28);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src3m73omiakl12, 190, 33);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 190, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l3 = t36;
                }
            }
lb6:;
        }
    }
    {
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t38 = t37;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_f_runs_of_adlemfk((void *)&F.l5, (void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        {
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l6 = t42;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t44 = gm_l7;
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t46 = (t44 < t45);
                if (!t46) goto lb43;
                {
                    uint64_t t49 = gm_l7;
                    uint64_t t50 = (uintptr_t)t49;
                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_terja4da2tj6r t54 = GM_LD(gm_terja4da2tj6r, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t50 * 24));
                    gm_l8 = t54;
                    {
                        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 16));
                        if (!t57) goto ie56;
                        {
                            uint8_t t59 = 0;
                            F.ret = ((uint8_t)1);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t59;
                            goto gm_ll47;
                        }
                        goto ix56;
ie56:;
ix56:;
                    }
                    goto me52;
mn53:;
                    gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 193, 5);
me52:;
                }
lc43:;
                {
                    uint64_t t61 = gm_l7;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l7 = t64;
                }
            }
gm_ll47:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
            }
            goto gm_ld1_2;
lb43:;
        }
    }
    {
        uint8_t t65 = 0;
        F.ret = ((uint8_t)0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t65;
        goto gm_ld1_2;
    }
    {
        void * t66 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_qdhd62f(t66);
    }
    {
        void * t67 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_padgogk(t67);
    }
    goto gm_ls68;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t69 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_qdhd62f(t69);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t70 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_padgogk(t70);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls68:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_egshfcm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_egshfcm F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2));
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l1));
        uint8_t t5 = (t3 < t4);
        if (!t5) goto ie2;
        {
            uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2));
            void * t8 = gm_l0;
            uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l1));
            uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2));
            uint32_t r11; bool o12 = __builtin_sub_overflow(t9, t10, &r11);
            if (o12) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 63, 37);
            uint32_t t13 = r11;
            uint32_t t14 = gm_f_weight_nu7ub4o(t8, t13);
            uint32_t r15; bool o16 = __builtin_add_overflow(t7, t14, &r15);
            if (o16) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 63, 22);
            uint32_t t17 = r15;
            GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_weight_nu7ub4o *)((&F.h)->up))->l2), t17);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_nnsuab7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2) {
    struct gm_fr_jump_pvnkvme F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* label */
    gm_tf5srh4mp4b3i gm_l2; /* scope */
    void * gm_l3; /* run */
    uint8_t gm_l4; /* outside */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* k */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 572u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 352) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 352) + 0) + 8);
            uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
            if (o10) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 96, 31);
            uint64_t t11 = r9;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 352) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_src3m73omiakl12, 96, 17);
            void * t15 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 352) + 0)) + t12 * 48);
            gm_l3 = t15;
        }
        {
            gm_l4 = ((uint8_t)1);
        }
        {
            if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie16;
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
            gm_l5 = t17;
            {
                uint64_t t19 = gm_l5;
                void * t20 = gm_l3;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 0));
                uint8_t t22 = (t19 < t21);
                gm_l4 = t22;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            uint8_t t24 = gm_l4;
            if (!t24) goto ie23;
            {
                void * t26 = gm_l3;
                uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 8) + 0) + 8);
                gm_l6 = t27;
            }
            {
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    void * t29 = gm_l3;
                    uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 8) + 0) + 8);
                    gm_l7 = t30;
                }
                {
                    for (;;) {
                        uint64_t t32 = gm_l8;
                        uint64_t t33 = gm_l7;
                        uint8_t t34 = (t32 < t33);
                        if (!t34) goto lb31;
                        {
                            uint64_t t37 = gm_l8;
                            uint64_t t38 = t37;
                            uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&t38));
                            gm_l9 = t41;
                            {
                                uint64_t t44 = gm_l9;
                                uint64_t t45 = (uintptr_t)t44;
                                void * t46 = gm_l3;
                                uint64_t t47 = GM_LD(uintptr_t, ((((uint8_t *)t46) + 8) + 0) + 8);
                                if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_src3m73omiakl12, 102, 20);
                                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t46) + 8) + 0)) + t45 * 32) + 0));
                                gm_b16_8 t49 = gm_l1;
                                gm_b16_8 t50 = t48;
                                gm_b16_8 t51 = t49;
                                uint8_t t52 = (gm_eq0iqiubfm7e(((uint8_t *)&t50), ((uint8_t *)&t51)));
                                if (!t52) goto ie43;
                                {
                                    uint64_t t54 = gm_l9;
                                    gm_l6 = t54;
                                }
                                {
                                    goto lb31;
                                }
                                goto ix43;
ie43:;
ix43:;
                            }
                            goto me39;
mn40:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 101, 13);
me39:;
                        }
lc31:;
                        {
                            uint64_t t56 = gm_l8;
                            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                            uint64_t t59 = r57;
                            gm_l8 = t59;
                        }
                    }
lb31:;
                }
            }
            {
                uint64_t t61 = gm_l6;
                void * t62 = gm_l3;
                uint64_t t63 = GM_LD(uintptr_t, ((((uint8_t *)t62) + 8) + 0) + 8);
                uint8_t t64 = ((t61 == t63));
                if (!t64) goto ie60;
                {
                    void * t66 = gm_l3;
                    void * t67 = (void *)(((uint8_t *)t66) + 8);
                    gm_tj64ui6kruni6 t68;
                    gm_b16_8 t69 = gm_l1;
                    GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
                    gm_tf5srh4mp4b3i t70 = gm_l2;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t68 + 16, t70);
                    gm_tftb1odc739t5 t71 = gm_f_push_kcrf85d(t67, &t68);
                    gm_tftb1odc739t5 t72 = t71;
                    if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_src3m73omiakl12, 107, 43);
                    gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                }
                goto ix60;
ie60:;
ix60:;
            }
            {
                void * t74 = gm_l0;
                uint64_t t75 = gm_l6;
                uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                if (o77) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 108, 36);
                uint64_t t78 = r76;
                gm_t4ce0jd3tdro4 t79;
                GM_ST(uint64_t, ((uint8_t *)&t79 + 0), t78);
                gm_b0_1 t80 = gm_f_linef_vepvkou((void *)NULL, cx, NULL, t74, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t81;
                uint8_t t82 = 0;
                F.ret = t81;
                goto gm_fret;
            }
            goto ix23;
ie23:;
ix23:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t83 = gm_l0;
        gm_b16_8 t84 = gm_l1;
        gm_tnpifv20irnph t85;
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
        gm_b0_1 t86 = gm_f_linef_njoe3l6((void *)NULL, cx, NULL, t83, t85);
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
inline __attribute__((aligned(4))) uint8_t gm_f_long_body_qgvuu1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_long_body_qgvuu1c F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* known */
    uint8_t gm_l4; /* long */
    void * gm_l5; /* b */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 3000) + 0) + 8);
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb4;
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 3000);
                gm_tqs5p4n2tq19v t13;
                memset(&t13, 0, sizeof t13);
                gm_tftb1odc739t5 t14 = gm_f_push_849ebnd(t12, t13);
                gm_tftb1odc739t5 t15 = t14;
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src3m73omiakl12, 51, 40);
                gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t18 = gm_l2;
        uint64_t t19 = (uintptr_t)t18;
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 3000) + 0) + 8);
        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src3m73omiakl12, 52, 17);
        if (!((((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 3000) + 0)) + t19 * 2))[1] != 0)) goto ie17;
        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 3000) + 0)) + t19 * 2));
        gm_l3 = t22;
        {
            uint8_t t24 = gm_l3;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        uint32_t t29 = gm_l1;
        void * t30 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 152));
        void * t32 = t31;
        if (!(GM_LD(void *, ((uint8_t *)&t32)) != 0)) goto ie26;
        void * t33 = GM_LD(void *, ((uint8_t *)&t32));
        gm_l5 = t33;
        {
            void * t35 = gm_l5;
            uint32_t t36 = GM_LD(uint32_t, gm_c9pef0g9fng);
            uint32_t r37; bool o38 = __builtin_add_overflow(t36, ((uint32_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 54, 52);
            uint32_t t39 = r37;
            uint32_t t40 = gm_f_weight_nu7ub4o(t35, t39);
            uint32_t t41 = GM_LD(uint32_t, gm_c9pef0g9fng);
            uint8_t t42 = (t40 > t41);
            gm_l4 = t42;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        uint64_t t43 = gm_l2;
        uint64_t t44 = (uintptr_t)t43;
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 3000) + 0) + 8);
        if (t44 >= (uintptr_t)(t46)) gm_trap_index(t44, t46, gm_src3m73omiakl12, 55, 5);
        uint8_t t47 = gm_l4;
        gm_tqs5p4n2tq19v t48;
        GM_ST(uint8_t, ((uint8_t *)&t48), t47);
        ((uint8_t *)&t48)[1] = 1;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t45) + 3000) + 0)) + t44 * 2), t48);
    }
    {
        uint8_t t49 = gm_l4;
        uint8_t t50 = 0;
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
__attribute__((aligned(4))) void gm_f_outline_run_cjmhkvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_outline_run_cjmhkvl F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* stmts */
    uint32_t gm_l2; /* k */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* outer_cx */
    uint8_t gm_l5; /* own_cx */
    uint64_t gm_l7; /* saved_indent */
    uint32_t gm_l8; /* saved_names */
    uint32_t gm_l12; /* names */
    uint32_t gm_l13; /* fid */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    uint32_t gm_l19; /* p */
    gm_t88rcucg1b7uk gm_l20; /* t */
    uint8_t gm_l21; /* debug */
    gm_b16_8 gm_l23; /* head */
    gm_b16_8 gm_l25; /* call */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* j */
    gm_tj64ui6kruni6 gm_l29; /* x */
    GM_STACK_CHECK(&F, 3864u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            uint32_t t4 = gm_f_fresh_5kokjmg(t3);
            gm_l2 = t4;
        }
        {
            gm_tommf4j9upomc t5;
            memset((uint8_t *)&t5 + 20, 0, 4);
            void * t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&t5 + 0, t7);
            uint32_t t8 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t5 + 16, t8);
            gm_tabb710d9mq2e t9;
            gm_f_format_hc3p1hh((void *)&t9, (void *)NULL, cx, NULL, &t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src3m73omiakl12, 207, 13);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_l3 = t11;
        }
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 280));
            gm_l4 = t13;
        }
        {
            uint8_t t14 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_s10iv34fu7qe7, 2) == 0));
            gm_l5 = t14;
        }
        {
            void * t15 = gm_l0;
            gm_trqfvmjtt4sij t16 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t15) + 184));
            F.l6 = t16;
        }
        {
            void * t17 = gm_l0;
            uint8_t *t18 = (((uint8_t *)t17) + 184);
            gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)t18), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 224));
            gm_l7 = t20;
        }
        {
            void * t21 = gm_l0;
            GM_ST(uint64_t, (((uint8_t *)t21) + 224), ((uint64_t)1ULL));
        }
        {
            void * t22 = gm_l0;
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)t22) + 344));
            gm_l8 = t23;
        }
        {
            void * t24 = gm_l0;
            GM_ST(uint32_t, (((uint8_t *)t24) + 344), ((uint32_t)0ULL));
        }
        {
            uint8_t t26 = gm_l5;
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) goto ie25;
            {
                void * t29 = gm_l0;
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s1s3m1fet79ge));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (((uint8_t *)t29) + 280), t30);
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            void * t31 = gm_l0;
            void * t32 = (void *)(((uint8_t *)t31) + 352);
            gm_tmupb6k0asuti t33;
            void * t34 = gm_l0;
            uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 240) + 0) + 8);
            GM_ST(uint64_t, (uint8_t *)&t33 + 0, t35);
            gm_f_list_6sh7i1h((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t33 + 8)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t36 = gm_f_push_bs5eajd(t32, &t33);
            gm_tftb1odc739t5 t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src3m73omiakl12, 217, 5);
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
        }
        {
            void * t39 = gm_l0;
            gm_b16_8 t40 = gm_l1;
            gm_f_outline_stmts_4hhf3mf((void *)&F.l9, (void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t41 = gm_l0;
            gm_b0_1 t42 = gm_f_end_lines_99v0bhd((void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t43 = gm_l0;
            void * t44 = (void *)(((uint8_t *)t43) + 352);
            gm_tj6rff10m69j7 t45;
            gm_f_pop_03sdim1((void *)&t45, t44);
            gm_tj6rff10m69j7 t46 = t45;
            if (!((((uint8_t *)&t46))[48] != 0)) gm_trap("unwrap of null", gm_src3m73omiakl12, 220, 12);
            gm_tmupb6k0asuti t47 = GM_LD(gm_tmupb6k0asuti, ((uint8_t *)&t46));
            F.l10 = t47;
        }
        {
            void * t48 = gm_l0;
            gm_trqfvmjtt4sij t49 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t48) + 184));
            F.l11 = t49;
        }
        {
            void * t50 = gm_l0;
            gm_trqfvmjtt4sij t51 = F.l6;
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t50) + 184), t51);
        }
        {
            void * t52 = gm_l0;
            uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t52) + 344));
            gm_l12 = t53;
        }
        {
            void * t54 = gm_l0;
            uint32_t t55 = gm_l8;
            GM_ST(uint32_t, (((uint8_t *)t54) + 344), t55);
        }
        {
            void * t56 = gm_l0;
            uint64_t t57 = gm_l7;
            GM_ST(uint64_t, (((uint8_t *)t56) + 224), t57);
        }
        {
            void * t58 = gm_l0;
            gm_b16_8 t59 = gm_l4;
            GM_ST(gm_b16_8, (((uint8_t *)t58) + 280), t59);
        }
        {
            gm_tqgtd5gtgj65s t60 = F.l9;
            gm_tqgtd5gtgj65s t61 = t60;
            if (((((uint8_t *)&t61))[0] != 0)) {
                gm_t08olq97rscn3 t62 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t61) + 8));
                gm_tqgtd5gtgj65s t63;
                GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                ((uint8_t *)&t63)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t63 + 8), t62);
                F.ret = t63;
                goto gm_fret;
            }
            gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 8));
        }
        {
            void * t65 = gm_l0;
            uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t65) + 8));
            gm_l13 = t66;
        }
        {
            gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t67 = (void *)((uint8_t *)&F.l14);
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            uint32_t t70 = gm_l13;
            gm_b16_8 t71 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t72;
            GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
            gm_tabb710d9mq2e t73;
            gm_f_format_i5sf3rq((void *)&t73, (void *)NULL, cx, NULL, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t74 = t73;
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_src3m73omiakl12, 231, 17);
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
            gm_b16_8 t76 = t75;
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            gm_tftb1odc739t5 t78 = gm_f_push_3ef746b(t67, t77);
            gm_tftb1odc739t5 t79 = t78;
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_src3m73omiakl12, 231, 5);
            gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
        }
        {
            void * t81 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_siji3oo7ud53t));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(9));
            gm_tftb1odc739t5 t83 = gm_f_push_3ef746b(t81, t82);
            gm_tftb1odc739t5 t84 = t83;
            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_src3m73omiakl12, 232, 5);
            gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
        }
        {
            void * t86 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s2kvk8bnjk52t));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(11));
            gm_tftb1odc739t5 t88 = gm_f_push_3ef746b(t86, t87);
            gm_tftb1odc739t5 t89 = t88;
            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_src3m73omiakl12, 233, 5);
            gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
        }
        {
            void * t91 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sdi44r3kug6n6));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(13));
            gm_tftb1odc739t5 t93 = gm_f_push_3ef746b(t91, t92);
            gm_tftb1odc739t5 t94 = t93;
            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_src3m73omiakl12, 234, 5);
            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
        }
        {
            gm_f_list_epvducg((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t96 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sjl4446pn0dup));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t98 = gm_f_push_3ef746b(t96, t97);
            gm_tftb1odc739t5 t99 = t98;
            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 1, gm_src3m73omiakl12, 236, 5);
            gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 1));
        }
        {
            void * t101 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t102;
            GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s205esok2u8pk));
            GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t103 = gm_f_push_3ef746b(t101, t102);
            gm_tftb1odc739t5 t104 = t103;
            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_src3m73omiakl12, 237, 5);
            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
        }
        {
            void * t106 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s10iv34fu7qe7));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t108 = gm_f_push_3ef746b(t106, t107);
            gm_tftb1odc739t5 t109 = t108;
            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_src3m73omiakl12, 238, 5);
            gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
        }
        {
            void * t111 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_suqchf5vh2hqp));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t111, t112);
            gm_tftb1odc739t5 t114 = t113;
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_src3m73omiakl12, 239, 5);
            gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
        }
        {
            void * t117 = gm_l0;
            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)t117) + 298));
            uint8_t t119 = (uint8_t)!t118;
            if (!t119) goto ie116;
            {
                void * t122 = gm_l0;
                void * t123 = GM_LD(void *, (((uint8_t *)t122) + 0));
                void * t124 = (void *)(((uint8_t *)t123) + 8);
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 176));
                gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t126) + 64));
                uint8_t t128 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t124, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t128) goto ie121;
                {
                    void * t130 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t131;
                    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s4ql65vkv7h9p));
                    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(12));
                    gm_tftb1odc739t5 t132 = gm_f_push_3ef746b(t130, t131);
                    gm_tftb1odc739t5 t133 = t132;
                    if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_src3m73omiakl12, 243, 13);
                    gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
                }
                {
                    void * t135 = (void *)((uint8_t *)&F.l15);
                    gm_b16_8 t136;
                    GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sq9pusqm0kq2f));
                    GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(6));
                    gm_tftb1odc739t5 t137 = gm_f_push_3ef746b(t135, t136);
                    gm_tftb1odc739t5 t138 = t137;
                    if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 1, gm_src3m73omiakl12, 244, 13);
                    gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t138) + 1));
                }
                goto ix121;
ie121:;
ix121:;
            }
            {
                {
                    void * t141 = gm_l0;
                    void * t142 = GM_LD(void *, (((uint8_t *)t141) + 176));
                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)t142) + 32));
                    gm_l16 = t143;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t145 = gm_l17;
                        uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t147 = (t145 < t146);
                        if (!t147) goto lb144;
                        {
                            uint64_t t150 = gm_l17;
                            uint64_t t151 = gm_l17;
                            uint64_t t152 = (uintptr_t)t151;
                            uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            uint32_t t154 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t152 * 4));
                            gm_tn68el8d14vku t155;
                            GM_ST(uint32_t, (uint8_t *)&t155 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t155 + 0), t150);
                            GM_ST(uint32_t, ((uint8_t *)&t155 + 8), t154);
                            gm_tn68el8d14vku t156 = t155;
                            uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&t156) + 0));
                            gm_l18 = t159;
                            uint32_t t160 = GM_LD(uint32_t, (((uint8_t *)&t156) + 8));
                            gm_l19 = t160;
                            {
                                uint64_t t163 = gm_l18;
                                uint64_t t164 = (uintptr_t)t163;
                                void * t165 = gm_l0;
                                uint64_t t166 = GM_LD(uintptr_t, (((uint8_t *)t165) + 312) + 8);
                                if (t164 >= (uintptr_t)(t166)) gm_trap_index(t164, t166, gm_src3m73omiakl12, 247, 16);
                                uint8_t t167 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t165) + 312)) + t164 * 1));
                                if (!t167) goto ie162;
                                {
                                    uint32_t t169 = gm_l19;
                                    uint64_t t170 = (uint64_t)t169;
                                    uint64_t t171 = (uintptr_t)t170;
                                    void * t172 = gm_l0;
                                    void * t173 = GM_LD(void *, (((uint8_t *)t172) + 176));
                                    uint64_t t174 = GM_LD(uintptr_t, (((uint8_t *)t173) + 48) + 8);
                                    if (t171 >= (uintptr_t)(t174)) gm_trap_index(t171, t174, gm_src3m73omiakl12, 248, 22);
                                    gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t173) + 48)) + t171 * 40) + 16));
                                    gm_l20 = t175;
                                }
                                {
                                    void * t176 = (void *)((uint8_t *)&F.l14);
                                    gm_t3877pk7lv748 t177;
                                    void * t178 = gm_l0;
                                    void * t179 = GM_LD(void *, (((uint8_t *)t178) + 0));
                                    void * t180 = (void *)(((uint8_t *)t179) + 8);
                                    gm_t88rcucg1b7uk t181 = gm_l20;
                                    gm_b16_8 t182 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t180, t181);
                                    if (gm_unw.kind) {
                                        goto gm_ll148;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t177 + 0, t182);
                                    uint64_t t183 = gm_l18;
                                    GM_ST(uint64_t, (uint8_t *)&t177 + 16, t183);
                                    gm_tabb710d9mq2e t184;
                                    gm_f_format_eq87866((void *)&t184, (void *)NULL, cx, NULL, &t177);
                                    if (gm_unw.kind) {
                                        goto gm_ll148;
                                    }
                                    gm_tabb710d9mq2e t185 = t184;
                                    if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_src3m73omiakl12, 249, 29);
                                    gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
                                    gm_b16_8 t187 = t186;
                                    gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                                    gm_tftb1odc739t5 t189 = gm_f_push_3ef746b(t176, t188);
                                    gm_tftb1odc739t5 t190 = t189;
                                    if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_src3m73omiakl12, 249, 17);
                                    gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
                                }
                                {
                                    void * t192 = (void *)((uint8_t *)&F.l15);
                                    uint64_t t193 = gm_l18;
                                    gm_t4ce0jd3tdro4 t194;
                                    GM_ST(uint64_t, ((uint8_t *)&t194 + 0), t193);
                                    gm_tabb710d9mq2e t195;
                                    gm_f_format_l05am3t((void *)&t195, (void *)NULL, cx, NULL, t194);
                                    if (gm_unw.kind) {
                                        goto gm_ll148;
                                    }
                                    gm_tabb710d9mq2e t196 = t195;
                                    if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 8, gm_src3m73omiakl12, 250, 27);
                                    gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&t196) + 8));
                                    gm_b16_8 t198 = t197;
                                    gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
                                    gm_tftb1odc739t5 t200 = gm_f_push_3ef746b(t192, t199);
                                    gm_tftb1odc739t5 t201 = t200;
                                    if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_src3m73omiakl12, 250, 17);
                                    gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
                                }
                                goto ix162;
ie162:;
ix162:;
                            }
                            goto me157;
mn158:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 246, 9);
me157:;
                        }
lc144:;
                        {
                            uint64_t t204 = gm_l17;
                            uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                            uint64_t t207 = r205;
                            gm_l17 = t207;
                        }
                    }
gm_ll148:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb144; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc144; }
                    }
                    goto gm_lad;
lb144:;
                }
            }
            goto ix116;
ie116:;
ix116:;
        }
        {
            void * t208 = gm_l0;
            void * t209 = GM_LD(void *, (((uint8_t *)t208) + 0));
            uint8_t t210 = GM_LD(uint8_t, ((((uint8_t *)t209) + 1136) + 1));
            gm_l21 = t210;
        }
        {
            uint8_t t212 = gm_l21;
            if (!t212) goto ie211;
            {
                void * t214 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t215;
                GM_ST(void *, (uint8_t *)&t215, (void *)(gm_std40ghjh6qib));
                GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(12));
                gm_tftb1odc739t5 t216 = gm_f_push_3ef746b(t214, t215);
                gm_tftb1odc739t5 t217 = t216;
                if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_src3m73omiakl12, 256, 9);
                gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
            }
            {
                void * t219 = (void *)((uint8_t *)&F.l15);
                gm_b16_8 t220;
                GM_ST(void *, (uint8_t *)&t220, (void *)(gm_s6db4pm3k5olv));
                GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t221 = gm_f_push_3ef746b(t219, t220);
                gm_tftb1odc739t5 t222 = t221;
                if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 1, gm_src3m73omiakl12, 257, 9);
                gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t222) + 1));
            }
            goto ix211;
ie211:;
ix211:;
        }
        {
            uint8_t t225 = gm_l5;
            uint8_t t226 = (uint8_t)!t225;
            if (!t226) goto ie224;
            {
                void * t228 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t229;
                GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sura2ofp16u7l));
                GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(15));
                gm_tftb1odc739t5 t230 = gm_f_push_3ef746b(t228, t229);
                gm_tftb1odc739t5 t231 = t230;
                if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 1, gm_src3m73omiakl12, 260, 9);
                gm_b0_1 t232 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 1));
            }
            {
                void * t233 = (void *)((uint8_t *)&F.l15);
                gm_b16_8 t234 = gm_l4;
                gm_tftb1odc739t5 t235 = gm_f_push_3ef746b(t233, t234);
                gm_tftb1odc739t5 t236 = t235;
                if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 1, gm_src3m73omiakl12, 261, 9);
                gm_b0_1 t237 = GM_LD(gm_b0_1, (((uint8_t *)&t236) + 1));
            }
            goto ix224;
ie224:;
ix224:;
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t238 = (void *)((uint8_t *)&F.l22);
            gm_b16_8 t239;
            GM_ST(void *, (uint8_t *)&t239, (void *)(gm_secrjke11f3ph));
            GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(12));
            gm_tftb1odc739t5 t240 = gm_f_extend_0cs85gs(t238, t239);
            gm_tftb1odc739t5 t241 = t240;
            if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_src3m73omiakl12, 264, 5);
            gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
        }
        {
            void * t244 = gm_l0;
            void * t245 = GM_LD(void *, (((uint8_t *)t244) + 176));
            uint8_t t246 = GM_LD(uint8_t, ((((uint8_t *)t245) + 145) + 0));
            if (!t246) goto ie243;
            {
                void * t248 = (void *)((uint8_t *)&F.l22);
                gm_b16_8 t249;
                GM_ST(void *, (uint8_t *)&t249, (void *)(gm_slnuijmj5p1vk));
                GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(18));
                gm_tftb1odc739t5 t250 = gm_f_extend_0cs85gs(t248, t249);
                gm_tftb1odc739t5 t251 = t250;
                if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 1, gm_src3m73omiakl12, 265, 33);
                gm_b0_1 t252 = GM_LD(gm_b0_1, (((uint8_t *)&t251) + 1));
            }
            goto ix243;
ie243:;
ix243:;
        }
        {
            void * t253 = (void *)((uint8_t *)&F.l22);
            uint32_t t254 = gm_l12;
            gm_b16_8 t255 = gm_f_size_attrs_abk1l3m(t254);
            gm_tftb1odc739t5 t256 = gm_f_extend_0cs85gs(t253, t255);
            gm_tftb1odc739t5 t257 = t256;
            if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 1, gm_src3m73omiakl12, 266, 5);
            gm_b0_1 t258 = GM_LD(gm_b0_1, (((uint8_t *)&t257) + 1));
        }
        {
            gm_tfeab279c16rr t259;
            gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_b16_8 t261 = t260;
            gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&t261));
            GM_ST(gm_b16_8, (uint8_t *)&t259 + 0, t262);
            gm_b16_8 t263 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t259 + 16, t263);
            gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t265 = t264;
            gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
            gm_b16_8 t267;
            GM_ST(void *, (uint8_t *)&t267, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t268;
            gm_f_join_sfknljb((void *)&t268, (void *)NULL, cx, NULL, t266, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t269 = t268;
            if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 8, gm_src3m73omiakl12, 267, 74);
            gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t259 + 32, t270);
            gm_tabb710d9mq2e t271;
            gm_f_format_ti4oun0((void *)&t271, (void *)NULL, cx, NULL, &t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t272 = t271;
            if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 8, gm_src3m73omiakl12, 267, 13);
            gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&t272) + 8));
            gm_l23 = t273;
        }
        {
            void * t274 = gm_l0;
            void * t275 = GM_LD(void *, (((uint8_t *)t274) + 0));
            void * t276 = (void *)(((uint8_t *)t275) + 1888);
            gm_b16_8 t277 = gm_l23;
            gm_tnpifv20irnph t278;
            GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t277);
            gm_tftb1odc739t5 t279 = gm_f_write_i2vvij8(t276, t278);
            gm_tftb1odc739t5 t280 = t279;
            if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_src3m73omiakl12, 268, 5);
            gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l24, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t282 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t283 = gm_l23;
            gm_tnpifv20irnph t284;
            GM_ST(gm_b16_8, ((uint8_t *)&t284 + 0), t283);
            gm_tftb1odc739t5 t285 = gm_f_write_2j0ojke(t282, t284);
            gm_tftb1odc739t5 t286 = t285;
            if (((((uint8_t *)&t286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t286) + 1, gm_src3m73omiakl12, 270, 5);
            gm_b0_1 t287 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 1));
        }
        {
            uint8_t t289 = gm_l21;
            if (!t289) goto ie288;
            {
                void * t291 = (void *)((uint8_t *)&F.l24);
                gm_b16_8 t292;
                GM_ST(void *, (uint8_t *)&t292, (void *)(gm_sj53lcu5ne531));
                GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(19));
                gm_tftb1odc739t5 t293 = gm_f_extend_0cs85gs(t291, t292);
                gm_tftb1odc739t5 t294 = t293;
                if (((((uint8_t *)&t294))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t294) + 1, gm_src3m73omiakl12, 271, 16);
                gm_b0_1 t295 = GM_LD(gm_b0_1, (((uint8_t *)&t294) + 1));
            }
            goto ix288;
ie288:;
ix288:;
        }
        {
            void * t297 = gm_l0;
            void * t298 = GM_LD(void *, (((uint8_t *)t297) + 176));
            uint8_t t299 = GM_LD(uint8_t, ((((uint8_t *)t298) + 145) + 0));
            if (!t299) goto ie296;
            {
                void * t301 = (void *)((uint8_t *)&F.l24);
                gm_b16_8 t302;
                GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sdvlm6i5s2j37));
                GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(22));
                gm_tftb1odc739t5 t303 = gm_f_extend_0cs85gs(t301, t302);
                gm_tftb1odc739t5 t304 = t303;
                if (((((uint8_t *)&t304))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t304) + 1, gm_src3m73omiakl12, 272, 33);
                gm_b0_1 t305 = GM_LD(gm_b0_1, (((uint8_t *)&t304) + 1));
            }
            goto ix296;
ie296:;
ix296:;
        }
        {
            void * t306 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_b16_8 t308 = t307;
            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
            gm_tftb1odc739t5 t310 = gm_f_extend_0cs85gs(t306, t309);
            gm_tftb1odc739t5 t311 = t310;
            if (((((uint8_t *)&t311))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t311) + 1, gm_src3m73omiakl12, 273, 5);
            gm_b0_1 t312 = GM_LD(gm_b0_1, (((uint8_t *)&t311) + 1));
        }
        {
            void * t313 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t314;
            GM_ST(void *, (uint8_t *)&t314, (void *)(gm_s5bohs6opthjm));
            GM_ST(uintptr_t, (uint8_t *)&t314 + 8, (uintptr_t)(23));
            gm_tftb1odc739t5 t315 = gm_f_extend_0cs85gs(t313, t314);
            gm_tftb1odc739t5 t316 = t315;
            if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_src3m73omiakl12, 274, 5);
            gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
        }
        {
            uint8_t t319 = gm_l21;
            if (!t319) goto ie318;
            {
                void * t321 = (void *)((uint8_t *)&F.l24);
                gm_b16_8 t322;
                GM_ST(void *, (uint8_t *)&t322, (void *)(gm_s84k1uo35omt8));
                GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(9));
                gm_tftb1odc739t5 t323 = gm_f_extend_0cs85gs(t321, t322);
                gm_tftb1odc739t5 t324 = t323;
                if (((((uint8_t *)&t324))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t324) + 1, gm_src3m73omiakl12, 275, 16);
                gm_b0_1 t325 = GM_LD(gm_b0_1, (((uint8_t *)&t324) + 1));
            }
            goto ix318;
ie318:;
ix318:;
        }
        {
            void * t326 = (void *)((uint8_t *)&F.l24);
            gm_b16_8 t327;
            GM_ST(void *, (uint8_t *)&t327, (void *)(gm_sikpqtja8pqpv));
            GM_ST(uintptr_t, (uint8_t *)&t327 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t328 = gm_f_extend_0cs85gs(t326, t327);
            gm_tftb1odc739t5 t329 = t328;
            if (((((uint8_t *)&t329))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t329) + 1, gm_src3m73omiakl12, 276, 5);
            gm_b0_1 t330 = GM_LD(gm_b0_1, (((uint8_t *)&t329) + 1));
        }
        {
            void * t331 = gm_l0;
            void * t332 = GM_LD(void *, (((uint8_t *)t331) + 0));
            void * t333 = (void *)(((uint8_t *)t332) + 1928);
            gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
            gm_b16_8 t335 = t334;
            gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
            gm_tftb1odc739t5 t337 = gm_f_extend_0cs85gs(t333, t336);
            gm_tftb1odc739t5 t338 = t337;
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_src3m73omiakl12, 277, 5);
            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
        }
        {
            gm_t6j6smj7qoctu t340;
            gm_b16_8 t341 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t340 + 0, t341);
            gm_b16_8 t342 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t343 = t342;
            gm_b16_8 t344 = GM_LD(gm_b16_8, ((uint8_t *)&t343));
            gm_b16_8 t345;
            GM_ST(void *, (uint8_t *)&t345, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t346;
            gm_f_join_sfknljb((void *)&t346, (void *)NULL, cx, NULL, t344, t345);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t347 = t346;
            if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 8, gm_src3m73omiakl12, 279, 40);
            gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&t347) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t340 + 16, t348);
            gm_tabb710d9mq2e t349;
            gm_f_format_a09tbhs((void *)&t349, (void *)NULL, cx, NULL, &t340);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t350 = t349;
            if (((((uint8_t *)&t350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t350) + 8, gm_src3m73omiakl12, 279, 13);
            gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&t350) + 8));
            gm_l25 = t351;
        }
        {
            uint64_t t353 = GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 8) + 0) + 8);
            uint8_t t354 = ((t353 == ((uint64_t)0ULL)));
            if (!t354) goto ie352;
            {
                void * t356 = gm_l0;
                gm_b16_8 t357 = gm_l25;
                gm_tnpifv20irnph t358;
                GM_ST(gm_b16_8, ((uint8_t *)&t358 + 0), t357);
                gm_b0_1 t359 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t356, t358);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix352;
ie352:;
            {
                void * t361 = gm_l0;
                gm_b16_8 t362 = gm_l25;
                gm_tnpifv20irnph t363;
                GM_ST(gm_b16_8, ((uint8_t *)&t363 + 0), t362);
                gm_b0_1 t364 = gm_f_linef_q6e8rn6((void *)NULL, cx, NULL, t361, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t366 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l10) + 8) + 0));
                    gm_b16_8 t367 = t366;
                    gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&t367));
                    gm_l26 = t368;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t370 = gm_l27;
                        uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint8_t t372 = (t370 < t371);
                        if (!t372) goto lb369;
                        {
                            gm_t19us6e77elqj t375;
                            uint64_t t376 = gm_l27;
                            GM_ST(uint64_t, (uint8_t *)&t375 + 0, t376);
                            uint64_t t377 = gm_l27;
                            uint64_t t378 = (uintptr_t)t377;
                            uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                            memcpy((uint8_t *)&t375 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t378 * 32), 32);
                            gm_t19us6e77elqj t380 = t375;
                            uint64_t t383 = GM_LD(uint64_t, (((uint8_t *)&t380) + 0));
                            gm_l28 = t383;
                            gm_tj64ui6kruni6 t384 = GM_LD(gm_tj64ui6kruni6, (((uint8_t *)&t380) + 8));
                            gm_l29 = t384;
                            {
                                void * t386 = gm_l0;
                                uint64_t t387 = gm_l28;
                                uint64_t r388; bool o389 = __builtin_add_overflow(t387, ((uint64_t)1ULL), &r388);
                                if (o389) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 285, 34);
                                uint64_t t390 = r388;
                                gm_t4ce0jd3tdro4 t391;
                                GM_ST(uint64_t, ((uint8_t *)&t391 + 0), t390);
                                gm_b0_1 t392 = gm_f_linef_j9naoa6((void *)NULL, cx, NULL, t386, t391);
                                if (gm_unw.kind) {
                                    goto gm_ll373;
                                }
                            }
                            {
                                void * t393 = gm_l0;
                                void * t394 = gm_l0;
                                uint64_t t395 = GM_LD(uint64_t, (((uint8_t *)t394) + 224));
                                uint64_t r396; bool o397 = __builtin_add_overflow(t395, ((uint64_t)1ULL), &r396);
                                if (o397) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 286, 13);
                                uint64_t t398 = r396;
                                GM_ST(uint64_t, (((uint8_t *)t393) + 224), t398);
                            }
                            {
                                void * t399 = gm_l0;
                                gm_b16_8 t400 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 0));
                                gm_tf5srh4mp4b3i t401 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l29) + 16));
                                gm_b0_1 t402 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t399, t400, t401);
                                if (gm_unw.kind) {
                                    goto gm_ll373;
                                }
                            }
                            {
                                void * t403 = gm_l0;
                                void * t404 = gm_l0;
                                uint64_t t405 = GM_LD(uint64_t, (((uint8_t *)t404) + 224));
                                uint64_t r406; bool o407 = __builtin_sub_overflow(t405, ((uint64_t)1ULL), &r406);
                                if (o407) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 288, 13);
                                uint64_t t408 = r406;
                                GM_ST(uint64_t, (((uint8_t *)t403) + 224), t408);
                            }
                            goto me381;
mn382:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 284, 9);
me381:;
                        }
lc369:;
                        {
                            uint64_t t410 = gm_l27;
                            uint64_t r411; bool o412 = __builtin_add_overflow(t410, ((uint64_t)1ULL), &r411);
                            uint64_t t413 = r411;
                            gm_l27 = t413;
                        }
                    }
gm_ll373:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb369; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc369; }
                    }
                    goto gm_lad;
lb369:;
                }
            }
            {
                void * t414 = gm_l0;
                gm_b16_8 t415;
                GM_ST(void *, (uint8_t *)&t415, (void *)(gm_s2cuflhqkkuaq));
                GM_ST(uintptr_t, (uint8_t *)&t415 + 8, (uintptr_t)(1));
                gm_b0_1 t416 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t414, t415);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix352:;
        }
    }
    gm_b0_1 t417;
    gm_tqgtd5gtgj65s t418;
    memset(&t418, 0, sizeof t418);
    ((uint8_t *)&t418)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t418 + 8), t417);
    gm_tqgtd5gtgj65s t419 = t418;
    F.ret = t419;
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
inline __attribute__((aligned(4))) void gm_f_outline_stmts_4hhf3mf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_outline_stmts_4hhf3mf F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* stmts */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                gm_l2 = t4;
            }
            {
                for (;;) {
                    uint64_t t6 = gm_l3;
                    uint64_t t7 = gm_l2;
                    uint8_t t8 = (t6 < t7);
                    if (!t8) goto lb5;
                    {
                        uint64_t t11 = gm_l3;
                        uint64_t t12 = t11;
                        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                        gm_l4 = t15;
                        {
                            void * t17 = gm_l0;
                            uint64_t t18 = gm_l4;
                            uint64_t t19 = (uintptr_t)t18;
                            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src3m73omiakl12, 201, 48);
                            void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t19 * 32);
                            gm_tqgtd5gtgj65s t22;
                            gm_f_stmt_block_5vkbn0n((void *)&t22, (void *)NULL, cx, NULL, t17, t21);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tqgtd5gtgj65s t23 = t22;
                            if (((((uint8_t *)&t23))[0] != 0)) {
                                gm_t08olq97rscn3 t24 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t23) + 8));
                                gm_tqgtd5gtgj65s t25;
                                GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                                ((uint8_t *)&t25)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t25 + 8), t24);
                                F.ret = t25;
                                goto gm_fret;
                            }
                            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
                        }
                        goto me13;
mn14:;
                        gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 201, 5);
me13:;
                    }
lc5:;
                    {
                        uint64_t t28 = gm_l3;
                        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                        uint64_t t31 = r29;
                        gm_l3 = t31;
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
    gm_b0_1 t32;
    gm_tqgtd5gtgj65s t33;
    memset(&t33, 0, sizeof t33);
    ((uint8_t *)&t33)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t33 + 8), t32);
    gm_tqgtd5gtgj65s t34 = t33;
    F.ret = t34;
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
inline __attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_outlines_2kvbvit F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* b */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        uint8_t t5 = gm_f_long_body_qgvuu1c((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint32_t t11 = gm_l1;
        void * t12 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 152));
        void * t14 = t13;
        void * t15;
        if ((GM_LD(void *, ((uint8_t *)&t14)) != 0)) {
            void * t16 = GM_LD(void *, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t15;
    }
    {
        void * t18 = gm_l2;
        uint8_t t19 = gm_f_any_outlined_7juvsf3((void *)NULL, cx, NULL, t18);
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
__attribute__((aligned(4))) void gm_f_runs_of_adlemfk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runs_of_adlemfk F;
    gm_b16_8 gm_l0; /* weights */
    uint64_t gm_l2; /* small */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* w */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* start */
    uint32_t gm_l8; /* total */
    GM_STACK_CHECK(&F, 607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_1imoitn((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 4));
                    uint32_t t14 = t13;
                    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&t14));
                    gm_l5 = t17;
                    {
                        uint32_t t20 = gm_l5;
                        uint32_t t21 = GM_LD(uint32_t, gm_cega6s9dl3o);
                        uint8_t t22 = (t20 <= t21);
                        if (!t22) goto ie19;
                        {
                            uint64_t t24 = gm_l2;
                            uint32_t t25 = gm_l5;
                            uint64_t t26 = (uint64_t)t25;
                            uint64_t r27; bool o28 = __builtin_add_overflow(t24, t26, &r27);
                            if (o28) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 143, 30);
                            uint64_t t29 = r27;
                            gm_l2 = t29;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 142, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t31 = gm_l4;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l4 = t34;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t36 = 0;
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t38 = (t37 < ((uint64_t)2ULL));
        if (!t38) {
            uint64_t t39 = gm_l2;
            uint32_t t40 = GM_LD(uint32_t, gm_cega6s9dl3o);
            uint64_t t41 = (uint64_t)t40;
            uint8_t t42 = (t39 <= t41);
            t36 = t42;
        } else t36 = 1;
        if (!t36) goto ie35;
        {
            void * t44 = (void *)((uint8_t *)&F.l1);
            gm_terja4da2tj6r t45;
            memset((uint8_t *)&t45 + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&t45 + 0, ((uint64_t)0ULL));
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            GM_ST(uint64_t, (uint8_t *)&t45 + 8, t46);
            GM_ST(uint8_t, (uint8_t *)&t45 + 16, ((uint8_t)0));
            gm_tftb1odc739t5 t47 = gm_f_push_ai20sgs(t44, &t45);
            gm_tftb1odc739t5 t48 = t47;
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src3m73omiakl12, 146, 9);
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l1), 40);
            goto gm_fdone;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        gm_l6 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t51 = gm_l6;
            uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t53 = (t51 < t52);
            if (!t53) goto lb50;
            {
                uint64_t t57 = gm_l6;
                uint64_t t58 = (uintptr_t)t57;
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_src3m73omiakl12, 152, 12);
                uint32_t t60 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t58 * 4));
                uint32_t t61 = GM_LD(uint32_t, gm_cega6s9dl3o);
                uint8_t t62 = (t60 > t61);
                if (!t62) goto ie56;
                {
                    void * t64 = (void *)((uint8_t *)&F.l1);
                    gm_terja4da2tj6r t65;
                    memset((uint8_t *)&t65 + 17, 0, 7);
                    uint64_t t66 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t65 + 0, t66);
                    uint64_t t67 = gm_l6;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    if (o69) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 153, 38);
                    uint64_t t70 = r68;
                    GM_ST(uint64_t, (uint8_t *)&t65 + 8, t70);
                    GM_ST(uint8_t, (uint8_t *)&t65 + 16, ((uint8_t)0));
                    gm_tftb1odc739t5 t71 = gm_f_push_ai20sgs(t64, &t65);
                    gm_tftb1odc739t5 t72 = t71;
                    if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_src3m73omiakl12, 153, 13);
                    gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                }
                {
                    uint64_t t74 = gm_l6;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    if (o76) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 154, 13);
                    uint64_t t77 = r75;
                    gm_l6 = t77;
                }
                {
                    goto lc50;
                }
                goto ix56;
ie56:;
ix56:;
            }
            {
                uint64_t t78 = gm_l6;
                gm_l7 = t78;
            }
            {
                gm_l8 = ((uint32_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t80 = gm_l6;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t82 = (t80 < t81);
                    if (!t82) goto lb79;
                    uint64_t t83 = gm_l6;
                    uint64_t t84 = (uintptr_t)t83;
                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t84 >= (uintptr_t)(t85)) gm_trap_index(t84, t85, gm_src3m73omiakl12, 159, 35);
                    uint32_t t86 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t84 * 4));
                    uint32_t t87 = GM_LD(uint32_t, gm_cega6s9dl3o);
                    uint8_t t88 = (t86 <= t87);
                    if (!t88) goto lb79;
                    uint32_t t89 = gm_l8;
                    uint64_t t90 = gm_l6;
                    uint64_t t91 = (uintptr_t)t90;
                    uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_src3m73omiakl12, 159, 72);
                    uint32_t t93 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t91 * 4));
                    uint32_t r94; bool o95 = __builtin_add_overflow(t89, t93, &r94);
                    if (o95) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 159, 64);
                    uint32_t t96 = r94;
                    uint32_t t97 = GM_LD(uint32_t, gm_cega6s9dl3o);
                    uint8_t t98 = (t96 <= t97);
                    if (!t98) goto lb79;
                    {
                        uint32_t t101 = gm_l8;
                        uint64_t t102 = gm_l6;
                        uint64_t t103 = (uintptr_t)t102;
                        uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_src3m73omiakl12, 160, 22);
                        uint32_t t105 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t103 * 4));
                        uint32_t r106; bool o107 = __builtin_add_overflow(t101, t105, &r106);
                        if (o107) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 160, 13);
                        uint32_t t108 = r106;
                        gm_l8 = t108;
                    }
                    {
                        uint64_t t109 = gm_l6;
                        uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                        if (o111) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 161, 13);
                        uint64_t t112 = r110;
                        gm_l6 = t112;
                    }
lc79:;
                }
lb79:;
            }
            {
                void * t113 = (void *)((uint8_t *)&F.l1);
                gm_terja4da2tj6r t114;
                memset((uint8_t *)&t114 + 17, 0, 7);
                uint64_t t115 = gm_l7;
                GM_ST(uint64_t, (uint8_t *)&t114 + 0, t115);
                uint64_t t116 = gm_l6;
                GM_ST(uint64_t, (uint8_t *)&t114 + 8, t116);
                uint32_t t117 = gm_l8;
                uint32_t t118 = GM_LD(uint32_t, gm_crhue63gorj);
                uint8_t t119 = (t117 >= t118);
                GM_ST(uint8_t, (uint8_t *)&t114 + 16, t119);
                gm_tftb1odc739t5 t120 = gm_f_push_ai20sgs(t113, &t114);
                gm_tftb1odc739t5 t121 = t120;
                if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_src3m73omiakl12, 163, 9);
                gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
            }
lc50:;
        }
lb50:;
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
__attribute__((aligned(4))) void gm_f_stmt_list_d9ovq7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_stmt_list_d9ovq7d F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* stmts */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_terja4da2tj6r gm_l12; /* r */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* j */
    GM_STACK_CHECK(&F, 1338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            uint8_t t4 = 0;
            void * t5 = gm_l0;
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 348));
            uint8_t t7 = (uint8_t)!t6;
            if (!t7) {
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t9 = (t8 < ((uint64_t)2ULL));
                t4 = t9;
            } else t4 = 1;
            if (!t4) goto ie3;
            {
                {
                    gm_l3 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_l2 = t12;
                }
                {
                    for (;;) {
                        uint64_t t14 = gm_l3;
                        uint64_t t15 = gm_l2;
                        uint8_t t16 = (t14 < t15);
                        if (!t16) goto lb13;
                        {
                            uint64_t t19 = gm_l3;
                            uint64_t t20 = t19;
                            uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                            gm_l4 = t23;
                            {
                                void * t25 = gm_l0;
                                uint64_t t26 = gm_l4;
                                uint64_t t27 = (uintptr_t)t26;
                                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_src3m73omiakl12, 119, 52);
                                void * t29 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 32);
                                gm_tqgtd5gtgj65s t30;
                                gm_f_stmt_block_5vkbn0n((void *)&t30, (void *)NULL, cx, NULL, t25, t29);
                                if (gm_unw.kind) {
                                    goto gm_ll17;
                                }
                                gm_tqgtd5gtgj65s t31 = t30;
                                if (((((uint8_t *)&t31))[0] != 0)) {
                                    gm_t08olq97rscn3 t32 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t31) + 8));
                                    gm_tqgtd5gtgj65s t33;
                                    GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                                    ((uint8_t *)&t33)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t33 + 8), t32);
                                    F.ret = t33;
                                    goto gm_fret;
                                }
                                gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
                            }
                            goto me21;
mn22:;
                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 119, 9);
me21:;
                        }
lc13:;
                        {
                            uint64_t t36 = gm_l3;
                            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                            uint64_t t39 = r37;
                            gm_l3 = t39;
                        }
                    }
gm_ll17:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
                    }
                    goto gm_lad;
lb13:;
                }
            }
            {
                gm_b0_1 t40;
                gm_tqgtd5gtgj65s t41;
                memset(&t41, 0, sizeof t41);
                ((uint8_t *)&t41)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t41 + 8), t40);
                gm_tqgtd5gtgj65s t42 = t41;
                uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
                F.ret = t41;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            gm_f_list_go2qg0b((void *)&F.l5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                gm_l6 = t45;
            }
            {
                for (;;) {
                    uint64_t t47 = gm_l7;
                    uint64_t t48 = gm_l6;
                    uint8_t t49 = (t47 < t48);
                    if (!t49) goto lb46;
                    {
                        uint64_t t52 = gm_l7;
                        uint64_t t53 = t52;
                        uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&t53));
                        gm_l8 = t56;
                        {
                            void * t58 = (void *)((uint8_t *)&F.l5);
                            uint64_t t59 = gm_l8;
                            uint64_t t60 = (uintptr_t)t59;
                            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_src3m73omiakl12, 123, 55);
                            void * t62 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t60 * 32);
                            uint32_t t63 = GM_LD(uint32_t, gm_cega6s9dl3o);
                            uint32_t r64; bool o65 = __builtin_add_overflow(t63, ((uint32_t)1ULL), &r64);
                            if (o65) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 123, 65);
                            uint32_t t66 = r64;
                            uint32_t t67 = gm_f_stmt_weight_21p4irs(t62, t66);
                            gm_tftb1odc739t5 t68 = gm_f_push_im9d4ro(t58, t67);
                            gm_tftb1odc739t5 t69 = t68;
                            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_src3m73omiakl12, 123, 29);
                            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
                        }
                        goto me54;
mn55:;
                        gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 123, 5);
me54:;
                    }
lc46:;
                    {
                        uint64_t t72 = gm_l7;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l7 = t75;
                    }
                }
lb46:;
            }
        }
        {
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t77 = t76;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_f_runs_of_adlemfk((void *)&F.l9, (void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_l10 = t80;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t82 = gm_l11;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t84 = (t82 < t83);
                    if (!t84) goto lb81;
                    {
                        uint64_t t87 = gm_l11;
                        uint64_t t88 = (uintptr_t)t87;
                        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_terja4da2tj6r t92 = GM_LD(gm_terja4da2tj6r, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t88 * 24));
                        gm_l12 = t92;
                        {
                            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 16));
                            if (!t95) goto ie94;
                            {
                                void * t97 = gm_l0;
                                uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                void * t99 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 0));
                                uint64_t t101 = (uintptr_t)t100;
                                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t103 = (uintptr_t)t102;
                                if (t101 > t103 || t103 > (uintptr_t)t98) gm_trap_slice(t101, t103, t98, gm_src3m73omiakl12, 127, 32);
                                gm_b16_8 t104;
                                GM_ST(void *, (uint8_t *)&t104, (void *)(((uint8_t *)t99 + t101 * 32)));
                                GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(t103 - t101));
                                gm_tqgtd5gtgj65s t105;
                                gm_f_outline_run_cjmhkvl((void *)&t105, (void *)NULL, cx, NULL, t97, t104);
                                if (gm_unw.kind) {
                                    goto gm_ll85;
                                }
                                gm_tqgtd5gtgj65s t106 = t105;
                                if (((((uint8_t *)&t106))[0] != 0)) {
                                    gm_t08olq97rscn3 t107 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t106) + 8));
                                    gm_tqgtd5gtgj65s t108;
                                    GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                                    ((uint8_t *)&t108)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t108 + 8), t107);
                                    F.ret = t108;
                                    goto gm_fret;
                                }
                                gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 8));
                            }
                            goto ix94;
ie94:;
                            {
                                {
                                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 0));
                                    gm_l14 = t112;
                                }
                                {
                                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    gm_l13 = t113;
                                }
                                {
                                    for (;;) {
                                        uint64_t t115 = gm_l14;
                                        uint64_t t116 = gm_l13;
                                        uint8_t t117 = (t115 < t116);
                                        if (!t117) goto lb114;
                                        {
                                            uint64_t t120 = gm_l14;
                                            uint64_t t121 = t120;
                                            uint64_t t124 = GM_LD(uint64_t, ((uint8_t *)&t121));
                                            gm_l15 = t124;
                                            {
                                                void * t126 = gm_l0;
                                                uint64_t t127 = gm_l15;
                                                uint64_t t128 = (uintptr_t)t127;
                                                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                                if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_src3m73omiakl12, 129, 58);
                                                void * t130 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t128 * 32);
                                                gm_tqgtd5gtgj65s t131;
                                                gm_f_stmt_block_5vkbn0n((void *)&t131, (void *)NULL, cx, NULL, t126, t130);
                                                if (gm_unw.kind) {
                                                    goto gm_ll118;
                                                }
                                                gm_tqgtd5gtgj65s t132 = t131;
                                                if (((((uint8_t *)&t132))[0] != 0)) {
                                                    gm_t08olq97rscn3 t133 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t132) + 8));
                                                    gm_tqgtd5gtgj65s t134;
                                                    GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
                                                    ((uint8_t *)&t134)[0] = 1;
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t134 + 8), t133);
                                                    F.ret = t134;
                                                    goto gm_fret;
                                                }
                                                gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 8));
                                            }
                                            goto me122;
mn123:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 129, 13);
me122:;
                                        }
lc114:;
                                        {
                                            uint64_t t137 = gm_l14;
                                            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                            uint64_t t140 = r138;
                                            gm_l14 = t140;
                                        }
                                    }
gm_ll118:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb114; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc114; }
                                    }
                                    goto gm_ll85;
lb114:;
                                }
                            }
ix94:;
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 125, 5);
me90:;
                    }
lc81:;
                    {
                        uint64_t t142 = gm_l11;
                        uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                        uint64_t t145 = r143;
                        gm_l11 = t145;
                    }
                }
gm_ll85:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
                }
                goto gm_lad;
lb81:;
            }
        }
    }
    gm_b0_1 t146;
    gm_tqgtd5gtgj65s t147;
    memset(&t147, 0, sizeof t147);
    ((uint8_t *)&t147)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t147 + 8), t146);
    gm_tqgtd5gtgj65s t148 = t147;
    F.ret = t148;
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
__attribute__((aligned(4))) uint32_t gm_f_stmt_weight_21p4irs(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_weight_21p4irs F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* cap */
    uint32_t gm_l2; /* n */
    void * gm_l3; /* init */
    void * gm_l4; /* x */
    void * gm_l5; /* init */
    void * gm_l6; /* place */
    void * gm_l7; /* value */
    gm_b16_8 gm_l8; /* places */
    void * gm_l9; /* value */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l12; /* p */
    void * gm_l13; /* x */
    void * gm_l14; /* e */
    void * gm_l15; /* e */
    GM_STACK_CHECK(&F, 717u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint32_t)1ULL);
    }
    {
        void * t2 = gm_l0;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; default: goto ma10; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        void * t14 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t14;
        void * t16 = gm_l3;
        void * t17 = t16;
        if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie15;
        void * t18 = GM_LD(void *, ((uint8_t *)&t17));
        gm_l4 = t18;
        {
            uint32_t t21 = gm_l2;
            uint32_t t22 = gm_l1;
            uint8_t t23 = (t21 < t22);
            if (!t23) goto ie20;
            {
                uint32_t t25 = gm_l2;
                void * t26 = gm_l4;
                uint32_t t27 = gm_l1;
                uint32_t t28 = gm_l2;
                uint32_t r29; bool o30 = __builtin_sub_overflow(t27, t28, &r29);
                if (o30) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 72, 70);
                uint32_t t31 = r29;
                uint32_t t32 = gm_f_weight_nu7ub4o(t26, t31);
                uint32_t r33; bool o34 = __builtin_add_overflow(t25, t32, &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 72, 55);
                uint32_t t35 = r33;
                gm_l2 = t35;
            }
            goto ix20;
ie20:;
ix20:;
        }
        goto ix15;
ie15:;
ix15:;
        goto me3;
mn12:;
ma5:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t37 != ((uint8_t)1ULL)) goto mn36;
        void * t38 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 16));
        gm_l5 = t38;
        uint32_t t40 = gm_l2;
        uint32_t t41 = gm_l1;
        uint8_t t42 = (t40 < t41);
        if (!t42) goto ie39;
        {
            uint32_t t44 = gm_l2;
            void * t45 = gm_l5;
            uint32_t t46 = gm_l1;
            uint32_t t47 = gm_l2;
            uint32_t r48; bool o49 = __builtin_sub_overflow(t46, t47, &r48);
            if (o49) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 73, 62);
            uint32_t t50 = r48;
            uint32_t t51 = gm_f_weight_nu7ub4o(t45, t50);
            uint32_t r52; bool o53 = __builtin_add_overflow(t44, t51, &r52);
            if (o53) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 73, 44);
            uint32_t t54 = r52;
            gm_l2 = t54;
        }
        goto ix39;
ie39:;
ix39:;
        goto me3;
mn36:;
ma6:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t56 != ((uint8_t)2ULL)) goto mn55;
        void * t57 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 0));
        gm_l6 = t57;
        void * t58 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t58;
        {
            uint32_t t61 = gm_l2;
            uint32_t t62 = gm_l1;
            uint8_t t63 = (t61 < t62);
            if (!t63) goto ie60;
            {
                uint32_t t65 = gm_l2;
                void * t66 = gm_l6;
                uint32_t t67 = gm_l1;
                uint32_t t68 = gm_l2;
                uint32_t r69; bool o70 = __builtin_sub_overflow(t67, t68, &r69);
                if (o70) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 75, 45);
                uint32_t t71 = r69;
                uint32_t t72 = gm_f_weight_nu7ub4o(t66, t71);
                uint32_t r73; bool o74 = __builtin_add_overflow(t65, t72, &r73);
                if (o74) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 75, 26);
                uint32_t t75 = r73;
                gm_l2 = t75;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            uint32_t t77 = gm_l2;
            uint32_t t78 = gm_l1;
            uint8_t t79 = (t77 < t78);
            if (!t79) goto ie76;
            {
                uint32_t t81 = gm_l2;
                void * t82 = gm_l7;
                uint32_t t83 = gm_l1;
                uint32_t t84 = gm_l2;
                uint32_t r85; bool o86 = __builtin_sub_overflow(t83, t84, &r85);
                if (o86) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 76, 45);
                uint32_t t87 = r85;
                uint32_t t88 = gm_f_weight_nu7ub4o(t82, t87);
                uint32_t r89; bool o90 = __builtin_add_overflow(t81, t88, &r89);
                if (o90) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 76, 26);
                uint32_t t91 = r89;
                gm_l2 = t91;
            }
            goto ix76;
ie76:;
ix76:;
        }
        goto me3;
mn55:;
ma7:;
        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t93 != ((uint8_t)3ULL)) goto mn92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l8 = t94;
        void * t95 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 16));
        gm_l9 = t95;
        {
            {
                gm_b16_8 t98 = gm_l8;
                gm_l10 = t98;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t100 = gm_l11;
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t102 = (t100 < t101);
                    if (!t102) goto lb99;
                    {
                        uint64_t t105 = gm_l11;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        void * t108 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t106 * 8));
                        void * t109 = t108;
                        void * t112 = GM_LD(void *, ((uint8_t *)&t109));
                        gm_l12 = t112;
                        {
                            void * t115 = gm_l12;
                            void * t116 = t115;
                            if (!(GM_LD(void *, ((uint8_t *)&t116)) != 0)) goto ie114;
                            void * t117 = GM_LD(void *, ((uint8_t *)&t116));
                            gm_l13 = t117;
                            {
                                uint32_t t120 = gm_l2;
                                uint32_t t121 = gm_l1;
                                uint8_t t122 = (t120 < t121);
                                if (!t122) goto ie119;
                                {
                                    uint32_t t124 = gm_l2;
                                    void * t125 = gm_l13;
                                    uint32_t t126 = gm_l1;
                                    uint32_t t127 = gm_l2;
                                    uint32_t r128; bool o129 = __builtin_sub_overflow(t126, t127, &r128);
                                    if (o129) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 81, 49);
                                    uint32_t t130 = r128;
                                    uint32_t t131 = gm_f_weight_nu7ub4o(t125, t130);
                                    uint32_t r132; bool o133 = __builtin_add_overflow(t124, t131, &r132);
                                    if (o133) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 81, 34);
                                    uint32_t t134 = r132;
                                    gm_l2 = t134;
                                }
                                goto ix119;
ie119:;
ix119:;
                            }
                            goto ix114;
ie114:;
ix114:;
                        }
                        goto me110;
mn111:;
                        gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 79, 13);
me110:;
                    }
lc99:;
                    {
                        uint64_t t136 = gm_l11;
                        uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                        uint64_t t139 = r137;
                        gm_l11 = t139;
                    }
                }
lb99:;
            }
        }
        {
            uint32_t t141 = gm_l2;
            uint32_t t142 = gm_l1;
            uint8_t t143 = (t141 < t142);
            if (!t143) goto ie140;
            {
                uint32_t t145 = gm_l2;
                void * t146 = gm_l9;
                uint32_t t147 = gm_l1;
                uint32_t t148 = gm_l2;
                uint32_t r149; bool o150 = __builtin_sub_overflow(t147, t148, &r149);
                if (o150) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 84, 45);
                uint32_t t151 = r149;
                uint32_t t152 = gm_f_weight_nu7ub4o(t146, t151);
                uint32_t r153; bool o154 = __builtin_add_overflow(t145, t152, &r153);
                if (o154) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 84, 26);
                uint32_t t155 = r153;
                gm_l2 = t155;
            }
            goto ix140;
ie140:;
ix140:;
        }
        goto me3;
mn92:;
ma8:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t157 != ((uint8_t)4ULL)) goto mn156;
        void * t158 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l14 = t158;
        uint32_t t160 = gm_l2;
        uint32_t t161 = gm_l1;
        uint8_t t162 = (t160 < t161);
        if (!t162) goto ie159;
        {
            uint32_t t164 = gm_l2;
            void * t165 = gm_l14;
            uint32_t t166 = gm_l1;
            uint32_t t167 = gm_l2;
            uint32_t r168; bool o169 = __builtin_sub_overflow(t166, t167, &r168);
            if (o169) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 86, 49);
            uint32_t t170 = r168;
            uint32_t t171 = gm_f_weight_nu7ub4o(t165, t170);
            uint32_t r172; bool o173 = __builtin_add_overflow(t164, t171, &r172);
            if (o173) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 86, 34);
            uint32_t t174 = r172;
            gm_l2 = t174;
        }
        goto ix159;
ie159:;
ix159:;
        goto me3;
mn156:;
ma9:;
        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t176 != ((uint8_t)5ULL)) goto mn175;
        void * t177 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t177;
        uint32_t t179 = gm_l2;
        uint32_t t180 = gm_l1;
        uint8_t t181 = (t179 < t180);
        if (!t181) goto ie178;
        {
            uint32_t t183 = gm_l2;
            void * t184 = gm_l15;
            uint32_t t185 = gm_l1;
            uint32_t t186 = gm_l2;
            uint32_t r187; bool o188 = __builtin_sub_overflow(t185, t186, &r187);
            if (o188) gm_trap("integer overflow in `-`", gm_src3m73omiakl12, 87, 53);
            uint32_t t189 = r187;
            uint32_t t190 = gm_f_weight_nu7ub4o(t184, t189);
            uint32_t r191; bool o192 = __builtin_add_overflow(t183, t190, &r191);
            if (o192) gm_trap("integer overflow in `+`", gm_src3m73omiakl12, 87, 38);
            uint32_t t193 = r191;
            gm_l2 = t193;
        }
        goto ix178;
ie178:;
ix178:;
        goto me3;
mn175:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_src3m73omiakl12, 71, 5);
me3:;
    }
    {
        uint32_t t194 = gm_l2;
        uint32_t t195 = gm_l1;
        uint32_t t196 = (t194 < t195 ? t194 : t195);
        uint8_t t197 = 0;
        F.ret = t196;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_weight_nu7ub4o(void * a0, uint32_t a1);
