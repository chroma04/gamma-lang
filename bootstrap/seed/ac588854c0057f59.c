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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
struct gm_fr_arg_expr_2h4reqo {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* x */
    gm_tc3oht4ksth9d l3; /* x */
    gm_tc3oht4ksth9d l4; /* x */
    gm_b0_1 ret;
};
struct gm_fr_binop_text_0ljfc0r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_flavored_m9vbtpd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_each_child_tkrfoqu {
    gm_frame h;
    gm_tet9guhev5r4u l38; /* x */
    gm_tchr52g0ema53 l45; /* b */
    gm_b0_1 ret;
};
struct gm_fr_has_spread_bndtpd6 {
    gm_frame h;
    gm_tv1o1j44ii4db l3; /* el */
    uint8_t ret;
};
__attribute__((aligned(4))) void gm_f_arg_expr_2h4reqo(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_flavored_m9vbtpd(gm_t9qkh0rsooc8d a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_for_each_child_tkrfoqu(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s0s4ef50964r9[3];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s1dbslfck7laf[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s3bkmscn5fi56[3];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_s9s0gbnmtpvjl[4];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sabc7m3rug04j[3];
extern const uint8_t gm_sad0udrpgk27o[3];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sfg1e0lbt8sf5[3];
extern const uint8_t gm_sgemh066sqogf[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_sjid61i849qic[3];
extern const uint8_t gm_sjjig60ja37ka[3];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skriisqpofdin[3];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_sp3rdossibc7f[4];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const char gm_src8os28n25rsa4[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svm0rfaqk3b2q[4];
inline __attribute__((aligned(4))) void gm_f_arg_expr_2h4reqo(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_expr_2h4reqo F;
    void * gm_l0; /* a */
    gm_closure gm_l1; /* f */
    GM_STACK_CHECK(&F, 500u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; default: goto ma7; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t2) + 0) + 8));
        F.l2 = t11;
        gm_closure t12 = gm_l1;
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_closure t14 = t12;
        gm_node *ccx15 = t14.cx;
        void * t16 = t14.code;
        gm_b0_1 t17;
        if (GM_PTR_TAGGED(t16)) {
            ((void (*)(void *))GM_PTR_UNTAG(t16))(t13);
        } else {
            t17 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t16)(t14.env, ccx15, NULL, t13);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t19 != ((uint8_t)1ULL)) goto mn18;
        gm_tc3oht4ksth9d t20 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)t2) + 0) + 8) + 32));
        F.l3 = t20;
        gm_closure t21 = gm_l1;
        void * t22 = (void *)((uint8_t *)&F.l3);
        gm_closure t23 = t21;
        gm_node *ccx24 = t23.cx;
        void * t25 = t23.code;
        gm_b0_1 t26;
        if (GM_PTR_TAGGED(t25)) {
            ((void (*)(void *))GM_PTR_UNTAG(t25))(t22);
        } else {
            t26 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t25)(t23.env, ccx24, NULL, t22);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn18:;
ma6:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t28 != ((uint8_t)2ULL)) goto mn27;
        gm_tc3oht4ksth9d t29 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t2) + 0) + 8));
        F.l4 = t29;
        gm_closure t30 = gm_l1;
        void * t31 = (void *)((uint8_t *)&F.l4);
        gm_closure t32 = t30;
        gm_node *ccx33 = t32.cx;
        void * t34 = t32.code;
        gm_b0_1 t35;
        if (GM_PTR_TAGGED(t34)) {
            ((void (*)(void *))GM_PTR_UNTAG(t34))(t31);
        } else {
            t35 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t34)(t32.env, ccx33, NULL, t31);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn27:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 779, 5);
me3:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binop_text_0ljfc0r F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t26) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; default: goto ma25; }
ma3:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)0ULL)) goto mn27;
    gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t29;
    gm_t9qkh0rsooc8d t30 = gm_l1;
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_skriisqpofdin));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s0s4ef50964r9));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(2));
    gm_b16_8 t34;
    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sjid61i849qic));
    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(2));
    gm_b16_8 t35 = gm_f_flavored_m9vbtpd(t30, t31, t32, t33, t34);
    t1 = t35;
    goto me2;
