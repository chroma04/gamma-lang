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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; union { uint8_t v0; } f1; } gm_tk5oh6c9vhlab;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tk5oh6c9vhlab) == 2 && _Alignof(gm_tk5oh6c9vhlab) == 1 && offsetof(gm_tk5oh6c9vhlab, f0) == 0 && offsetof(gm_tk5oh6c9vhlab, f1) == 1, "layout of `Step`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t p3[7]; gm_t4c7ipjj9s3fp f3; } gm_td5sh6gra0q3b;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; } gm_tdm3p1stpne4q;
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tha5675rv512q;
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; gm_t9qkh0rsooc8d f4; gm_tqs5p4n2tq19v f5; uint8_t p6[4]; gm_th2lqbo77pvsc f6; } gm_tiaut12ia9v85;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t3mf58gv9kf3n f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint64_t f5; uint8_t f6; uint8_t p7[7]; gm_t4c7ipjj9s3fp f7; uint64_t f8; } gm_tsnrtk0hcnkhj;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
typedef struct { void * f0; gm_tpcj7kul6hlib f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint8_t f9; uint8_t p10[7]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; uint8_t f12; uint8_t p13[7]; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; gm_trqfvmjtt4sij f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_t9805qtonbsag f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_trqfvmjtt4sij f25; gm_trqfvmjtt4sij f26; gm_t4c7ipjj9s3fp f27; gm_t4c7ipjj9s3fp f28; gm_trqfvmjtt4sij f29; uint32_t f30; uint8_t f31; uint8_t p32[3]; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; uint32_t f42; uint8_t p43[4]; gm_trqfvmjtt4sij f43; uint32_t f44; uint8_t p45[4]; gm_trqfvmjtt4sij f45; uint8_t f46; uint8_t p47[7]; gm_trqfvmjtt4sij f47; gm_trqfvmjtt4sij f48; uint32_t f49; gm_t9805qtonbsag f50; uint8_t p51[4]; gm_trqfvmjtt4sij f51; gm_t4c7ipjj9s3fp f52; gm_t4c7ipjj9s3fp f53; gm_trqfvmjtt4sij f54; uint32_t f55; uint8_t p56[4]; void * f56; void * f57; void * f58; gm_trqfvmjtt4sij f59; gm_trqfvmjtt4sij f60; gm_trqfvmjtt4sij f61; uint64_t f62; gm_t4c7ipjj9s3fp f63; gm_trqfvmjtt4sij f64; gm_trqfvmjtt4sij f65; gm_trqfvmjtt4sij f66; gm_trqfvmjtt4sij f67; uint8_t f68; uint8_t p69[7]; gm_trqfvmjtt4sij f69; gm_trqfvmjtt4sij f70; gm_t4c7ipjj9s3fp f71; gm_t4c7ipjj9s3fp f72; gm_t4c7ipjj9s3fp f73; } gm_tcrr6nmqqhjbj;
_Static_assert(sizeof(gm_td5sh6gra0q3b) == 96 && _Alignof(gm_td5sh6gra0q3b) == 8 && offsetof(gm_td5sh6gra0q3b, f0) == 0 && offsetof(gm_td5sh6gra0q3b, f1) == 16 && offsetof(gm_td5sh6gra0q3b, f2) == 32 && offsetof(gm_td5sh6gra0q3b, f3) == 40, "layout of `Scope`");
_Static_assert(sizeof(gm_tdm3p1stpne4q) == 48 && _Alignof(gm_tdm3p1stpne4q) == 8 && offsetof(gm_tdm3p1stpne4q, f0) == 0 && offsetof(gm_tdm3p1stpne4q, f1) == 40, "layout of `Plan`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tha5675rv512q) == 48 && _Alignof(gm_tha5675rv512q) == 8 && offsetof(gm_tha5675rv512q, f0) == 0 && offsetof(gm_tha5675rv512q, f1) == 8, "layout of `StackFrame`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tiaut12ia9v85) == 80 && _Alignof(gm_tiaut12ia9v85) == 8 && offsetof(gm_tiaut12ia9v85, f0) == 0 && offsetof(gm_tiaut12ia9v85, f1) == 16 && offsetof(gm_tiaut12ia9v85, f2) == 32 && offsetof(gm_tiaut12ia9v85, f3) == 48 && offsetof(gm_tiaut12ia9v85, f4) == 49 && offsetof(gm_tiaut12ia9v85, f5) == 50 && offsetof(gm_tiaut12ia9v85, f6) == 56, "layout of `Setup`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_tsnrtk0hcnkhj) == 224 && _Alignof(gm_tsnrtk0hcnkhj) == 8 && offsetof(gm_tsnrtk0hcnkhj, f0) == 0 && offsetof(gm_tsnrtk0hcnkhj, f1) == 16 && offsetof(gm_tsnrtk0hcnkhj, f2) == 24 && offsetof(gm_tsnrtk0hcnkhj, f3) == 64 && offsetof(gm_tsnrtk0hcnkhj, f4) == 104 && offsetof(gm_tsnrtk0hcnkhj, f5) == 144 && offsetof(gm_tsnrtk0hcnkhj, f6) == 152 && offsetof(gm_tsnrtk0hcnkhj, f7) == 160 && offsetof(gm_tsnrtk0hcnkhj, f8) == 216, "layout of `State`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_tcrr6nmqqhjbj) == 2792 && _Alignof(gm_tcrr6nmqqhjbj) == 8 && offsetof(gm_tcrr6nmqqhjbj, f0) == 0 && offsetof(gm_tcrr6nmqqhjbj, f1) == 8 && offsetof(gm_tcrr6nmqqhjbj, f2) == 304 && offsetof(gm_tcrr6nmqqhjbj, f3) == 344 && offsetof(gm_tcrr6nmqqhjbj, f4) == 384 && offsetof(gm_tcrr6nmqqhjbj, f5) == 424 && offsetof(gm_tcrr6nmqqhjbj, f6) == 464 && offsetof(gm_tcrr6nmqqhjbj, f7) == 504 && offsetof(gm_tcrr6nmqqhjbj, f8) == 544 && offsetof(gm_tcrr6nmqqhjbj, f9) == 584 && offsetof(gm_tcrr6nmqqhjbj, f10) == 592 && offsetof(gm_tcrr6nmqqhjbj, f11) == 632 && offsetof(gm_tcrr6nmqqhjbj, f12) == 672 && offsetof(gm_tcrr6nmqqhjbj, f13) == 680 && offsetof(gm_tcrr6nmqqhjbj, f14) == 720 && offsetof(gm_tcrr6nmqqhjbj, f15) == 760 && offsetof(gm_tcrr6nmqqhjbj, f16) == 800 && offsetof(gm_tcrr6nmqqhjbj, f17) == 840 && offsetof(gm_tcrr6nmqqhjbj, f18) == 880 && offsetof(gm_tcrr6nmqqhjbj, f19) == 920 && offsetof(gm_tcrr6nmqqhjbj, f20) == 960 && offsetof(gm_tcrr6nmqqhjbj, f21) == 968 && offsetof(gm_tcrr6nmqqhjbj, f22) == 1008 && offsetof(gm_tcrr6nmqqhjbj, f23) == 1048 && offsetof(gm_tcrr6nmqqhjbj, f24) == 1088 && offsetof(gm_tcrr6nmqqhjbj, f25) == 1128 && offsetof(gm_tcrr6nmqqhjbj, f26) == 1168 && offsetof(gm_tcrr6nmqqhjbj, f27) == 1208 && offsetof(gm_tcrr6nmqqhjbj, f28) == 1264 && offsetof(gm_tcrr6nmqqhjbj, f29) == 1320 && offsetof(gm_tcrr6nmqqhjbj, f30) == 1360 && offsetof(gm_tcrr6nmqqhjbj, f31) == 1364 && offsetof(gm_tcrr6nmqqhjbj, f32) == 1368 && offsetof(gm_tcrr6nmqqhjbj, f33) == 1408 && offsetof(gm_tcrr6nmqqhjbj, f34) == 1448 && offsetof(gm_tcrr6nmqqhjbj, f35) == 1488 && offsetof(gm_tcrr6nmqqhjbj, f36) == 1528 && offsetof(gm_tcrr6nmqqhjbj, f37) == 1568 && offsetof(gm_tcrr6nmqqhjbj, f38) == 1608 && offsetof(gm_tcrr6nmqqhjbj, f39) == 1648 && offsetof(gm_tcrr6nmqqhjbj, f40) == 1688 && offsetof(gm_tcrr6nmqqhjbj, f41) == 1728 && offsetof(gm_tcrr6nmqqhjbj, f42) == 1768 && offsetof(gm_tcrr6nmqqhjbj, f43) == 1776 && offsetof(gm_tcrr6nmqqhjbj, f44) == 1816 && offsetof(gm_tcrr6nmqqhjbj, f45) == 1824 && offsetof(gm_tcrr6nmqqhjbj, f46) == 1864 && offsetof(gm_tcrr6nmqqhjbj, f47) == 1872 && offsetof(gm_tcrr6nmqqhjbj, f48) == 1912 && offsetof(gm_tcrr6nmqqhjbj, f49) == 1952 && offsetof(gm_tcrr6nmqqhjbj, f50) == 1956 && offsetof(gm_tcrr6nmqqhjbj, f51) == 1968 && offsetof(gm_tcrr6nmqqhjbj, f52) == 2008 && offsetof(gm_tcrr6nmqqhjbj, f53) == 2064 && offsetof(gm_tcrr6nmqqhjbj, f54) == 2120 && offsetof(gm_tcrr6nmqqhjbj, f55) == 2160 && offsetof(gm_tcrr6nmqqhjbj, f56) == 2168 && offsetof(gm_tcrr6nmqqhjbj, f57) == 2176 && offsetof(gm_tcrr6nmqqhjbj, f58) == 2184 && offsetof(gm_tcrr6nmqqhjbj, f59) == 2192 && offsetof(gm_tcrr6nmqqhjbj, f60) == 2232 && offsetof(gm_tcrr6nmqqhjbj, f61) == 2272 && offsetof(gm_tcrr6nmqqhjbj, f62) == 2312 && offsetof(gm_tcrr6nmqqhjbj, f63) == 2320 && offsetof(gm_tcrr6nmqqhjbj, f64) == 2376 && offsetof(gm_tcrr6nmqqhjbj, f65) == 2416 && offsetof(gm_tcrr6nmqqhjbj, f66) == 2456 && offsetof(gm_tcrr6nmqqhjbj, f67) == 2496 && offsetof(gm_tcrr6nmqqhjbj, f68) == 2536 && offsetof(gm_tcrr6nmqqhjbj, f69) == 2544 && offsetof(gm_tcrr6nmqqhjbj, f70) == 2584 && offsetof(gm_tcrr6nmqqhjbj, f71) == 2624 && offsetof(gm_tcrr6nmqqhjbj, f72) == 2680 && offsetof(gm_tcrr6nmqqhjbj, f73) == 2736, "layout of `Sema`");
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_trqfvmjtt4sij f5; gm_b16_8 f6; gm_t0rfe1ou14h21 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_trqfvmjtt4sij f13; gm_tf5srh4mp4b3i f14; gm_trqfvmjtt4sij f15; gm_tv37d3dpt2opu f16; void * f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; } gm_tcuh1vih4g9cr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
typedef struct { gm_tv37d3dpt2opu f0; gm_tr8dvlu9ik4gj f1; } gm_tljis7hgku5bj;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_tcuh1vih4g9cr) == 552 && _Alignof(gm_tcuh1vih4g9cr) == 8 && offsetof(gm_tcuh1vih4g9cr, f0) == 0 && offsetof(gm_tcuh1vih4g9cr, f1) == 8 && offsetof(gm_tcuh1vih4g9cr, f2) == 16 && offsetof(gm_tcuh1vih4g9cr, f3) == 24 && offsetof(gm_tcuh1vih4g9cr, f4) == 25 && offsetof(gm_tcuh1vih4g9cr, f5) == 32 && offsetof(gm_tcuh1vih4g9cr, f6) == 72 && offsetof(gm_tcuh1vih4g9cr, f7) == 88 && offsetof(gm_tcuh1vih4g9cr, f8) == 152 && offsetof(gm_tcuh1vih4g9cr, f9) == 168 && offsetof(gm_tcuh1vih4g9cr, f10) == 184 && offsetof(gm_tcuh1vih4g9cr, f11) == 200 && offsetof(gm_tcuh1vih4g9cr, f12) == 240 && offsetof(gm_tcuh1vih4g9cr, f13) == 280 && offsetof(gm_tcuh1vih4g9cr, f14) == 320 && offsetof(gm_tcuh1vih4g9cr, f15) == 336 && offsetof(gm_tcuh1vih4g9cr, f16) == 376 && offsetof(gm_tcuh1vih4g9cr, f17) == 424 && offsetof(gm_tcuh1vih4g9cr, f18) == 432 && offsetof(gm_tcuh1vih4g9cr, f19) == 472 && offsetof(gm_tcuh1vih4g9cr, f20) == 512, "layout of `Loader`");
typedef struct { gm_tljis7hgku5bj f0; uint8_t f1; uint8_t pe[7]; } gm_tdsi8a7qahakk;
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
_Static_assert(sizeof(gm_tljis7hgku5bj) == 168 && _Alignof(gm_tljis7hgku5bj) == 8 && offsetof(gm_tljis7hgku5bj, f0) == 0 && offsetof(gm_tljis7hgku5bj, f1) == 48, "layout of `Prepared`");
_Static_assert(sizeof(gm_tdsi8a7qahakk) == 176 && _Alignof(gm_tdsi8a7qahakk) == 8 && offsetof(gm_tdsi8a7qahakk, f0) == 0 && offsetof(gm_tdsi8a7qahakk, f1) == 168, "layout of `?Prepared`");
struct gm_fr_absolute_path_gj3a0ni {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_canonical_2k2pqlg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_clear_4vov4lr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_dvdem7p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_command_o05p6ui {
    gm_frame h;
    gm_tiaut12ia9v85 l2; /* setup */
    gm_tin97tt4ud0s1 l6; /* cfg */
    gm_td5sh6gra0q3b l8; /* scope */
    gm_tsnrtk0hcnkhj l9; /* st */
    gm_tjt4ka19str13 l11; /* a */
    uint8_t ret;
    gm_b0_1 w1;
};
struct gm_fr_config_d0film7 {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* c */
    gm_tm43kqe3e8b4a l8; /* manifest */
    gm_tftksr4op114e l11; /* item */
    gm_t62efos6ssciq ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_acanf35 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_default_level_urberdg {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ic4qap3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oab689p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_octbn8r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ootn2ah {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_root_vg2av48 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_v5vf4j7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_install_db2gf1l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_keep_vme6lns {
    gm_frame h;
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_key_of_kh3apqr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_lint_round_04c1gbp {
    gm_frame h;
    gm_tiaut12ia9v85 l2; /* setup */
    gm_td5sh6gra0q3b l7; /* scope */
    gm_tin97tt4ud0s1 l8; /* sm */
    gm_t2792tsg5eg72 l9; /* diags */
    gm_tcuh1vih4g9cr l11; /* l */
    gm_tljis7hgku5bj l12; /* prepared */
    gm_trqfvmjtt4sij l13; /* parsed */
    gm_tcrr6nmqqhjbj l15; /* s */
    gm_tdm3p1stpne4q l27; /* p */
    gm_trqfvmjtt4sij l28; /* todo */
    gm_trqfvmjtt4sij l29; /* keys */
    gm_trqfvmjtt4sij l42; /* parts */
    gm_tha5675rv512q l46; /* fe */
    gm_tk5oh6c9vhlab ret;
};
struct gm_fr_list_7cdag6l {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_cn2lofo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pul4qc0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rules_tjipqqd {
    gm_frame h;
    gm_tk9n3vlila9qg l3; /* r */
    gm_trqfvmjtt4sij l4; /* out */
    gm_tk9n3vlila9qg l7; /* r */
    gm_b0_1 ret;
};
struct gm_fr_list_t5p1bue {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_pad_3pipl52 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_plural_es_2s51qvc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plural_s_6n4fvg3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_scope_al6a28r {
    gm_frame h;
    gm_td5sh6gra0q3b ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_write_file_d3o5k61 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_apply_57egqj5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_length_f852tll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_clear_4vov4lr(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_dvdem7p(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_command_o05p6ui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_config_d0film7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_hook_2qtf05o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_contains_acanf35(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ic4qap3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oab689p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_octbn8r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ootn2ah(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_v5vf4j7(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) void gm_f_format_guqevu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i3a5v4o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q8uj6ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uc6bvcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_install_db2gf1l(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_keep_vme6lns(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_key_of_kh3apqr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_lint_round_04c1gbp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2, void * a3, void * a4, void * a5, uint64_t a6);
inline __attribute__((aligned(4))) void gm_f_list_7cdag6l(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_cn2lofo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pul4qc0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_list_rules_tjipqqd(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t5p1bue(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_load_core_du4go5i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_load_package_modules_t4hh6gf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_own_files_4lms2sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_pad_3pipl52(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_plan_1bssrl4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plural_es_2s51qvc(uint64_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plural_s_6n4fvg3(uint64_t a0);
__attribute__((aligned(4))) void gm_f_prepare_build_b1lg1n1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_gnvji28(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_j3qd697(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_9she7th(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4);
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_of_expr_l8qm9vv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_write_file_d3o5k61(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern uint8_t gm_cqss81ml8qn[920] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_kja7oc5dvo4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s1b2h3bb9to0f[10];
extern const uint8_t gm_s465vmg9c4jq6[14];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_sb9bgabg66tgj[42];
extern const uint8_t gm_sdajtpr06648l[6];
extern const uint8_t gm_sdl70rjr9en4m[16];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se2a9lqlhae47[22];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sioup6jsbm84g[10];
extern const uint8_t gm_sj0qpc1n9qui9[7];
extern const uint8_t gm_sm26kv3hefp6i[13];
extern const uint8_t gm_sn1m0t4k3vpa5[5];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_southrlg4k72m[6];
extern const uint8_t gm_sp7fbepv94vjk[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0m6lnv6m2d3p[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src361hl447ggcv[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_absolute_path_gj3a0ni F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonical_2k2pqlg F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) void gm_f_clear_4vov4lr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4vov4lr F;
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
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
inline __attribute__((aligned(4))) void gm_f_clear_dvdem7p(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_dvdem7p F;
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me6:;
        uint8_t t9 = 0;
        F.ret = t5;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_level_urberdg F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    void * t3 = gm_l0;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 32));
    if (!t4) goto ie2;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t10 = t9;
    t1 = t10;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ic4qap3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ic4qap3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oab689p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oab689p F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_octbn8r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_octbn8r F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ootn2ah(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ootn2ah F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_vg2av48 F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_v5vf4j7(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_fn_v5vf4j7 F;
    uint32_t gm_l0; /* f */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = (void *)((uint8_t *)&((struct gm_fr_lint_round_04c1gbp *)((&F.h)->up))->l7);
    void * t2 = (void *)((uint8_t *)&((struct gm_fr_lint_round_04c1gbp *)((&F.h)->up))->l8);
    uint32_t t3 = gm_l0;
    void * t4 = gm_f_file_o15fqku(t2, t3);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
    uint8_t t6 = gm_f_in_scope_tm13267((void *)NULL, cx, NULL, t1, t5);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_list_7cdag6l(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7cdag6l F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ic4qap3();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_cn2lofo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_cn2lofo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_octbn8r();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_list_pul4qc0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pul4qc0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ootn2ah();
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
inline __attribute__((aligned(4))) void gm_f_list_t5p1bue(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t5p1bue F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oab689p();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plural_es_2s51qvc(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plural_es_2s51qvc F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint64_t t3 = gm_l0;
    uint8_t t4 = ((t3 == ((uint64_t)1ULL)));
    if (!t4) goto ie2;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
    gm_b16_8 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sp7fbepv94vjk));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(2));
    gm_b16_8 t10 = t9;
    t1 = t10;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plural_s_6n4fvg3(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plural_s_6n4fvg3 F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint64_t t3 = gm_l0;
    uint8_t t4 = ((t3 == ((uint64_t)1ULL)));
    if (!t4) goto ie2;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
    gm_b16_8 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s03pmf867dcsr));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
    gm_b16_8 t10 = t9;
    t1 = t10;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_scope_al6a28r F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* std_root */
    uint8_t gm_l2; /* linting_std */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 33, 0, 7);
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        uint8_t t4 = gm_l2;
        gm_t4c7ipjj9s3fp t5;
        gm_f_hash_map_p3t5tj8((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 0, t2);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 16, t3);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 32, t4);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 40, t5);
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
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_bvkp9ir((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_th2lqbo77pvsc t3;
    gm_f_strip_prefix_vl3t0h4((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
me5:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_write_file_d3o5k61(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_write_file_d3o5k61 F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* text */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_ttln0d0hd70en t4 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5;
        if (!((((uint8_t *)&t4))[0] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_src361hl447ggcv, 227, 12);
me6:;
        uint8_t t9 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_command_o05p6ui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2) {
    struct gm_fr_command_o05p6ui F;
    gm_b16_8 gm_l0; /* path */
    void * gm_l1; /* o */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l4; /* dir */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l7; /* std_root */
    uint64_t gm_l10; /* round */
    gm_tk5oh6c9vhlab gm_l12; /* step */
    uint8_t gm_l13; /* code */
    GM_STACK_CHECK(&F, 609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t3ujhu6qph7de t3;
        gm_f_own_files_4lms2sv((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t4;
        if (!((((uint8_t *)&t3))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
            t4 = t5;
        } else {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
            gm_l3 = t6;
            {
                gm_b16_8 t8 = gm_l3;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_src0m6lnv6m2d3p, 86, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t8)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8), gm_src0m6lnv6m2d3p, 86, 9))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src0m6lnv6m2d3p, 86, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t9 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        gm_l4 = t4;
    }
    {
        void * t10 = gm_l1;
        gm_b16_8 t11 = gm_l0;
        gm_t62efos6ssciq t12;
        gm_f_config_d0film7((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t13;
        if (!((((uint8_t *)&t12))[0] != 0)) {
            t13 = (*(gm_tin97tt4ud0s1 *)(((uint8_t *)&t12) + 8));
        } else {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_l5 = t14;
            {
                gm_b16_8 t16 = gm_l5;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_src0m6lnv6m2d3p, 90, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8), gm_src0m6lnv6m2d3p, 90, 9))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src0m6lnv6m2d3p, 90, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t17 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        F.l6 = t13;
    }
    {
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t19 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t19;
    }
    {
        gm_b16_8 t20 = gm_l4;
        gm_b16_8 t21 = gm_l7;
        gm_b16_8 t22 = gm_l0;
        gm_b16_8 t23 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t24 = gm_l7;
        uint8_t t25 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_scope_al6a28r((void *)&F.l8, (void *)NULL, cx, NULL, t20, t21, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&F.l9 + 153, 0, 7);
        gm_node *pn26 = gm_find(cx, 587034433);
        if (!pn26) gm_trap("no handler installed for effect `Alloc`", gm_src0m6lnv6m2d3p, 96, 16);
        gm_t3mf58gv9kf3n t27;
        t27 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn26->clauses[0])(pn26->env, pn26->hctx, pn26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&F.l9 + 0, t27);
        GM_ST(uint64_t, (uint8_t *)&F.l9 + 16, ((uint64_t)0ULL));
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l9 + 24)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_pul4qc0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l9 + 64)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l9 + 104)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&F.l9 + 144, ((uint64_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l9 + 152, ((uint8_t)0));
        gm_f_hash_map_cuh4fs9((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l9 + 160)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&F.l9 + 216, ((uint64_t)0ULL));
    }
    {
        gm_l10 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb28;
            {
                gm_f_new_jr5ejs0((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
            }
            {
                gm_tk5oh6c9vhlab t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)1ULL));
                gm_l12 = t31;
            }
            {
                void * t32 = (void *)((uint8_t *)&F.l11);
                gm_t3mf58gv9kf3n t33 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t32));
                gm_t3mf58gv9kf3n t34 = t33;
                gm_node wn35 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t34, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
                {
                    gm_b16_8 t39 = gm_l0;
                    void * t40 = gm_l1;
                    void * t41 = (void *)((uint8_t *)&F.l8);
                    void * t42 = (void *)((uint8_t *)&F.l6);
                    void * t43 = (void *)((uint8_t *)&F.l9);
                    uint64_t t44 = gm_l10;
                    gm_tk5oh6c9vhlab t45 = gm_f_lint_round_04c1gbp((void *)NULL, (&wn35), NULL, t39, t40, &(*(gm_tiaut12ia9v85 *)((uint8_t *)a2)), t41, t42, t43, t44);
                    if (gm_unw.kind) {
                        goto gm_ll37;
                    }
                    gm_l12 = t45;
                }
                F.w1 = GM_ZERO(gm_b0_1);
                goto gm_ls46;
gm_ll37:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we36; }
                goto gm_ll29;
gm_ls46:;
we36:;
                gm_b0_1 t47 = F.w1;
            }
            {
                void * t48 = (void *)((uint8_t *)&F.l11);
                gm_f_deinit_sejdpqv(t48);
            }
            {
                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                if (t51 != ((uint8_t)0ULL)) goto mn50;
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 1));
                gm_l13 = t52;
                uint8_t t53 = gm_l13;
                uint8_t t54 = 0;
                F.ret = t53;
                goto gm_fret;
                goto me49;
mn50:;
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                if (t56 != ((uint8_t)1ULL)) goto mn55;
                goto me49;
mn55:;
                gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 114, 9);
me49:;
            }
            {
                uint64_t t58 = gm_l10;
                uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                if (o60) gm_trap("integer overflow in `+`", gm_src0m6lnv6m2d3p, 118, 9);
                uint64_t t61 = r59;
                gm_l10 = t61;
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
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_config_d0film7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_config_d0film7 F;
    void * gm_l0; /* o */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l3; /* root */
    gm_b16_8 gm_l4; /* file */
    gm_tj5jn79kq9pnm gm_l5; /* _ */
    gm_b16_8 gm_l6; /* text */
    gm_tf1bjg81u0glv gm_l7; /* e */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l12; /* lv */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tibupt4dmcqai gm_l15; /* st */
    gm_b16_8 gm_l16; /* message */
    GM_STACK_CHECK(&F, 1682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_default_config_s3r9hnq((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t3 = gm_l1;
        gm_th2lqbo77pvsc t4;
        gm_f_find_root_vg2av48((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t4))[16] != 0)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_l3 = t5;
        {
            gm_b16_8 t7 = gm_l3;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_se24c7hpp401n));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(10));
            gm_b16_8 t9 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t9;
        }
        {
            gm_b16_8 t10 = gm_l4;
            gm_tgvh15qshf643 t11;
            gm_f_read_all_r9rh9qd((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t12;
            if (!((((uint8_t *)&t11))[0] != 0)) {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
                t12 = t13;
            } else {
                gm_tj5jn79kq9pnm t14 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t11) + 8));
                gm_l5 = t14;
                gm_b16_8 t16 = gm_l4;
                gm_tnpifv20irnph t17;
                GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
                gm_tabb710d9mq2e t18;
                gm_f_format_guqevu2((void *)&t18, (void *)NULL, cx, NULL, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_src0m6lnv6m2d3p, 35, 21);
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8, t20);
                goto gm_fdone;
            }
            gm_l6 = t12;
        }
        {
            gm_b16_8 t21 = gm_l6;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_tjgmtdrk88t7m t23;
            gm_f_parse_manifest_b61s3ln((void *)&t23, (void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tm43kqe3e8b4a t24;
            if (!((((uint8_t *)&t23))[0] != 0)) {
                t24 = (*(gm_tm43kqe3e8b4a *)(((uint8_t *)&t23) + 8));
            } else {
                gm_l7 = (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t23) + 8));
                gm_tq3m5qgvkpje8 t26;
                gm_b16_8 t27 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
                GM_ST(uint64_t, (uint8_t *)&t26 + 16, t28);
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t26 + 24, t29);
                gm_tabb710d9mq2e t30;
                gm_f_format_n6eprd4((void *)&t30, (void *)NULL, cx, NULL, &t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src0m6lnv6m2d3p, 38, 21);
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8, t32);
                goto gm_fdone;
            }
            F.l8 = t24;
        }
        {
            {
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 280) + 0));
                gm_l9 = t34;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t36 = gm_l10;
                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t38 = (t36 < t37);
                    if (!t38) goto lb35;
                    {
                        uint64_t t41 = gm_l10;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tftksr4op114e t46 = GM_LD(gm_tftksr4op114e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t42 * 40));
                        F.l11 = t46;
                        {
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 16));
                            gm_taari1s0m51a7 t49 = gm_f_parse_level_4iat2k8(t48);
                            gm_t9qkh0rsooc8d t50;
                            if (((((uint8_t *)&t49))[1] != 0)) {
                                gm_t9qkh0rsooc8d t51 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t49) + 0));
                                t50 = t51;
                            } else {
                                goto lc35;
                            }
                            gm_l12 = t50;
                        }
                        {
                            {
                                void * t53 = (void *)((uint8_t *)&F.l2);
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                gm_t9qkh0rsooc8d t55 = gm_l12;
                                gm_th2lqbo77pvsc t56;
                                gm_f_set_5cf9rlg((void *)&t56, (void *)NULL, cx, NULL, t53, t54, t55);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                            }
                        }
                        goto me44;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 40, 9);
me44:;
                    }
lc35:;
                    {
                        uint64_t t58 = gm_l10;
                        uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                        uint64_t t61 = r59;
                        gm_l10 = t61;
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
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t63 = gm_l0;
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((((uint8_t *)t63) + 8) + 0));
            gm_l13 = t64;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t66 = gm_l14;
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t68 = (t66 < t67);
                if (!t68) goto lb65;
                {
                    uint64_t t71 = gm_l14;
                    uint64_t t72 = (uintptr_t)t71;
                    uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_tibupt4dmcqai t76 = GM_LD(gm_tibupt4dmcqai, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t72 * 24));
                    gm_l15 = t76;
                    {
                        void * t79 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                        gm_t9qkh0rsooc8d t81 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                        gm_th2lqbo77pvsc t82;
                        gm_f_set_5cf9rlg((void *)&t82, (void *)NULL, cx, NULL, t79, t80, t81);
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                        if (!((((uint8_t *)&t82))[16] != 0)) goto ie78;
                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                        gm_l16 = t83;
                        {
                            gm_b16_8 t85 = gm_l16;
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                            ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8, t85);
                            goto gm_fdone;
                        }
                        goto ix78;
ie78:;
ix78:;
                    }
                    goto me74;
mn75:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 45, 5);
me74:;
                }
lc65:;
                {
                    uint64_t t87 = gm_l14;
                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                    uint64_t t90 = r88;
                    gm_l14 = t90;
                }
            }
gm_ll69:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
            }
            goto gm_lad;
lb65:;
        }
    }
    {
        void * t92 = gm_l0;
        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t92) + 2));
        if (!t93) goto ie91;
        {
            void * t95 = (void *)((uint8_t *)&F.l2);
            gm_f_deny_warnings_vk3pcl7(t95);
        }
        goto ix91;
