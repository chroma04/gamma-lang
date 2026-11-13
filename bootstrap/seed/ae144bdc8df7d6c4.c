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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3q46euu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n6686bi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
extern uint8_t gm_c0nre4icjau[1] __attribute__((aligned(1)));
extern uint8_t gm_c0ocv2md26k[9] __attribute__((aligned(1)));
extern uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
extern uint8_t gm_c1nolilrvln[8] __attribute__((aligned(1)));
extern uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
extern uint8_t gm_c30ot371k6d[9] __attribute__((aligned(1)));
extern uint8_t gm_c3k4pnhd8av[14] __attribute__((aligned(1)));
extern uint8_t gm_c3oglifus73[18] __attribute__((aligned(1)));
extern uint8_t gm_c433keo1ggm[5] __attribute__((aligned(1)));
extern uint8_t gm_c51kem98eiq[6] __attribute__((aligned(1)));
extern uint8_t gm_c5h64s41na8[15] __attribute__((aligned(1)));
extern uint8_t gm_c64sa63uljg[1] __attribute__((aligned(1)));
extern uint8_t gm_c66tv9kq8na[208] __attribute__((aligned(8)));
extern uint8_t gm_c6fqpnlmjmc[1] __attribute__((aligned(8)));
extern uint8_t gm_c6v8oe6vn3d[9] __attribute__((aligned(1)));
extern uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
extern uint8_t gm_c8rj3oaeld7[14] __attribute__((aligned(1)));
extern uint8_t gm_c9ik8ouhqm8[16] __attribute__((aligned(1)));
extern uint8_t gm_c9paf88rqhe[8] __attribute__((aligned(8)));
extern uint8_t gm_cae9ud3ihhl[15] __attribute__((aligned(1)));
extern uint8_t gm_cbemal9c6bf[8] __attribute__((aligned(8)));
extern uint8_t gm_cbg3jjml4ui[5] __attribute__((aligned(1)));
extern uint8_t gm_cbl5e0crtor[16] __attribute__((aligned(1)));
extern uint8_t gm_cc5s1q78gi3[9] __attribute__((aligned(1)));
extern uint8_t gm_ccdcg9bumn7[5] __attribute__((aligned(1)));
extern uint8_t gm_ccgde627fhi[11] __attribute__((aligned(1)));
extern uint8_t gm_cdeh13viquh[4] __attribute__((aligned(1)));
extern uint8_t gm_ce9kahcvfp7[1] __attribute__((aligned(8)));
extern uint8_t gm_cea991q0g8k[13] __attribute__((aligned(1)));
extern uint8_t gm_cega6s9dl3o[4] __attribute__((aligned(4)));
extern uint8_t gm_cel6p901dc1[1] __attribute__((aligned(1)));
extern uint8_t gm_cf4m0q5ms2i[20] __attribute__((aligned(1)));
extern uint8_t gm_cg36ndjqc5r[14] __attribute__((aligned(1)));
extern uint8_t gm_cgomkqt0377[65] __attribute__((aligned(1)));
extern uint8_t gm_cgsmfcq937e_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cgt3j8alm30[10] __attribute__((aligned(1)));
extern uint8_t gm_ch21cqrvvl4[8] __attribute__((aligned(1)));
extern uint8_t gm_chkfraubo1r[4] __attribute__((aligned(4)));
extern uint8_t gm_cj5e0v8gk2i[20] __attribute__((aligned(1)));
extern uint8_t gm_cjidvhvshci[19] __attribute__((aligned(1)));
extern uint8_t gm_cjvkergb5k5[10] __attribute__((aligned(1)));
extern uint8_t gm_ck1b0l8lsvc[39] __attribute__((aligned(1)));
extern uint8_t gm_ckgo58jroh8[8] __attribute__((aligned(1)));
extern uint8_t gm_cl2be5u9pqk[16] __attribute__((aligned(8)));
extern uint8_t gm_clcchfce70r[32] __attribute__((aligned(1)));
extern uint8_t gm_clkdg6jbli4[16] __attribute__((aligned(8)));
extern uint8_t gm_cll96946g4q[9] __attribute__((aligned(1)));
extern uint8_t gm_cm7k8v105sj[1] __attribute__((aligned(4)));
extern uint8_t gm_cmd1mqfuq2m[8] __attribute__((aligned(1)));
extern uint8_t gm_cmr3530gn23[13] __attribute__((aligned(1)));
extern uint8_t gm_cn8o84dqbf8[12] __attribute__((aligned(1)));
extern uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
extern uint8_t gm_cnbt0kfagfe[2] __attribute__((aligned(1)));
extern uint8_t gm_cnkg4pj33ln[1] __attribute__((aligned(1)));
extern uint8_t gm_coeovpdckud[2] __attribute__((aligned(1)));
extern uint8_t gm_cono7645157[11] __attribute__((aligned(1)));
extern uint8_t gm_cp2r20aoong[17] __attribute__((aligned(1)));
extern uint8_t gm_cphj36bitbq[688] __attribute__((aligned(8)));
extern uint8_t gm_cqq41hlun1j[5] __attribute__((aligned(1)));
extern uint8_t gm_crmf74ucsme[15] __attribute__((aligned(1)));
extern uint8_t gm_crr7q8eonfb[11] __attribute__((aligned(1)));
extern uint8_t gm_csmeedg7pnp[80] __attribute__((aligned(1)));
extern uint8_t gm_ct1fm60hrci[6] __attribute__((aligned(1)));
extern uint8_t gm_ct5b5llk605[13] __attribute__((aligned(1)));
extern uint8_t gm_ctdm11uuaj6[16] __attribute__((aligned(8)));
extern uint8_t gm_ctpnchchr9h[8] __attribute__((aligned(1)));
extern uint8_t gm_cu2ve789ji2[96] __attribute__((aligned(8)));
extern uint8_t gm_cuju2tms0ds[12] __attribute__((aligned(1)));
extern void *const gm_hl7iqllvekj4[];
extern void *const gm_hlib8i5oaffv[];
extern void *const gm_hlifd4tg9fgk[];
extern void *const gm_hlucu919n5n4[];
extern void *const gm_hlvop6ag413a[];
extern uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
extern uint8_t gm_k2sohj2k4ib[40] __attribute__((aligned(8)));
extern uint8_t gm_k6oa34t4v6n[40] __attribute__((aligned(8)));
extern uint8_t gm_k8qpjmtnqct[40] __attribute__((aligned(8)));
extern uint8_t gm_k9pi0h2o5i2[40] __attribute__((aligned(8)));
extern uint8_t gm_khdoei7bkbm[40] __attribute__((aligned(8)));
extern uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
extern uint8_t gm_kkh67d5mpb1[40] __attribute__((aligned(8)));
extern uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
extern uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
extern uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
extern uint8_t gm_kovthkfk7ta[40] __attribute__((aligned(8)));
extern uint8_t gm_kplh4t9vgh7[40] __attribute__((aligned(8)));
extern uint8_t gm_krkn5fd15l7[40] __attribute__((aligned(8)));
extern uint8_t gm_ksjvoab7999[40] __attribute__((aligned(8)));
extern uint8_t gm_ktv0eonmebe[40] __attribute__((aligned(8)));
extern const uint8_t gm_s000up2l16pib[9];
extern const uint8_t gm_s01ekgjjrfehv[13];
extern const uint8_t gm_s01k0p56k10gk[29];
extern const uint8_t gm_s02aodrl2fpra[12];
extern const uint8_t gm_s0adus7j7p0g0[41];
extern const uint8_t gm_s0b795193rvs2[7];
extern const uint8_t gm_s0cf8090s33vc[30];
extern const uint8_t gm_s0dhnude2n5fe[8];
extern const uint8_t gm_s0eudsv4chfi5[44];
extern const uint8_t gm_s0hcm1974rbe7[34];
extern const uint8_t gm_s0jbci4ff2esa[23];
extern const uint8_t gm_s0kf3vtbitlnb[58];
extern const uint8_t gm_s0pi5sf493jqq[41];
extern const uint8_t gm_s0s3f0v5p7674[27];
extern const uint8_t gm_s0t5omnh472sf[21];
extern const uint8_t gm_s0uoh0jkm28kv[24];
extern const uint8_t gm_s13jru0todls1[38];
extern const uint8_t gm_s19069udlvhni[9];
extern const uint8_t gm_s199bfpbr16jo[44];
extern const uint8_t gm_s1bp6pdherlbe[41];
extern const uint8_t gm_s1eo6mkdbgh2b[38];
extern const uint8_t gm_s1jkh3mpunvd6[64];
extern const uint8_t gm_s1lh34nabv9g2[5];
extern const uint8_t gm_s1m38bo3naic1[8];
extern const uint8_t gm_s1o778dp7d59d[43];
extern const uint8_t gm_s1p0thm43h6d6[21];
extern const uint8_t gm_s1smed2pig5ts[35];
extern const uint8_t gm_s25qdppmkh7s6[7];
extern const uint8_t gm_s268n7l3tugli[6];
extern const uint8_t gm_s283bs8hsleb3[9];
extern const uint8_t gm_s29avse796bo5[18];
extern const uint8_t gm_s2asttl6mu7q3[41];
extern const uint8_t gm_s2dog0ijtvmd2[32];
extern const uint8_t gm_s2ef11ime8o01[42];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s2ogeh8fp9da3[12];
extern const uint8_t gm_s2rk6mdib4mtv[17];
extern const uint8_t gm_s2sfjihvaabi3[9];
extern const uint8_t gm_s2u7k18iev3v2[4];
extern const uint8_t gm_s32blj9c3tc60[11];
extern const uint8_t gm_s32rhj249n222[10];
extern const uint8_t gm_s38cjdjl7n99k[10];
extern const uint8_t gm_s39jha51ulivf[27];
extern const uint8_t gm_s3hb2q98sfsao[5];
extern const uint8_t gm_s3ierue9bs27g[46];
extern const uint8_t gm_s3jflb7gdk32k[32];
extern const uint8_t gm_s3p26aim75mms[41];
extern const uint8_t gm_s3qu37je1jc8f[46];
extern const uint8_t gm_s3ss0hcg1h7js[7];
extern const uint8_t gm_s3t2d4i4t34ct[74];
extern const uint8_t gm_s3tr540lff6s6[11];
extern const uint8_t gm_s3udvgirbbq3p[41];
extern const uint8_t gm_s418vrbemh247[49];
extern const uint8_t gm_s41uc8v0dkl4a[15];
extern const uint8_t gm_s463po27vtmnn[15];
extern const uint8_t gm_s46ki8g7j9f6v[6];
extern const uint8_t gm_s48hq6ubqo1rd[10];
extern const uint8_t gm_s48vlq6lddb8m[29];
extern const uint8_t gm_s4fhghepo9qb6[13];
extern const uint8_t gm_s4g35ls4ma80g[13];
extern const uint8_t gm_s4hf5j4eu72mv[66];
extern const uint8_t gm_s4lmdtjb9rnah[70];
extern const uint8_t gm_s4m4ttv1faeb1[175];
extern const uint8_t gm_s4nikb38gk9t0[22];
extern const uint8_t gm_s4ok0uoiq1nu9[97];
extern const uint8_t gm_s4r4tmb9bg65q[41];
extern const uint8_t gm_s527dqlmmaier[6];
extern const uint8_t gm_s55ccd7do7rbs[11];
extern const uint8_t gm_s592b618tctb1[15];
extern const uint8_t gm_s5cbc2v9e254r[37];
extern const uint8_t gm_s5cfou479ljhn[29];
extern const uint8_t gm_s5f9u0o3duhns[13];
extern const uint8_t gm_s5gkjub7hejc1[9];
extern const uint8_t gm_s5r23iu6ncd7o[41];
extern const uint8_t gm_s5rqsv8c71bds[11];
extern const uint8_t gm_s5vakuk6ui4kj[9];
extern const uint8_t gm_s5vb1b1mnli2b[72];
extern const uint8_t gm_s5ve9r6iab8ga[26];
extern const uint8_t gm_s5veo86cvpdae[86];
extern const uint8_t gm_s61i2o7ldibjh[35];
extern const uint8_t gm_s63e7t279sbjq[47];
extern const uint8_t gm_s64skgbd4ii1k[10];
extern const uint8_t gm_s6cmbp3c9g09j[61];
extern const uint8_t gm_s6eefft9fser8[9];
extern const uint8_t gm_s6g2mpli2mu72[11];
extern const uint8_t gm_s6i74fbmndo4a[15];
extern const uint8_t gm_s6mmuahmmssv6[17];
extern const uint8_t gm_s6pgabt9io52p[132];
extern const uint8_t gm_s6qf5dtv8rcdo[13];
extern const uint8_t gm_s6rj805fm39qm[65];
extern const uint8_t gm_s70iivik802q4[26];
extern const uint8_t gm_s70j71mp70pdq[14];
extern const uint8_t gm_s72gibfnd8lcj[8];
extern const uint8_t gm_s744lbotjcl51[27];
extern const uint8_t gm_s76ekntop342o[8];
extern const uint8_t gm_s783a88c5t0ve[67];
extern const uint8_t gm_s7an5gtrdgen2[14];
extern const uint8_t gm_s7b8bvi5kqaqt[7];
extern const uint8_t gm_s7bq3dilmfq6u[47];
extern const uint8_t gm_s7e422oeo6jkt[8];
extern const uint8_t gm_s7ed59qo09cfg[41];
extern const uint8_t gm_s7fdjhp9ga5oo[8];
extern const uint8_t gm_s7m3rbk4l87ah[15];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_s7qcnd652ae6d[25];
extern const uint8_t gm_s816n04ro16nc[48];
extern const uint8_t gm_s81csu1l86dmv[52];
extern const uint8_t gm_s86kdga2ul6h4[9];
extern const uint8_t gm_s87p55h1isc6m[6];
extern const uint8_t gm_s89gtqa9dnss0[35];
extern const uint8_t gm_s8d2alh114kri[15];
extern const uint8_t gm_s8g92mbmiiva2[48];
extern const uint8_t gm_s8jclq8b6mi27[17];
extern const uint8_t gm_s8l22p9l388io[67];
extern const uint8_t gm_s8l7qb8s3942u[35];
extern const uint8_t gm_s8m12els4res5[26];
extern const uint8_t gm_s8o6lm1cn4eo5[3];
extern const uint8_t gm_s8ps5ka0vnf6d[42];
extern const uint8_t gm_s8qkarceaerkr[22];
extern const uint8_t gm_s8rs2s65rnmku[18];
extern const uint8_t gm_s8u62e7lr2m4s[13];
extern const uint8_t gm_s903nhsp8hiug[14];
extern const uint8_t gm_s92n4v7htp6pi[11];
extern const uint8_t gm_s96j6o9smoqdu[8];
extern const uint8_t gm_s9dh9d8gs6a16[9];
extern const uint8_t gm_s9dudft75oi1c[33];
extern const uint8_t gm_s9eqqrnq18n3l[31];
extern const uint8_t gm_s9gk1b0ve64mt[10];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9i2ld3a42lap[8];
extern const uint8_t gm_s9impgj2b5t1c[50];
extern const uint8_t gm_s9k65bfq9n6l0[14];
extern const uint8_t gm_s9kou5ia9uri9[6];
extern const uint8_t gm_s9m6kd6gec368[14];
extern const uint8_t gm_s9p37ubghllhn[64];
extern const uint8_t gm_s9qjpsba9cv0i[10];
extern const uint8_t gm_s9rk6ipadg2pq[111];
extern const uint8_t gm_sa3k87epai61e[25];
extern const uint8_t gm_sa5m6gegug17p[40];
extern const uint8_t gm_sa5s0jools6a2[9];
extern const uint8_t gm_sa7q2bqmn8lgb[9];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sadg245agtuao[45];
extern const uint8_t gm_sadj3uigeodiu[29];
extern const uint8_t gm_sagd3osht35og[13];
extern const uint8_t gm_sahd19pngn4vn[14];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_sak3h3rlsvn4o[10];
extern const uint8_t gm_salpeh0v5rqdf[8];
extern const uint8_t gm_saovklm35s57t[184];
extern const uint8_t gm_saq8ri2pmi7o5[19];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sat505mlqphna[26];
extern const uint8_t gm_sauu2s1pbjqsh[11];
extern const uint8_t gm_sb21gep34rmm9[36];
extern const uint8_t gm_sb3sf0ef436fi[40];
extern const uint8_t gm_sb5b70djeqhg8[16];
extern const uint8_t gm_sb5h01ig0b9oe[34];
extern const uint8_t gm_sb641udihu5hs[10];
extern const uint8_t gm_sb8b1gf0gtt49[43];
extern const uint8_t gm_sb8qu1qqed6pt[85];
extern const uint8_t gm_sbbgtkch3q4n5[3];
extern const uint8_t gm_sbht6tj2u5vfl[8];
extern const uint8_t gm_sbjshone12eeu[158];
extern const uint8_t gm_sbjuojbqo51av[36];
extern const uint8_t gm_sbkd9ldfr86ri[5];
extern const uint8_t gm_sbl7p8tv4j986[17];
extern const uint8_t gm_sblomia8fb3mu[20];
extern const uint8_t gm_sbm19bhbgtvrj[129];
extern const uint8_t gm_sbmtphtelgust[29];
extern const uint8_t gm_sbo8mn3eri83c[23];
extern const uint8_t gm_sbqoiitqtfv75[11];
extern const uint8_t gm_sbssn254n4fs2[25];
extern const uint8_t gm_sbtq9nj91tefv[10];
extern const uint8_t gm_sbvi33ihvufj1[20];
extern const uint8_t gm_sc1nadatuanhc[5];
extern const uint8_t gm_sc46nvblu52f3[18];
extern const uint8_t gm_sc559v8etqgda[19];
extern const uint8_t gm_sc6pb1tb5tnnv[34];
extern const uint8_t gm_scai7a7ojvpjh[19];
extern const uint8_t gm_scc5n7285iivu[13];
extern const uint8_t gm_scd1bb5dijnis[15];
extern const uint8_t gm_scgm14ju6d0p8[15];
extern const uint8_t gm_scgvr0isof71e[9];
extern const uint8_t gm_sckmhri27q39l[53];
extern const uint8_t gm_scllp0lpbljde[15];
extern const uint8_t gm_scobqq9qv6nnk[6];
extern const uint8_t gm_scqqkvgm1j3cs[65];
extern const uint8_t gm_scrnhm4ha22vj[18];
extern const uint8_t gm_scs3asa1395s7[6];
extern const uint8_t gm_scvsibgrrt3sc[13];
extern const uint8_t gm_sd2a8ckkfb013[26];
extern const uint8_t gm_sd2iiq8cuaks3[5];
extern const uint8_t gm_sd5giovmg2s7p[17];
extern const uint8_t gm_sdak0upmjiivc[15];
extern const uint8_t gm_sdak257j1qgl8[6];
extern const uint8_t gm_sdbgs2fic3oib[11];
extern const uint8_t gm_sdbtbup89cn84[19];
extern const uint8_t gm_sdcf6v5v0oma5[6];
extern const uint8_t gm_sddniufplfkku[66];
extern const uint8_t gm_sdl70rjr9en4m[16];
extern const uint8_t gm_sdm98dp52j8ep[16];
extern const uint8_t gm_sdmti3s9j8p44[11];
extern const uint8_t gm_sdnh9sm5pppq1[40];
extern const uint8_t gm_sdprvvqv16trj[61];
extern const uint8_t gm_sdubr1na84odv[12];
extern const uint8_t gm_se09jntm6upln[4];
extern const uint8_t gm_se0oi28sapu01[11];
extern const uint8_t gm_se2efihu2rvsa[27];
extern const uint8_t gm_se5r6p8tqdo82[31];
extern const uint8_t gm_seajfhgtfmhv7[12];
extern const uint8_t gm_seak1jbr9qmd1[9];
extern const uint8_t gm_sebk4v8jac58q[43];
extern const uint8_t gm_seflbetisd1ui[3];
extern const uint8_t gm_segm7or15hs5h[20];
extern const uint8_t gm_sei6ib477d1ds[4];
extern const uint8_t gm_sennl2fjkcbdb[4];
extern const uint8_t gm_senuk3dngdds0[17];
extern const uint8_t gm_seqap6hh2h8p5[5];
extern const uint8_t gm_serasf77ce9g3[52];
extern const uint8_t gm_serke084a3lbj[41];
extern const uint8_t gm_sf51sbqtper4f[74];
extern const uint8_t gm_sfil4j1onqa90[14];
extern const uint8_t gm_sfinup9etdjip[4];
extern const uint8_t gm_sfiqishktrbg2[27];
extern const uint8_t gm_sfjrpo4i6b0at[24];
extern const uint8_t gm_sfnq22hc2kms9[14];
extern const uint8_t gm_sfpnied4dp46h[15];
extern const uint8_t gm_sfs5rrntfu3mi[25];
extern const uint8_t gm_sft51gvrcleut[16];
extern const uint8_t gm_sfu0in59pituf[62];
extern const uint8_t gm_sfurr787jvkcg[6];
extern const uint8_t gm_sg0tidc1tpl0e[33];
extern const uint8_t gm_sg8a1r64kvpgo[3];
extern const uint8_t gm_sgd9tn7p5ihgk[10];
extern const uint8_t gm_sge2cgjaklnf2[5];
extern const uint8_t gm_sge6a56sqn6kj[21];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sggm36eg9tkga[10];
extern const uint8_t gm_sgjkgsqdq34nu[37];
extern const uint8_t gm_sgli4lbivdt1d[38];
extern const uint8_t gm_sgls4l5hb5j8p[75];
extern const uint8_t gm_sgnpj6tqkbeom[12];
extern const uint8_t gm_sgo00f0mafbnd[4];
extern const uint8_t gm_sgovtffcl93pk[15];
extern const uint8_t gm_sgq1v4nk059uv[10];
extern const uint8_t gm_sgq6hvrcda4sp[37];
extern const uint8_t gm_sgs7af5k634cn[11];
extern const uint8_t gm_sgu3j1te831tl[24];
extern const uint8_t gm_sguhoqhco0rfk[9];
extern const uint8_t gm_sh1emun6fmppt[4];
extern const uint8_t gm_sh1scn6dg8mpr[14];
extern const uint8_t gm_sh75rh8dk51gf[12];
extern const uint8_t gm_sh8houjk34evm[15];
extern const uint8_t gm_sh8lvmk1ev2kd[16];
extern const uint8_t gm_shdnhcpvptc1q[5];
extern const uint8_t gm_shf8e0v8f47ml[17];
extern const uint8_t gm_shg1pn1r3sjgi[13];
extern const uint8_t gm_shgjlb2nrn5fk[22];
extern const uint8_t gm_shhrbleiv8o3j[14];
extern const uint8_t gm_shjpff93a0f25[7];
extern const uint8_t gm_shmgu63rvoce3[36];
extern const uint8_t gm_shq9t6lutffbf[4];
extern const uint8_t gm_shtrdthev5fjn[14];
extern const uint8_t gm_shttb5kqse295[12];
extern const uint8_t gm_shu6vhibjovbk[18];
extern const uint8_t gm_siaeuo6l48ehh[15];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sic6g350stk7h[21];
extern const uint8_t gm_sifqqc9n3eem2[6];
extern const uint8_t gm_sihlhf2982kip[12];
extern const uint8_t gm_sii9tamh3vah6[32];
extern const uint8_t gm_sil41urphp1ol[12];
extern const uint8_t gm_sipi0b0c6ms62[6];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sisc4n92pk600[13];
extern const uint8_t gm_sisd1n433forg[52];
extern const uint8_t gm_sismsc98l6u4e[4];
extern const uint8_t gm_sisn94tbdoeof[65];
extern const uint8_t gm_sium1s93t7o1a[19];
extern const uint8_t gm_sivsapeh482sm[8];
extern const uint8_t gm_sj0ur1o7a5584[16];
extern const uint8_t gm_sj3lfuqd4f1np[37];
extern const uint8_t gm_sj718thiqudru[10];
extern const uint8_t gm_sj97id02v9fh3[6];
extern const uint8_t gm_sjcdr1cibbeq6[64];
extern const uint8_t gm_sjcocjnsa7u7g[85];
extern const uint8_t gm_sjf1r2hmheh87[29];
extern const uint8_t gm_sjkbtepn8bqeb[24];
extern const uint8_t gm_sjmhvdjct2lq7[19];
extern const uint8_t gm_sjpml17j24nb4[64];
extern const uint8_t gm_sjrtlfuv3os00[75];
extern const uint8_t gm_sjt1hcgrgj8be[36];
extern const uint8_t gm_sk36cj5av4kuu[17];
extern const uint8_t gm_sk83con0mdsfe[6];
extern const uint8_t gm_sk9at358cmauo[27];
extern const uint8_t gm_ska00mcp5pgpq[53];
extern const uint8_t gm_skfo9c0nr3nrd[50];
extern const uint8_t gm_skgulapvvrhkp[36];
extern const uint8_t gm_skjfb4faajq73[13];
extern const uint8_t gm_skkog7g7r3edb[8];
extern const uint8_t gm_skl1tisbdbp2m[13];
extern const uint8_t gm_skme3gpq5egsp[40];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_skrdd0u45d86g[17];
extern const uint8_t gm_sksgae11pvk6p[2];
extern const uint8_t gm_skt7g4v99o4j0[42];
extern const uint8_t gm_skusvrbuvvp37[13];
extern const uint8_t gm_sl1b73huqrikt[41];
extern const uint8_t gm_sl4daef1gs04o[37];
extern const uint8_t gm_sl80cge5ts0aj[14];
extern const uint8_t gm_sl815v6oegv7n[14];
extern const uint8_t gm_slc3rjglch387[50];
extern const uint8_t gm_sle9lbvtgi3v6[41];
extern const uint8_t gm_slfddg7c1u1oc[62];
extern const uint8_t gm_slfinpjh0406b[47];
extern const uint8_t gm_slfk2il1skugc[13];
extern const uint8_t gm_slgu49nv3o8e7[12];
extern const uint8_t gm_slmnp81e6ev88[10];
extern const uint8_t gm_sln5qir3tgi7j[10];
extern const uint8_t gm_slpi6dabivg2e[29];
extern const uint8_t gm_sm0f8bs4gd2ma[18];
extern const uint8_t gm_sm34bm8ke9n7b[22];
extern const uint8_t gm_sm5ffrmin8340[10];
extern const uint8_t gm_sm6gmoq645v8o[70];
extern const uint8_t gm_sm732n1u78vvc[16];
extern const uint8_t gm_sm922ui8k0oca[9];
extern const uint8_t gm_smbfsgie9fgg4[4];
extern const uint8_t gm_smf8ll9cm8fgf[26];
extern const uint8_t gm_smhve5pq35drr[9];
extern const uint8_t gm_smlcnjfj6t3mc[6];
extern const uint8_t gm_smnkiec8dekcp[8];
extern const uint8_t gm_smoi1nh7b1b6o[65];
extern const uint8_t gm_smp6ue9h98hbc[31];
extern const uint8_t gm_smsmo0f5ao530[10];
extern const uint8_t gm_sn2e6t65fstoj[19];
extern const uint8_t gm_sn6vmked7l7pf[7];
extern const uint8_t gm_sn8d077r4fgpq[26];
extern const uint8_t gm_snaq7hvenqsum[4];
extern const uint8_t gm_snfsrvfr6vnkt[8];
extern const uint8_t gm_sngckqdbegtlc[26];
extern const uint8_t gm_snhkhrbd8tjtf[45];
extern const uint8_t gm_snku1611u1u7p[9];
extern const uint8_t gm_snm19vfbec4q3[19];
extern const uint8_t gm_snmrruc88a739[10];
extern const uint8_t gm_snritti8butc6[41];
extern const uint8_t gm_snrk30ug2qmu2[8];
extern const uint8_t gm_snsqui8o89u28[15];
extern const uint8_t gm_snstimc3n04u0[12];
extern const uint8_t gm_snuadcbi5e786[15];
extern const uint8_t gm_snvodad5t43r7[11];
extern const uint8_t gm_so1m4qmhi2fvm[93];
extern const uint8_t gm_so3bl4q7kp6gl[21];
extern const uint8_t gm_so94vinsu4uib[50];
extern const uint8_t gm_sob1n7uh7kpps[14];
extern const uint8_t gm_sodg3e7aummvt[8];
extern const uint8_t gm_soeo46rfuspd5[32];
extern const uint8_t gm_soi6iv1hnej40[13];
extern const uint8_t gm_solguqm0afvc4[44];
extern const uint8_t gm_sor8flejv9lfq[67];
extern const uint8_t gm_sorb6gi9d49ic[9];
extern const uint8_t gm_souep3pirutur[55];
extern const uint8_t gm_sp2a19d61vbe0[4];
extern const uint8_t gm_sp4skfj2bn2ut[7];
extern const uint8_t gm_sp4uhnkunj2cg[3];
extern const uint8_t gm_sp7vnnm6ibcad[41];
extern const uint8_t gm_spbv5h8c0ukao[5];
extern const uint8_t gm_spfleivdeduud[4];
extern const uint8_t gm_sppcsr92ukfol[64];
extern const uint8_t gm_sppeomacjvkpm[7];
extern const uint8_t gm_spqlqjii789ln[57];
extern const uint8_t gm_spsbmlrti644m[11];
extern const uint8_t gm_sq05ai6ts8qlk[17];
extern const uint8_t gm_sq1mn6f7h111r[3];
extern const uint8_t gm_sq3r7vbcbs4ll[26];
extern const uint8_t gm_sq4jcg2uujqmj[16];
extern const uint8_t gm_sq7c24l9r5rti[118];
extern const uint8_t gm_sq89uknu8mulu[8];
extern const uint8_t gm_sq95m8isfa7vd[2];
extern const uint8_t gm_sq9org4qoda29[12];
extern const uint8_t gm_sqfu5kl4vat80[9];
extern const uint8_t gm_sqin9fcebg38h[7];
extern const uint8_t gm_sqkcsn1qihjbl[41];
extern const uint8_t gm_sqsb0jek355o9[17];
extern const uint8_t gm_sqssndasqkc4l[17];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sqviis18fpvj7[5];
extern const uint8_t gm_sr6b2mblh2jam[223];
extern const uint8_t gm_sr8f1b5ta7294[9];
extern const char gm_src0s0aq2lhn254[];
extern const char gm_src2n2ugogq2cq2[];
extern const char gm_src361hl447ggcv[];
extern const char gm_src3a4t4bi1ba85[];
extern const char gm_src41vqrhgekac9[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srci09ctgf0djoe[];
extern const char gm_srciagdq75um1d5[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srcn6q5fuum979p[];
extern const char gm_srco9vbah234t6e[];
extern const char gm_srcqp133sovb148[];
extern const char gm_srcrorgjlg51brd[];
extern const char gm_srcush70170kjg3[];
extern const uint8_t gm_srd5daqbo6ilc[58];
extern const uint8_t gm_srehvgd1sod2m[18];
extern const uint8_t gm_srfobl8913gvl[3];
extern const uint8_t gm_sri78umnjbra8[11];
extern const uint8_t gm_srjb0lv60gms5[62];
extern const uint8_t gm_srltdqer7h3t7[55];
extern const uint8_t gm_srok3f5iht8q8[23];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_sruk98mc5gf6c[44];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_ss3kdmf5g7qmv[82];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_ss9d6rvgfqq53[13];
extern const uint8_t gm_ssb6m4k22290v[17];
extern const uint8_t gm_sscmtmpbttmhb[93];
extern const uint8_t gm_ssffver18si54[25];
extern const uint8_t gm_ssift49n290vj[5];
extern const uint8_t gm_ssjk8k45afgjb[11];
extern const uint8_t gm_ssnbkpbck3vbo[22];
extern const uint8_t gm_ssp208k4afn15[23];
extern const uint8_t gm_st35tcurhvabs[31];
extern const uint8_t gm_st4gqcsm35nph[90];
extern const uint8_t gm_st9djk4oc14um[67];
extern const uint8_t gm_stbno5snno6c5[8];
extern const uint8_t gm_stbrououb87po[51];
extern const uint8_t gm_stfd80s46313k[38];
extern const uint8_t gm_stgra1esdnhlk[12];
extern const uint8_t gm_stjq3hbrk4g4r[4];
extern const uint8_t gm_stkm2m8f3okk8[18];
extern const uint8_t gm_stna0ubmss8p6[48];
extern const uint8_t gm_sto79bcv9e91i[4];
extern const uint8_t gm_stocunr1upos5[9];
extern const uint8_t gm_sts2e9lggtpr2[47];
extern const uint8_t gm_stt50isac07pf[46];
extern const uint8_t gm_su0gvqh5vvvlj[37];
extern const uint8_t gm_su1atn16ojnm6[9];
extern const uint8_t gm_su4j8ek23cevi[51];
extern const uint8_t gm_su6ipjjrnjs7p[10];
extern const uint8_t gm_suafpipqeom8q[7];
extern const uint8_t gm_suc2pvoh9ktdi[6];
extern const uint8_t gm_sudpbkssneeek[11];
extern const uint8_t gm_suevj69t71tjc[35];
extern const uint8_t gm_sufoqrq4onvgh[16];
extern const uint8_t gm_suhl0206dfbdm[5];
extern const uint8_t gm_suj1i29321vmu[10];
extern const uint8_t gm_suod5ocif7o5v[5];
extern const uint8_t gm_suqchf5vh2hqp[5];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_svbrm62b64ga9[11];
extern const uint8_t gm_svgaurqm99100[48];
extern const uint8_t gm_sviu03g0f5s30[43];
extern const uint8_t gm_svldd4tfe8pf3[41];
extern const uint8_t gm_svshci6b9lbnq[9];
extern const uint8_t gm_svth7kg659g2i[13];
extern const uint8_t gm_svtkcim3vundp[18];
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_tyehhdtpeti1;
extern const gm_variant gm_tyehhdtpeti1_v[];
extern const gm_type gm_tyg63o53ar52;
extern const gm_variant gm_tyg63o53ar52_v[];
extern const gm_type gm_tyqqajgod2bb;
const char gm_src0s0aq2lhn254[] = "compiler/load/core.gm";
const char gm_src2n2ugogq2cq2[] = "compiler/repl/report.gm";
const char gm_src361hl447ggcv[] = "compiler/fix/fix.gm";
const char gm_src3a4t4bi1ba85[] = "compiler/check/index.gm";
const char gm_src41vqrhgekac9[] = "compiler/syntax/token.gm";
const char gm_srcgfglnv8p059r[] = "compiler/diag/diag.gm";
const char gm_srcht195tfrfuqo[] = "compiler/cgen/names.gm";
const char gm_srci09ctgf0djoe[] = "compiler/check/mathvec.gm";
const char gm_srciagdq75um1d5[] = "compiler/doc/collect.gm";
const char gm_srcl43af23eher3[] = "compiler/check/fnlit.gm";
const char gm_srcn6q5fuum979p[] = "compiler/syntax/dump.gm";
const char gm_srco9vbah234t6e[] = "compiler/lint/checked.gm";
const char gm_srcqp133sovb148[] = "compiler/repl/session.gm";
const char gm_srcrorgjlg51brd[] = "compiler/check/generic.gm";
const char gm_srcush70170kjg3[] = "compiler/sema/dump.gm";
const gm_type gm_ty467d5cm1d5 = {.size = 4, .align = 4, .kind = GM_K_INT, .bits = 32, .is_signed = false};
const gm_type gm_ty467d5cm1d5;
const gm_type gm_tyehhdtpeti1 = {.size = 1, .align = 1, .name = "Flavor", .kind = GM_K_UNION, .nvariants = 4, .variants = gm_tyehhdtpeti1_v, .tag_offset = 0, .payload_offset = 0, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tyehhdtpeti1;
const gm_type gm_tyg63o53ar52 = {.size = 12, .align = 4, .name = "Builtin", .kind = GM_K_UNION, .nvariants = 82, .variants = gm_tyg63o53ar52_v, .tag_offset = 0, .payload_offset = 4, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tyg63o53ar52;
const gm_type gm_tyqqajgod2bb = {.size = 8, .align = 8, .kind = GM_K_FLOAT, .bits = 64};
const gm_type gm_tyqqajgod2bb;
const uint8_t gm_s000up2l16pib[9] = "unary {}";
const uint8_t gm_s01ekgjjrfehv[13] = "gm_main_call";
const uint8_t gm_s01k0p56k10gk[29] = "cannot cast `{}` to `{}`: {}";
const uint8_t gm_s02aodrl2fpra[12] = "cannot use ";
const uint8_t gm_s0adus7j7p0g0[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000H\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s0b795193rvs2[7] = "` has ";
const uint8_t gm_s0cf8090s33vc[30] = "`pub` cannot be applied to {}";
const uint8_t gm_s0dhnude2n5fe[8] = "{} tag;";
const uint8_t gm_s0eudsv4chfi5[44] = "` has a single payload; use one sub-pattern";
const uint8_t gm_s0hcm1974rbe7[34] = "the parameter `{}` is given twice";
const uint8_t gm_s0jbci4ff2esa[23] = "compiler/interp/run.gm";
const uint8_t gm_s0kf3vtbitlnb[58] = "a math vector's elements are integers or floats, not `{}`";
const uint8_t gm_s0pi5sf493jqq[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000u\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s0s3f0v5p7674[27] = "((void *)(uintptr_t){}ULL)";
const uint8_t gm_s0t5omnh472sf[21] = "` has no doc comment";
const uint8_t gm_s0uoh0jkm28kv[24] = "{} gm_r; {}; return {};";
const uint8_t gm_s13jru0todls1[38] = "{} needs a concrete {} type, not `{}`";
const uint8_t gm_s19069udlvhni[9] = "ProcKill";
const uint8_t gm_s199bfpbr16jo[44] = "no enclosing loop or `with` is labeled `{}`";
const uint8_t gm_s1bp6pdherlbe[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\263\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s1eo6mkdbgh2b[38] = "the effect `{}` has no operation `{}`";
const uint8_t gm_s1jkh3mpunvd6[64] = "`#{}` cannot precede an array type; only `#simd` and `#vec` can";
const uint8_t gm_s1lh34nabv9g2[5] = "--> ";
const uint8_t gm_s1m38bo3naic1[8] = "binding";
const uint8_t gm_s1o778dp7d59d[43] = "`{}` is private to the module `{}` (\302\2473.3)";
const uint8_t gm_s1p0thm43h6d6[21] = "method not found: {}";
const uint8_t gm_s1smed2pig5ts[35] = "unexpected whitespace character U+";
const uint8_t gm_s25qdppmkh7s6[7] = "`null`";
const uint8_t gm_s268n7l3tugli[6] = "{}@{}";
const uint8_t gm_s283bs8hsleb3[9] = "let-else";
const uint8_t gm_s29avse796bo5[18] = "`{}` is a keyword";
const uint8_t gm_s2asttl6mu7q3[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\275\000\000\0005\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s2dog0ijtvmd2[32] = "unsupported lockfile version {}";
const uint8_t gm_s2ef11ime8o01[42] = "internal error: `@fiber` outside a clause";
const uint8_t gm_s2kvk8bnjk52t[12] = "gm_node *cx";
const uint8_t gm_s2ogeh8fp9da3[12] = "GM_CONTINUE";
const uint8_t gm_s2rk6mdib4mtv[17] = "0123456789abcdef";
const uint8_t gm_s2sfjihvaabi3[9] = "{}.index";
const uint8_t gm_s2u7k18iev3v2[4] = "u, ";
const uint8_t gm_s32blj9c3tc60[11] = "((void *)&";
const uint8_t gm_s32rhj249n222[10] = "make_info";
const uint8_t gm_s38cjdjl7n99k[10] = "uint8_t p";
const uint8_t gm_s39jha51ulivf[27] = "__attribute__((noinline)) ";
const uint8_t gm_s3hb2q98sfsao[5] = "; }\012";
const uint8_t gm_s3ierue9bs27g[46] = "slicing a many-pointer without an upper bound";
const uint8_t gm_s3jflb7gdk32k[32] = "unnecessary parentheses around ";
const uint8_t gm_s3p26aim75mms[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\0004\001\000\000;\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s3qu37je1jc8f[46] = "` in [lint] (`gamma lint --rules` lists them)";
const uint8_t gm_s3ss0hcg1h7js[7] = " runs)";
const uint8_t gm_s3t2d4i4t34ct[74] = "the elements of a tuple iterated by `for` must have one type, but found `";
const uint8_t gm_s3tr540lff6s6[11] = "unit_value";
const uint8_t gm_s3udvgirbbq3p[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\235\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s418vrbemh247[49] = "a string pattern needs a `string`, but this is `";
const uint8_t gm_s41uc8v0dkl4a[15] = "), tb = GM_LD(";
const uint8_t gm_s463po27vtmnn[15] = "-ffreestanding";
const uint8_t gm_s46ki8g7j9f6v[6] = "({}) ";
const uint8_t gm_s48hq6ubqo1rd[10] = "{} a[{}];";
const uint8_t gm_s48vlq6lddb8m[29] = " __attribute__((vector_size(";
const uint8_t gm_s4fhghepo9qb6[13] = "gm_atag_grow";
const uint8_t gm_s4g35ls4ma80g[13] = "gamma.toml: ";
const uint8_t gm_s4hf5j4eu72mv[66] = "invalid unicode escape: expected `}` after the hexadecimal digits";
const uint8_t gm_s4lmdtjb9rnah[70] = "`continue` finishes the clause with `()`, but the operation returns `";
const uint8_t gm_s4m4ttv1faeb1[175] = "`{}(\342\200\246)` is an iterator, not a value: it cannot be passed for a parameter of type `{}`; run it with `for x in {}(\342\200\246)`, or gather its values with `collect({}(\342\200\246))` (\302\24713.5)";
const uint8_t gm_s4nikb38gk9t0[22] = "gm_heap_alloc({}, {})";
const uint8_t gm_s4ok0uoiq1nu9[97] = "\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_s4r4tmb9bg65q[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\306\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s527dqlmmaier[6] = ":help";
const uint8_t gm_s55ccd7do7rbs[11] = "InOptional";
const uint8_t gm_s592b618tctb1[15] = "layout of `{}`";
const uint8_t gm_s5cbc2v9e254r[37] = "({})((({}){} & {}) | (({}){} & ~{}))";
const uint8_t gm_s5cfou479ljhn[29] = "compiler/check/array_ctor.gm";
const uint8_t gm_s5f9u0o3duhns[13] = "empty-branch";
const uint8_t gm_s5gkjub7hejc1[9] = "gm_cont ";
const uint8_t gm_s5r23iu6ncd7o[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000q\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s5rqsv8c71bds[11] = "{\"edits\":[";
const uint8_t gm_s5vakuk6ui4kj[9] = "untyped ";
const uint8_t gm_s5vb1b1mnli2b[72] = "`try x else d` is ambiguous: write `try (x else d)` or `(try x) else d`";
const uint8_t gm_s5ve9r6iab8ga[26] = "`: unknown architecture `";
const uint8_t gm_s5veo86cvpdae[86] = "an array can be indexed only by an enum, and `{}` has a variant with a payload (`{}`)";
const uint8_t gm_s61i2o7ldibjh[35] = " is not exactly representable as `";
const uint8_t gm_s63e7t279sbjq[47] = "call through an invalid function pointer {:#x}";
const uint8_t gm_s64skgbd4ii1k[10] = "Variables";
const uint8_t gm_s6cmbp3c9g09j[61] = "float literal patterns are not allowed; compare with a guard";
const uint8_t gm_s6eefft9fser8[9] = "gm_mulhi";
const uint8_t gm_s6g2mpli2mu72[11] = "gm_closure";
const uint8_t gm_s6i74fbmndo4a[15] = "gm_finish_main";
const uint8_t gm_s6mmuahmmssv6[17] = "` has no payload";
const uint8_t gm_s6pgabt9io52p[132] = "the package contains the symbolic link `{}` -> `{}`, which points outside the package; links must stay inside the package directory";
const uint8_t gm_s6qf5dtv8rcdo[13] = "gm_cpu_count";
const uint8_t gm_s6rj805fm39qm[65] = "`{}` is an effect set, not a type; use it after `needs` (\302\24716.6)";
const uint8_t gm_s70iivik802q4[26] = "integer number overflowed";
const uint8_t gm_s70j71mp70pdq[14] = ")pn->clauses[";
const uint8_t gm_s72gibfnd8lcj[8] = " != 0))";
const uint8_t gm_s744lbotjcl51[27] = "a long chain of operations";
const uint8_t gm_s76ekntop342o[8] = "MaskAny";
const uint8_t gm_s783a88c5t0ve[67] = "unknown type `{}`: integer types are at most 128 bits wide (\302\2475.2)";
const uint8_t gm_s7an5gtrdgen2[14] = "1 placeholder";
const uint8_t gm_s7b8bvi5kqaqt[7] = ": type";
const uint8_t gm_s7bq3dilmfq6u[47] = "({} && memcmp(GM_LD(void *, {}), {}, {}) == 0)";
const uint8_t gm_s7e422oeo6jkt[8] = "mk_node";
const uint8_t gm_s7ed59qo09cfg[41] = "slicing a many-pointer needs both bounds";
const uint8_t gm_s7fdjhp9ga5oo[8] = "gm_xe{}";
const uint8_t gm_s7m3rbk4l87ah[15] = "NetSetTimeouts";
const uint8_t gm_s7mfvroqmll1f[3] = "{ ";
const uint8_t gm_s7qcnd652ae6d[25] = "static const gm_variant ";
const uint8_t gm_s816n04ro16nc[48] = "this `else` holds only an `if`: write `else if`";
const uint8_t gm_s81csu1l86dmv[52] = "a `#global` needs a type: `#global name: T = value`";
const uint8_t gm_s86kdga2ul6h4[9] = "net_send";
const uint8_t gm_s87p55h1isc6m[6] = "gm_xe";
const uint8_t gm_s89gtqa9dnss0[35] = "`: it would hide the declaration `";
const uint8_t gm_s8d2alh114kri[15] = "pat-const : {}";
const uint8_t gm_s8g92mbmiiva2[48] = "an `@asm` clobber must be a compile-time string";
const uint8_t gm_s8jclq8b6mi27[17] = " was called here";
const uint8_t gm_s8l22p9l388io[67] = "a fiber's argument must have a first-class type, not `{}` (\302\24714.6)";
const uint8_t gm_s8l7qb8s3942u[35] = "and a function type after the name";
const uint8_t gm_s8m12els4res5[26] = "the arguments of `.clamp`";
const uint8_t gm_s8o6lm1cn4eo5[3] = "fd";
const uint8_t gm_s8ps5ka0vnf6d[42] = "call through an invalid function pointer ";
const uint8_t gm_s8qkarceaerkr[22] = "` is not defined on `";
const uint8_t gm_s8rs2s65rnmku[18] = "-ffp-contract=off";
const uint8_t gm_s8u62e7lr2m4s[13] = "let-tuple {}";
const uint8_t gm_s903nhsp8hiug[14] = "resolve_write";
const uint8_t gm_s92n4v7htp6pi[11] = "->clauses[";
const uint8_t gm_s96j6o9smoqdu[8] = "FnToPtr";
const uint8_t gm_s9dh9d8gs6a16[9] = "block : ";
const uint8_t gm_s9dudft75oi1c[33] = "internal error: `{}` has no body";
const uint8_t gm_s9eqqrnq18n3l[31] = "`{}` in {} holds an empty path";
const uint8_t gm_s9gk1b0ve64mt[10] = "float pow";
const uint8_t gm_s9h8lp3gkedth[29] = "float builtin on a non-float";
const uint8_t gm_s9i2ld3a42lap[8] = "FsWrite";
const uint8_t gm_s9impgj2b5t1c[50] = "stack sizes through functions of the core library";
const uint8_t gm_s9k65bfq9n6l0[14] = "render_region";
const uint8_t gm_s9kou5ia9uri9[6] = "; } }";
const uint8_t gm_s9m6kd6gec368[14] = "    Removing ";
const uint8_t gm_s9p37ubghllhn[64] = "` may be misaligned; copy the field to a local first (\302\2475.10.4)";
const uint8_t gm_s9qjpsba9cv0i[10] = "thread_op";
const uint8_t gm_s9rk6ipadg2pq[111] = "    { const void *mp = msg; uintptr_t ml = len; memcpy((uint8_t *)&m, &mp, sizeof mp); memcpy((uint8_t *)&m + ";
const uint8_t gm_sa3k87epai61e[25] = "to close the parenthesis";
const uint8_t gm_sa5m6gegug17p[40] = "stop after writing the C file (<exe>.c)";
const uint8_t gm_sa5s0jools6a2[9] = "RandFill";
const uint8_t gm_sa7q2bqmn8lgb[9] = "subtract";
const uint8_t gm_sa9e77lq9q04p[16] = "caller_location";
const uint8_t gm_sadg245agtuao[45] = "; an array can start filled: `[N]T(fill: v)`";
const uint8_t gm_sadj3uigeodiu[29] = "type `{}` has no method `{}`";
const uint8_t gm_sagd3osht35og[13] = "freed memory";
const uint8_t gm_sahd19pngn4vn[14] = "tag_offset = ";
const uint8_t gm_sahim4484f9l7[3] = ".c";
const uint8_t gm_sak3h3rlsvn4o[10] = "bool evok";
const uint8_t gm_salpeh0v5rqdf[8] = "shuffle";
const uint8_t gm_saovklm35s57t[184] = "{} needs the fields of `{}`, which a generic function does not know; a `#soa` of a type parameter has whole elements (`a[i]`), `len`, slicing, `copy_from`, `make` and `free` (\302\2475.6.4)";
const uint8_t gm_saq8ri2pmi7o5[19] = "parse_fn_type_only";
const uint8_t gm_sasea0sobfkrs[8] = "extern ";
const uint8_t gm_sat505mlqphna[26] = "invalid package version `";
const uint8_t gm_sauu2s1pbjqsh[11] = "the clause";
const uint8_t gm_sb21gep34rmm9[36] = "`{}` of dependency `{}` needs `git`";
const uint8_t gm_sb3sf0ef436fi[40] = "the body of this `with` never completes";
const uint8_t gm_sb5b70djeqhg8[16] = "` is a variable";
const uint8_t gm_sb5h01ig0b9oe[34] = "Asm {{ outputs: {}, inputs: {} }}";
const uint8_t gm_sb641udihu5hs[10] = "rand_fill";
const uint8_t gm_sb8b1gf0gtt49[43] = "(((unsigned __int128){}ULL << 64) | {}ULL)";
const uint8_t gm_sb8qu1qqed6pt[85] = "the rest parameter `{}` takes the remaining positional arguments; it cannot be named";
const uint8_t gm_sbbgtkch3q4n5[3] = " B";
const uint8_t gm_sbht6tj2u5vfl[8] = "runtime";
const uint8_t gm_sbjshone12eeu[158] = "checksum mismatch for package {} (expected {}, found {}); gamma.lock was edited or the cached checkout {} was modified (`gamma cache clean --all` removes it)";
const uint8_t gm_sbjuojbqo51av[36] = "`bits` to this type at compile time";
const uint8_t gm_sbkd9ldfr86ri[5] = "@\015\003\000";
const uint8_t gm_sbl7p8tv4j986[17] = " elements, not `";
const uint8_t gm_sblomia8fb3mu[20] = "<table>\012<thead><tr>";
const uint8_t gm_sbm19bhbgtvrj[129] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sbmtphtelgust[29] = "__attribute__((export_name(\"";
const uint8_t gm_sbo8mn3eri83c[23] = "unwrap of error value ";
const uint8_t gm_sbqoiitqtfv75[11] = "reduce_min";
const uint8_t gm_sbssn254n4fs2[25] = "gm_asyncify_start_rewind";
const uint8_t gm_sbtq9nj91tefv[10] = ", sizeof ";
const uint8_t gm_sbvi33ihvufj1[20] = "((uint8_t *)gm_g{})";
const uint8_t gm_sc1nadatuanhc[5] = "'\\0'";
const uint8_t gm_sc46nvblu52f3[18] = "remove the import";
const uint8_t gm_sc559v8etqgda[19] = ") __asm__(GM_SYM(\"";
const uint8_t gm_sc6pb1tb5tnnv[34] = "{}gm_poison(&F, 0xDD, sizeof F); ";
const uint8_t gm_scai7a7ojvpjh[19] = "[package]\012name = \"";
const uint8_t gm_scc5n7285iivu[13] = "reference = ";
const uint8_t gm_scd1bb5dijnis[15] = "(w)` (\302\2475.6.2)";
const uint8_t gm_scgm14ju6d0p8[15] = "cannot negate ";
const uint8_t gm_scgvr0isof71e[9] = "__wasi__";
const uint8_t gm_sckmhri27q39l[53] = "    uint8_t x = v[0] != 0; return gm_mix(h, &x, 1);\012";
const uint8_t gm_scllp0lpbljde[15] = "`pub(package)`";
const uint8_t gm_scobqq9qv6nnk[6] = "&#39;";
const uint8_t gm_scqqkvgm1j3cs[65] = "internal error: a lane operation on a value that is not a vector";
const uint8_t gm_scrnhm4ha22vj[18] = "` is indexed by `";
const uint8_t gm_scs3asa1395s7[6] = "{}e{}";
const uint8_t gm_scvsibgrrt3sc[13] = "check_module";
const uint8_t gm_sd2a8ckkfb013[26] = "` contains the character ";
const uint8_t gm_sd2iiq8cuaks3[5] = "rotr";
const uint8_t gm_sd5giovmg2s7p[17] = "static int gm_xt";
const uint8_t gm_sdak0upmjiivc[15] = "gm_unw.kind = ";
const uint8_t gm_sdak257j1qgl8[6] = "cstr ";
const uint8_t gm_sdbgs2fic3oib[11] = "gm_fs_open";
const uint8_t gm_sdbtbup89cn84[19] = " needs a concrete ";
const uint8_t gm_sdcf6v5v0oma5[6] = "dedup";
const uint8_t gm_sddniufplfkku[66] = "invalid memory access: write to read-only memory at address {:#x}";
const uint8_t gm_sdl70rjr9en4m[16] = "gamma: applied ";
const uint8_t gm_sdm98dp52j8ep[16] = "`{}` is a label";
const uint8_t gm_sdmti3s9j8p44[11] = "indexing `";
const uint8_t gm_sdnh9sm5pppq1[40] = "expected a value, found the effect `{}`";
const uint8_t gm_sdprvvqv16trj[61] = "`; use `.trunc`, `.round`, `.checked` or `.sat` for integers";
const uint8_t gm_sdubr1na84odv[12] = ".align = {}";
const uint8_t gm_se09jntm6upln[4] = "ccx";
const uint8_t gm_se0oi28sapu01[11] = "`, found `";
const uint8_t gm_se2efihu2rvsa[27] = "check build run test bench";
const uint8_t gm_se5r6p8tqdo82[31] = "the literal is too large for `";
const uint8_t gm_seajfhgtfmhv7[12] = "fs_open_dir";
const uint8_t gm_seak1jbr9qmd1[9] = "({})({})";
const uint8_t gm_sebk4v8jac58q[43] = "attribute `#{}` does not apply to a struct";
const uint8_t gm_seflbetisd1ui[3] = "kc";
const uint8_t gm_segm7or15hs5h[20] = "dynamicRegistration";
const uint8_t gm_sei6ib477d1ds[4] = "loc";
const uint8_t gm_sennl2fjkcbdb[4] = "big";
const uint8_t gm_senuk3dngdds0[17] = "prefer-immutable";
const uint8_t gm_seqap6hh2h8p5[5] = "<h2>";
const uint8_t gm_serasf77ce9g3[52] = "a tuple must be either fully named or fully unnamed";
const uint8_t gm_serke084a3lbj[41] = "\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\000-\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sf51sbqtper4f[74] = "`{}` has no variant carrying `{}`, so the error cannot be lifted (\302\24710.6)";
const uint8_t gm_sfil4j1onqa90[14] = "gamma_repl.gm";
const uint8_t gm_sfinup9etdjip[4] = "MiB";
const uint8_t gm_sfiqishktrbg2[27] = "to begin the foreign block";
const uint8_t gm_sfjrpo4i6b0at[24] = "compiler/pkg/support.gm";
const uint8_t gm_sfnq22hc2kms9[14] = "binary_search";
const uint8_t gm_sfpnied4dp46h[15] = "gm_proc_launch";
const uint8_t gm_sfs5rrntfu3mi[25] = "a constant cannot hold `";
const uint8_t gm_sft51gvrcleut[16] = "gm_fs_canonical";
const uint8_t gm_sfu0in59pituf[62] = "a precision goes with floats, not with an integer base or `q`";
const uint8_t gm_sfurr787jvkcg[6] = ") <= ";
const uint8_t gm_sg0tidc1tpl0e[33] = "internal error: not an operation";
const uint8_t gm_sg8a1r64kvpgo[3] = "mk";
const uint8_t gm_sgd9tn7p5ihgk[10] = "Module {}";
const uint8_t gm_sge2cgjaklnf2[5] = "_$.@";
const uint8_t gm_sge6a56sqn6kj[21] = "    return na < nb;\012";
const uint8_t gm_sgfqei77bsia8[4] = " < ";
const uint8_t gm_sggm36eg9tkga[10] = "=` needs ";
const uint8_t gm_sgjkgsqdq34nu[37] = "the `with` that installs the handler";
const uint8_t gm_sgli4lbivdt1d[38] = "__attribute__((export_name(\"{}\"))) {}";
const uint8_t gm_sgls4l5hb5j8p[75] = "a spread needs a tuple, an array or a struct with visible fields, not `{}`";
const uint8_t gm_sgnpj6tqkbeom[12] = "` on floats";
const uint8_t gm_sgo00f0mafbnd[4] = "Max";
const uint8_t gm_sgovtffcl93pk[15] = "GM_PTR_TAG({})";
const uint8_t gm_sgq1v4nk059uv[10] = "this size";
const uint8_t gm_sgq6hvrcda4sp[37] = "`@{}` needs a pointer `^T`, not `{}`";
const uint8_t gm_sgs7af5k634cn[11] = "__mips64__";
const uint8_t gm_sgu3j1te831tl[24] = "`new` needs a directory";
const uint8_t gm_sguhoqhco0rfk[9] = "-pthread";
const uint8_t gm_sh1emun6fmppt[4] = "fix";
const uint8_t gm_sh1scn6dg8mpr[14] = ")((__int128)(";
const uint8_t gm_sh75rh8dk51gf[12] = ",\"message\":";
const uint8_t gm_sh8houjk34evm[15] = "import cycle: ";
const uint8_t gm_sh8lvmk1ev2kd[16] = "Not a directory";
const uint8_t gm_shdnhcpvptc1q[5] = "str ";
const uint8_t gm_shf8e0v8f47ml[17] = "\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_shg1pn1r3sjgi[13] = "gamma-bench-";
const uint8_t gm_shgjlb2nrn5fk[22] = "operation would block";
const uint8_t gm_shhrbleiv8o3j[14] = "__builtin_fma";
const uint8_t gm_shjpff93a0f25[7] = "CFLAGS";
const uint8_t gm_shmgu63rvoce3[36] = "a lane index is a `usize`, not `{}`";
const uint8_t gm_shq9t6lutffbf[4] = ".{}";
const uint8_t gm_shtrdthev5fjn[14] = ", (uint8_t *)";
const uint8_t gm_shttb5kqse295[12] = "end of file";
const uint8_t gm_shu6vhibjovbk[18] = "this field access";
const uint8_t gm_siaeuo6l48ehh[15] = "a `when` block";
const uint8_t gm_sic5vsema0r36[8] = "handler";
const uint8_t gm_sic6g350stk7h[21] = "cannot read CFLAGS: ";
const uint8_t gm_sifqqc9n3eem2[6] = "alloc";
const uint8_t gm_sihlhf2982kip[12] = "gm_wasi_ctx";
const uint8_t gm_sii9tamh3vah6[32] = "` (`:help` lists the commands)\012";
const uint8_t gm_sil41urphp1ol[12] = "piece|{}|{}";
const uint8_t gm_sipi0b0c6ms62[6] = "char ";
const uint8_t gm_sirpcjgkmlehr[8] = "newText";
const uint8_t gm_sisc4n92pk600[13] = "gm_hash_uint";
const uint8_t gm_sisd1n433forg[52] = "invalid memory access: address {:#x} points into {}";
const uint8_t gm_sismsc98l6u4e[4] = "Xor";
const uint8_t gm_sisn94tbdoeof[65] = "this changes `{}`, a copy of `{}`, which does not see the change";
const uint8_t gm_sium1s93t7o1a[19] = "the assigned value";
const uint8_t gm_sivsapeh482sm[8] = "Convert";
const uint8_t gm_sj0ur1o7a5584[16] = " elements, but ";
const uint8_t gm_sj3lfuqd4f1np[37] = "`#export` applies only to a function";
const uint8_t gm_sj718thiqudru[10] = "({} + {})";
const uint8_t gm_sj97id02v9fh3[6] = "Chunk";
const uint8_t gm_sjcdr1cibbeq6[64] = "`\", file, line, col);\012    if (pn->kind == GM_NATIVE) {\012        ";
const uint8_t gm_sjcocjnsa7u7g[85] = "the value of this expression (of type `{}`) is unused; write `_ = \342\200\246` to discard it";
const uint8_t gm_sjf1r2hmheh87[29] = "` to bind the value as it is";
const uint8_t gm_sjkbtepn8bqeb[24] = "  called from {} in {}\012";
const uint8_t gm_sjmhvdjct2lq7[19] = "-D --deny-warnings";
const uint8_t gm_sjpml17j24nb4[64] = "invalid suffix `{}` for float literal; expected f16, f32 or f64";
const uint8_t gm_sjrtlfuv3os00[75] = "dependency `{}` needs a `path = \"\342\200\246\"`, a `git = \"\342\200\246\"` or a `url = \"\342\200\246\"`";
const uint8_t gm_sjt1hcgrgj8be[36] = "` does not apply to complex numbers";
const uint8_t gm_sk36cj5av4kuu[17] = "\000\000\000\000\000\000\000\0005\000\000\000\000\000\000\000";
const uint8_t gm_sk83con0mdsfe[6] = "tuple";
const uint8_t gm_sk9at358cmauo[27] = ")-__builtin_convertvector(";
const uint8_t gm_ska00mcp5pgpq[53] = "`return` from a handler clause to a function literal";
const uint8_t gm_skfo9c0nr3nrd[50] = "`: its field `len` would hide the length, `a.len`";
const uint8_t gm_skgulapvvrhkp[36] = " kb; memcpy(&kb, &k, sizeof kb); ((";
const uint8_t gm_skjfb4faajq73[13] = ", (uint32_t)";
const uint8_t gm_skkog7g7r3edb[8] = "windows";
const uint8_t gm_skl1tisbdbp2m[13] = "gm_atag_slot";
const uint8_t gm_skme3gpq5egsp[40] = "invalid suffix `{}` for integer literal";
const uint8_t gm_skr6p9lfc5ftp[11] = "type alias";
const uint8_t gm_skrdd0u45d86g[17] = "\000\000\000\000\000\000\000\000S\000\000\000\000\000\000\000";
const uint8_t gm_sksgae11pvk6p[2] = "\011";
const uint8_t gm_skt7g4v99o4j0[42] = "a local variable whose lifetime has ended";
const uint8_t gm_skusvrbuvvp37[13] = "after `else`";
const uint8_t gm_sl1b73huqrikt[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\032\002\000\000/\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sl4daef1gs04o[37] = "internal error: get on a #bits field";
const uint8_t gm_sl80cge5ts0aj[14] = "pub(package) ";
const uint8_t gm_sl815v6oegv7n[14] = "; memcpy(gm_g";
const uint8_t gm_slc3rjglch387[50] = "missing argument for the compile-time parameter `";
const uint8_t gm_sle9lbvtgi3v6[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\355\001\000\000\032\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_slfddg7c1u1oc[62] = "cannot assign to `{}`: it is immutable (declare it with `:=`)";
const uint8_t gm_slfinpjh0406b[47] = "vectors are not ordered; compare lanes with `.";
const uint8_t gm_slfk2il1skugc[13] = "gm_backtrace";
const uint8_t gm_slgu49nv3o8e7[12] = "    memcpy(";
const uint8_t gm_slmnp81e6ev88[10] = ", .env = ";
const uint8_t gm_sln5qir3tgi7j[10] = "FutexWake";
const uint8_t gm_slpi6dabivg2e[29] = "field `{}` is supplied twice";
const uint8_t gm_sm0f8bs4gd2ma[18] = "__atomic_fetch_or";
const uint8_t gm_sm34bm8ke9n7b[22] = "` for integer literal";
const uint8_t gm_sm5ffrmin8340[10] = "subdir = ";
const uint8_t gm_sm6gmoq645v8o[70] = "`if c { true } else { false }` and the like, which are `c` or `not c`";
const uint8_t gm_sm732n1u78vvc[16] = "    if (ta == (";
const uint8_t gm_sm922ui8k0oca[9] = ",\"line\":";
const uint8_t gm_smbfsgie9fgg4[4] = "Env";
const uint8_t gm_smf8ll9cm8fgf[26] = "build run test bench repl";
const uint8_t gm_smhve5pq35drr[9] = "variants";
const uint8_t gm_smlcnjfj6t3mc[6] = "&amp;";
const uint8_t gm_smnkiec8dekcp[8] = "-target";
const uint8_t gm_smoi1nh7b1b6o[65] = "`@fiber` may be used only in the body of a `ctl` clause (\302\24714.6)";
const uint8_t gm_smp6ue9h98hbc[31] = ", argc > 2 \077 argv[2] : NULL);\012";
const uint8_t gm_smsmo0f5ao530[10] = "fn-ref {}";
const uint8_t gm_sn2e6t65fstoj[19] = "__SIZEOF_POINTER__";
const uint8_t gm_sn6vmked7l7pf[7] = "-mf16c";
const uint8_t gm_sn8d077r4fgpq[26] = " __attribute__((packed)) ";
const uint8_t gm_snaq7hvenqsum[4] = "Rem";
const uint8_t gm_snfsrvfr6vnkt[8] = "elem = ";
const uint8_t gm_sngckqdbegtlc[26] = " is not an array or slice";
const uint8_t gm_snhkhrbd8tjtf[45] = "range patterns match integers, but this is `";
const uint8_t gm_snku1611u1u7p[9] = "memmove(";
const uint8_t gm_snm19vfbec4q3[19] = "call perform {} {}";
const uint8_t gm_snmrruc88a739[10] = "inline {}";
const uint8_t gm_snritti8butc6[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\356\000\000\000#\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_snrk30ug2qmu2[8] = "freebsd";
const uint8_t gm_snsqui8o89u28[15] = "` with `using`";
const uint8_t gm_snstimc3n04u0[12] = "FsRemoveDir";
const uint8_t gm_snuadcbi5e786[15] = " lanes, like `";
const uint8_t gm_snvodad5t43r7[11] = ")) return ";
const uint8_t gm_so1m4qmhi2fvm[93] = "`{}` takes one value per element, so {} is not converted to it; spread it: `{}(..x)` (\302\2476.5)";
const uint8_t gm_so3bl4q7kp6gl[21] = "unknown import kind ";
const uint8_t gm_so94vinsu4uib[50] = "vector {} of {} lanes with a slice of {} elements";
const uint8_t gm_sob1n7uh7kpps[14] = "useless-defer";
const uint8_t gm_sodg3e7aummvt[8] = "release";
const uint8_t gm_soeo46rfuspd5[32] = "` must be 64 hexadecimal digits";
const uint8_t gm_soi6iv1hnej40[13] = "loop {} : {}";
const uint8_t gm_solguqm0afvc4[44] = "cannot infer the result type of `!e` here{}";
const uint8_t gm_sor8flejv9lfq[67] = "    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);\012";
const uint8_t gm_sorb6gi9d49ic[9] = "--filter";
const uint8_t gm_souep3pirutur[55] = "this `if` never completes: each of its branches leaves";
const uint8_t gm_sp2a19d61vbe0[4] = ", \"";
const uint8_t gm_sp4skfj2bn2ut[7] = "` is `";
const uint8_t gm_sp4uhnkunj2cg[3] = "Gt";
const uint8_t gm_sp7vnnm6ibcad[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\223\001\000\000\"\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_spbv5h8c0ukao[5] = "call";
const uint8_t gm_spfleivdeduud[4] = "; `";
const uint8_t gm_sppcsr92ukfol[64] = "`#deprecated` takes an optional message: `#deprecated(\"use g\")`";
const uint8_t gm_sppeomacjvkpm[7] = "punct ";
const uint8_t gm_spqlqjii789ln[57] = "internal error: a select of a value that is not a vector";
const uint8_t gm_spsbmlrti644m[11] = "<dt><code>";
const uint8_t gm_sq05ai6ts8qlk[17] = "(\342\200\246))` (\302\24713.5)";
const uint8_t gm_sq1mn6f7h111r[3] = "_s";
const uint8_t gm_sq3r7vbcbs4ll[26] = "`package` must be a table";
const uint8_t gm_sq4jcg2uujqmj[16] = "` values, not `";
const uint8_t gm_sq7c24l9r5rti[118] = "a program for a freestanding target does not declare `main`; its entry points are its `#export`ed functions (\302\2473.7.1)";
const uint8_t gm_sq89uknu8mulu[8] = "a float";
const uint8_t gm_sq95m8isfa7vd[2] = "t";
const uint8_t gm_sq9org4qoda29[12] = "GM_PTR_ADD(";
const uint8_t gm_sqfu5kl4vat80[9] = "CpuCount";
const uint8_t gm_sqin9fcebg38h[7] = "unary ";
const uint8_t gm_sqkcsn1qihjbl[41] = "    uintptr_t na = GM_LD(uintptr_t, a + ";
const uint8_t gm_sqsb0jek355o9[17] = "a struct literal";
const uint8_t gm_sqssndasqkc4l[17] = "__builtin_trunc(";
const uint8_t gm_sqvepl1893o5g[3] = "))";
const uint8_t gm_sqviis18fpvj7[5] = "-tzf";
const uint8_t gm_sr6b2mblh2jam[223] = " {\012    gm_buf b = {0};\012    uint8_t sb[GM_VIEW_SCRATCH];\012    const void *d;\012    size_t n;\012    if (s) { gm_format_spec(&b, t, v, s); d = b.data; n = b.len; } else n = gm_format_view(&b, sb, t, v, &d);\012    bool u = n != 0 && ";
const uint8_t gm_sr8f1b5ta7294[9] = "\377\377\377\377\377\377\017\000";
const uint8_t gm_srd5daqbo6ilc[58] = "` is not a symbol name: use letters, digits and `_ $ . @`";
const uint8_t gm_srehvgd1sod2m[18] = "missing `version`";
const uint8_t gm_srfobl8913gvl[3] = "` ";
const uint8_t gm_sri78umnjbra8[11] = "statements";
const uint8_t gm_srjb0lv60gms5[62] = "        gm_write_err(b.data, b.len);\012        return 1;\012    }\012";
const uint8_t gm_srltdqer7h3t7[55] = "`{}.from_tag` needs an enum (a union without payloads)";
const uint8_t gm_srok3f5iht8q8[23] = "` (it does not define ";
const uint8_t gm_srqehfttj6ngk[4] = "Err";
const uint8_t gm_sruk98mc5gf6c[44] = "values of type `{}` are not ordered (\302\2477.5)";
const uint8_t gm_srv4cua6q51nq[5] = "free";
const uint8_t gm_ss3kdmf5g7qmv[82] = "` is a symbol of this implementation's runtime, so it cannot be exported (\302\24723.5)";
const uint8_t gm_ss67f0gmfen1m[3] = "::";
const uint8_t gm_ss9d6rvgfqq53[13] = "pat-variant ";
const uint8_t gm_ssb6m4k22290v[17] = "` was not closed";
const uint8_t gm_sscmtmpbttmhb[93] = "invalid suffix `{}` for a binary or octal literal; only decimal and hexadecimal floats exist";
const uint8_t gm_ssffver18si54[25] = "compiler/sema/ty_expr.gm";
const uint8_t gm_ssift49n290vj[5] = "we{}";
const uint8_t gm_ssjk8k45afgjb[11] = "test bench";
const uint8_t gm_ssnbkpbck3vbo[22] = "` cannot convert to `";
const uint8_t gm_ssp208k4afn15[23] = "unknown import kind {}";
const uint8_t gm_st35tcurhvabs[31] = ", .kind = GM_DECL, .clauses = ";
const uint8_t gm_st4gqcsm35nph[90] = "invalid format spec `{{:{}}}`: expected `[[fill]align][+][#][0][width][.precision][kind]`";
const uint8_t gm_st9djk4oc14um[67] = "`else` needs an optional or a result on its left, but this is `{}`";
const uint8_t gm_stbno5snno6c5[8] = "if : {}";
const uint8_t gm_stbrououb87po[51] = "an index must have an unsigned integer type, not `";
const uint8_t gm_stfd80s46313k[38] = "internal error: not a process service";
const uint8_t gm_stgra1esdnhlk[12] = "GM_PTR_TAG(";
const uint8_t gm_stjq3hbrk4g4r[4] = "tir";
const uint8_t gm_stkm2m8f3okk8[18] = "internal error: `";
const uint8_t gm_stna0ubmss8p6[48] = "an assignment operator after the list of places";
const uint8_t gm_sto79bcv9e91i[4] = "{} ";
const uint8_t gm_stocunr1upos5[9] = "({}->up)";
const uint8_t gm_sts2e9lggtpr2[47] = "a float literal cannot have the integer type `";
const uint8_t gm_stt50isac07pf[46] = "` for float literal; expected f16, f32 or f64";
const uint8_t gm_su0gvqh5vvvlj[37] = "the package version must be a string";
const uint8_t gm_su1atn16ojnm6[9] = "(v, h);\012";
const uint8_t gm_su4j8ek23cevi[51] = "; install Binaryen or set WASM_OPT to its wasm-opt";
const uint8_t gm_su6ipjjrnjs7p[10] = "sys_write";
const uint8_t gm_suafpipqeom8q[7] = "gm_rt-";
const uint8_t gm_suc2pvoh9ktdi[6] = "total";
const uint8_t gm_sudpbkssneeek[11] = "gm_rt_args";
const uint8_t gm_suevj69t71tjc[35] = "` does not exist in the repository";
const uint8_t gm_sufoqrq4onvgh[16] = "gm_ctl_perform(";
const uint8_t gm_suhl0206dfbdm[5] = "Proc";
const uint8_t gm_suj1i29321vmu[10] = "gm_hb_cap";
const uint8_t gm_suod5ocif7o5v[5] = "save";
const uint8_t gm_suqchf5vh2hqp[5] = "inst";
const uint8_t gm_sv0fbf330lmcd[8] = "foreign";
const uint8_t gm_svbrm62b64ga9[11] = "closure {}";
const uint8_t gm_svgaurqm99100[48] = "(__builtin_isfinite({}) && {} >= {} && {} < {})";
const uint8_t gm_sviu03g0f5s30[43] = "empty identifier segment in build metadata";
const uint8_t gm_svldd4tfe8pf3[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\036\001\000\000\037\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_svshci6b9lbnq[9] = "gm_st_v_";
const uint8_t gm_svth7kg659g2i[13] = ", 0, sizeof ";
const uint8_t gm_svtkcim3vundp[18] = "an operation name";
uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
uint8_t gm_c0nre4icjau[1] __attribute__((aligned(1)));
uint8_t gm_c0ocv2md26k[9] __attribute__((aligned(1)));
uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
uint8_t gm_c1nolilrvln[8] __attribute__((aligned(1)));
uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
uint8_t gm_c30ot371k6d[9] __attribute__((aligned(1)));
uint8_t gm_c3k4pnhd8av[14] __attribute__((aligned(1)));
uint8_t gm_c3oglifus73[18] __attribute__((aligned(1)));
uint8_t gm_c433keo1ggm[5] __attribute__((aligned(1)));
uint8_t gm_c51kem98eiq[6] __attribute__((aligned(1)));
uint8_t gm_c5h64s41na8[15] __attribute__((aligned(1)));
uint8_t gm_c64sa63uljg[1] __attribute__((aligned(1)));
uint8_t gm_c66tv9kq8na[208] __attribute__((aligned(8)));
uint8_t gm_c6fqpnlmjmc[1] __attribute__((aligned(8)));
uint8_t gm_c6v8oe6vn3d[9] __attribute__((aligned(1)));
uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
uint8_t gm_c8rj3oaeld7[14] __attribute__((aligned(1)));
uint8_t gm_c9ik8ouhqm8[16] __attribute__((aligned(1)));
uint8_t gm_c9paf88rqhe[8] __attribute__((aligned(8)));
uint8_t gm_cae9ud3ihhl[15] __attribute__((aligned(1)));
uint8_t gm_cbemal9c6bf[8] __attribute__((aligned(8)));
uint8_t gm_cbg3jjml4ui[5] __attribute__((aligned(1)));
uint8_t gm_cbl5e0crtor[16] __attribute__((aligned(1)));
uint8_t gm_cc5s1q78gi3[9] __attribute__((aligned(1)));
uint8_t gm_ccdcg9bumn7[5] __attribute__((aligned(1)));
uint8_t gm_ccgde627fhi[11] __attribute__((aligned(1)));
uint8_t gm_cdeh13viquh[4] __attribute__((aligned(1)));
uint8_t gm_ce9kahcvfp7[1] __attribute__((aligned(8)));
uint8_t gm_cea991q0g8k[13] __attribute__((aligned(1)));
uint8_t gm_cega6s9dl3o[4] __attribute__((aligned(4)));
uint8_t gm_cel6p901dc1[1] __attribute__((aligned(1)));
uint8_t gm_cf4m0q5ms2i[20] __attribute__((aligned(1)));
uint8_t gm_cg36ndjqc5r[14] __attribute__((aligned(1)));
uint8_t gm_cgomkqt0377[65] __attribute__((aligned(1)));
uint8_t gm_cgsmfcq937e_2[16] __attribute__((aligned(8)));
uint8_t gm_cgt3j8alm30[10] __attribute__((aligned(1)));
uint8_t gm_ch21cqrvvl4[8] __attribute__((aligned(1)));
uint8_t gm_chkfraubo1r[4] __attribute__((aligned(4)));
uint8_t gm_cj5e0v8gk2i[20] __attribute__((aligned(1)));
uint8_t gm_cjidvhvshci[19] __attribute__((aligned(1)));
uint8_t gm_cjvkergb5k5[10] __attribute__((aligned(1)));
uint8_t gm_ck1b0l8lsvc[39] __attribute__((aligned(1)));
uint8_t gm_ckgo58jroh8[8] __attribute__((aligned(1)));
uint8_t gm_cl2be5u9pqk[16] __attribute__((aligned(8)));
uint8_t gm_clcchfce70r[32] __attribute__((aligned(1)));
uint8_t gm_clkdg6jbli4[16] __attribute__((aligned(8)));
uint8_t gm_cll96946g4q[9] __attribute__((aligned(1)));
uint8_t gm_cm7k8v105sj[1] __attribute__((aligned(4)));
uint8_t gm_cmd1mqfuq2m[8] __attribute__((aligned(1)));
uint8_t gm_cmr3530gn23[13] __attribute__((aligned(1)));
uint8_t gm_cn8o84dqbf8[12] __attribute__((aligned(1)));
uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
uint8_t gm_cnbt0kfagfe[2] __attribute__((aligned(1)));
uint8_t gm_cnkg4pj33ln[1] __attribute__((aligned(1)));
uint8_t gm_coeovpdckud[2] __attribute__((aligned(1)));
uint8_t gm_cono7645157[11] __attribute__((aligned(1)));
uint8_t gm_cp2r20aoong[17] __attribute__((aligned(1)));
uint8_t gm_cphj36bitbq[688] __attribute__((aligned(8)));
uint8_t gm_cqq41hlun1j[5] __attribute__((aligned(1)));
uint8_t gm_crmf74ucsme[15] __attribute__((aligned(1)));
uint8_t gm_crr7q8eonfb[11] __attribute__((aligned(1)));
uint8_t gm_csmeedg7pnp[80] __attribute__((aligned(1)));
uint8_t gm_ct1fm60hrci[6] __attribute__((aligned(1)));
uint8_t gm_ct5b5llk605[13] __attribute__((aligned(1)));
uint8_t gm_ctdm11uuaj6[16] __attribute__((aligned(8)));
uint8_t gm_ctpnchchr9h[8] __attribute__((aligned(1)));
uint8_t gm_cu2ve789ji2[96] __attribute__((aligned(8)));
uint8_t gm_cuju2tms0ds[12] __attribute__((aligned(1)));
uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
uint8_t gm_k2sohj2k4ib[40] __attribute__((aligned(8)));
uint8_t gm_k6oa34t4v6n[40] __attribute__((aligned(8)));
uint8_t gm_k8qpjmtnqct[40] __attribute__((aligned(8)));
uint8_t gm_k9pi0h2o5i2[40] __attribute__((aligned(8)));
uint8_t gm_khdoei7bkbm[40] __attribute__((aligned(8)));
uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
uint8_t gm_kkh67d5mpb1[40] __attribute__((aligned(8)));
uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
uint8_t gm_kovthkfk7ta[40] __attribute__((aligned(8)));
uint8_t gm_kplh4t9vgh7[40] __attribute__((aligned(8)));
uint8_t gm_krkn5fd15l7[40] __attribute__((aligned(8)));
uint8_t gm_ksjvoab7999[40] __attribute__((aligned(8)));
uint8_t gm_ktv0eonmebe[40] __attribute__((aligned(8)));
void *const gm_hl7iqllvekj4[] = {(void *)gm_f_for_body_cbj68cn};
void *const gm_hlib8i5oaffv[] = {(void *)gm_f_for_body_tdd1e60};
void *const gm_hlifd4tg9fgk[] = {(void *)gm_f_for_body_3q46euu};
void *const gm_hlucu919n5n4[] = {(void *)gm_f_for_body_1avb45k};
void *const gm_hlvop6ag413a[] = {(void *)gm_f_for_body_n6686bi};