mn27:;
ma4:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t37 != ((uint8_t)1ULL)) goto mn36;
    gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l2 = t38;
    gm_t9qkh0rsooc8d t39 = gm_l2;
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(1));
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sjjig60ja37ka));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sfg1e0lbt8sf5));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(2));
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sabc7m3rug04j));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
    gm_b16_8 t44 = gm_f_flavored_m9vbtpd(t39, t40, t41, t42, t43);
    t1 = t44;
    goto me2;
mn36:;
ma5:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)2ULL)) goto mn45;
    gm_t9qkh0rsooc8d t47 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l3 = t47;
    gm_t9qkh0rsooc8d t48 = gm_l3;
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
    gm_b16_8 t50;
    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s1dbslfck7laf));
    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(2));
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sad0udrpgk27o));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(2));
    gm_b16_8 t52;
    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s3bkmscn5fi56));
    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
    gm_b16_8 t53 = gm_f_flavored_m9vbtpd(t48, t49, t50, t51, t52);
    t1 = t53;
    goto me2;
mn45:;
ma6:;
    uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t55 != ((uint8_t)3ULL)) goto mn54;
    gm_t9qkh0rsooc8d t56 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l4 = t56;
    gm_t9qkh0rsooc8d t57 = gm_l4;
    gm_b16_8 t58;
    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
    gm_b16_8 t59;
    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sgemh066sqogf));
    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(3));
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s9s0gbnmtpvjl));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
    gm_b16_8 t61;
    GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sp3rdossibc7f));
    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(3));
    gm_b16_8 t62 = gm_f_flavored_m9vbtpd(t57, t58, t59, t60, t61);
    t1 = t62;
    goto me2;
mn54:;
ma7:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t64 != ((uint8_t)4ULL)) goto mn63;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
    t1 = t65;
    goto me2;
mn63:;
ma8:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t67 != ((uint8_t)5ULL)) goto mn66;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
    t1 = t68;
    goto me2;
mn66:;
ma9:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t70 != ((uint8_t)6ULL)) goto mn69;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(2));
    t1 = t71;
    goto me2;
mn69:;
ma10:;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t73 != ((uint8_t)7ULL)) goto mn72;
    gm_b16_8 t74;
    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
    t1 = t74;
    goto me2;
mn72:;
ma11:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t76 != ((uint8_t)8ULL)) goto mn75;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
    t1 = t77;
    goto me2;
mn75:;
ma12:;
    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t79 != ((uint8_t)9ULL)) goto mn78;
    gm_b16_8 t80;
    GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(1));
    t1 = t80;
    goto me2;
mn78:;
ma13:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t82 != ((uint8_t)10ULL)) goto mn81;
    gm_b16_8 t83;
    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_snenkhfev166s));
    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(2));
    t1 = t83;
    goto me2;
mn81:;
ma14:;
    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t85 != ((uint8_t)11ULL)) goto mn84;
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_svm0rfaqk3b2q));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(3));
    t1 = t86;
    goto me2;
mn84:;
ma15:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t88 != ((uint8_t)12ULL)) goto mn87;
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sgpdmvc9s0d2p));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(2));
    t1 = t89;
    goto me2;
mn87:;
ma16:;
    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t91 != ((uint8_t)13ULL)) goto mn90;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s1c4sfff8ac39));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(6));
    t1 = t92;
    goto me2;
mn90:;
ma17:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t94 != ((uint8_t)14ULL)) goto mn93;
    gm_b16_8 t95;
    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_svbhcvlahd4n2));
    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(2));
    t1 = t95;
    goto me2;
mn93:;
ma18:;
    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t97 != ((uint8_t)15ULL)) goto mn96;
    gm_b16_8 t98;
    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sidtn67sdk4e0));
    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(6));
    t1 = t98;
    goto me2;
mn96:;
ma19:;
    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t100 != ((uint8_t)16ULL)) goto mn99;
    gm_b16_8 t101;
    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
    t1 = t101;
    goto me2;
mn99:;
ma20:;
    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t103 != ((uint8_t)17ULL)) goto mn102;
    gm_b16_8 t104;
    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s4g3cajt15dmi));
    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(2));
    t1 = t104;
    goto me2;
mn102:;
ma21:;
    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t106 != ((uint8_t)18ULL)) goto mn105;
    gm_b16_8 t107;
    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sm5be4659ulci));
    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(1));
    t1 = t107;
    goto me2;
