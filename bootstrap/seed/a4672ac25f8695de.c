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
typedef struct gm_cs8r2qbercod2k { uint32_t f0; uint8_t f1; uint8_t f2; } gm_cs8r2qbercod2k;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint32_t f0; uint8_t f1; uint8_t f2; uint8_t pe[2]; } gm_t0n7rigt5nh46;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs8r2qbercod2k) == 8 && _Alignof(gm_cs8r2qbercod2k) == 4 && offsetof(gm_cs8r2qbercod2k, f0) == 0 && offsetof(gm_cs8r2qbercod2k, f1) == 4 && offsetof(gm_cs8r2qbercod2k, f2) == 5, "layout of `LoopState`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t0n7rigt5nh46) == 8 && _Alignof(gm_t0n7rigt5nh46) == 4 && offsetof(gm_t0n7rigt5nh46, f0) == 0 && offsetof(gm_t0n7rigt5nh46, f1) == 4 && offsetof(gm_t0n7rigt5nh46, f2) == 5, "layout of `LoopState`");
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { gm_t0n7rigt5nh46 f0; uint8_t f1; uint8_t pe[3]; } gm_t72g7o4mtivnc;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t72g7o4mtivnc) == 12 && _Alignof(gm_t72g7o4mtivnc) == 4 && offsetof(gm_t72g7o4mtivnc, f0) == 0 && offsetof(gm_t72g7o4mtivnc, f1) == 8, "layout of `?LoopState`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; } gm_tbliclsb5tiqp;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { uint64_t f0; gm_tdgfduqjheqdm f1; } gm_tks8t13utjt96;
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbliclsb5tiqp) == 96 && _Alignof(gm_tbliclsb5tiqp) == 8 && offsetof(gm_tbliclsb5tiqp, f0) == 0 && offsetof(gm_tbliclsb5tiqp, f1) == 8 && offsetof(gm_tbliclsb5tiqp, f2) == 16 && offsetof(gm_tbliclsb5tiqp, f3) == 56, "layout of `ContCheck`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tks8t13utjt96) == 72 && _Alignof(gm_tks8t13utjt96) == 8 && offsetof(gm_tks8t13utjt96, f0) == 0 && offsetof(gm_tks8t13utjt96, f1) == 8, "layout of `(i: usize, x: FnDef)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_check_continuations_ukkdj9c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* errors */
    gm_tdgfduqjheqdm l5; /* def */
    gm_tdgfduqjheqdm l11; /* f */
    gm_tbliclsb5tiqp l13; /* cx */
    gm_trqfvmjtt4sij l14; /* seen */
    gm_b0_1 ret;
};
struct gm_fr_fn_4f453lc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_clkgu5i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_it95hr1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9invmr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_k_09a6ffc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_k_block_87r6qua {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_k_children_3gr7c2u {
    gm_frame h;
    void * l0; /* cx */
    uint8_t l3; /* sc */
    uint8_t ret;
};
struct gm_fr_k_cond_d97airn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_k_consume_nr50dr5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_k_exit_ttiloj7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_k_expr_hdusp9d {
    gm_frame h;
    gm_t710lsrq1m8gk l35; /* l */
    gm_t710lsrq1m8gk l36; /* w */
    uint8_t ret;
};
struct gm_fr_k_jump_41ae4gj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_k_loop_ot156p6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mentions_local_6sbsseu {
    gm_frame h;
    uint32_t l1; /* k */
    uint8_t ret;
};
struct gm_fr_mentions_upvar_f852a0h {
    gm_frame h;
    uint32_t l1; /* k */
    uint8_t ret;
};
struct gm_fr_only_consumed_pk7topa {
    gm_frame h;
    uint32_t l1; /* k */
    uint8_t l11; /* ok */
    uint8_t ret;
};
struct gm_fr_pop_7fqs7gj {
    gm_frame h;
    gm_t72g7o4mtivnc ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_continuations_ukkdj9c(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_jk8kame(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_4f453lc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_clkgu5i(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_it95hr1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_fn_j9invmr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_k_09a6ffc(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_k_block_87r6qua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) uint8_t gm_f_k_children_3gr7c2u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_k_cond_d97airn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_k_consume_nr50dr5(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_k_exit_ttiloj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_k_expr_hdusp9d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_k_jump_41ae4gj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_te9hrk0u35rev a1, uint8_t a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_k_loop_ot156p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_list_1hi08ho(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_6ldri0d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_mentions_local_6sbsseu(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_mentions_upvar_f852a0h(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_only_consumed_pk7topa(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t72g7o4mtivnc gm_f_pop_7fqs7gj(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_dqg4p50(void * a0, gm_cs8r2qbercod2k a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_j129uo3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_truncate_0s6rl4q(void * a0, uint64_t a1);
extern uint8_t gm_c3seud9n9be[1] __attribute__((aligned(1)));
extern uint8_t gm_ckpkn5umvr0[1] __attribute__((aligned(1)));
extern const uint8_t gm_siho9h2ssfvv9[62];
extern const char gm_srcfa3vclj60ege[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svhflm4d80ub1[62];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t72g7o4mtivnc gm_f_pop_7fqs7gj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_7fqs7gj F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t0n7rigt5nh46 gm_l2; /* x */
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
            gm_t72g7o4mtivnc t8;
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
        gm_t0n7rigt5nh46 t17 = GM_LD(gm_t0n7rigt5nh46, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 8));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t0n7rigt5nh46 t29 = gm_l2;
        gm_t72g7o4mtivnc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(gm_t0n7rigt5nh46, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_check_continuations_ukkdj9c(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_continuations_ukkdj9c F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l6; /* body */
    uint32_t gm_l7; /* k */
    uint8_t gm_l8; /* captured */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l12; /* fb */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tbgd14esf0jvi gm_l17; /* e */
    uint8_t gm_l18; /* dup */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tbgd14esf0jvi gm_l21; /* x */
    GM_STACK_CHECK(&F, 1543u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_1hi08ho((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 384) + 0));
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_tks8t13utjt96 t13;
                    uint64_t t14 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l3;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 64), 64);
                    gm_tks8t13utjt96 t18 = t13;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l4 = t21;
                    gm_tdgfduqjheqdm t22 = GM_LD(gm_tdgfduqjheqdm, (((uint8_t *)&t18) + 8));
                    F.l5 = t22;
                    {
                        void * t24 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                        void * t25 = t24;
                        void * t26;
                        if ((GM_LD(void *, ((uint8_t *)&t25)) != 0)) {
                            void * t27 = GM_LD(void *, (((uint8_t *)&t25) + 0));
                            t26 = t27;
                        } else {
                            goto lc7;
                        }
                        gm_l6 = t26;
                    }
                    {
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                        gm_b16_8 t29 = t28;
                        void * t30 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t29)));
                        uint64_t t31 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t29) + 8);
                        uint64_t t32 = (t31 - 1);
                        gm_t9805qtonbsag t33;
                        if (t32 < t31) {
                        uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)t30 + t32 * 4));
                        gm_t9805qtonbsag t35;
                        GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t35), t34);
                        ((uint8_t *)&t35)[4] = 1;
                            t33 = t35;
                        } else {
                        gm_t9805qtonbsag t36;
                        memset(&t36, 0, sizeof t36);
                            t33 = t36;
                        }
                        gm_t9805qtonbsag t37 = t33;
                        uint32_t t38;
                        if (((((uint8_t *)&t37))[4] != 0)) {
                            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&t37) + 0));
                            t38 = t39;
                        } else {
                            goto lc7;
                        }
                        gm_l7 = t38;
                    }
                    {
                        void * t41 = gm_l0;
                        void * t42 = (void *)(((uint8_t *)t41) + 8);
                        uint32_t t43 = gm_l7;
                        uint64_t t44 = (uint64_t)t43;
                        uint64_t t45 = (uintptr_t)t44;
                        uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 24) + 8);
                        if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcfa3vclj60ege, 22, 23);
                        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 24)) + t45 * 40) + 16));
                        gm_t53at80pfdus3 t48;
                        gm_f_kind_jcnt4hm((void *)&t48, t42, GM_CONV(gm_csguankko4lmhd, t47));
                        gm_t53at80pfdus3 t49 = t48;
                        uint8_t t50;
                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                        if (t53 != ((uint8_t)18ULL)) goto mn52;
                        t50 = ((uint8_t)0);
                        goto me51;
mn52:;
                        t50 = ((uint8_t)1);
                        goto me51;
mn54:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 22, 12);
me51:;
                        if (!t50) goto ie40;
                        {
                            goto lc7;
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    {
                        gm_l8 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t57 = gm_l0;
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((((uint8_t *)t57) + 384) + 0));
                            gm_l9 = t58;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t60 = gm_l10;
                                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t62 = (t60 < t61);
                                if (!t62) goto lb59;
                                {
                                    uint64_t t65 = gm_l10;
                                    uint64_t t66 = (uintptr_t)t65;
                                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_tdgfduqjheqdm t70 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t66 * 64));
                                    F.l11 = t70;
                                    {
                                        gm_t9805qtonbsag t73 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l11) + 48));
                                        uint64_t t74 = gm_l4;
                                        uint32_t t75 = (uint32_t)t74;
                                        gm_t9805qtonbsag t76;
                                        GM_ST(uint8_t, (uint8_t *)&t76 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t76 + 6, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t76), t75);
                                        ((uint8_t *)&t76)[4] = 1;
                                        gm_t9805qtonbsag t77 = t73;
                                        gm_t9805qtonbsag t78 = t76;
                                        uint8_t t79 = (gm_eqkricceou4u(((uint8_t *)&t77), ((uint8_t *)&t78)));
                                        if (!t79) goto ie72;
                                        void * t80 = GM_LD(void *, (((uint8_t *)&F.l11) + 40));
                                        void * t81 = t80;
                                        if (!(GM_LD(void *, ((uint8_t *)&t81)) != 0)) goto ie72;
                                        void * t82 = GM_LD(void *, ((uint8_t *)&t81));
                                        gm_l12 = t82;
                                        void * t83 = gm_l12;
                                        uint32_t t84 = gm_l7;
                                        uint8_t t85 = gm_f_mentions_upvar_f852a0h(t83, t84);
                                        if (!t85) goto ie72;
                                        {
                                            gm_l8 = ((uint8_t)1);
                                        }
                                        goto ix72;
ie72:;
ix72:;
                                    }
                                    goto me68;
mn69:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 25, 9);
me68:;
                                }
lc59:;
                                {
                                    uint64_t t88 = gm_l10;
                                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                                    uint64_t t91 = r89;
                                    gm_l10 = t91;
                                }
                            }
lb59:;
                        }
                    }
                    {
                        uint8_t t93 = 0;
                        uint8_t t94 = gm_l8;
                        if (!t94) {
                            void * t95 = gm_l6;
                            uint32_t t96 = gm_l7;
                            uint8_t t97 = gm_f_only_consumed_pk7topa(t95, t96);
                            uint8_t t98 = (uint8_t)!t97;
                            t93 = t98;
                        } else t93 = 1;
                        if (!t93) goto ie92;
                        {
                            goto lc7;
                        }
                        goto ix92;
ie92:;
ix92:;
                    }
                    {
                        memset((uint8_t *)&F.l13 + 12, 0, 4);
                        void * t100 = gm_l0;
                        void * t101 = (void *)(((uint8_t *)t100) + 8);
                        GM_ST(void *, (uint8_t *)&F.l13 + 0, t101);
                        uint32_t t102 = gm_l7;
                        GM_ST(uint32_t, (uint8_t *)&F.l13 + 8, t102);
                        gm_f_list_6ldri0d((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 16)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_f_list_1hi08ho((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 56)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                    }
                    {
                        {
                            void * t104 = (void *)((uint8_t *)&F.l13);
                            void * t105 = gm_l6;
                            uint8_t t106 = GM_LD(uint8_t, gm_c3seud9n9be);
                            uint8_t t107 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t104, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                        }
                    }
                    {
                        void * t108 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t109 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l13) + 56) + 0));
                        gm_b16_8 t110 = t109;
                        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                        gm_tftb1odc739t5 t112 = gm_f_extend_jk8kame(t108, t111);
                        gm_tftb1odc739t5 t113 = t112;
                        if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srcfa3vclj60ege, 32, 9);
                        gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 19, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t116 = gm_l3;
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    uint64_t t119 = r117;
                    gm_l3 = t119;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        gm_f_list_1hi08ho((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l15 = t121;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t123 = gm_l16;
                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t125 = (t123 < t124);
                if (!t125) goto lb122;
                {
                    uint64_t t128 = gm_l16;
                    uint64_t t129 = (uintptr_t)t128;
                    uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_tbgd14esf0jvi t133 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t129 * 32));
                    gm_l17 = t133;
                    {
                        gm_l18 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                            gm_l19 = t136;
                        }
                        {
                            gm_l20 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t138 = gm_l20;
                                uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                uint8_t t140 = (t138 < t139);
                                if (!t140) goto lb137;
                                {
                                    uint64_t t143 = gm_l20;
                                    uint64_t t144 = (uintptr_t)t143;
                                    uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                    gm_tbgd14esf0jvi t148 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t144 * 32));
                                    gm_l21 = t148;
                                    {
                                        gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 0));
                                        gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 0));
                                        gm_tl874t2atnlh3 t153 = t151;
                                        gm_tl874t2atnlh3 t154 = t152;
                                        uint8_t t155 = (gm_eqff85ac10o3(((uint8_t *)&t153), ((uint8_t *)&t154)));
                                        if (!t155) goto ie150;
                                        gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 16));
                                        gm_b16_8 t158 = t156;
                                        gm_b16_8 t159 = t157;
                                        uint8_t t160 = (gm_eq0iqiubfm7e(((uint8_t *)&t158), ((uint8_t *)&t159)));
                                        if (!t160) goto ie150;
                                        {
                                            gm_l18 = ((uint8_t)1);
                                        }
                                        goto ix150;
ie150:;
ix150:;
                                    }
                                    goto me146;
mn147:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 37, 9);
me146:;
                                }
lc137:;
                                {
                                    uint64_t t163 = gm_l20;
                                    uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                                    uint64_t t166 = r164;
                                    gm_l20 = t166;
                                }
                            }
lb137:;
                        }
                    }
                    {
                        uint8_t t168 = gm_l18;
                        if (!t168) goto ie167;
                        {
                            goto lc122;
                        }
                        goto ix167;
ie167:;
ix167:;
                    }
                    {
                        void * t170 = (void *)((uint8_t *)&F.l14);
                        gm_tftb1odc739t5 t171 = gm_f_push_j129uo3(t170, &(*(gm_tbgd14esf0jvi *)((uint8_t *)&gm_l17)));
                        gm_tftb1odc739t5 t172 = t171;
                        if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srcfa3vclj60ege, 41, 9);
                        gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                    }
                    {
                        void * t174 = gm_l0;
                        gm_tl874t2atnlh3 t175 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 0));
                        gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 16));
                        gm_b0_1 t177 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t174, t175, t176);
                        if (gm_unw.kind) {
                            goto gm_ll126;
                        }
                    }
                    goto me131;
mn132:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 35, 5);
me131:;
                }
lc122:;
                {
                    uint64_t t179 = gm_l16;
                    uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                    uint64_t t182 = r180;
                    gm_l16 = t182;
                }
            }
gm_ll126:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb122; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc122; }
            }
            goto gm_lad;
lb122:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_4f453lc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_4f453lc F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mentions_local_6sbsseu *)((&F.h)->up))->l1));
    uint8_t t3 = gm_f_mentions_local_6sbsseu(t1, t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_clkgu5i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_clkgu5i F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_only_consumed_pk7topa *)((&F.h)->up))->l11));
        if (t3) {
            void * t4 = gm_l0;
            uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_only_consumed_pk7topa *)((&F.h)->up))->l1));
            uint8_t t6 = gm_f_only_consumed_pk7topa(t4, t5);
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_only_consumed_pk7topa *)((&F.h)->up))->l11), t2);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it95hr1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_it95hr1 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_k_children_3gr7c2u *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_k_children_3gr7c2u *)((&F.h)->up))->l3));
        uint8_t t5 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_k_children_3gr7c2u *)((&F.h)->up))->l3), t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j9invmr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_j9invmr F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mentions_upvar_f852a0h *)((&F.h)->up))->l1));
    uint8_t t3 = gm_f_mentions_upvar_f852a0h(t1, t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_k_09a6ffc(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_k_09a6ffc F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* k */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)15ULL)) goto ie2;
        uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            uint32_t t7 = gm_l2;
            uint32_t t8 = gm_l1;
            uint8_t t9 = ((t7 == t8));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t12 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_k_block_87r6qua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_k_block_87r6qua F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* sb */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* x */
    void * gm_l7; /* i */
    void * gm_l8; /* i */
    void * gm_l9; /* place */
    void * gm_l10; /* value */
    gm_b16_8 gm_l11; /* places */
    void * gm_l12; /* value */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l15; /* p */
    void * gm_l16; /* px */
    void * gm_l17; /* e */
    void * gm_l18; /* t */
    GM_STACK_CHECK(&F, 673u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t2 = gm_l2;
        gm_l3 = t2;
    }
    {
        {
            void * t4 = gm_l1;
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
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
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_trcmmd12cefb1 t17 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t13 * 32));
                    gm_l6 = t17;
                    {
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        switch (t28) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma25; case ((uint8_t)5ULL): goto ma26; default: goto ma27; }
ma20:;
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t30 != ((uint8_t)0ULL)) goto mn29;
                        if ((GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8)) != 0)) goto mn29;
                        goto me19;
mn29:;
ma21:;
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t33 != ((uint8_t)0ULL)) goto mn32;
                        if (!(GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8)) != 0)) goto mn32;
                        void * t34 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8));
                        gm_l7 = t34;
                        {
                            void * t36 = gm_l0;
                            void * t37 = gm_l7;
                            uint8_t t38 = gm_l3;
                            uint8_t t39 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t36, t37, t38);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l3 = t39;
                        }
                        goto me19;
mn32:;
ma22:;
                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t41 != ((uint8_t)1ULL)) goto mn40;
                        void * t42 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 16));
                        gm_l8 = t42;
                        {
                            void * t44 = gm_l0;
                            void * t45 = gm_l8;
                            uint8_t t46 = gm_l3;
                            uint8_t t47 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t44, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l3 = t47;
                        }
                        goto me19;
mn40:;
ma23:;
                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t49 != ((uint8_t)2ULL)) goto mn48;
                        void * t50 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 0));
                        gm_l9 = t50;
                        void * t51 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8));
                        gm_l10 = t51;
                        {
                            void * t53 = gm_l0;
                            void * t54 = gm_l9;
                            uint8_t t55 = gm_l3;
                            uint8_t t56 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t53, t54, t55);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l3 = t56;
                        }
                        {
                            void * t57 = gm_l0;
                            void * t58 = gm_l10;
                            uint8_t t59 = gm_l3;
                            uint8_t t60 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t57, t58, t59);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l3 = t60;
                        }
                        goto me19;
mn48:;
ma24:;
                        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t62 != ((uint8_t)3ULL)) goto mn61;
                        gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l6) + 8) + 0));
                        gm_l11 = t63;
                        void * t64 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 16));
                        gm_l12 = t64;
                        {
                            {
                                gm_b16_8 t67 = gm_l11;
                                gm_l13 = t67;
                            }
                            {
                                gm_l14 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t69 = gm_l14;
                                    uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    uint8_t t71 = (t69 < t70);
                                    if (!t71) goto lb68;
                                    {
                                        uint64_t t74 = gm_l14;
                                        uint64_t t75 = (uintptr_t)t74;
                                        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                        void * t77 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t75 * 8));
                                        void * t78 = t77;
                                        void * t81 = GM_LD(void *, ((uint8_t *)&t78));
                                        gm_l15 = t81;
                                        {
                                            void * t84 = gm_l15;
                                            void * t85 = t84;
                                            if (!(GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto ie83;
                                            void * t86 = GM_LD(void *, ((uint8_t *)&t85));
                                            gm_l16 = t86;
                                            {
                                                void * t88 = gm_l0;
                                                void * t89 = gm_l16;
                                                uint8_t t90 = gm_l3;
                                                uint8_t t91 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t88, t89, t90);
                                                if (gm_unw.kind) {
                                                    goto gm_ll72;
                                                }
                                                gm_l3 = t91;
                                            }
                                            goto ix83;
ie83:;
ix83:;
                                        }
                                        goto me79;
mn80:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 277, 17);
me79:;
                                    }
lc68:;
                                    {
                                        uint64_t t93 = gm_l14;
                                        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                        uint64_t t96 = r94;
                                        gm_l14 = t96;
                                    }
                                }
gm_ll72:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb68; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc68; }
                                }
                                goto gm_ll10;
lb68:;
                            }
                        }
                        {
                            void * t97 = gm_l0;
                            void * t98 = gm_l12;
                            uint8_t t99 = gm_l3;
                            uint8_t t100 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t97, t98, t99);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l3 = t100;
                        }
                        goto me19;
mn61:;
ma25:;
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t102 != ((uint8_t)4ULL)) goto mn101;
                        void * t103 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                        gm_l17 = t103;
                        {
                            void * t105 = gm_l0;
                            void * t106 = gm_l17;
                            uint8_t t107 = gm_l3;
                            uint8_t t108 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t105, t106, t107);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l3 = t108;
                        }
                        goto me19;
mn101:;
ma26:;
                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t110 != ((uint8_t)5ULL)) goto mn109;
                        goto me19;
mn109:;
ma27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 268, 9);
me19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 267, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t113 = gm_l5;
                    uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                    uint64_t t116 = r114;
                    gm_l5 = t116;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        void * t118 = gm_l1;
        void * t119 = GM_LD(void *, (((uint8_t *)t118) + 16));
        void * t120 = t119;
        if (!(GM_LD(void *, ((uint8_t *)&t120)) != 0)) goto ie117;
        void * t121 = GM_LD(void *, ((uint8_t *)&t120));
        gm_l18 = t121;
        {
            void * t123 = gm_l0;
            void * t124 = gm_l18;
            uint8_t t125 = gm_l3;
            uint8_t t126 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t126;
        }
        goto ix117;
ie117:;
ix117:;
    }
    {
        uint8_t t127 = gm_l3;
        uint8_t t128 = 0;
        F.ret = t127;
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
inline __attribute__((aligned(4))) uint8_t gm_f_k_children_3gr7c2u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_k_children_3gr7c2u F;
    void * gm_l1; /* e */
    uint8_t gm_l2; /* st */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t2 = gm_l2;
        F.l3 = t2;
    }
    {
        void * t3 = gm_l1;
        gm_closure t4 = ((gm_closure){(void *)gm_f_fn_it95hr1, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t5 = F.l3;
        uint8_t t6 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_k_cond_d97airn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_k_cond_d97airn F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* cond */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* pass */
    uint8_t gm_l4; /* fail */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tlr9dp305htm7 gm_l7; /* c */
    void * gm_l8; /* x */
    void * gm_l9; /* value */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t2 = gm_l2;
        gm_l3 = t2;
    }
    {
        gm_l4 = ((uint8_t)0ULL);
    }
    {
        {
            gm_b16_8 t4 = gm_l1;
            gm_l5 = t4;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l6;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tlr9dp305htm7 t16 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 24));
                    gm_l7 = t16;
                    {
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t20 != ((uint8_t)0ULL)) goto mn19;
                        void * t21 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_l8 = t21;
                        {
                            void * t23 = gm_l0;
                            void * t24 = gm_l8;
                            uint8_t t25 = gm_l3;
                            uint8_t t26 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t23, t24, t25);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l3 = t26;
                        }
                        {
                            void * t28 = gm_l8;
                            uint8_t t29 = (!((GM_LD(uint8_t, (((uint8_t *)t28) + 8) + 0) == ((uint8_t)4ULL)) && ((GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 8)) != 0) == (((uint8_t)1) != 0))));
                            if (!t29) goto ie27;
                            {
                                uint8_t t31 = gm_l4;
                                uint8_t t32 = gm_l3;
                                uint8_t t33 = (uint8_t)(t31 | t32);
                                gm_l4 = t33;
                            }
                            goto ix27;
ie27:;
ix27:;
                        }
                        goto me18;
mn19:;
                        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t35 != ((uint8_t)1ULL)) goto mn34;
                        void * t36 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                        gm_l9 = t36;
                        {
                            void * t38 = gm_l0;
                            void * t39 = gm_l9;
                            uint8_t t40 = gm_l3;
                            uint8_t t41 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l3 = t41;
                        }
                        {
                            uint8_t t42 = gm_l4;
                            uint8_t t43 = gm_l3;
                            uint8_t t44 = (uint8_t)(t42 | t43);
                            gm_l4 = t44;
                        }
                        goto me18;
mn34:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 233, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 232, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t46 = gm_l6;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l6 = t49;
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
        uint8_t t50 = gm_l3;
        uint8_t t51 = gm_l4;
        gm_tqs5p4n2tq19v t52;
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), t50);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 1), t51);
        uint8_t t53 = 0;
        F.ret = t52;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_consume_nr50dr5(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_k_consume_nr50dr5 F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* st */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, gm_ckpkn5umvr0);
        uint8_t t5 = (uint8_t)(t3 & t4);
        uint8_t t6 = (!(t5 == ((uint8_t)0ULL)));
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 56);
            gm_tbgd14esf0jvi t10;
            memset((uint8_t *)&t10 + 12, 0, 4);
            gm_tl874t2atnlh3 t11 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 0, t11);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_svhflm4d80ub1));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(61));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t12);
            gm_tftb1odc739t5 t13 = gm_f_push_j129uo3(t9, &t10);
            gm_tftb1odc739t5 t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcfa3vclj60ege, 104, 29);
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_exit_ttiloj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_k_exit_ttiloj7 F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* st */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, gm_c3seud9n9be);
        uint8_t t5 = (uint8_t)(t3 & t4);
        uint8_t t6 = (!(t5 == ((uint8_t)0ULL)));
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 56);
            gm_tbgd14esf0jvi t10;
            memset((uint8_t *)&t10 + 12, 0, 4);
            gm_tl874t2atnlh3 t11 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 0, t11);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_siho9h2ssfvv9));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(61));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t12);
            gm_tftb1odc739t5 t13 = gm_f_push_j129uo3(t9, &t10);
            gm_tftb1odc739t5 t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcfa3vclj60ege, 100, 29);
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
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
__attribute__((aligned(4))) uint8_t gm_f_k_expr_hdusp9d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_k_expr_hdusp9d F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* out */
    void * gm_l4; /* c */
    void * gm_l5; /* v */
    uint8_t gm_l6; /* sv */
    uint8_t gm_l7; /* sc */
    void * gm_l8; /* c */
    gm_teo9d4mncjfdu gm_l9; /* b */
    uint8_t gm_l10; /* sv */
    void * gm_l11; /* v */
    gm_te9hrk0u35rev gm_l12; /* t */
    void * gm_l13; /* v */
    void * gm_l14; /* b */
    gm_trl2khmuphncd gm_l15; /* x */
    gm_tqs5p4n2tq19v gm_l16; /* r */
    uint8_t gm_l17; /* t */
    uint8_t gm_l18; /* f */
    void * gm_l19; /* el */
    void * gm_l20; /* scrutinee */
    gm_b16_8 gm_l21; /* arms */
    uint8_t gm_l22; /* ss */
    uint8_t gm_l23; /* guard_fail */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l26; /* a */
    uint8_t gm_l27; /* entry */
    uint8_t gm_l28; /* g */
    void * gm_l29; /* gx */
    void * gm_l30; /* lhs */
    void * gm_l31; /* rhs */
    uint8_t gm_l32; /* l */
    gm_t3dcoc7077t1q gm_l33; /* x */
    uint8_t gm_l34; /* l */
    uint8_t gm_l37; /* sw */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t4lnf71mruqpt gm_l40; /* h */
    void * gm_l41; /* x */
    void * gm_l42; /* x */
    void * gm_l43; /* x */
    void * gm_l44; /* o */
    uint8_t gm_l45; /* b */
    gm_t0n7rigt5nh46 gm_l46; /* top */
    void * gm_l47; /* m */
    GM_STACK_CHECK(&F, 1911u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint8_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l3 = ((uint8_t)0ULL);
    }
    {
        void * t7 = gm_l1;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma22; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma22; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma22; case ((uint8_t)27ULL): goto ma22; case ((uint8_t)28ULL): goto ma22; case ((uint8_t)29ULL): goto ma22; case ((uint8_t)30ULL): goto ma22; case ((uint8_t)31ULL): goto ma17; case ((uint8_t)32ULL): goto ma22; case ((uint8_t)33ULL): goto ma22; case ((uint8_t)34ULL): goto ma22; case ((uint8_t)35ULL): goto ma22; case ((uint8_t)36ULL): goto ma22; case ((uint8_t)37ULL): goto ma22; case ((uint8_t)38ULL): goto ma22; case ((uint8_t)39ULL): goto ma22; case ((uint8_t)40ULL): goto ma22; case ((uint8_t)41ULL): goto ma18; case ((uint8_t)42ULL): goto ma22; case ((uint8_t)43ULL): goto ma22; case ((uint8_t)44ULL): goto ma16; case ((uint8_t)45ULL): goto ma22; case ((uint8_t)46ULL): goto ma14; case ((uint8_t)47ULL): goto ma15; case ((uint8_t)48ULL): goto ma19; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma13; case ((uint8_t)52ULL): goto ma21; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma22; case ((uint8_t)56ULL): goto ma22; case ((uint8_t)57ULL): goto ma22; case ((uint8_t)58ULL): goto ma20; default: goto ma22; }
ma9:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t26 != ((uint8_t)53ULL)) goto mn25;
        void * t27 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l4 = t27;
        void * t28 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l5 = t28;
        {
            void * t30 = gm_l0;
            void * t31 = gm_l5;
            uint8_t t32 = gm_l2;
            uint8_t t33 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t30, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t33;
        }
        {
            uint8_t t35 = gm_l6;
            uint8_t t36 = (!(t35 == ((uint8_t)0ULL)));
            if (!t36) goto ie34;
            {
                void * t39 = gm_l4;
                void * t40 = gm_l0;
                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t40) + 8));
                uint8_t t42 = gm_f_is_k_09a6ffc(t39, t41);
                if (!t42) goto ie38;
                {
                    void * t44 = gm_l0;
                    uint8_t t45 = gm_l6;
                    void * t46 = gm_l1;
                    gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 208));
                    gm_b0_1 t48 = gm_f_k_consume_nr50dr5((void *)NULL, cx, NULL, t44, t45, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix38;
ie38:;
                {
                    void * t50 = gm_l0;
                    void * t51 = gm_l4;
                    uint8_t t52 = gm_l6;
                    uint8_t t53 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t50, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t53;
                }
                {
                    void * t54 = gm_l0;
                    uint8_t t55 = gm_l7;
                    void * t56 = gm_l1;
                    gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 208));
                    gm_b0_1 t58 = gm_f_k_exit_ttiloj7((void *)NULL, cx, NULL, t54, t55, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix38:;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            gm_l3 = ((uint8_t)0ULL);
        }
        goto me8;
mn25:;
ma10:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t60 != ((uint8_t)54ULL)) goto mn59;
        void * t61 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l8 = t61;
        {
            void * t64 = gm_l8;
            void * t65 = gm_l0;
            uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t65) + 8));
            uint8_t t67 = gm_f_is_k_09a6ffc(t64, t66);
            if (!t67) goto ie63;
            {
                void * t69 = gm_l0;
                uint8_t t70 = gm_l2;
                void * t71 = gm_l1;
                gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 208));
                gm_b0_1 t73 = gm_f_k_consume_nr50dr5((void *)NULL, cx, NULL, t69, t70, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t74 = GM_LD(uint8_t, gm_ckpkn5umvr0);
                gm_l3 = t74;
            }
            goto ix63;
ie63:;
            {
                void * t76 = gm_l0;
                void * t77 = gm_l1;
                uint8_t t78 = gm_l2;
                uint8_t t79 = gm_f_k_children_3gr7c2u((void *)NULL, cx, NULL, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l3 = t79;
            }
ix63:;
        }
        goto me8;
mn59:;
ma11:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t81 != ((uint8_t)49ULL)) goto mn80;
        gm_teo9d4mncjfdu t82 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t82;
        {
            uint8_t t84 = gm_l2;
            gm_l10 = t84;
        }
        {
            void * t86 = GM_LD(void *, (((uint8_t *)&gm_l9) + 16));
            void * t87 = t86;
            if (!(GM_LD(void *, ((uint8_t *)&t87)) != 0)) goto ie85;
            void * t88 = GM_LD(void *, ((uint8_t *)&t87));
            gm_l11 = t88;
            {
                void * t90 = gm_l0;
                void * t91 = gm_l11;
                uint8_t t92 = gm_l2;
                uint8_t t93 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t90, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t93;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            void * t94 = gm_l0;
            gm_te9hrk0u35rev t95 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&gm_l9) + 0));
            uint8_t t96 = gm_l10;
            void * t97 = gm_l1;
            gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t97) + 208));
            gm_b0_1 t99 = gm_f_k_jump_41ae4gj((void *)NULL, cx, NULL, t94, t95, t96, ((uint8_t)1), t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l3 = ((uint8_t)0ULL);
        }
        goto me8;
mn80:;
ma12:;
        uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t101 != ((uint8_t)50ULL)) goto mn100;
        gm_te9hrk0u35rev t102 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t7) + 8) + 8));
        gm_l12 = t102;
        {
            void * t104 = gm_l0;
            gm_te9hrk0u35rev t105 = gm_l12;
            uint8_t t106 = gm_l2;
            void * t107 = gm_l1;
            gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 208));
            gm_b0_1 t109 = gm_f_k_jump_41ae4gj((void *)NULL, cx, NULL, t104, t105, t106, ((uint8_t)0), t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l3 = ((uint8_t)0ULL);
        }
        goto me8;
mn100:;
ma13:;
        uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t111 != ((uint8_t)51ULL)) goto mn110;
        void * t112 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l13 = t112;
        {
            {
                void * t115 = gm_l0;
                void * t116 = gm_l13;
                uint8_t t117 = gm_l2;
                uint8_t t118 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t115, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_l3 = ((uint8_t)0ULL);
        }
        goto me8;
mn110:;
ma14:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t120 != ((uint8_t)46ULL)) goto mn119;
        void * t121 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l14 = t121;
        {
            void * t123 = gm_l0;
            void * t124 = gm_l14;
            uint8_t t125 = gm_l2;
            uint8_t t126 = gm_f_k_block_87r6qua((void *)NULL, cx, NULL, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t126;
        }
        goto me8;
mn119:;
ma15:;
        uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t128 != ((uint8_t)47ULL)) goto mn127;
        gm_trl2khmuphncd t129 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t7) + 8) + 8));
        gm_l15 = t129;
        {
            void * t131 = gm_l0;
            gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
            uint8_t t133 = gm_l2;
            gm_tqs5p4n2tq19v t134 = gm_f_k_cond_d97airn((void *)NULL, cx, NULL, t131, t132, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t134;
        }
        {
            void * t135 = gm_l0;
            void * t136 = GM_LD(void *, (((uint8_t *)&gm_l15) + 16));
            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t138 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t135, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t138;
        }
        {
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 1));
            gm_l18 = t139;
        }
        {
            void * t141 = GM_LD(void *, (((uint8_t *)&gm_l15) + 24));
            void * t142 = t141;
            if (!(GM_LD(void *, ((uint8_t *)&t142)) != 0)) goto ie140;
            void * t143 = GM_LD(void *, ((uint8_t *)&t142));
            gm_l19 = t143;
            {
                void * t145 = gm_l0;
                void * t146 = gm_l19;
                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 1));
                uint8_t t148 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t145, t146, t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t148;
            }
            goto ix140;
ie140:;
ix140:;
        }
        {
            uint8_t t149 = gm_l17;
            uint8_t t150 = gm_l18;
            uint8_t t151 = (uint8_t)(t149 | t150);
            gm_l3 = t151;
        }
        goto me8;
mn127:;
ma16:;
        uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t153 != ((uint8_t)44ULL)) goto mn152;
        void * t154 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l20 = t154;
        gm_b16_8 t155 = GM_LD(gm_b16_8, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l21 = t155;
        {
            void * t157 = gm_l0;
            void * t158 = gm_l20;
            uint8_t t159 = gm_l2;
            uint8_t t160 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t157, t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t160;
        }
        {
            uint8_t t161 = gm_l22;
            gm_l23 = t161;
        }
        {
            {
                gm_b16_8 t163 = gm_l21;
                gm_l24 = t163;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t165 = gm_l25;
                    uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t167 = (t165 < t166);
                    if (!t167) goto lb164;
                    {
                        uint64_t t170 = gm_l25;
                        uint64_t t171 = (uintptr_t)t170;
                        uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        gm_tbbqefuhv2r8h t175 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t171 * 24));
                        gm_l26 = t175;
                        {
                            uint8_t t177 = gm_l23;
                            uint8_t t178 = gm_l22;
                            uint8_t t179 = (uint8_t)(t177 | t178);
                            gm_l27 = t179;
                        }
                        {
                            uint8_t t180 = gm_l27;
                            gm_l28 = t180;
                        }
                        {
                            void * t182 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
                            void * t183 = t182;
                            if (!(GM_LD(void *, ((uint8_t *)&t183)) != 0)) goto ie181;
                            void * t184 = GM_LD(void *, ((uint8_t *)&t183));
                            gm_l29 = t184;
                            {
                                void * t186 = gm_l0;
                                void * t187 = gm_l29;
                                uint8_t t188 = gm_l27;
                                uint8_t t189 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t186, t187, t188);
                                if (gm_unw.kind) {
                                    goto gm_ll168;
                                }
                                gm_l28 = t189;
                            }
                            goto ix181;
ie181:;
ix181:;
                        }
                        {
                            uint8_t t190 = gm_l23;
                            uint8_t t191 = gm_l28;
                            uint8_t t192 = (uint8_t)(t190 | t191);
                            gm_l23 = t192;
                        }
                        {
                            uint8_t t193 = gm_l3;
                            void * t194 = gm_l0;
                            void * t195 = GM_LD(void *, (((uint8_t *)&gm_l26) + 16));
                            uint8_t t196 = gm_l28;
                            uint8_t t197 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t194, t195, t196);
                            if (gm_unw.kind) {
                                goto gm_ll168;
                            }
                            uint8_t t198 = (uint8_t)(t193 | t197);
                            gm_l3 = t198;
                        }
                        goto me173;
mn174:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 159, 13);
me173:;
                    }
lc164:;
                    {
                        uint64_t t200 = gm_l25;
                        uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                        uint64_t t203 = r201;
                        gm_l25 = t203;
                    }
                }
gm_ll168:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb164; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc164; }
                }
                goto gm_lad;
lb164:;
            }
        }
        goto me8;
mn152:;
ma17:;
        uint8_t t205 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t205 != ((uint8_t)31ULL)) goto mn204;
        void * t206 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l30 = t206;
        void * t207 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 16));
        gm_l31 = t207;
        {
            void * t209 = gm_l0;
            void * t210 = gm_l30;
            uint8_t t211 = gm_l2;
            uint8_t t212 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t209, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l32 = t212;
        }
        {
            uint8_t t213 = gm_l32;
            void * t214 = gm_l0;
            void * t215 = gm_l31;
            uint8_t t216 = gm_l32;
            uint8_t t217 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t214, t215, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t218 = (uint8_t)(t213 | t217);
            gm_l3 = t218;
        }
        goto me8;
mn204:;
ma18:;
        uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t220 != ((uint8_t)41ULL)) goto mn219;
        gm_t3dcoc7077t1q t221 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t7) + 8) + 8));
        gm_l33 = t221;
        {
            void * t223 = gm_l0;
            void * t224 = GM_LD(void *, (((uint8_t *)&gm_l33) + 0));
            uint8_t t225 = gm_l2;
            uint8_t t226 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t223, t224, t225);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t226;
        }
        {
            uint8_t t227 = gm_l34;
            void * t228 = gm_l0;
            void * t229 = GM_LD(void *, (((uint8_t *)&gm_l33) + 16));
            uint8_t t230 = gm_l34;
            uint8_t t231 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t228, t229, t230);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t232 = (uint8_t)(t227 | t231);
            gm_l3 = t232;
        }
        goto me8;
mn219:;
ma19:;
        uint8_t t234 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t234 != ((uint8_t)48ULL)) goto mn233;
        gm_t710lsrq1m8gk t235 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t7) + 8) + 8));
        F.l35 = t235;
        {
            void * t237 = gm_l0;
            uint32_t t238 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 0));
            gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 8));
            void * t240 = GM_LD(void *, (((uint8_t *)&F.l35) + 24));
            void * t241 = GM_LD(void *, (((uint8_t *)&F.l35) + 32));
            uint8_t t242 = gm_l2;
            uint8_t t243 = gm_f_k_loop_ot156p6((void *)NULL, cx, NULL, t237, t238, t239, t240, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t243;
        }
        goto me8;
mn233:;
ma20:;
        uint8_t t245 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t245 != ((uint8_t)58ULL)) goto mn244;
        gm_t710lsrq1m8gk t246 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t7) + 8) + 8));
        F.l36 = t246;
        {
            uint8_t t248 = gm_l2;
            gm_l37 = t248;
        }
        {
            {
                gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 8));
                gm_l38 = t250;
            }
            {
                gm_l39 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t252 = gm_l39;
                    uint64_t t253 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                    uint8_t t254 = (t252 < t253);
                    if (!t254) goto lb251;
                    {
                        uint64_t t257 = gm_l39;
                        uint64_t t258 = (uintptr_t)t257;
                        uint64_t t259 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        gm_t4lnf71mruqpt t262 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t258 * 32));
                        gm_l40 = t262;
                        {
                            void * t264;
                            uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                            if (t267 != ((uint8_t)0ULL)) goto mn266;
                            void * t268 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
                            gm_l41 = t268;
                            void * t269 = gm_l41;
                            t264 = t269;
                            goto me265;
mn266:;
                            uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                            if (t271 != ((uint8_t)1ULL)) goto mn270;
                            void * t272 = GM_LD(void *, ((((uint8_t *)&gm_l40) + 8) + 16));
                            gm_l42 = t272;
                            void * t273 = gm_l42;
                            t264 = t273;
                            goto me265;
mn270:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 179, 22);
me265:;
                            gm_l43 = t264;
                        }
                        {
                            void * t274 = gm_l0;
                            void * t275 = gm_l43;
                            uint8_t t276 = gm_l37;
                            uint8_t t277 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t274, t275, t276);
                            if (gm_unw.kind) {
                                goto gm_ll255;
                            }
                            gm_l37 = t277;
                        }
                        goto me260;
mn261:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 178, 13);
me260:;
                    }
lc251:;
                    {
                        uint64_t t279 = gm_l39;
                        uint64_t r280; bool o281 = __builtin_add_overflow(t279, ((uint64_t)1ULL), &r280);
                        uint64_t t282 = r280;
                        gm_l39 = t282;
                    }
                }
gm_ll255:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb251; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc251; }
                }
                goto gm_lad;
lb251:;
            }
        }
        {
            void * t284 = GM_LD(void *, (((uint8_t *)&F.l36) + 24));
            void * t285 = t284;
            if (!(GM_LD(void *, ((uint8_t *)&t285)) != 0)) goto ie283;
            void * t286 = GM_LD(void *, ((uint8_t *)&t285));
            gm_l44 = t286;
            {
                void * t288 = gm_l0;
                void * t289 = gm_l44;
                uint8_t t290 = gm_l37;
                uint8_t t291 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t288, t289, t290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t291;
            }
            goto ix283;
ie283:;
ix283:;
        }
        {
            void * t292 = gm_l0;
            void * t293 = (void *)(((uint8_t *)t292) + 16);
            uint32_t t294 = GM_LD(uint32_t, (((uint8_t *)&F.l36) + 0));
            gm_t0n7rigt5nh46 t295;
            GM_ST(uint16_t, (uint8_t *)&t295 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t295 + 0), t294);
            GM_ST(uint8_t, ((uint8_t *)&t295 + 4), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t295 + 5), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t296 = gm_f_push_dqg4p50(t293, GM_CONV(gm_cs8r2qbercod2k, t295));
            gm_tftb1odc739t5 t297 = t296;
            if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 1, gm_srcfa3vclj60ege, 186, 13);
            gm_b0_1 t298 = GM_LD(gm_b0_1, (((uint8_t *)&t297) + 1));
        }
        {
            void * t299 = gm_l0;
            void * t300 = GM_LD(void *, (((uint8_t *)&F.l36) + 32));
            uint8_t t301 = gm_l37;
            uint8_t t302 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t299, t300, t301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l45 = t302;
        }
        {
            void * t303 = gm_l0;
            void * t304 = (void *)(((uint8_t *)t303) + 16);
            gm_t72g7o4mtivnc t305 = gm_f_pop_7fqs7gj(t304);
            gm_t72g7o4mtivnc t306 = t305;
            gm_t0n7rigt5nh46 t307;
            if (((((uint8_t *)&t306))[8] != 0)) {
                gm_t0n7rigt5nh46 t308 = GM_LD(gm_t0n7rigt5nh46, (((uint8_t *)&t306) + 0));
                t307 = t308;
            } else {
                uint8_t t309 = 0;
                F.ret = ((uint8_t)0ULL);
                goto gm_fret;
            }
            gm_l46 = t307;
        }
        {
            uint8_t t310 = gm_l45;
            uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 5));
            uint8_t t312 = (uint8_t)(t310 | t311);
            gm_l3 = t312;
        }
        goto me8;
mn244:;
ma21:;
        uint8_t t314 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t314 != ((uint8_t)52ULL)) goto mn313;
        void * t315 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l47 = t315;
        {
            {
                void * t318 = gm_l0;
                void * t319 = gm_l47;
                uint8_t t320 = gm_l2;
                uint8_t t321 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t318, t319, t320);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_l3 = ((uint8_t)0ULL);
        }
        goto me8;
mn313:;
ma22:;
        {
            void * t324 = gm_l0;
            void * t325 = gm_l1;
            uint8_t t326 = gm_l2;
            uint8_t t327 = gm_f_k_children_3gr7c2u((void *)NULL, cx, NULL, t324, t325, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t327;
        }
        goto me8;
mn322:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 112, 5);
me8:;
    }
    {
        void * t329 = gm_l0;
        void * t330 = GM_LD(void *, (((uint8_t *)t329) + 0));
        void * t331 = gm_l1;
        gm_t88rcucg1b7uk t332 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t331) + 0));
        gm_t53at80pfdus3 t333;
        gm_f_kind_jcnt4hm((void *)&t333, t330, GM_CONV(gm_csguankko4lmhd, t332));
        gm_t53at80pfdus3 t334 = t333;
        uint8_t t335 = ((GM_LD(uint8_t, ((uint8_t *)&t334) + 0) == ((uint8_t)5ULL)));
        if (!t335) goto ie328;
        {
            uint8_t t337 = 0;
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
        goto ix328;
ie328:;
ix328:;
    }
    {
        uint8_t t338 = gm_l3;
        uint8_t t339 = 0;
        F.ret = t338;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_k_jump_41ae4gj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_te9hrk0u35rev a1, uint8_t a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_k_jump_41ae4gj F;
    void * gm_l0; /* cx */
    gm_te9hrk0u35rev gm_l1; /* target */
    uint8_t gm_l2; /* st */
    uint8_t gm_l3; /* is_break */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* levels */
    uint32_t gm_l6; /* label */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* l */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 4) + 0));
        gm_l5 = t4;
        uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 4) + 4));
        gm_l6 = t5;
        uint32_t t6 = gm_l5;
        uint8_t t7 = ((t6 == ((uint32_t)0ULL)));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 16) + 0) + 8);
            gm_l7 = t10;
        }
        {
            for (;;) {
                uint64_t t12 = gm_l7;
                uint8_t t13 = (t12 > ((uint64_t)0ULL));
                if (!t13) goto lb11;
                {
                    uint64_t t16 = gm_l7;
                    uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
                    if (o18) gm_trap("integer overflow in `-`", gm_srcfa3vclj60ege, 212, 13);
                    uint64_t t19 = r17;
                    gm_l7 = t19;
                }
                {
                    uint64_t t20 = gm_l7;
                    uint64_t t21 = (uintptr_t)t20;
                    void * t22 = gm_l0;
                    uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 16) + 0) + 8);
                    if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcfa3vclj60ege, 213, 19);
                    void * t24 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t22) + 16) + 0)) + t21 * 8);
                    gm_l8 = t24;
                }
                {
                    void * t26 = gm_l8;
                    uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 0));
                    uint32_t t28 = gm_l6;
                    uint8_t t29 = (!(t27 == t28));
                    if (!t29) goto ie25;
                    {
                        goto lc11;
                    }
                    goto ix25;
ie25:;
ix25:;
                }
                {
                    uint8_t t32 = gm_l3;
                    if (!t32) goto ie31;
                    {
                        void * t34 = gm_l8;
                        void * t35 = gm_l8;
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t35) + 5));
                        uint8_t t37 = gm_l2;
                        uint8_t t38 = (uint8_t)(t36 | t37);
                        GM_ST(uint8_t, (((uint8_t *)t34) + 5), t38);
                    }
                    goto ix31;
ie31:;
                    {
                        void * t40 = gm_l8;
                        void * t41 = gm_l8;
                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 4));
                        uint8_t t43 = gm_l2;
                        uint8_t t44 = (uint8_t)(t42 | t43);
                        GM_ST(uint8_t, (((uint8_t *)t40) + 4), t44);
                    }
ix31:;
                }
                {
                    gm_b0_1 t45;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
lc11:;
            }
lb11:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t48 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
        if (!t48) goto ie47;
        {
            void * t50 = gm_l0;
            uint8_t t51 = gm_l2;
            gm_tl874t2atnlh3 t52 = gm_l4;
            gm_b0_1 t53 = gm_f_k_exit_ttiloj7((void *)NULL, cx, NULL, t50, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix47;
ie47:;
ix47:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_k_loop_ot156p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, void * a4, uint8_t a5) {
    struct gm_fr_k_loop_ot156p6 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* cond */
    void * gm_l3; /* body */
    void * gm_l4; /* step */
    uint8_t gm_l5; /* st */
    uint8_t gm_l6; /* entry */
    uint64_t gm_l7; /* saved */
    gm_tqs5p4n2tq19v gm_l8; /* r */
    uint8_t gm_l9; /* b */
    gm_t0n7rigt5nh46 gm_l10; /* top */
    uint8_t gm_l11; /* back */
    void * gm_l12; /* x */
    uint8_t gm_l13; /* next */
    GM_STACK_CHECK(&F, 308u);
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
        uint8_t t2 = gm_l5;
        gm_l6 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 56) + 0) + 8);
                gm_l7 = t7;
            }
            {
                void * t8 = gm_l0;
                gm_b16_8 t9 = gm_l2;
                uint8_t t10 = gm_l6;
                gm_tqs5p4n2tq19v t11 = gm_f_k_cond_d97airn((void *)NULL, cx, NULL, t8, t9, t10);
                if (gm_unw.kind) {
                    goto gm_ll4;
                }
                gm_l8 = t11;
            }
            {
                void * t12 = gm_l0;
                void * t13 = (void *)(((uint8_t *)t12) + 16);
                uint32_t t14 = gm_l1;
                gm_t0n7rigt5nh46 t15;
                GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t14);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 4), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t15 + 5), ((uint8_t)0ULL));
                gm_tftb1odc739t5 t16 = gm_f_push_dqg4p50(t13, GM_CONV(gm_cs8r2qbercod2k, t15));
                gm_tftb1odc739t5 t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcfa3vclj60ege, 252, 9);
                gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
            }
            {
                void * t19 = gm_l0;
                void * t20 = gm_l3;
                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                uint8_t t22 = gm_f_k_block_87r6qua((void *)NULL, cx, NULL, t19, t20, t21);
                if (gm_unw.kind) {
                    goto gm_ll4;
                }
                gm_l9 = t22;
            }
            {
                void * t23 = gm_l0;
                void * t24 = (void *)(((uint8_t *)t23) + 16);
                gm_t72g7o4mtivnc t25 = gm_f_pop_7fqs7gj(t24);
                gm_t72g7o4mtivnc t26 = t25;
                gm_t0n7rigt5nh46 t27;
                if (((((uint8_t *)&t26))[8] != 0)) {
                    gm_t0n7rigt5nh46 t28 = GM_LD(gm_t0n7rigt5nh46, (((uint8_t *)&t26) + 0));
                    t27 = t28;
                } else {
                    uint8_t t29 = 0;
                    F.ret = ((uint8_t)0ULL);
                    goto gm_fret;
                }
                gm_l10 = t27;
            }
            {
                uint8_t t30 = gm_l9;
                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 4));
                uint8_t t32 = (uint8_t)(t30 | t31);
                gm_l11 = t32;
            }
            {
                void * t34 = gm_l4;
                void * t35 = t34;
                if (!(GM_LD(void *, ((uint8_t *)&t35)) != 0)) goto ie33;
                void * t36 = GM_LD(void *, ((uint8_t *)&t35));
                gm_l12 = t36;
                {
                    void * t38 = gm_l0;
                    void * t39 = gm_l12;
                    uint8_t t40 = gm_l11;
                    uint8_t t41 = gm_f_k_expr_hdusp9d((void *)NULL, cx, NULL, t38, t39, t40);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_l11 = t41;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                uint8_t t42 = gm_l6;
                uint8_t t43 = gm_l11;
                uint8_t t44 = (uint8_t)(t42 | t43);
                gm_l13 = t44;
            }
            {
                uint8_t t46 = gm_l13;
                uint8_t t47 = gm_l6;
                uint8_t t48 = ((t46 == t47));
                if (!t48) goto ie45;
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 1));
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 5));
                    uint8_t t52 = (uint8_t)(t50 | t51);
                    uint8_t t53 = 0;
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix45;
ie45:;
ix45:;
            }
            {
                void * t54 = gm_l0;
                void * t55 = (void *)(((uint8_t *)t54) + 56);
                uint64_t t56 = gm_l7;
                gm_f_truncate_0s6rl4q(t55, t56);
            }
            {
                uint8_t t57 = gm_l13;
                gm_l6 = t57;
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_local_6sbsseu(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_local_6sbsseu F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = F.l1;
        uint8_t t5 = gm_f_is_k_09a6ffc(t3, t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_closure t9 = ((gm_closure){(void *)gm_f_fn_4f453lc, (void *)&F.h, cx});
        uint8_t t10 = gm_f_any_child_ffc8h1k(t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_upvar_f852a0h(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_upvar_f852a0h F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* l */
    GM_STACK_CHECK(&F, 71u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)16ULL)) goto ie2;
        uint32_t t5 = GM_LD(uint32_t, (((((uint8_t *)t3) + 8) + 8) + 4));
        gm_l2 = t5;
        uint32_t t6 = gm_l2;
        uint32_t t7 = F.l1;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie2;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        gm_closure t12 = ((gm_closure){(void *)gm_f_fn_j9invmr, (void *)&F.h, cx});
        uint8_t t13 = gm_f_any_child_ffc8h1k(t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = 0;
        F.ret = t13;
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
__attribute__((aligned(4))) uint8_t gm_f_only_consumed_pk7topa(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_only_consumed_pk7topa F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* b */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* st */
    void * gm_l7; /* body */
    void * gm_l8; /* c */
    void * gm_l9; /* v */
    void * gm_l10; /* c */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)15ULL)) goto mn11;
        uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t13;
        uint32_t t14 = gm_l2;
        uint32_t t15 = F.l1;
        uint8_t t16 = (!(t14 == t15));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)46ULL)) goto mn18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t20;
        {
            void * t22 = gm_l3;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
            gm_l4 = t23;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l5;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_trcmmd12cefb1 t35 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 32));
                    gm_l6 = t35;
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                        if (t38 != ((uint8_t)5ULL)) goto ie37;
                        void * t39 = GM_LD(void *, ((((uint8_t *)&gm_l6) + 8) + 8));
                        gm_l7 = t39;
                        void * t40 = gm_l7;
                        uint32_t t41 = F.l1;
                        uint8_t t42 = gm_f_mentions_local_6sbsseu(t40, t41);
                        if (!t42) goto ie37;
                        {
                            uint8_t t44 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 71, 22);
me33:;
                }
lc24:;
                {
                    uint64_t t46 = gm_l5;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l5 = t49;
                }
            }
lb24:;
        }
        goto me3;
mn18:;
ma6:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t51 != ((uint8_t)53ULL)) goto mn50;
        void * t52 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t52;
        void * t53 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t53;
        void * t55 = gm_l8;
        uint32_t t56 = F.l1;
        uint8_t t57 = gm_f_is_k_09a6ffc(t55, t56);
        if (!t57) goto ie54;
        {
            void * t59 = gm_l9;
            uint32_t t60 = F.l1;
            uint8_t t61 = gm_f_only_consumed_pk7topa(t59, t60);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix54;
ie54:;
ix54:;
        goto me3;
mn50:;
ma7:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t64 != ((uint8_t)54ULL)) goto mn63;
        void * t65 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l10 = t65;
        void * t67 = gm_l10;
        uint32_t t68 = F.l1;
        uint8_t t69 = gm_f_is_k_09a6ffc(t67, t68);
        if (!t69) goto ie66;
        {
            uint8_t t71 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
        goto me3;
mn63:;
ma8:;
        goto me3;
mn72:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcfa3vclj60ege, 69, 5);
me3:;
    }
    {
        F.l11 = ((uint8_t)1);
    }
    {
        void * t74 = gm_l0;
        gm_closure t75 = ((gm_closure){(void *)gm_f_fn_clkgu5i, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t76 = F.l11;
        uint8_t t77 = 0;
        F.ret = t76;
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