ie91:;
ix91:;
    }
    {
        const uint8_t *t96 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8, t96, 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_acanf35(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_acanf35 F;
    gm_b16_8 gm_l0; /* paths */
    gm_b16_8 gm_l1; /* p */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 16));
                    gm_l4 = t15;
                    {
                        gm_b16_8 t18 = gm_l4;
                        gm_b16_8 t19 = gm_l1;
                        uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                        if (!t20) goto ie17;
                        {
                            uint8_t t22 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 231, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t24 = gm_l3;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l3 = t27;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t28 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_fn_v5vf4j7(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keep_vme6lns(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keep_vme6lns F;
    void * gm_l0; /* st */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t3mf58gv9kf3n t3 = GM_LD(gm_t3mf58gv9kf3n, (((uint8_t *)t2) + 0));
        gm_t3mf58gv9kf3n t4 = t3;
        gm_node wn5 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t4, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_b16_8 t9 = gm_l1;
            gm_tabb710d9mq2e t10;
            gm_f_dupe_g9iqpvn((void *)&t10, (void *)NULL, (&wn5), NULL, t9, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kja7oc5dvo4)));
            if (gm_unw.kind) {
                goto gm_ll7;
            }
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src0m6lnv6m2d3p, 80, 28);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto gm_ls14;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls14:;
we6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_key_of_kh3apqr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_key_of_kh3apqr F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    gm_b16_8 gm_l2; /* code */
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_temv198mna2jp t2;
        memset((uint8_t *)&t2 + 20, 0, 4);
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)t4) + 24) + 0));
        void * t6 = gm_f_file_o15fqku(t3, t5);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t7);
        void * t8 = gm_l1;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)t8) + 24) + 4));
        GM_ST(uint32_t, (uint8_t *)&t2 + 16, t9);
        gm_b16_8 t10 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 24, t10);
        gm_tabb710d9mq2e t11;
        gm_f_format_q8uj6ou((void *)&t11, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src0m6lnv6m2d3p, 227, 12);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
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
__attribute__((aligned(4))) gm_tk5oh6c9vhlab gm_f_lint_round_04c1gbp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2, void * a3, void * a4, void * a5, uint64_t a6) {
    struct gm_fr_lint_round_04c1gbp F;
    gm_b16_8 gm_l0; /* path */
    void * gm_l1; /* o */
    void * gm_l3; /* base */
    void * gm_l4; /* cfg */
    void * gm_l5; /* st */
    uint64_t gm_l6; /* round */
    uint32_t gm_l10; /* next_id */
    gm_tc70a9dbbmgkg gm_l14; /* outcome */
    uint64_t gm_l16; /* m */
    uint8_t gm_l17; /* packaged */
    uint8_t gm_l18; /* program */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_t6j6smj7qoctu gm_l21; /* u */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_b16_8 gm_l24; /* k */
    uint64_t gm_l25; /* rounds */
    uint64_t gm_l26; /* n */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    void * gm_l33; /* d */
    gm_b16_8 gm_l34; /* code */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    void * gm_l37; /* d */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l40; /* k */
    gm_t2lqu8jsj511d gm_l41; /* out */
    gm_t2lqu8jsj511d gm_l43; /* out */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    void * gm_l47; /* file */
    gm_b16_8 gm_l48; /* text */
    GM_STACK_CHECK(&F, 3822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l3;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        void * t4 = gm_l3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 16));
        void * t6 = gm_l3;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 32));
        gm_f_scope_al6a28r((void *)&F.l7, (void *)NULL, cx, NULL, t3, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_source_map_4v8dg4b((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l10 = ((uint32_t)0ULL);
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l8);
        void * t9 = (void *)((uint8_t *)&F.l9);
        void * t10 = (void *)((uint8_t *)&gm_l10);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a2) + 48));
        gm_f_loader_on0up0a((void *)&F.l11, (void *)NULL, cx, NULL, t8, t9, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t13 = gm_l0;
        gm_tqs5p4n2tq19v t14 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a2) + 50));
        gm_tdsi8a7qahakk t15;
        gm_f_prepare_build_b1lg1n1((void *)&t15, (void *)NULL, cx, NULL, t13, t14, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)a2) + 56)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tljis7hgku5bj t16;
        if (((((uint8_t *)&t15))[168] != 0)) {
            t16 = (*(gm_tljis7hgku5bj *)(((uint8_t *)&t15) + 0));
        } else {
            gm_tk5oh6c9vhlab t17;
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t17 + 1), ((uint8_t)1ULL));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        F.l12 = t16;
    }
    {
        gm_tv37d3dpt2opu t19 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&F.l12) + 0));
        GM_ST(gm_tv37d3dpt2opu, (((uint8_t *)&F.l11) + 376), t19);
    }
    {
        gm_f_list_cn2lofo((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = (void *)((uint8_t *)&F.l13);
        GM_ST(void *, (((uint8_t *)&F.l11) + 424), t20);
    }
    {
        gm_tc70a9dbbmgkg t21;
        memset(&t21, 0, sizeof t21);
        gm_l14 = t21;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l11);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 32));
        uint8_t t26 = gm_f_load_core_du4go5i((void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t26) goto ie22;
        {
            void * t28 = (void *)((uint8_t *)&F.l11);
            gm_f_sema_r1nlnnm((void *)&F.l15, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t29 = (void *)((uint8_t *)&F.l15);
            gm_f_install_db2gf1l(t29);
        }
        {
            void * t30 = (void *)((uint8_t *)&F.l15);
            gm_b0_1 t31 = gm_f_resolve_ambient_6orl0vo((void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t33 = (void *)((uint8_t *)&F.l11);
            gm_b16_8 t34 = gm_l0;
            gm_tf5srh4mp4b3i t35 = gm_f_load_root_af4ud7b((void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t35))[8] != 0)) goto ie32;
            uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&t35));
            gm_l16 = t36;
            {
                gm_b16_8 t38 = gm_l0;
                gm_th2lqbo77pvsc t39;
                gm_f_find_root_vg2av48((void *)&t39, (void *)NULL, cx, NULL, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t40;
                if (((((uint8_t *)&t39))[16] != 0)) goto mn42;
                t40 = ((uint8_t)0);
                goto me41;
mn42:;
                t40 = ((uint8_t)1);
                goto me41;
mn43:;
                gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 141, 25);
me41:;
                gm_l17 = t40;
            }
            {
                uint8_t t45 = gm_l17;
                if (!t45) goto ie44;
                {
                    void * t47 = (void *)((uint8_t *)&F.l11);
                    uint64_t t48 = gm_l16;
                    gm_b0_1 t49 = gm_f_load_package_modules_t4hh6gf((void *)NULL, cx, NULL, t47, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix44;
ie44:;
ix44:;
            }
            {
                void * t50 = (void *)((uint8_t *)&F.l15);
                gm_b0_1 t51 = gm_f_resolve_declarations_vrpg1fl((void *)NULL, cx, NULL, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t52 = (void *)((uint8_t *)&F.l15);
                uint64_t t53 = gm_l16;
                gm_b0_1 t54 = gm_f_check_program_rltsqqp((void *)NULL, cx, NULL, t52, t53, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t55 = 0;
                uint8_t t56 = gm_l17;
                if (t56) {
                    void * t57 = (void *)((uint8_t *)&F.l11);
                    uint64_t t58 = gm_l16;
                    gm_b16_8 t59;
                    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sog4rksrmsqd2));
                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(4));
                    gm_tf5srh4mp4b3i t60 = gm_f_lookup_59juqoo(t57, t58, t59);
                    uint8_t t61;
                    if (((((uint8_t *)&t60))[8] != 0)) goto mn63;
                    t61 = ((uint8_t)0);
                    goto me62;
mn63:;
                    t61 = ((uint8_t)1);
                    goto me62;
mn64:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 145, 37);
me62:;
                    t55 = t61;
                } else t55 = 0;
                gm_l18 = t55;
            }
            {
                void * t65 = (void *)((uint8_t *)&F.l15);
                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                uint8_t t68 = gm_l18;
                void * t69 = (void *)((uint8_t *)&F.l7);
                void * t70 = gm_l4;
                gm_f_run_n6la0ov((void *)&(*(gm_tc70a9dbbmgkg *)((uint8_t *)&gm_l14)), (void *)NULL, cx, NULL, t65, t67, t68, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix32;
ie32:;
ix32:;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        uint8_t t72;
        if (((((uint8_t *)&gm_l14))[16] != 0)) goto mn74;
        t72 = ((uint8_t)1);
        goto me73;
mn74:;
        t72 = ((uint8_t)0);
        goto me73;
mn75:;
        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 149, 8);
me73:;
        if (!t72) goto ie71;
        void * t76 = gm_l5;
        uint64_t t77 = GM_LD(uintptr_t, ((((uint8_t *)t76) + 64) + 0) + 8);
        uint8_t t78 = (t77 > ((uint64_t)0ULL));
        if (!t78) goto ie71;
        {
            {
                void * t81 = gm_l5;
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((((uint8_t *)t81) + 64) + 0));
                gm_l19 = t82;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t84 = gm_l20;
                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t86 = (t84 < t85);
                    if (!t86) goto lb83;
                    {
                        uint64_t t89 = gm_l20;
                        uint64_t t90 = (uintptr_t)t89;
                        uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_t6j6smj7qoctu t94 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t90 * 32));
                        gm_l21 = t94;
                        {
                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                            uint8_t t99 = gm_f_write_file_d3o5k61((void *)NULL, cx, NULL, t97, t98);
                            if (gm_unw.kind) {
                                goto gm_ll87;
                            }
                            uint8_t t100 = (uint8_t)!t99;
                            if (!t100) goto ie96;
                            {
                                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                if (gm_pf454qfrpt6r(cx, gm_se2a9lqlhae47, 21, gm_src0m6lnv6m2d3p, 152, 17)) {
                                    goto gm_ll87;
                                }
                                if ((GM_LD(uintptr_t, ((uint8_t *)&t102) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t102)), GM_LD(uintptr_t, ((uint8_t *)&t102) + 8), gm_src0m6lnv6m2d3p, 152, 17))) {
                                    goto gm_ll87;
                                }
                                if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_src0m6lnv6m2d3p, 152, 17)) {
                                    goto gm_ll87;
                                }
                            }
                            {
                                gm_tk5oh6c9vhlab t103;
                                GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)0ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t103 + 1), ((uint8_t)1ULL));
                                uint8_t t104 = 0;
                                F.ret = t103;
                                goto gm_fret;
                            }
                            goto ix96;