mn105:;
ma22:;
    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t109 != ((uint8_t)19ULL)) goto mn108;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_s02ru83e3fffc));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(2));
    t1 = t110;
    goto me2;
mn108:;
ma23:;
    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t112 != ((uint8_t)20ULL)) goto mn111;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_s920dpvvv6cej));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(3));
    t1 = t113;
    goto me2;
mn111:;
ma24:;
    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t115 != ((uint8_t)21ULL)) goto mn114;
    gm_b16_8 t116;
    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_spi47lg4tr80j));
    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(2));
    t1 = t116;
    goto me2;
mn114:;
ma25:;
    gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 418, 46);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_flavored_m9vbtpd(gm_t9qkh0rsooc8d a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavored_m9vbtpd F;
    gm_t9qkh0rsooc8d gm_l0; /* f */
    gm_b16_8 gm_l1; /* checked */
    gm_b16_8 gm_l2; /* wrap */
    gm_b16_8 gm_l3; /* sat */
    gm_b16_8 gm_l4; /* opt */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    gm_b16_8 t11 = gm_l1;
    t1 = t11;
    goto me2;
mn9:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn12;
    gm_b16_8 t14 = gm_l2;
    t1 = t14;
    goto me2;
mn12:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    gm_b16_8 t17 = gm_l3;
    t1 = t17;
    goto me2;
mn15:;
ma6:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto mn18;
    gm_b16_8 t20 = gm_l4;
    t1 = t20;
    goto me2;
mn18:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 443, 73);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_for_each_child_tkrfoqu(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_child_tkrfoqu F;
    void * gm_l0; /* e */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* x */
    void * gm_l3; /* x */
    void * gm_l4; /* x */
    void * gm_l5; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* x */
    void * gm_l8; /* x */
    void * gm_l9; /* x */
    void * gm_l10; /* x */
    void * gm_l11; /* x */
    void * gm_l12; /* x */
    void * gm_l13; /* x */
    gm_b16_8 gm_l14; /* elems */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* a */
    void * gm_l19; /* b */
    void * gm_l20; /* a */
    void * gm_l21; /* b */
    void * gm_l22; /* a */
    void * gm_l23; /* b */
    void * gm_l24; /* a */
    void * gm_l25; /* b */
    void * gm_l26; /* x */
    void * gm_l27; /* x */
    gm_t3mf58gv9kf3n gm_l28; /* a */
    void * gm_l29; /* l */
    gm_tao63pk3r18v5 gm_l30; /* sl */
    void * gm_l31; /* x */
    void * gm_l32; /* x */
    void * gm_l33; /* callee */
    gm_b16_8 gm_l34; /* args */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* i */
    gm_b16_8 gm_l39; /* args */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    void * gm_l43; /* v */
    void * gm_l44; /* x */
    void * gm_l46; /* v */
    gm_t3mf58gv9kf3n gm_l47; /* r */
    void * gm_l48; /* v */
    GM_STACK_CHECK(&F, 2462u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma17; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma4; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma4; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma17; case ((uint8_t)31ULL): goto ma4; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma17; case ((uint8_t)35ULL): goto ma5; case ((uint8_t)36ULL): goto ma17; case ((uint8_t)37ULL): goto ma17; case ((uint8_t)38ULL): goto ma17; case ((uint8_t)39ULL): goto ma17; case ((uint8_t)40ULL): goto ma17; case ((uint8_t)41ULL): goto ma17; case ((uint8_t)42ULL): goto ma17; case ((uint8_t)43ULL): goto ma17; case ((uint8_t)44ULL): goto ma17; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma15; case ((uint8_t)47ULL): goto ma17; case ((uint8_t)48ULL): goto ma16; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma17; default: goto ma17; }
ma4:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)9ULL)) goto pa22;
        void * t24 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t24;
        goto po21;
pa22:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)11ULL)) goto pa25;
        void * t27 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t27;
        goto po21;
pa25:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)15ULL)) goto pa28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t30;
        goto po21;
pa28:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t32 != ((uint8_t)16ULL)) goto pa31;
        void * t33 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t33;
        goto po21;
pa31:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)18ULL)) goto pa34;
        void * t36 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t36;
        goto po21;
pa34:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t38 != ((uint8_t)24ULL)) goto pa37;
        void * t39 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t39;
        goto po21;
