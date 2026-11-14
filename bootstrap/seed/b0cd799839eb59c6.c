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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_053et7u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_11hcaja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1auho78 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1i5jc5h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1u6ftde {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2qc2nou {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3ihgef1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4ardfru {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4u9qq9u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5hv7i3t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5nchccl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_61u2nim {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_66u4b89 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6tl8ttq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8c059kc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8h832sm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_927k65l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_92ourn8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9qfvlmc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9qrfu7d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9vgs8ss {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a0699r5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a0hldi0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ai60mv6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aofeg5c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aqnqo4c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b7dkepv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b85dpit {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bl0f8ic {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_brq0ks6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dffog2e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e1svbei {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e86lkj6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ehksvf5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fcish15 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fdfo1ok {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g6krjfn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ggcbak2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gmkau1l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_h8eera6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hq195jh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i7691t5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i8uj85v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ic37s45 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_igu1hv0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_it6lh03 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_itvjhph {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k6dia3b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ka4ql8l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kal0p1e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kcagbgp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kqlk7gk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lgt4sf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lnnrp7c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m1le1hn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m3fp9c0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_meio80g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mk6gkbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mk73dop {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mptq7qh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ne2h5dd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_niater6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_oag0r9a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_objrr21 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p1ntnbu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p2q9bls {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pkhg8vp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pmhj9nd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q7b5er6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q84sqii {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qd7qlkr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qf86e0t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qjv22ro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rma6q6p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rpcu5fo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rt997vs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rtj0m4n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rvrffvv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s3g2bns {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s49gi03 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_slrf6c6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_t493dmr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tdmbelm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vfpqk7m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vn7cmu1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_407qqln {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_40d716h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_40j8dpf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_40ni57j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_40t0ers {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_41c1370 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_41hm2he {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_44833dr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_458q1b3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_45bh632 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_45lt1qt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_45m70a7 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_463tt80 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_46627ts {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_478hq5o {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_47t5q9n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_484p9e9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_48m8fhm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4af676s {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4duro1p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4e7kcl2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4eslq57 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4f3kbgr {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4f5imdp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4frhu1b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4g0prpe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hbjrqo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hckftk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hhihlj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hl9u7b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hn970h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4i3nb5n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4nn4ect {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4nuejdf {
    gm_frame h;
    gm_t7neromnu8t62 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t7neromnu8t62 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4ok6i29 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4otle6l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4pbmdvl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4pq08p9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4q9e8cc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4qd139f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4to43c3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4un19t8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4v9fh5b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k09npf9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k34396d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k399iof {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k3kbc4n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k64sjdb {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k6j7893 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k7k07fo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k7lpr9c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ka5ced8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ka8hqpj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kadoe0b {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kb96ddt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kbs7h9q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kd24nd8 {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kdp6nh1 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kfcf8tk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kgkjlf6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kgo83c4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj13ck1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj15nqp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj30rlk {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj3qi5h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kjp7rmf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kk1cd25 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kl4ha3s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kl8i1sq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_klj2nqm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_km55tit {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ko1kpil {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ko5t8rr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_koh61mc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kpd1ovq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kr44ep8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_krr1d3v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ksi8bsm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ktbu9b9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ktg7ckd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ku38rie {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kub2tep {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kvhj8q4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kvsrtt1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_053et7u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_11hcaja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1auho78(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1i5jc5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1u6ftde(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2qc2nou(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3ihgef1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4ardfru(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4u9qq9u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5hv7i3t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5nchccl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_61u2nim(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_66u4b89(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6tl8ttq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8c059kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8h832sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_927k65l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_92ourn8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9qfvlmc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9qrfu7d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9vgs8ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a0699r5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a0hldi0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ai60mv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aofeg5c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqnqo4c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b7dkepv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b85dpit(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bl0f8ic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_brq0ks6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dffog2e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e1svbei(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e86lkj6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ehksvf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fcish15(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fdfo1ok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g6krjfn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ggcbak2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gmkau1l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h8eera6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hq195jh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i7691t5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i8uj85v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ic37s45(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_igu1hv0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it6lh03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_itvjhph(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k6dia3b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ka4ql8l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kal0p1e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kcagbgp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kqlk7gk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lgt4sf2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lnnrp7c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1le1hn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m3fp9c0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meio80g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mk6gkbt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mk73dop(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mptq7qh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ne2h5dd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_niater6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oag0r9a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_objrr21(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p1ntnbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p2q9bls(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pkhg8vp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pmhj9nd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q7b5er6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q84sqii(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qd7qlkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qf86e0t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qjv22ro(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rma6q6p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rpcu5fo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rt997vs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rtj0m4n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rvrffvv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s3g2bns(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s49gi03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_slrf6c6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t493dmr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tdmbelm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vfpqk7m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vn7cmu1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_407qqln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_40j8dpf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_40ni57j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_40t0ers(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_41c1370(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_41hm2he(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_44833dr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_458q1b3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_45bh632(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_45m70a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_463tt80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_46627ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_478hq5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_47t5q9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_484p9e9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_48m8fhm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_4af676s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4duro1p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_4e7kcl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4eslq57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4f3kbgr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4f5imdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_4frhu1b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4g0prpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4hbjrqo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4hckftk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4hhihlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4hl9u7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4hn970h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4i3nb5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0);
__attribute__((aligned(4))) void gm_f_format_4nn4ect(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4ok6i29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4otle6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4pbmdvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4pq08p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4q9e8cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4qd139f(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4to43c3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4v9fh5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_k09npf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k34396d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_k399iof(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k3kbc4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_k64sjdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k6j7893(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k7k07fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_k7lpr9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ka5ced8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ka8hqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kadoe0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kb96ddt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kbs7h9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kd24nd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kdp6nh1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kfcf8tk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kgkjlf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kgo83c4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kj13ck1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj30rlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kj3qi5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_kjp7rmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kk1cd25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kl4ha3s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kl8i1sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_klj2nqm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_km55tit(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ko1kpil(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_koh61mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kpd1ovq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kr44ep8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_krr1d3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ksi8bsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ktg7ckd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ku38rie(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kub2tep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kvhj8q4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kvsrtt1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s01fuch54uo3k[11];
extern const uint8_t gm_s01tfkbjoijgc[23];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s08ekjvgo9fro[101];
extern const uint8_t gm_s091lteo20dar[64];
extern const uint8_t gm_s09r5jslo2reb[13];
extern const uint8_t gm_s0cf8090s33vc[30];
extern const uint8_t gm_s0d1hchv6cdbo[11];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s15gg5sblr4kl[4];
extern const uint8_t gm_s15i3qlq36ep1[16];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s17l5kif0frp2[19];
extern const uint8_t gm_s1amcoqteaeio[38];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1d982oimmu24[7];
extern const uint8_t gm_s1dc9boe5unhg[32];
extern const uint8_t gm_s1djo7fpeeqnt[11];
extern const uint8_t gm_s1idibqkta392[7];
extern const uint8_t gm_s1pmmpf8t54g4[23];
extern const uint8_t gm_s1s57v200fem0[6];
extern const uint8_t gm_s1tdsk62jj75n[8];
extern const uint8_t gm_s1tfd03l0gv6e[14];
extern const uint8_t gm_s20qcu4sv6mcn[19];
extern const uint8_t gm_s22lkrbtp01c5[23];
extern const uint8_t gm_s23lfljk9icjn[9];
extern const uint8_t gm_s26crtotu14pn[3];
extern const uint8_t gm_s2lldqflqcpmt[3];
extern const uint8_t gm_s302tqgcs0oai[49];
extern const uint8_t gm_s33ft5ck7snhl[4];
extern const uint8_t gm_s34n6pusofh71[8];
extern const uint8_t gm_s37sjoj9k6mkj[52];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3c7ikv4cpqg1[13];
extern const uint8_t gm_s3emon0a31046[81];
extern const uint8_t gm_s3ijb9gbhdbib[27];
extern const uint8_t gm_s3jgqb0t6i7b8[22];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s4be485tkfn41[15];
extern const uint8_t gm_s4oloc6hggc50[92];
extern const uint8_t gm_s4tgvb7s2dbuj[20];
extern const uint8_t gm_s4vqnsfknimfg[28];
extern const uint8_t gm_s52ul4dg63jhe[11];
extern const uint8_t gm_s5a6hd5ouael9[37];
extern const uint8_t gm_s5agga61vhqnt[20];
extern const uint8_t gm_s5kue8bfcvqfr[27];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5q33up034m64[37];
extern const uint8_t gm_s5sdmhb4570qm[20];
extern const uint8_t gm_s63e7t279sbjq[47];
extern const uint8_t gm_s68nsnvq9a8da[20];
extern const uint8_t gm_s6f1t1a0knpo4[82];
extern const uint8_t gm_s6g8mt9e5cjjr[11];
extern const uint8_t gm_s6mmuahmmssv6[17];
extern const uint8_t gm_s6uis6ovn881c[9];
extern const uint8_t gm_s76ovr591an92[4];
extern const uint8_t gm_s805anh9k3rr5[7];
extern const uint8_t gm_s85kbq4rs5fa7[83];
extern const uint8_t gm_s87h4fof1n9ou[11];
extern const uint8_t gm_s89fn6dpk70na[17];
extern const uint8_t gm_s8eg138piktj1[12];
extern const uint8_t gm_s8p032eorpf80[3];
extern const uint8_t gm_s8ps5ka0vnf6d[42];
extern const uint8_t gm_s8srbeac8og16[29];
extern const uint8_t gm_s93ft79mfnlvp[10];
extern const uint8_t gm_s96uiluq5ufvd[66];
extern const uint8_t gm_s9f47s6tkuln3[42];
extern const uint8_t gm_s9h42skj8t388[90];
extern const uint8_t gm_s9j4gjjc4pmge[41];
extern const uint8_t gm_s9kudvjpckcsm[10];
extern const uint8_t gm_s9lvs84vghure[12];
extern const uint8_t gm_s9pqiujjjjsta[55];
extern const uint8_t gm_sa45sq4bgugc4[50];
extern const uint8_t gm_sa7fm83sqt8aa[16];
extern const uint8_t gm_sagc0kltjb8fc[12];
extern const uint8_t gm_sahd19pngn4vn[14];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_sal83f6sn6m3n[78];
extern const uint8_t gm_sand7bpicpp6t[26];
extern const uint8_t gm_saqhctf8022h0[33];
extern const uint8_t gm_sb3o36rmuvj28[56];
extern const uint8_t gm_sbapuchngtvak[28];
extern const uint8_t gm_sboedjgihdapp[81];
extern const uint8_t gm_sbpf3hgo18oq5[52];
extern const uint8_t gm_sbtnbgj3h592u[19];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_scduvjunh0oei[9];
extern const uint8_t gm_scfppf8naer38[21];
extern const uint8_t gm_sck9nihohhsb7[15];
extern const uint8_t gm_scl69k9ct9m19[16];
extern const uint8_t gm_sct928cj9v78e[137];
extern const uint8_t gm_scv8i3jrgh2rk[65];
extern const uint8_t gm_sd4qrpv3lpilp[4];
extern const uint8_t gm_sd5fg79k33phf[37];
extern const uint8_t gm_sd912tgjus40m[28];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_sdkba1tit6s52[17];
extern const uint8_t gm_sdle7podd8151[45];
extern const uint8_t gm_sds1asc07m30k[16];
extern const uint8_t gm_sdtdaesvk7dfl[8];
extern const uint8_t gm_sdvpeftn9n4aa[7];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se62v1mjbig2r[21];
extern const uint8_t gm_seb1p7qlbq9p0[39];
extern const uint8_t gm_seegi1cuj806d[168];
extern const uint8_t gm_segv6dkv6t4tr[7];
extern const uint8_t gm_sel5oh1k2839e[12];
extern const uint8_t gm_semhbv8seph85[21];
extern const uint8_t gm_sfc071avthns7[21];
extern const uint8_t gm_sfcgvtliokb7a[28];
extern const uint8_t gm_sfhbo1m6f9ql4[35];
extern const uint8_t gm_sfled84a022e2[50];
extern const uint8_t gm_sfm8ps2e26dh1[3];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfsqcni8p8345[42];
extern const uint8_t gm_sfsrjkeonr0ad[51];
extern const uint8_t gm_sfv61btcutqd4[21];
extern const uint8_t gm_sg46oe1nh64ub[9];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg92g0je3abps[14];
extern const uint8_t gm_sge7ihldgqi49[20];
extern const uint8_t gm_sgi6la00bpjt2[15];
extern const uint8_t gm_sh630b08j7vfk[84];
extern const uint8_t gm_sha303gigke52[10];
extern const uint8_t gm_shhugap3t1rkg[41];
extern const uint8_t gm_shoiushfjjjs8[9];
extern const uint8_t gm_shp2btrg6e14g[29];
extern const uint8_t gm_sidiiakhbvqqi[9];
extern const uint8_t gm_sifq8gd3d748j[36];
extern const uint8_t gm_sighahslvsc1i[11];
extern const uint8_t gm_sinve1cmean4f[24];
extern const uint8_t gm_sj6ih18t5qqaf[36];
extern const uint8_t gm_sj70rum4dd30i[18];
extern const uint8_t gm_sj9krj49k3580[10];
extern const uint8_t gm_sjjcm6bafumir[47];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjlnsaoa4hibc[8];
extern const uint8_t gm_sjltai3b82h79[4];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_skki6c17jgbtj[30];
extern const uint8_t gm_sl0eq2rejro5l[18];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl52kab9m437h[74];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_slrmmf184fvu8[26];
extern const uint8_t gm_smaq5hrff6a5d[56];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smc854k364mj6[21];
extern const uint8_t gm_smd171q6j6al8[20];
extern const uint8_t gm_smjth8dldg6d0[59];
extern const uint8_t gm_smktpoks16pgi[6];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smuiobhjc3kll[30];
extern const uint8_t gm_sn2risld8qhf6[65];
extern const uint8_t gm_sn4gb2qts08ck[14];
extern const uint8_t gm_sn5f6h6alcm9q[12];
extern const uint8_t gm_sn62ecr79ngvo[7];
extern const uint8_t gm_sne1lgqg4j81f[27];
extern const uint8_t gm_snf9ak4s55usv[18];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snjq35j2j9d3c[113];
extern const uint8_t gm_snuadcbi5e786[15];
extern const uint8_t gm_snvncjka7ph6m[7];
extern const uint8_t gm_so3t10be8g68d[10];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soulf4opjqcvv[10];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp0rb1clvmpp5[5];
extern const uint8_t gm_sp13ju5l53585[24];
extern const uint8_t gm_sp24uj950qpkl[16];
extern const uint8_t gm_sp6oikjubf9mg[7];
extern const uint8_t gm_sp6qfs9r7ep2n[75];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_spaa4sseck2fg[45];
extern const uint8_t gm_spdm0kqdda6co[25];
extern const uint8_t gm_spghmfn4rjv14[22];
extern const uint8_t gm_spjfgtrrvac57[14];
extern const uint8_t gm_spmv5l6cklh5d[18];
extern const uint8_t gm_spn26bfbaqp3j[32];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sq05ai6ts8qlk[17];
extern const uint8_t gm_sq1mn6f7h111r[3];
extern const uint8_t gm_sq64eqmcuoej6[15];
extern const uint8_t gm_sq94kuvk30n36[6];
extern const uint8_t gm_sqc2or5f1575b[31];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqf5m57l1ua5d[27];
extern const uint8_t gm_sqh06nbksvl8i[17];
extern const uint8_t gm_sqis5ba78j7cv[42];
extern const uint8_t gm_sql9r2b8fvivt[10];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr0sh5asl4po7[22];
extern const uint8_t gm_sr23o038u76nd[13];
extern const uint8_t gm_sr64vq6s9ci7q[35];
extern const uint8_t gm_sr6pcl2ua259q[12];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srd2ukct5o1qh[6];
extern const uint8_t gm_sreop6h89ulb0[18];
extern const uint8_t gm_srg30626j66d1[140];
extern const uint8_t gm_srjdrn9cldsu5[57];
extern const uint8_t gm_srkvn420e4huu[26];
extern const uint8_t gm_srnhu66btobke[9];
extern const uint8_t gm_srp5cji88najc[10];
extern const uint8_t gm_sruq3cf9ro3q2[66];
extern const uint8_t gm_ss52pcn7gi1ca[25];
extern const uint8_t gm_ss5c184rv03vi[18];
extern const uint8_t gm_ss8v6rf5v54r7[9];
extern const uint8_t gm_ssici0eo813ko[17];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssmb6ukuk62fe[53];
extern const uint8_t gm_sspbbn4skfqau[8];
extern const uint8_t gm_ssr6vk8erbj18[13];
extern const uint8_t gm_st78509mboaa4[22];
extern const uint8_t gm_stf7lvoc7oe54[16];
extern const uint8_t gm_stmdki8kna56a[24];
extern const uint8_t gm_str1v7jglok76[3];
extern const uint8_t gm_su06jr9k1rhdo[15];
extern const uint8_t gm_su2ndlglvt2c0[11];
extern const uint8_t gm_su41v0g030vmk[95];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const uint8_t gm_supi6mhbcvtna[5];
extern const uint8_t gm_svbrm62b64ga9[11];
extern const uint8_t gm_svjl1hr0jvvbv[9];
extern const uint8_t gm_svv63hse0pejb[18];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_ty929lennfd3;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyqa6gfa8omk;
extern const gm_type gm_tyqqajgod2bb;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_053et7u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_053et7u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_48m8fhm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_48m8fhm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_48m8fhm *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_11hcaja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_11hcaja F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_46627ts *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_46627ts *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_46627ts *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1auho78(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1auho78 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k6j7893 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k6j7893 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k6j7893 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1i5jc5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1i5jc5h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kpd1ovq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kpd1ovq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kpd1ovq *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1u6ftde(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1u6ftde F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k3kbc4n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k3kbc4n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k3kbc4n *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2qc2nou(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2qc2nou F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kl8i1sq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kl8i1sq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kl8i1sq *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3ihgef1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3ihgef1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hhihlj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4hhihlj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hhihlj *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4ardfru(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4ardfru F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k64sjdb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k64sjdb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k64sjdb *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4u9qq9u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4u9qq9u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4f3kbgr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4f3kbgr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4f3kbgr *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5hv7i3t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5hv7i3t F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_40t0ers *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_40t0ers *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_40t0ers *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5nchccl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5nchccl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kadoe0b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kadoe0b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kadoe0b *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_61u2nim(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_61u2nim F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_463tt80 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_463tt80 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_463tt80 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_66u4b89(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_66u4b89 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k34396d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k34396d *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k34396d *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6tl8ttq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6tl8ttq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_478hq5o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_478hq5o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_478hq5o *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8c059kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8c059kc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kgo83c4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kgo83c4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kgo83c4 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8h832sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8h832sm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kvhj8q4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kvhj8q4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kvhj8q4 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_927k65l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_927k65l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kd24nd8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kd24nd8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kd24nd8 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_92ourn8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_92ourn8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kb96ddt *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kb96ddt *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kb96ddt *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9qfvlmc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9qfvlmc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_41hm2he *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_41hm2he *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_41hm2he *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9qrfu7d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9qrfu7d F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k7lpr9c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k7lpr9c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k7lpr9c *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9vgs8ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9vgs8ss F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_45m70a7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_45m70a7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_45m70a7 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a0699r5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a0699r5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_484p9e9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_484p9e9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_484p9e9 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a0hldi0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a0hldi0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_klj2nqm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_klj2nqm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_klj2nqm *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ai60mv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ai60mv6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kub2tep *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kub2tep *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kub2tep *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aofeg5c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aofeg5c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4q9e8cc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4q9e8cc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4q9e8cc *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqnqo4c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aqnqo4c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4ok6i29 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4ok6i29 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4ok6i29 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b7dkepv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b7dkepv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kdp6nh1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kdp6nh1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kdp6nh1 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b85dpit(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b85dpit F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4af676s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4af676s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4af676s *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bl0f8ic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bl0f8ic F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj13ck1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kj13ck1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj13ck1 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_brq0ks6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_brq0ks6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_47t5q9n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_47t5q9n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_47t5q9n *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dffog2e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dffog2e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k09npf9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k09npf9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k09npf9 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e1svbei(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e1svbei F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_44833dr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_44833dr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_44833dr *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e86lkj6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e86lkj6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kgkjlf6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kgkjlf6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kgkjlf6 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ehksvf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ehksvf5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ka5ced8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ka5ced8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ka5ced8 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fcish15(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fcish15 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4nn4ect *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4nn4ect *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4nn4ect *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fdfo1ok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fdfo1ok F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_40ni57j *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_40ni57j *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_40ni57j *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g6krjfn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g6krjfn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_koh61mc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_koh61mc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_koh61mc *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ggcbak2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ggcbak2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj3qi5h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kj3qi5h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj3qi5h *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gmkau1l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gmkau1l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4duro1p *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4duro1p *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4duro1p *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h8eera6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_h8eera6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4f5imdp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4f5imdp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4f5imdp *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hq195jh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hq195jh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kfcf8tk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kfcf8tk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kfcf8tk *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i7691t5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i7691t5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4v9fh5b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4v9fh5b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4v9fh5b *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i8uj85v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i8uj85v F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hl9u7b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4hl9u7b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hl9u7b *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ic37s45(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ic37s45 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4to43c3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4to43c3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4to43c3 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_igu1hv0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_igu1hv0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4qd139f *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4qd139f *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4qd139f *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it6lh03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_it6lh03 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4eslq57 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4eslq57 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4eslq57 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_itvjhph(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_itvjhph F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ktbu9b9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ktbu9b9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ktbu9b9 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k6dia3b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_k6dia3b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_458q1b3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_458q1b3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_458q1b3 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ka4ql8l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ka4ql8l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4frhu1b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4frhu1b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4frhu1b *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kal0p1e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kal0p1e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_40d716h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_40d716h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_40d716h *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kcagbgp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kcagbgp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_km55tit *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_km55tit *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_km55tit *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kqlk7gk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kqlk7gk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kk1cd25 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kk1cd25 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kk1cd25 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lgt4sf2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lgt4sf2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kbs7h9q *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kbs7h9q *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kbs7h9q *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lnnrp7c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lnnrp7c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_45bh632 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_45bh632 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_45bh632 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1le1hn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m1le1hn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_46el73v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_46el73v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_46el73v *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m3fp9c0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m3fp9c0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hbjrqo *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4hbjrqo *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hbjrqo *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meio80g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_meio80g F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kvsrtt1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kvsrtt1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kvsrtt1 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mk6gkbt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mk6gkbt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kl4ha3s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kl4ha3s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kl4ha3s *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mk73dop(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mk73dop F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k7k07fo *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k7k07fo *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k7k07fo *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mptq7qh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mptq7qh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4pq08p9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4pq08p9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4pq08p9 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ne2h5dd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ne2h5dd F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4otle6l *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4otle6l *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4otle6l *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_niater6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_niater6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj30rlk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kj30rlk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj30rlk *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oag0r9a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_oag0r9a F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hn970h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4hn970h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hn970h *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_objrr21(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_objrr21 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_407qqln *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_407qqln *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_407qqln *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p1ntnbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p1ntnbu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ka8hqpj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ka8hqpj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ka8hqpj *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p2q9bls(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p2q9bls F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4nuejdf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4nuejdf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4nuejdf *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pkhg8vp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pkhg8vp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hckftk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4hckftk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4hckftk *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pmhj9nd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pmhj9nd F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ksi8bsm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ksi8bsm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ksi8bsm *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q7b5er6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q7b5er6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_41c1370 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_41c1370 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_41c1370 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q84sqii(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q84sqii F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4pbmdvl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4pbmdvl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4pbmdvl *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qd7qlkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qd7qlkr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ko1kpil *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ko1kpil *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ko1kpil *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qf86e0t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qf86e0t F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4un19t8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4un19t8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4un19t8 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qjv22ro(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qjv22ro F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4i3nb5n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4i3nb5n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4i3nb5n *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rma6q6p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rma6q6p F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_40j8dpf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_40j8dpf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_40j8dpf *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rpcu5fo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rpcu5fo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ku38rie *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ku38rie *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ku38rie *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rt997vs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rt997vs F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ktg7ckd *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ktg7ckd *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ktg7ckd *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rtj0m4n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rtj0m4n F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4e7kcl2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4e7kcl2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4e7kcl2 *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rvrffvv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rvrffvv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_45lt1qt *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_45lt1qt *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_45lt1qt *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s3g2bns(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_s3g2bns F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj15nqp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kj15nqp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kj15nqp *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s49gi03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_s49gi03 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kjp7rmf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kjp7rmf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kjp7rmf *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_slrf6c6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_slrf6c6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ko5t8rr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ko5t8rr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ko5t8rr *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t493dmr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_t493dmr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_krr1d3v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_krr1d3v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_krr1d3v *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tdmbelm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tdmbelm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_4g0prpe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_4g0prpe *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_4g0prpe *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vfpqk7m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vfpqk7m F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_k399iof *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_k399iof *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_k399iof *)((&F.h)->up))->l2), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vn7cmu1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vn7cmu1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_kr44ep8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_kr44ep8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_kr44ep8 *)((&F.h)->up))->l2), ((uint8_t)1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_053et7u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_11hcaja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1auho78(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1i5jc5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1u6ftde(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2qc2nou(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3ihgef1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4ardfru(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4u9qq9u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5hv7i3t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5nchccl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_61u2nim(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_66u4b89(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6tl8ttq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8c059kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8h832sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_927k65l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_92ourn8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9qfvlmc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9qrfu7d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9vgs8ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a0699r5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a0hldi0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ai60mv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aofeg5c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqnqo4c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b7dkepv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b85dpit(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bl0f8ic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_brq0ks6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dffog2e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e1svbei(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e86lkj6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ehksvf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fcish15(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fdfo1ok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g6krjfn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ggcbak2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gmkau1l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h8eera6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hq195jh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i7691t5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i8uj85v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ic37s45(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_igu1hv0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_it6lh03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_itvjhph(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k6dia3b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ka4ql8l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kal0p1e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kcagbgp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kqlk7gk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lgt4sf2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lnnrp7c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1le1hn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m3fp9c0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meio80g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mk6gkbt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mk73dop(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mptq7qh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ne2h5dd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_niater6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oag0r9a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_objrr21(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p1ntnbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p2q9bls(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pkhg8vp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pmhj9nd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q7b5er6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q84sqii(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qd7qlkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qf86e0t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qjv22ro(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rma6q6p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rpcu5fo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rt997vs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rtj0m4n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rvrffvv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s3g2bns(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s49gi03(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_slrf6c6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t493dmr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tdmbelm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vfpqk7m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vn7cmu1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_407qqln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_407qqln F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd9r0l17bbuoe));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_objrr21, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_40d716h F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgi6la00bpjt2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_kal0p1e, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9lvs84vghure, 11)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_40j8dpf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_40j8dpf F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s85kbq4rs5fa7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(82));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rma6q6p, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_srkvn420e4huu, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sb3o36rmuvj28, 55)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_40ni57j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_40ni57j F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp0rb1clvmpp5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fdfo1ok, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_str1v7jglok76, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_40t0ers(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_40t0ers F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl52kab9m437h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5hv7i3t, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smuiobhjc3kll, 29)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s9j4gjjc4pmge, 40)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_41c1370(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_41c1370 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slrmmf184fvu8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_q7b5er6, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smd171q6j6al8, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s5m9qimbv7e8k, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_41hm2he(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_41hm2he F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9h42skj8t388));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(89));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9qfvlmc, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s3emon0a31046, 80)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_44833dr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_44833dr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa45sq4bgugc4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e1svbei, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sdle7podd8151, 44)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_458q1b3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_458q1b3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp6qfs9r7ep2n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_k6dia3b, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_svv63hse0pejb, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smaq5hrff6a5d, 55)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_45bh632(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_45bh632 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqis5ba78j7cv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_lnnrp7c, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_snh0sl2cdagdn, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sd912tgjus40m, 27)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_45lt1qt F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sukbpodfceoe1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t5hdnnftkup6m t18 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rvrffvv, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t23 = t22;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 20));
            uint32_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty467d5cm1d5, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_45m70a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_45m70a7 F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_seegi1cuj806d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(167));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9vgs8ss, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sboedjgihdapp, 80)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_scv8i3jrgh2rk, 64)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sq05ai6ts8qlk, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_463tt80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_463tt80 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0cf8090s33vc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_61u2nim, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s4vqnsfknimfg, 27)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_46627ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_46627ts F;
    gm_t19enj90vg8bd gm_l0; /* args */
    gm_t19enj90vg8bd gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfv61btcutqd4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t19enj90vg8bd t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_11hcaja, (void *)&F.h, cx});
            int32_t t20 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 0));
            int32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s20qcu4sv6mcn, 18)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyqa6gfa8omk, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_46el73v F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnkfiltk5b43));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_m1le1hn, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9pqiujjjjsta, 54)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_478hq5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_478hq5o F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s96uiluq5ufvd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_6tl8ttq, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sds1asc07m30k, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_scfppf8naer38, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_spdm0kqdda6co, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_47t5q9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_47t5q9n F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sifq8gd3d748j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_brq0ks6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sl0eq2rejro5l, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ssjhh9bdn23jf, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_484p9e9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_484p9e9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1d982oimmu24));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_a0699r5, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_supi6mhbcvtna, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_48m8fhm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_48m8fhm F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1tdsk62jj75n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_053et7u, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sjkj5g78lejrj, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s26crtotu14pn, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4af676s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4af676s F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6f1t1a0knpo4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(81));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b85dpit, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s03c24qg7i06q, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sr23o038u76nd, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sp7cdd0n91vli, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_spaa4sseck2fg, 44)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_4duro1p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_4duro1p F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smjth8dldg6d0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_gmkau1l, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1idibqkta392, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfsrjkeonr0ad, 50)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4e7kcl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4e7kcl2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snjq35j2j9d3c));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(112));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rtj0m4n, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sp7cdd0n91vli, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s08ekjvgo9fro, 100)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4eslq57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4eslq57 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skki6c17jgbtj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_it6lh03, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s0d1hchv6cdbo, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sreop6h89ulb0, 17)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4f3kbgr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4f3kbgr F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5a6hd5ouael9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4u9qq9u, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_semhbv8seph85, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sspbbn4skfqau, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_4f5imdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_4f5imdp F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd4qrpv3lpilp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_h8eera6, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s0m9d22aen6f8, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4frhu1b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4frhu1b F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1pmmpf8t54g4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ka4ql8l, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s5sdmhb4570qm, 19)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4g0prpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4g0prpe F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so3t10be8g68d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_tdmbelm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sq94kuvk30n36, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sbvutvpa8pe3d, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4hbjrqo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4hbjrqo F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd5fg79k33phf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_m3fp9c0, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s16mf4cvajh59, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s01tfkbjoijgc, 22)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4hckftk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4hckftk F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdtdaesvk7dfl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pkhg8vp, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sl3q52ep6aa53, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sahim4484f9l7, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4hhihlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4hhihlj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_seb1p7qlbq9p0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_3ihgef1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sqc2or5f1575b, 30)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sqvepl1893o5g, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4hl9u7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4hl9u7b F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfled84a022e2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_i8uj85v, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfhbo1m6f9ql4, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sg92g0je3abps, 13)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4hn970h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4hn970h F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stf7lvoc7oe54));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_oag0r9a, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_svjl1hr0jvvbv, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4i3nb5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0) {
    struct gm_fr_format_4i3nb5n F;
    gm_toh0ootodhjue gm_l0; /* args */
    gm_toh0ootodhjue gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfsqcni8p8345));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)3ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_toh0ootodhjue t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qjv22ro, (void *)&F.h, cx});
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t21 = t20;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 1));
            uint8_t t23 = t22;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 2));
            uint8_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s3c7ikv4cpqg1, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty929lennfd3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s805anh9k3rr5, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty929lennfd3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sn4gb2qts08ck, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty929lennfd3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s5m9qimbv7e8k, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_4nn4ect(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4nn4ect F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssmb6ukuk62fe));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fcish15, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_shhugap3t1rkg, 40)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s87h4fof1n9ou, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4nuejdf F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s091lteo20dar));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t7neromnu8t62 t18 = GM_LD(gm_t7neromnu8t62, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_p2q9bls, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sr64vq6s9ci7q, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_snuadcbi5e786, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sidiiakhbvqqi, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_4ok6i29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4ok6i29 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr6pcl2ua259q));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_aqnqo4c, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_shoiushfjjjs8, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4otle6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4otle6l F;
    gm_tkpb4kplftgaa gm_l0; /* args */
    gm_tkpb4kplftgaa gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1amcoqteaeio));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tkpb4kplftgaa t18 = GM_LD(gm_tkpb4kplftgaa, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ne2h5dd, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_srp5cji88najc, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s93ft79mfnlvp, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1tfd03l0gv6e, 13)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_4pbmdvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4pbmdvl F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sagc0kltjb8fc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_q84sqii, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s23lfljk9icjn, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4pq08p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4pq08p9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8srbeac8og16));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mptq7qh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sp13ju5l53585, 23)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4q9e8cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4q9e8cc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9kudvjpckcsm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_aofeg5c, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sjltai3b82h79, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4qd139f(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4qd139f F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s37sjoj9k6mkj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_igu1hv0, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s4tgvb7s2dbuj, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_shp2btrg6e14g, 28)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_4to43c3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4to43c3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srjdrn9cldsu5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ic37s45, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_spjfgtrrvac57, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9f47s6tkuln3, 41)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4un19t8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdkba1tit6s52));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qf86e0t, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s5pra70i8o734, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_4v9fh5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_4v9fh5b F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scl69k9ct9m19));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_i7691t5, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sahd19pngn4vn, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_k09npf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k09npf9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfc071avthns7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_dffog2e, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sa7fm83sqt8aa, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_k34396d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_k34396d F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s63e7t279sbjq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_66u4b89, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s8ps5ka0vnf6d, 41)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1})) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_k399iof(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k399iof F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqh06nbksvl8i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vfpqk7m, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sj9krj49k3580, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sqdfr4k3vea5p, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_k3kbc4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_k3kbc4n F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su41v0g030vmk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(94));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1u6ftde, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s4oloc6hggc50, 91)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_k64sjdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k64sjdb F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srnhu66btobke));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4ardfru, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_k6j7893(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k6j7893 F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sruq3cf9ro3q2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tq3m5qgvkpje8 t18 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1auho78, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_spmv5l6cklh5d, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfcgvtliokb7a, 27)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sq64eqmcuoej6, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_k7k07fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_k7k07fo F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    gm_tidu2er0d7h95 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spghmfn4rjv14));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tidu2er0d7h95 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mk73dop, (void *)&F.h, cx});
            double t20 = GM_LD(double, (((uint8_t *)&gm_l3) + 0));
            double t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ssici0eo813ko, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyqqajgod2bb, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s15gg5sblr4kl, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_k7lpr9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_k7lpr9c F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smktpoks16pgi));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9qrfu7d, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sp02qqgucmpt7, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ka5ced8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ka5ced8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp24uj950qpkl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ehksvf5, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sel5oh1k2839e, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2lldqflqcpmt, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ka8hqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ka8hqpj F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svbrm62b64ga9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_p1ntnbu, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sg46oe1nh64ub, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kadoe0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kadoe0b F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s302tqgcs0oai));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5nchccl, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sj6ih18t5qqaf, 35)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s76ovr591an92, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tybihgc69jv3, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_kb96ddt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kb96ddt F;
    gm_tgbgti82mf879 gm_l0; /* args */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sinve1cmean4f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tgbgti82mf879 t18 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_92ourn8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sha303gigke52, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_ss8v6rf5v54r7, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_kbs7h9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kbs7h9q F;
    gm_tommf4j9upomc gm_l0; /* args */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st78509mboaa4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tommf4j9upomc t18 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_lgt4sf2, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_su06jr9k1rhdo, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_kd24nd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kd24nd8 F;
    GM_STACK_CHECK(&F, 513u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr0sh5asl4po7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)96ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tif3elqk01bdl t18 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_927k65l, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
            gm_b16_8 t31 = t30;
            gm_closure t32 = t19;
            gm_node *ccx33 = t32.cx;
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sp02qqgucmpt7, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t31), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t35 = F.l2;
        if (!t35) goto ie34;
        {
            void * t37 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t37);
        }
        {
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 8), t38);
            gm_tabb710d9mq2e t40 = t39;
            uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
            F.ret = t39;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t42);
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
inline __attribute__((aligned(4))) void gm_f_format_kdp6nh1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kdp6nh1 F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sck9nihohhsb7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tq3m5qgvkpje8 t18 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b7dkepv, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sl3q52ep6aa53, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_srd2ukct5o1qh, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s8p032eorpf80, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_kfcf8tk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kfcf8tk F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3ijb9gbhdbib));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_hq195jh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ss52pcn7gi1ca, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kgkjlf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kgkjlf6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s68nsnvq9a8da));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e86lkj6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_snf9ak4s55usv, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kgo83c4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kgo83c4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3jgqb0t6i7b8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_8c059kc, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s6g8mt9e5cjjr, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sn62ecr79ngvo, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_kj13ck1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kj13ck1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdvpeftn9n4aa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_bl0f8ic, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sq1mn6f7h111r, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kj15nqp F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sge7ihldgqi49));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_s3g2bns, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sj70rum4dd30i, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kj30rlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kj30rlk F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjjcm6bafumir));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_niater6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_se08n5d1gngu7, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_smc854k364mj6, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_ssr6vk8erbj18, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_kj3qi5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_kj3qi5h F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1s57v200fem0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tcc1rq4n1avb2 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ggcbak2, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_kjp7rmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kjp7rmf F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5agga61vhqnt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_s49gi03, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s89fn6dpk70na, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kk1cd25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kk1cd25 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1djo7fpeeqnt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_kqlk7gk, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s6uis6ovn881c, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kl4ha3s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kl4ha3s F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soulf4opjqcvv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mk6gkbt, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s34n6pusofh71, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kl8i1sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_kl8i1sq F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s15i3qlq36ep1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_2qc2nou, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s8eg138piktj1, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm8ps2e26dh1, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_klj2nqm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_klj2nqm F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sal83f6sn6m3n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(77));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_a0hldi0, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1dc9boe5unhg, 31)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_saqhctf8022h0, 32)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sighahslvsc1i, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_km55tit(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_km55tit F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scduvjunh0oei));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_kcagbgp, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sp6oikjubf9mg, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ko1kpil(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ko1kpil F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s01fuch54uo3k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qd7qlkr, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smbbaf3n1gml2, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ko5t8rr F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se62v1mjbig2r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_slrf6c6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ss5c184rv03vi, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_koh61mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_koh61mc F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sne1lgqg4j81f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_g6krjfn, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_stmdki8kna56a, 23)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kpd1ovq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kpd1ovq F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s17l5kif0frp2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1i5jc5h, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1bpuqd44lur7, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_su2ndlglvt2c0, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kr44ep8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kr44ep8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s22lkrbtp01c5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vn7cmu1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s52ul4dg63jhe, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_segv6dkv6t4tr, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_krr1d3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_krr1d3v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn2risld8qhf6));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_t493dmr, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sds1asc07m30k, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_scfppf8naer38, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sand7bpicpp6t, 25)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_ksi8bsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ksi8bsm F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5q33up034m64));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tf1bjg81u0glv t18 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pmhj9nd, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sqf5m57l1ua5d, 26)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_snvncjka7ph6m, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ktbu9b9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbapuchngtvak));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_itvjhph, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sql9r2b8fvivt, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s6mmuahmmssv6, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ktg7ckd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ktg7ckd F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4be485tkfn41));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rt997vs, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s09r5jslo2reb, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ku38rie(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ku38rie F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh630b08j7vfk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(83));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rpcu5fo, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s5kue8bfcvqfr, 26)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sbpf3hgo18oq5, 51)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sjnkguo0atpm5, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_kub2tep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kub2tep F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srg30626j66d1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(139));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ai60mv6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sct928cj9v78e, 136)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kvhj8q4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kvhj8q4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spn26bfbaqp3j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_8h832sm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sn5f6h6alcm9q, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sbtnbgj3h592u, 18)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_kvsrtt1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kvsrtt1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjlnsaoa4hibc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_meio80g, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s33ft5ck7snhl, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