ie96:;
ix96:;
                        }
                        goto me92;
mn93:;
                        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 150, 9);
me92:;
                    }
lc83:;
                    {
                        uint64_t t106 = gm_l20;
                        uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                        uint64_t t109 = r107;
                        gm_l20 = t109;
                    }
                }
gm_ll87:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb83; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc83; }
                }
                goto gm_lad;
lb83:;
            }
        }
        {
            void * t110 = gm_l5;
            void * t111 = (void *)(((uint8_t *)t110) + 64);
            gm_f_clear_4vov4lr(t111);
        }
        {
            void * t112 = gm_l5;
            void * t113 = gm_l5;
            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)t113) + 16));
            void * t115 = gm_l5;
            uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)t115) + 144));
            uint64_t r117; bool o118 = __builtin_sub_overflow(t114, t116, &r117);
            if (o118) gm_trap("integer overflow in `-`", gm_src0m6lnv6m2d3p, 157, 9);
            uint64_t t119 = r117;
            GM_ST(uint64_t, (((uint8_t *)t112) + 16), t119);
        }
        {
            void * t121 = gm_l5;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)t121) + 152));
            if (!t122) goto ie120;
            {
                void * t124 = gm_l5;
                void * t125 = gm_l5;
                uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)t125) + 216));
                uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                if (o128) gm_trap("integer overflow in `+`", gm_src0m6lnv6m2d3p, 159, 13);
                uint64_t t129 = r127;
                GM_ST(uint64_t, (((uint8_t *)t124) + 216), t129);
            }
            {
                {
                    void * t131 = gm_l5;
                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((((uint8_t *)t131) + 104) + 0));
                    gm_l22 = t132;
                }
                {
                    gm_l23 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t134 = gm_l23;
                        uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        uint8_t t136 = (t134 < t135);
                        if (!t136) goto lb133;
                        {
                            uint64_t t139 = gm_l23;
                            uint64_t t140 = (uintptr_t)t139;
                            uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t140 * 16));
                            gm_l24 = t144;
                            {
                                void * t146 = gm_l5;
                                void * t147 = (void *)(((uint8_t *)t146) + 160);
                                gm_b16_8 t148 = gm_l24;
                                gm_tftb1odc739t5 t149 = gm_f_put_5c6qgui(t147, t148, ((uint8_t)1));
                                if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_src0m6lnv6m2d3p, 160, 38);
                                gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
                            }
                            goto me142;