pa37:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)31ULL)) goto pa40;
        void * t42 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t42;
        goto po21;
pa40:;
        goto mn20;
po21:;
        gm_closure t43 = gm_l1;
        void * t44 = gm_l2;
        gm_closure t45 = t43;
        gm_node *ccx46 = t45.cx;
        void * t47 = t45.code;
        gm_b0_1 t48;
        if (GM_PTR_TAGGED(t47)) {
            ((void (*)(void *))GM_PTR_UNTAG(t47))(t44);
        } else {
            t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t47)(t45.env, ccx46, NULL, t44);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn20:;
ma5:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)25ULL)) goto pa51;
        void * t53 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t53;
        goto po50;
pa51:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t55 != ((uint8_t)26ULL)) goto pa54;
        void * t56 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t56;
        goto po50;
pa54:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)27ULL)) goto pa57;
        void * t59 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t59;
        goto po50;
pa57:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t61 != ((uint8_t)35ULL)) goto pa60;
        void * t62 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l9 = t62;
        goto po50;
pa60:;
        goto mn49;
po50:;
        gm_closure t63 = gm_l1;
        void * t64 = gm_l9;
        gm_closure t65 = t63;
        gm_node *ccx66 = t65.cx;
        void * t67 = t65.code;
        gm_b0_1 t68;
        if (GM_PTR_TAGGED(t67)) {
            ((void (*)(void *))GM_PTR_UNTAG(t67))(t64);
        } else {
            t68 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t67)(t65.env, ccx66, NULL, t64);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn49:;
ma6:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t70 != ((uint8_t)12ULL)) goto mn69;
        void * t71 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l13 = t71;
        gm_closure t72 = gm_l1;
        void * t73 = gm_l13;
        gm_closure t74 = t72;
        gm_node *ccx75 = t74.cx;
        void * t76 = t74.code;
        gm_b0_1 t77;
        if (GM_PTR_TAGGED(t76)) {
            ((void (*)(void *))GM_PTR_UNTAG(t76))(t73);
        } else {
            t77 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t76)(t74.env, ccx75, NULL, t73);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn69:;
ma7:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t79 != ((uint8_t)10ULL)) goto mn78;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t80;
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
            gm_l15 = t82;
        }
        {
            for (;;) {
                uint64_t t84 = gm_l16;
                uint64_t t85 = gm_l15;
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l16;
                    uint64_t t90 = t89;
                    uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&t90));
                    gm_l17 = t93;
                    {
                        gm_closure t95 = gm_l1;
                        uint64_t t96 = gm_l17;
                        uint64_t t97 = (uintptr_t)t96;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_src8os28n25rsa4, 744, 53);
                        void * t99 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t97 * 136) + 40);
                        gm_closure t100 = t95;
                        gm_node *ccx101 = t100.cx;
                        void * t102 = t100.code;
                        gm_b0_1 t103;
                        if (GM_PTR_TAGGED(t102)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t102))(t99);
                        } else {
                            t103 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t102)(t100.env, ccx101, NULL, t99);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll87;
                        }
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 744, 26);
me91:;
                }
lc83:;
                {
                    uint64_t t105 = gm_l16;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l16 = t108;
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
        goto me3;
mn78:;
ma8:;
        uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t112 != ((uint8_t)13ULL)) goto pa111;
        void * t113 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l18 = t113;
        void * t114 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l19 = t114;
        goto po110;
pa111:;
        uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t116 != ((uint8_t)17ULL)) goto pa115;
        void * t117 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l18 = t117;
        void * t118 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l19 = t118;
        goto po110;
pa115:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t120 != ((uint8_t)29ULL)) goto pa119;
        void * t121 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l18 = t121;
        void * t122 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l19 = t122;
        goto po110;
pa119:;
        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t124 != ((uint8_t)20ULL)) goto pa123;
        void * t125 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l18 = t125;
        void * t126 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l19 = t126;
        goto po110;
pa123:;
        goto mn109;
po110:;
        {
            gm_closure t128 = gm_l1;
            void * t129 = gm_l18;
            gm_closure t130 = t128;
            gm_node *ccx131 = t130.cx;
            void * t132 = t130.code;
            gm_b0_1 t133;
            if (GM_PTR_TAGGED(t132)) {
                ((void (*)(void *))GM_PTR_UNTAG(t132))(t129);
            } else {
                t133 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t132)(t130.env, ccx131, NULL, t129);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t134 = gm_l1;
            void * t135 = gm_l19;
            gm_closure t136 = t134;
            gm_node *ccx137 = t136.cx;
            void * t138 = t136.code;
            gm_b0_1 t139;
            if (GM_PTR_TAGGED(t138)) {
                ((void (*)(void *))GM_PTR_UNTAG(t138))(t135);
            } else {
                t139 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t138)(t136.env, ccx137, NULL, t135);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn109:;
ma9:;
        uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t143 != ((uint8_t)22ULL)) goto pa142;
        void * t144 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l26 = t144;
        goto po141;
pa142:;
        uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t146 != ((uint8_t)23ULL)) goto pa145;
        void * t147 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l26 = t147;
        goto po141;
pa145:;
        goto mn140;
po141:;
        gm_closure t148 = gm_l1;
        void * t149 = gm_l26;
        gm_closure t150 = t148;
        gm_node *ccx151 = t150.cx;
        void * t152 = t150.code;
        gm_b0_1 t153;
        if (GM_PTR_TAGGED(t152)) {
            ((void (*)(void *))GM_PTR_UNTAG(t152))(t149);
        } else {
            t153 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t152)(t150.env, ccx151, NULL, t149);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn140:;
ma10:;
        uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t155 != ((uint8_t)28ULL)) goto mn154;
        gm_t3mf58gv9kf3n t156 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l28 = t156;
        {
            void * t159 = GM_LD(void *, (((uint8_t *)&gm_l28) + 0));
            void * t160 = t159;
            if (!(GM_LD(void *, ((uint8_t *)&t160)) != 0)) goto ie158;
            void * t161 = GM_LD(void *, ((uint8_t *)&t160));
            gm_l29 = t161;
            {
                gm_closure t163 = gm_l1;
                void * t164 = gm_l29;
                gm_closure t165 = t163;
                gm_node *ccx166 = t165.cx;
                void * t167 = t165.code;
                gm_b0_1 t168;
                if (GM_PTR_TAGGED(t167)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t167))(t164);
                } else {
                    t168 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t167)(t165.env, ccx166, NULL, t164);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix158;
ie158:;
ix158:;
        }
        {
            gm_closure t169 = gm_l1;
            void * t170 = GM_LD(void *, (((uint8_t *)&gm_l28) + 8));
            gm_closure t171 = t169;
            gm_node *ccx172 = t171.cx;
            void * t173 = t171.code;
            gm_b0_1 t174;
            if (GM_PTR_TAGGED(t173)) {
                ((void (*)(void *))GM_PTR_UNTAG(t173))(t170);
            } else {
                t174 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t173)(t171.env, ccx172, NULL, t170);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn154:;
ma11:;
        uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t176 != ((uint8_t)21ULL)) goto mn175;
        gm_tao63pk3r18v5 t177 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l30 = t177;
        {
            gm_closure t179 = gm_l1;
            void * t180 = GM_LD(void *, (((uint8_t *)&gm_l30) + 0));
            gm_closure t181 = t179;
            gm_node *ccx182 = t181.cx;
            void * t183 = t181.code;
            gm_b0_1 t184;
            if (GM_PTR_TAGGED(t183)) {
                ((void (*)(void *))GM_PTR_UNTAG(t183))(t180);
            } else {
                t184 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t183)(t181.env, ccx182, NULL, t180);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t186 = GM_LD(void *, (((uint8_t *)&gm_l30) + 8));
            void * t187 = t186;
            if (!(GM_LD(void *, ((uint8_t *)&t187)) != 0)) goto ie185;
            void * t188 = GM_LD(void *, ((uint8_t *)&t187));
            gm_l31 = t188;
            {
                gm_closure t190 = gm_l1;
                void * t191 = gm_l31;
                gm_closure t192 = t190;
                gm_node *ccx193 = t192.cx;
                void * t194 = t192.code;
                gm_b0_1 t195;
                if (GM_PTR_TAGGED(t194)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t194))(t191);
                } else {
                    t195 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t194)(t192.env, ccx193, NULL, t191);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix185;