mn143:;
                            gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 160, 13);
me142:;
                        }
lc133:;
                        {
                            uint64_t t152 = gm_l23;
                            uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                            uint64_t t155 = r153;
                            gm_l23 = t155;
                        }
                    }
lb133:;
                }
            }
            {
                void * t156 = gm_l5;
                void * t157 = (void *)(((uint8_t *)t156) + 104);
                gm_f_clear_dvdem7p(t157);
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            void * t158 = gm_l5;
            GM_ST(uint8_t, (((uint8_t *)t158) + 152), ((uint8_t)1));
        }
        {
            gm_tk5oh6c9vhlab t159;
            GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)1ULL));
            uint8_t t160 = 0;
            F.ret = t159;
            goto gm_fret;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        uint64_t t161;
        void * t163 = gm_l5;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)t163) + 152));
        if (!t164) goto ie162;
        uint64_t t166 = ((uint64_t)1000ULL);
        t161 = t166;
        goto ix162;
ie162:;
        uint64_t t168 = ((uint64_t)10ULL);
        t161 = t168;
ix162:;
        gm_l25 = t161;
    }
    {
        gm_l26 = ((uint64_t)0ULL);
    }
    {
        gm_f_list_t5p1bue((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l27 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&F.l27 + 40, ((uint64_t)0ULL));
    }
    {
        uint8_t t170;
        if (((((uint8_t *)&gm_l14))[16] != 0)) goto mn172;
        t170 = ((uint8_t)0);
        goto me171;
mn172:;
        t170 = ((uint8_t)1);
        goto me171;
mn173:;
        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 169, 8);
me171:;
        if (!t170) goto ie169;
        void * t174 = gm_l1;
        uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)t174) + 0));
        if (!t175) goto ie169;
        uint64_t t176 = gm_l6;
        uint64_t t177 = gm_l25;
        uint8_t t178 = (t176 < t177);
        if (!t178) goto ie169;
        {
            gm_f_list_7cdag6l((void *)&F.l28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l29, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l31 = ((uint64_t)0ULL);
            }
            {
                uint64_t t181 = GM_LD(uintptr_t, ((((uint8_t *)&F.l9) + 0) + 0) + 8);
                gm_l30 = t181;
            }
            {
                for (;;) {
                    uint64_t t183 = gm_l31;
                    uint64_t t184 = gm_l30;
                    uint8_t t185 = (t183 < t184);
                    if (!t185) goto lb182;
                    {
                        uint64_t t188 = gm_l31;
                        uint64_t t191 = GM_LD(uint64_t, ((uint8_t *)&t188));
                        gm_l32 = t191;
                        {
                            uint64_t t193 = gm_l32;
                            uint64_t t194 = (uintptr_t)t193;
                            uint64_t t195 = GM_LD(uintptr_t, ((((uint8_t *)&F.l9) + 0) + 0) + 8);
                            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_src0m6lnv6m2d3p, 173, 19);
                            void * t196 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l9) + 0) + 0)) + t194 * 184);
                            gm_l33 = t196;
                        }
                        {
                            void * t197 = gm_l33;
                            gm_th2lqbo77pvsc t198 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t197) + 160));
                            gm_b16_8 t199;
                            if (((((uint8_t *)&t198))[16] != 0)) {
                                gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 0));
                                t199 = t200;
                            } else {
                                goto lc182;
                            }
                            gm_l34 = t199;
                        }
                        {
                            void * t202 = gm_l33;
                            uint64_t t203 = GM_LD(uintptr_t, ((((uint8_t *)t202) + 120) + 0) + 8);
                            uint8_t t204 = (!(t203 == ((uint64_t)1ULL)));
                            if (!t204) goto ie201;
                            {
                                goto lc182;
                            }
                            goto ix201;
ie201:;
ix201:;
                        }
                        {
                            void * t207 = gm_l5;
                            void * t208 = (void *)(((uint8_t *)t207) + 160);
                            void * t209 = (void *)((uint8_t *)&F.l8);
                            void * t210 = gm_l33;
                            gm_b16_8 t211 = gm_l34;
                            gm_b16_8 t212 = gm_f_key_of_kh3apqr((void *)NULL, cx, NULL, t209, t210, t211);
                            if (gm_unw.kind) {
                                goto gm_ll186;
                            }
                            uint8_t t213 = gm_f_contains_57n7jqd(t208, t212);
                            if (!t213) goto ie206;
                            {
                                goto lc182;
                            }
                            goto ix206;
ie206:;
ix206:;
                        }
                        {
                            void * t215 = (void *)((uint8_t *)&F.l28);
                            void * t216 = gm_l33;
                            gm_tftb1odc739t5 t217 = gm_f_push_gnvji28(t215, t216);
                            if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_src0m6lnv6m2d3p, 177, 13);
                            gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
                        }
                        {
                            void * t220 = gm_l5;
                            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)t220) + 152));
                            if (!t221) goto ie219;
                            {
                                goto lb182;
                            }
                            goto ix219;