ie185:;
ix185:;
        }
        {
            void * t197 = GM_LD(void *, (((uint8_t *)&gm_l30) + 16));
            void * t198 = t197;
            if (!(GM_LD(void *, ((uint8_t *)&t198)) != 0)) goto ie196;
            void * t199 = GM_LD(void *, ((uint8_t *)&t198));
            gm_l32 = t199;
            {
                gm_closure t201 = gm_l1;
                void * t202 = gm_l32;
                gm_closure t203 = t201;
                gm_node *ccx204 = t203.cx;
                void * t205 = t203.code;
                gm_b0_1 t206;
                if (GM_PTR_TAGGED(t205)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t205))(t202);
                } else {
                    t206 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t205)(t203.env, ccx204, NULL, t202);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix196;
ie196:;
ix196:;
        }
        goto me3;
mn175:;
ma12:;
        uint8_t t208 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t208 != ((uint8_t)19ULL)) goto mn207;
        void * t209 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l33 = t209;
        gm_b16_8 t210 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l34 = t210;
        {
            gm_closure t212 = gm_l1;
            void * t213 = gm_l33;
            gm_closure t214 = t212;
            gm_node *ccx215 = t214.cx;
            void * t216 = t214.code;
            gm_b0_1 t217;
            if (GM_PTR_TAGGED(t216)) {
                ((void (*)(void *))GM_PTR_UNTAG(t216))(t213);
            } else {
                t217 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t216)(t214.env, ccx215, NULL, t213);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                gm_l35 = t219;
            }
            {
                for (;;) {
                    uint64_t t221 = gm_l36;
                    uint64_t t222 = gm_l35;
                    uint8_t t223 = (t221 < t222);
                    if (!t223) goto lb220;
                    {
                        uint64_t t226 = gm_l36;
                        uint64_t t227 = t226;
                        uint64_t t230 = GM_LD(uint64_t, ((uint8_t *)&t227));
                        gm_l37 = t230;
                        {
                            uint64_t t232 = gm_l37;
                            uint64_t t233 = (uintptr_t)t232;
                            uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            if (t233 >= (uintptr_t)(t234)) gm_trap_index(t233, t234, gm_src8os28n25rsa4, 761, 46);
                            void * t235 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t233 * 144);
                            gm_closure t236 = gm_l1;
                            gm_f_arg_expr_2h4reqo(t235, t236);
                            if (gm_unw.kind) {
                                goto gm_ll224;
                            }
                        }
                        goto me228;
mn229:;
                        gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 761, 13);
me228:;
                    }
lc220:;
                    {
                        uint64_t t238 = gm_l36;
                        uint64_t r239; bool o240 = __builtin_add_overflow(t238, ((uint64_t)1ULL), &r239);
                        uint64_t t241 = r239;
                        gm_l36 = t241;
                    }
                }
gm_ll224:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb220; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc220; }
                }
                goto gm_lad;
lb220:;
            }
        }
        goto me3;
mn207:;
ma13:;
        uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t243 != ((uint8_t)49ULL)) goto mn242;
        gm_tet9guhev5r4u t244 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t2) + 8) + 8));
        F.l38 = t244;
        {
            if (!(((((uint8_t *)&F.l38) + 32))[16] != 0)) goto ie246;
            gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&F.l38) + 32));
            gm_l39 = t247;
            {
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                    gm_l40 = t250;
                }
                {
                    for (;;) {
                        uint64_t t252 = gm_l41;
                        uint64_t t253 = gm_l40;
                        uint8_t t254 = (t252 < t253);
                        if (!t254) goto lb251;
                        {
                            uint64_t t257 = gm_l41;
                            uint64_t t258 = t257;
                            uint64_t t261 = GM_LD(uint64_t, ((uint8_t *)&t258));
                            gm_l42 = t261;
                            {
                                uint64_t t263 = gm_l42;
                                uint64_t t264 = (uintptr_t)t263;
                                uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                                if (t264 >= (uintptr_t)(t265)) gm_trap_index(t264, t265, gm_src8os28n25rsa4, 765, 50);
                                void * t266 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t264 * 144);
                                gm_closure t267 = gm_l1;
                                gm_f_arg_expr_2h4reqo(t266, t267);
                                if (gm_unw.kind) {
                                    goto gm_ll255;
                                }
                            }
                            goto me259;
mn260:;
                            gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 765, 17);
me259:;
                        }