ie219:;
ix219:;
                        }
                        goto me189;
mn190:;
                        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 172, 9);
me189:;
                    }
lc182:;
                    {
                        uint64_t t224 = gm_l31;
                        uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                        uint64_t t227 = r225;
                        gm_l31 = t227;
                    }
                }
gm_ll186:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb182; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc182; }
                }
                goto gm_lad;
lb182:;
            }
        }
        {
            {
                gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                gm_l35 = t229;
            }
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t231 = gm_l36;
                    uint64_t t232 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                    uint8_t t233 = (t231 < t232);
                    if (!t233) goto lb230;
                    {
                        uint64_t t236 = gm_l36;
                        uint64_t t237 = (uintptr_t)t236;
                        uint64_t t238 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                        void * t239 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t237 * 8));
                        void * t242 = GM_LD(void *, ((uint8_t *)&t239));
                        gm_l37 = t242;
                        {
                            void * t244 = (void *)((uint8_t *)&F.l29);
                            void * t245 = gm_l5;
                            void * t246 = (void *)((uint8_t *)&F.l8);
                            void * t247 = gm_l37;
                            void * t248 = gm_l37;
                            gm_th2lqbo77pvsc t249 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t248) + 160));
                            gm_b16_8 t250;
                            if (((((uint8_t *)&t249))[16] != 0)) {
                                gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 0));
                                t250 = t251;
                            } else {
                                gm_b16_8 t252;
                                GM_ST(void *, (uint8_t *)&t252, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(0));
                                t250 = t252;
                            }
                            gm_b16_8 t253 = gm_f_key_of_kh3apqr((void *)NULL, cx, NULL, t246, t247, t250);
                            if (gm_unw.kind) {
                                goto gm_ll234;
                            }
                            gm_b16_8 t254 = gm_f_keep_vme6lns(t245, t253);
                            gm_tftb1odc739t5 t255 = gm_f_push_3ef746b(t244, t254);
                            if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 1, gm_src0m6lnv6m2d3p, 180, 31);
                            gm_b0_1 t256 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 1));
                        }
                        goto me240;
mn241:;
                        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 180, 9);
me240:;
                    }
lc230:;
                    {
                        uint64_t t258 = gm_l36;
                        uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                        uint64_t t261 = r259;
                        gm_l36 = t261;
                    }
                }
gm_ll234:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb230; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc230; }
                }
                goto gm_lad;
lb230:;
            }
        }
        {
            void * t262 = gm_l5;
            void * t263 = (void *)(((uint8_t *)t262) + 104);
            gm_f_clear_dvdem7p(t263);
        }
        {
            {
                gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                gm_l38 = t265;
            }
            {
                gm_l39 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t267 = gm_l39;
                    uint64_t t268 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                    uint8_t t269 = (t267 < t268);
                    if (!t269) goto lb266;
                    {
                        uint64_t t272 = gm_l39;
                        uint64_t t273 = (uintptr_t)t272;
                        uint64_t t274 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t273 * 16));
                        gm_l40 = t277;
                        {
                            void * t279 = gm_l5;
                            void * t280 = (void *)(((uint8_t *)t279) + 104);
                            gm_b16_8 t281 = gm_l40;
                            gm_tftb1odc739t5 t282 = gm_f_push_3ef746b(t280, t281);
                            if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 1, gm_src0m6lnv6m2d3p, 182, 31);
                            gm_b0_1 t283 = GM_LD(gm_b0_1, (((uint8_t *)&t282) + 1));
                        }
                        goto me275;
mn276:;
                        gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 182, 9);
me275:;
                    }
lc266:;
                    {
                        uint64_t t285 = gm_l39;
                        uint64_t r286; bool o287 = __builtin_add_overflow(t285, ((uint64_t)1ULL), &r286);
                        uint64_t t288 = r286;
                        gm_l39 = t288;
                    }
                }
lb266:;
            }
        }
        {
            gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
            gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
            gm_closure t291 = ((gm_closure){(void *)gm_f_fn_v5vf4j7, (void *)&F.h, cx});
            gm_f_plan_1bssrl4((void *)&(*(gm_tdm3p1stpne4q *)((uint8_t *)&F.l27)), (void *)NULL, cx, NULL, t290, t291);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&F.l27) + 40));
            gm_l26 = t292;
        }
        goto ix169;
ie169:;
ix169:;
    }
    {
        uint64_t t294 = gm_l26;
        uint8_t t295 = ((t294 == ((uint64_t)0ULL)));
        if (!t295) goto ie293;
        {
            void * t297 = (void *)((uint8_t *)&F.l8);
            void * t298 = (void *)((uint8_t *)&F.l9);
            gm_t9qkh0rsooc8d t299 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a2) + 49));
            gm_b0_1 t300 = gm_f_render_9she7th((void *)NULL, cx, NULL, t297, t298, t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t302 = (!(GM_LD(uint8_t, (((uint8_t *)a2) + 49) + 0) == ((uint8_t)2ULL)));
            if (!t302) goto ie301;
            {
                void * t305 = gm_l5;
                uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)t305) + 216));
                uint8_t t307 = (t306 > ((uint64_t)0ULL));
                if (!t307) goto ie304;
                {
                    void * t309 = gm_l5;
                    uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)t309) + 216));
                    void * t311 = gm_l5;
                    uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)t311) + 216));
                    gm_b16_8 t313 = gm_f_plural_es_2s51qvc(t312);
                    gm_b16_8 t314;
                    void * t316 = gm_l5;
                    uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)t316) + 216));
                    uint8_t t318 = ((t317 == ((uint64_t)1ULL)));
                    if (!t318) goto ie315;
                    gm_b16_8 t320;
                    GM_ST(void *, (uint8_t *)&t320, (void *)(gm_sj0qpc1n9qui9));
                    GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)(6));
                    gm_b16_8 t321 = t320;
                    t314 = t321;
                    goto ix315;
ie315:;
                    gm_b16_8 t323;
                    GM_ST(void *, (uint8_t *)&t323, (void *)(gm_s1b2h3bb9to0f));
                    GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(9));
                    gm_b16_8 t324 = t323;
                    t314 = t324;
ix315:;
                    if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_src0m6lnv6m2d3p, 190, 17)) {
                        goto gm_lad;
                    }
                    if (gm_pfu8dq5bikbh(cx, &gm_ty6groq37ram, ((uint8_t *)&t310), gm_src0m6lnv6m2d3p, 190, 17)) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_sioup6jsbm84g, 9, gm_src0m6lnv6m2d3p, 190, 17)) {
                        goto gm_lad;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t313) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t313)), GM_LD(uintptr_t, ((uint8_t *)&t313) + 8), gm_src0m6lnv6m2d3p, 190, 17))) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_sb9bgabg66tgj, 41, gm_src0m6lnv6m2d3p, 190, 17)) {
                        goto gm_lad;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t314) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t314)), GM_LD(uintptr_t, ((uint8_t *)&t314) + 8), gm_src0m6lnv6m2d3p, 190, 17))) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_s465vmg9c4jq6, 13, gm_src0m6lnv6m2d3p, 190, 17)) {
                        goto gm_lad;
                    }
                }
                goto ix304;