lc251:;
                        {
                            uint64_t t269 = gm_l41;
                            uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                            uint64_t t272 = r270;
                            gm_l41 = t272;
                        }
                    }
gm_ll255:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb251; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc251; }
                    }
                    goto gm_lad;
lb251:;
                }
            }
            goto ix246;
ie246:;
ix246:;
        }
        goto me3;
mn242:;
ma14:;
        uint8_t t274 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t274 != ((uint8_t)45ULL)) goto mn273;
        void * t275 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l43 = t275;
        void * t277 = gm_l43;
        void * t278 = t277;
        if (!(GM_LD(void *, ((uint8_t *)&t278)) != 0)) goto ie276;
        void * t279 = GM_LD(void *, ((uint8_t *)&t278));
        gm_l44 = t279;
        {
            gm_closure t281 = gm_l1;
            void * t282 = gm_l44;
            gm_closure t283 = t281;
            gm_node *ccx284 = t283.cx;
            void * t285 = t283.code;
            gm_b0_1 t286;
            if (GM_PTR_TAGGED(t285)) {
                ((void (*)(void *))GM_PTR_UNTAG(t285))(t282);
            } else {
                t286 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t285)(t283.env, ccx284, NULL, t282);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix276;
ie276:;
ix276:;
        goto me3;
mn273:;
ma15:;
        uint8_t t288 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t288 != ((uint8_t)46ULL)) goto mn287;
        gm_tchr52g0ema53 t289 = GM_LD(gm_tchr52g0ema53, ((((uint8_t *)t2) + 8) + 8));
        F.l45 = t289;
        void * t291 = GM_LD(void *, (((uint8_t *)&F.l45) + 40));
        void * t292 = t291;
        if (!(GM_LD(void *, ((uint8_t *)&t292)) != 0)) goto ie290;
        void * t293 = GM_LD(void *, ((uint8_t *)&t292));
        gm_l46 = t293;
        {
            gm_closure t295 = gm_l1;
            void * t296 = gm_l46;
            gm_closure t297 = t295;
            gm_node *ccx298 = t297.cx;
            void * t299 = t297.code;
            gm_b0_1 t300;
            if (GM_PTR_TAGGED(t299)) {
                ((void (*)(void *))GM_PTR_UNTAG(t299))(t296);
            } else {
                t300 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t299)(t297.env, ccx298, NULL, t296);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix290;
ie290:;
ix290:;
        goto me3;
mn287:;
ma16:;
        uint8_t t302 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t302 != ((uint8_t)48ULL)) goto mn301;
        gm_t3mf58gv9kf3n t303 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l47 = t303;
        {
            gm_closure t305 = gm_l1;
            void * t306 = GM_LD(void *, (((uint8_t *)&gm_l47) + 0));
            gm_closure t307 = t305;
            gm_node *ccx308 = t307.cx;
            void * t309 = t307.code;
            gm_b0_1 t310;
            if (GM_PTR_TAGGED(t309)) {
                ((void (*)(void *))GM_PTR_UNTAG(t309))(t306);
            } else {
                t310 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t309)(t307.env, ccx308, NULL, t306);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t312 = GM_LD(void *, (((uint8_t *)&gm_l47) + 8));
            void * t313 = t312;
            if (!(GM_LD(void *, ((uint8_t *)&t313)) != 0)) goto ie311;
            void * t314 = GM_LD(void *, ((uint8_t *)&t313));
            gm_l48 = t314;
            {
                gm_closure t316 = gm_l1;
                void * t317 = gm_l48;
                gm_closure t318 = t316;
                gm_node *ccx319 = t318.cx;
                void * t320 = t318.code;
                gm_b0_1 t321;
                if (GM_PTR_TAGGED(t320)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t320))(t317);
                } else {
                    t321 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t320)(t318.env, ccx319, NULL, t317);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix311;
ie311:;
ix311:;
        }
        goto me3;
mn301:;
ma17:;
        goto me3;
mn322:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 740, 5);
me3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_spread_bndtpd6 F;
    gm_b16_8 gm_l0; /* elems */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l1 = t3;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tv1o1j44ii4db t15 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 136));
                    F.l3 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 128));
                        if (!t18) goto ie17;
                        {
                            uint8_t t20 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src8os28n25rsa4, 411, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l2 = t25;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t26 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