ie304:;
ix304:;
            }
            {
                void * t326 = gm_l5;
                uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)t326) + 16));
                uint8_t t328 = (t327 > ((uint64_t)0ULL));
                if (!t328) goto ie325;
                {
                    void * t330 = gm_l5;
                    uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)t330) + 16));
                    void * t332 = gm_l5;
                    uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)t332) + 16));
                    gm_b16_8 t334 = gm_f_plural_es_2s51qvc(t333);
                    void * t335 = gm_l5;
                    uint64_t t336 = GM_LD(uintptr_t, ((((uint8_t *)t335) + 24) + 0) + 8);
                    void * t337 = gm_l5;
                    uint64_t t338 = GM_LD(uintptr_t, ((((uint8_t *)t337) + 24) + 0) + 8);
                    gm_b16_8 t339 = gm_f_plural_s_6n4fvg3(t338);
                    if (gm_pf454qfrpt6r(cx, gm_sdl70rjr9en4m, 15, gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                    if (gm_pfu8dq5bikbh(cx, &gm_ty6groq37ram, ((uint8_t *)&t331), gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_sn1m0t4k3vpa5, 4, gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t334) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t334)), GM_LD(uintptr_t, ((uint8_t *)&t334) + 8), gm_src0m6lnv6m2d3p, 193, 17))) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_s7hkl5mqa15gm, 4, gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                    if (gm_pfu8dq5bikbh(cx, &gm_ty6groq37ram, ((uint8_t *)&t336), gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_sdajtpr06648l, 5, gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t339) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t339)), GM_LD(uintptr_t, ((uint8_t *)&t339) + 8), gm_src0m6lnv6m2d3p, 193, 17))) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src0m6lnv6m2d3p, 193, 17)) {
                        goto gm_lad;
                    }
                }
                goto ix325;
ie325:;
ix325:;
            }
            {
                if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie340;
                gm_t2lqu8jsj511d t341 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l14));
                gm_l41 = t341;
                {
                    uint64_t t344 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 0));
                    uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                    uint64_t r346; bool o347 = __builtin_add_overflow(t344, t345, &r346);
                    if (o347) gm_trap("integer overflow in `+`", gm_src0m6lnv6m2d3p, 196, 20);
                    uint64_t t348 = r346;
                    uint8_t t349 = (t348 > ((uint64_t)0ULL));
                    if (!t349) goto ie343;
                    {
                        gm_f_list_epvducg((void *)&F.l42, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                        uint8_t t353 = (t352 > ((uint64_t)0ULL));
                        if (!t353) goto ie351;
                        {
                            void * t355 = (void *)((uint8_t *)&F.l42);
                            gm_tf1bjg81u0glv t356;
                            uint64_t t357 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                            GM_ST(uint64_t, (uint8_t *)&t356 + 0, t357);
                            uint64_t t358 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                            gm_b16_8 t359 = gm_f_plural_s_6n4fvg3(t358);
                            GM_ST(gm_b16_8, (uint8_t *)&t356 + 8, t359);
                            gm_tabb710d9mq2e t360;
                            gm_f_format_uc6bvcv((void *)&t360, (void *)NULL, cx, NULL, &t356);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 8, gm_src0m6lnv6m2d3p, 198, 52);
                            gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&t360) + 8));
                            gm_b16_8 t362 = GM_LD(gm_b16_8, ((uint8_t *)&t361));
                            gm_tftb1odc739t5 t363 = gm_f_push_3ef746b(t355, t362);
                            if (((((uint8_t *)&t363))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t363) + 1, gm_src0m6lnv6m2d3p, 198, 41);
                            gm_b0_1 t364 = GM_LD(gm_b0_1, (((uint8_t *)&t363) + 1));
                        }
                        goto ix351;
ie351:;
ix351:;
                    }
                    {
                        uint64_t t366 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 0));
                        uint8_t t367 = (t366 > ((uint64_t)0ULL));
                        if (!t367) goto ie365;
                        {
                            void * t369 = (void *)((uint8_t *)&F.l42);
                            gm_tf1bjg81u0glv t370;
                            uint64_t t371 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 0));
                            GM_ST(uint64_t, (uint8_t *)&t370 + 0, t371);
                            uint64_t t372 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 0));
                            gm_b16_8 t373 = gm_f_plural_s_6n4fvg3(t372);
                            GM_ST(gm_b16_8, (uint8_t *)&t370 + 8, t373);
                            gm_tabb710d9mq2e t374;
                            gm_f_format_i3a5v4o((void *)&t374, (void *)NULL, cx, NULL, &t370);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (((((uint8_t *)&t374))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t374) + 8, gm_src0m6lnv6m2d3p, 199, 54);
                            gm_b16_8 t375 = GM_LD(gm_b16_8, (((uint8_t *)&t374) + 8));
                            gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                            gm_tftb1odc739t5 t377 = gm_f_push_3ef746b(t369, t376);
                            if (((((uint8_t *)&t377))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t377) + 1, gm_src0m6lnv6m2d3p, 199, 43);
                            gm_b0_1 t378 = GM_LD(gm_b0_1, (((uint8_t *)&t377) + 1));
                        }
                        goto ix365;
ie365:;
ix365:;
                    }
                    {
                        gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
                        gm_b16_8 t380 = GM_LD(gm_b16_8, ((uint8_t *)&t379));
                        gm_b16_8 t381;
                        GM_ST(void *, (uint8_t *)&t381, (void *)(gm_southrlg4k72m));
                        GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(5));
                        gm_tabb710d9mq2e t382;
                        gm_f_join_sfknljb((void *)&t382, (void *)NULL, cx, NULL, t380, t381);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t382))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t382) + 8, gm_src0m6lnv6m2d3p, 200, 48);
                        gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&t382) + 8));
                        if (gm_pf454qfrpt6r(cx, gm_sm26kv3hefp6i, 12, gm_src0m6lnv6m2d3p, 200, 21)) {
                            goto gm_lad;
                        }
                        if (gm_pfu8dq5bikbh(cx, &gm_tybihgc69jv3, ((uint8_t *)&t383), gm_src0m6lnv6m2d3p, 200, 21)) {
                            goto gm_lad;
                        }
                        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src0m6lnv6m2d3p, 200, 21)) {
                            goto gm_lad;
                        }
                    }
                    goto ix343;
ie343:;
ix343:;
                }
                goto ix340;
ie340:;
ix340:;
            }
            goto ix301;
ie301:;
ix301:;
        }
        {
            if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie384;
            gm_t2lqu8jsj511d t385 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l14));
            gm_l43 = t385;
            {
                uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                uint8_t t389 = ((t388 == ((uint64_t)0ULL)));
                if (!t389) goto ie387;
                {
                    gm_tk5oh6c9vhlab t391;
                    GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t391 + 1), ((uint8_t)0ULL));
                    uint8_t t392 = 0;
                    F.ret = t391;
                    goto gm_fret;
                }
                goto ix387;
ie387:;
ix387:;
            }
            goto ix384;
ie384:;
ix384:;
        }
        {
            gm_tk5oh6c9vhlab t393;
            GM_ST(uint8_t, ((uint8_t *)&t393 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t393 + 1), ((uint8_t)1ULL));
            uint8_t t394 = 0;
            F.ret = t393;
            goto gm_fret;
        }
        goto ix293;
ie293:;
ix293:;
    }
    {
        void * t395 = gm_l5;
        void * t396 = (void *)(((uint8_t *)t395) + 64);
        gm_f_clear_4vov4lr(t396);
    }
    {
        void * t397 = gm_l5;
        uint64_t t398 = gm_l26;
        GM_ST(uint64_t, (((uint8_t *)t397) + 144), t398);
    }
    {
        {
            gm_b16_8 t400 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l27) + 0) + 0));
            gm_l44 = t400;
        }
        {
            gm_l45 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t402 = gm_l45;
                uint64_t t403 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                uint8_t t404 = (t402 < t403);
                if (!t404) goto lb401;
                {
                    uint64_t t407 = gm_l45;
                    uint64_t t408 = (uintptr_t)t407;
                    uint64_t t409 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    gm_tha5675rv512q t412 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t408 * 48));
                    F.l46 = t412;
                    {
                        void * t414 = (void *)((uint8_t *)&F.l8);
                        uint32_t t415 = GM_LD(uint32_t, (((uint8_t *)&F.l46) + 0));
                        void * t416 = gm_f_file_o15fqku(t414, t415);
                        gm_l47 = t416;
                    }
                    {
                        void * t417 = gm_l47;
                        gm_b16_8 t418 = GM_LD(gm_b16_8, (((uint8_t *)t417) + 16));
                        gm_b16_8 t419 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l46) + 8) + 0));
                        gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&t419));
                        gm_b16_8 t421 = gm_f_apply_57egqj5((void *)NULL, cx, NULL, t418, t420);
                        if (gm_unw.kind) {
                            goto gm_ll405;
                        }
                        gm_l48 = t421;
                    }
                    {
                        void * t423 = gm_l47;
                        gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)t423) + 0));
                        gm_b16_8 t425 = gm_l48;
                        uint8_t t426 = gm_f_write_file_d3o5k61((void *)NULL, cx, NULL, t424, t425);
                        if (gm_unw.kind) {
                            goto gm_ll405;
                        }
                        uint8_t t427 = (uint8_t)!t426;
                        if (!t427) goto ie422;
                        {
                            void * t429 = gm_l47;
                            gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)t429) + 0));
                            if (gm_pf454qfrpt6r(cx, gm_se2a9lqlhae47, 21, gm_src0m6lnv6m2d3p, 215, 13)) {
                                goto gm_ll405;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t430) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t430)), GM_LD(uintptr_t, ((uint8_t *)&t430) + 8), gm_src0m6lnv6m2d3p, 215, 13))) {
                                goto gm_ll405;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_src0m6lnv6m2d3p, 215, 13)) {
                                goto gm_ll405;
                            }
                        }
                        {
                            gm_tk5oh6c9vhlab t431;
                            GM_ST(uint8_t, ((uint8_t *)&t431 + 0), ((uint8_t)0ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t431 + 1), ((uint8_t)1ULL));
                            uint8_t t432 = 0;
                            F.ret = t431;
                            goto gm_fret;
                        }
                        goto ix422;
ie422:;
ix422:;
                    }
                    {
                        void * t433 = gm_l5;
                        void * t434 = (void *)(((uint8_t *)t433) + 64);
                        gm_t6j6smj7qoctu t435;
                        void * t436 = gm_l5;
                        void * t437 = gm_l47;
                        gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)t437) + 0));
                        gm_b16_8 t439 = gm_f_keep_vme6lns(t436, t438);
                        GM_ST(gm_b16_8, (uint8_t *)&t435 + 0, t439);
                        void * t440 = gm_l5;
                        void * t441 = gm_l47;
                        gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)t441) + 16));
                        gm_b16_8 t443 = gm_f_keep_vme6lns(t440, t442);
                        GM_ST(gm_b16_8, (uint8_t *)&t435 + 16, t443);
                        gm_tftb1odc739t5 t444 = gm_f_push_j3qd697(t434, &t435);
                        if (((((uint8_t *)&t444))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t444) + 1, gm_src0m6lnv6m2d3p, 218, 9);
                        gm_b0_1 t445 = GM_LD(gm_b0_1, (((uint8_t *)&t444) + 1));
                    }
                    {
                        void * t447 = gm_l5;
                        gm_b16_8 t448 = GM_LD(gm_b16_8, ((((uint8_t *)t447) + 24) + 0));
                        gm_b16_8 t449 = GM_LD(gm_b16_8, ((uint8_t *)&t448));
                        void * t450 = gm_l47;
                        gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)t450) + 0));
                        uint8_t t452 = gm_f_contains_acanf35(t449, t451);
                        uint8_t t453 = (uint8_t)!t452;
                        if (!t453) goto ie446;
                        {
                            void * t455 = gm_l5;
                            void * t456 = (void *)(((uint8_t *)t455) + 24);
                            void * t457 = gm_l5;
                            void * t458 = gm_l47;
                            gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)t458) + 0));
                            gm_b16_8 t460 = gm_f_keep_vme6lns(t457, t459);
                            gm_tftb1odc739t5 t461 = gm_f_push_3ef746b(t456, t460);
                            if (((((uint8_t *)&t461))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t461) + 1, gm_src0m6lnv6m2d3p, 219, 54);
                            gm_b0_1 t462 = GM_LD(gm_b0_1, (((uint8_t *)&t461) + 1));
                        }
                        goto ix446;
ie446:;
ix446:;
                    }
                    goto me410;
mn411:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 211, 5);
me410:;
                }
lc401:;
                {
                    uint64_t t464 = gm_l45;
                    uint64_t r465; bool o466 = __builtin_add_overflow(t464, ((uint64_t)1ULL), &r465);
                    uint64_t t467 = r465;
                    gm_l45 = t467;
                }
            }
gm_ll405:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb401; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc401; }
            }
            goto gm_lad;
lb401:;
        }
    }
    {
        void * t468 = gm_l5;
        void * t469 = gm_l5;
        uint64_t t470 = GM_LD(uint64_t, (((uint8_t *)t469) + 16));
        uint64_t t471 = gm_l26;
        uint64_t r472; bool o473 = __builtin_add_overflow(t470, t471, &r472);
        if (o473) gm_trap("integer overflow in `+`", gm_src0m6lnv6m2d3p, 221, 5);
        uint64_t t474 = r472;
        GM_ST(uint64_t, (((uint8_t *)t468) + 16), t474);
    }
    {
        gm_tk5oh6c9vhlab t475;
        GM_ST(uint8_t, (uint8_t *)&t475 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t475 + 0), ((uint8_t)1ULL));
        uint8_t t476 = 0;
        F.ret = t475;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_list_rules_tjipqqd(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rules_tjipqqd F;
    uint64_t gm_l0; /* width */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* level */
    GM_STACK_CHECK(&F, 592u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_l0 = ((uint64_t)0ULL);
    }
    {
        {
            void * t3 = ((uint8_t *)gm_cqss81ml8qn);
            if ((uintptr_t)0 > (uintptr_t)23 || (uintptr_t)23 > (uintptr_t)23) gm_trap_slice((uintptr_t)0, (uintptr_t)23, 23, gm_src0m6lnv6m2d3p, 6, 14);
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t3 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)((uintptr_t)23 - (uintptr_t)0));
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
                    gm_tk9n3vlila9qg t16 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 40));
                    F.l3 = t16;
                    {
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        uint64_t t20 = gm_l0;
                        uint8_t t21 = (t19 > t20);
                        if (!t21) goto ie18;
                        {
                            uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            gm_l0 = t23;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 6, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t25 = gm_l2;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l2 = t28;
                }
            }
lb5:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t30 = ((uint8_t *)gm_cqss81ml8qn);
            if ((uintptr_t)0 > (uintptr_t)23 || (uintptr_t)23 > (uintptr_t)23) gm_trap_slice((uintptr_t)0, (uintptr_t)23, 23, gm_src0m6lnv6m2d3p, 10, 14);
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t30 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)((uintptr_t)23 - (uintptr_t)0));
            gm_l5 = t31;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t33 = gm_l6;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t35 = (t33 < t34);
                if (!t35) goto lb32;
                {
                    uint64_t t38 = gm_l6;
                    uint64_t t39 = (uintptr_t)t38;
                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tk9n3vlila9qg t43 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t39 * 40));
                    F.l7 = t43;
                    {
                        void * t45 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_tftb1odc739t5 t47 = gm_f_extend_0cs85gs(t45, t46);
                        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_src0m6lnv6m2d3p, 11, 9);
                        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                    }
                    {
                        void * t49 = (void *)((uint8_t *)&F.l4);
                        uint64_t t50 = gm_l0;
                        uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                        uint64_t r52; bool o53 = __builtin_sub_overflow(t50, t51, &r52);
                        if (o53) gm_trap("integer overflow in `-`", gm_src0m6lnv6m2d3p, 12, 19);
                        uint64_t t54 = r52;
                        gm_b0_1 t55 = gm_f_pad_3pipl52((void *)NULL, cx, NULL, t49, t54);
                        if (gm_unw.kind) {
                            goto gm_ll36;
                        }
                    }
                    {
                        void * t56 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t57;
                        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sgankvlme129s));
                        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t58 = gm_f_extend_0cs85gs(t56, t57);
                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_src0m6lnv6m2d3p, 13, 9);
                        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
                    }
                    {
                        void * t60 = (void *)((uint8_t *)&F.l7);
                        gm_t9qkh0rsooc8d t61 = gm_f_default_level_urberdg(t60);
                        gm_b16_8 t62 = gm_f_level_name_qqj3mmj(t61);
                        gm_l8 = t62;
                    }
                    {
                        void * t63 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t64 = gm_l8;
                        gm_tftb1odc739t5 t65 = gm_f_extend_0cs85gs(t63, t64);
                        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_src0m6lnv6m2d3p, 15, 9);
                        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                    }
                    {
                        void * t67 = (void *)((uint8_t *)&F.l4);
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint64_t r69; bool o70 = __builtin_sub_overflow(((uint64_t)5ULL), t68, &r69);
                        if (o70) gm_trap("integer overflow in `-`", gm_src0m6lnv6m2d3p, 16, 19);
                        uint64_t t71 = r69;
                        gm_b0_1 t72 = gm_f_pad_3pipl52((void *)NULL, cx, NULL, t67, t71);
                        if (gm_unw.kind) {
                            goto gm_ll36;
                        }
                    }
                    {
                        void * t73 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t74;
                        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sgankvlme129s));
                        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t75 = gm_f_extend_0cs85gs(t73, t74);
                        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src0m6lnv6m2d3p, 17, 9);
                        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                    }
                    {
                        void * t77 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                        gm_tftb1odc739t5 t79 = gm_f_extend_0cs85gs(t77, t78);
                        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_src0m6lnv6m2d3p, 18, 9);
                        gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
                    }
                    {
                        void * t81 = (void *)((uint8_t *)&F.l4);
                        gm_tftb1odc739t5 t82 = gm_f_push_t83rg2m(t81, ((uint8_t)10ULL));
                        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_src0m6lnv6m2d3p, 19, 9);
                        gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                    }
                    goto me41;
mn42:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 10, 5);
me41:;
                }
lc32:;
                {
                    uint64_t t85 = gm_l6;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    uint64_t t88 = r86;
                    gm_l6 = t88;
                }
            }
gm_ll36:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
            }
            goto gm_lad;
lb32:;
        }
    }
    {
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
        gm_pfmaojsuds6a(cx, t90, gm_src0m6lnv6m2d3p, 21, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_pad_3pipl52(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_pad_3pipl52 F;
    void * gm_l0; /* out */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l1;
            gm_l2 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = gm_l2;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    {
                        void * t14 = gm_l0;
                        gm_tftb1odc739t5 t15 = gm_f_push_t83rg2m(t14, ((uint8_t)32ULL));
                        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src0m6lnv6m2d3p, 25, 21);
                        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src0m6lnv6m2d3p, 25, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t18 = gm_l3;
                    uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
                    uint64_t t21 = r19;
                    gm_l3 = t21;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
