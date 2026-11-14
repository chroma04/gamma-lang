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
typedef struct { _Alignas(8) uint8_t b[0]; } gm_b0_8;
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t pe[6]; } gm_t553jgmqd1tlr;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 a[2]; } gm_t0kl141g5m2qf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t553jgmqd1tlr) == 16 && _Alignof(gm_t553jgmqd1tlr) == 8 && offsetof(gm_t553jgmqd1tlr, f0) == 0 && offsetof(gm_t553jgmqd1tlr, f1) == 8 && offsetof(gm_t553jgmqd1tlr, f2) == 9, "layout of `Policy`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 a[10]; } gm_thr4tus9rimig;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
_Static_assert(sizeof(gm_t0kl141g5m2qf) == 32 && _Alignof(gm_t0kl141g5m2qf) == 8 && offsetof(gm_t0kl141g5m2qf, a) == 0, "layout of `[2][]const string`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; } f1; } gm_t715ajl6o1ipo;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_trqfvmjtt4sij f4; } gm_t9j922kfqcdi9;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t pe[7]; } gm_tecsjmmgeeh52;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thr4tus9rimig) == 160 && _Alignof(gm_thr4tus9rimig) == 8 && offsetof(gm_thr4tus9rimig, a) == 0, "layout of `[10]string`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tu1089hhijadf f0; uint8_t f1; uint8_t pe[7]; } gm_tom1ad5bcqgi1;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
typedef struct { gm_b16_8 f0; gm_ttu09lij36f8c f1; gm_th2lqbo77pvsc f2; } gm_t03ccdabio1fj;
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t5detta1b4gnk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
_Static_assert(sizeof(gm_t715ajl6o1ipo) == 32 && _Alignof(gm_t715ajl6o1ipo) == 8 && offsetof(gm_t715ajl6o1ipo, f0) == 0 && offsetof(gm_t715ajl6o1ipo, f1) == 8, "layout of `Taken`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t77ebp3ogdble;
_Static_assert(sizeof(gm_t9j922kfqcdi9) == 72 && _Alignof(gm_t9j922kfqcdi9) == 8 && offsetof(gm_t9j922kfqcdi9, f0) == 0 && offsetof(gm_t9j922kfqcdi9, f1) == 8 && offsetof(gm_t9j922kfqcdi9, f2) == 16 && offsetof(gm_t9j922kfqcdi9, f3) == 24 && offsetof(gm_t9j922kfqcdi9, f4) == 32, "layout of `CleanReport`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tecsjmmgeeh52 v1; } f1; } gm_tcl27d4j9lskg;
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tecsjmmgeeh52) == 88 && _Alignof(gm_tecsjmmgeeh52) == 8 && offsetof(gm_tecsjmmgeeh52, f0) == 0 && offsetof(gm_tecsjmmgeeh52, f1) == 40 && offsetof(gm_tecsjmmgeeh52, f2) == 80, "layout of `ScaffoldReport`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tom1ad5bcqgi1 v1; } f1; } gm_tis0irsd01rfp;
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tom1ad5bcqgi1) == 64 && _Alignof(gm_tom1ad5bcqgi1) == 8 && offsetof(gm_tom1ad5bcqgi1, f0) == 0 && offsetof(gm_tom1ad5bcqgi1, f1) == 56, "layout of `?Version`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
_Static_assert(sizeof(gm_t03ccdabio1fj) == 80 && _Alignof(gm_t03ccdabio1fj) == 8 && offsetof(gm_t03ccdabio1fj, f0) == 0 && offsetof(gm_t03ccdabio1fj, f1) == 16 && offsetof(gm_t03ccdabio1fj, f2) == 56, "layout of `(url: string, reference: ?(key: string, value: string), subdir: ?string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tv37d3dpt2opu v1; } f1; } gm_t074vrl1hb4f6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t03ccdabio1fj v1; gm_tt008mgut8glb v2; } f1; } gm_t2jeshnsn30ep;
_Static_assert(sizeof(gm_t5detta1b4gnk) == 128 && _Alignof(gm_t5detta1b4gnk) == 8 && offsetof(gm_t5detta1b4gnk, f0) == 0 && offsetof(gm_t5detta1b4gnk, f1) == 8, "layout of `string!Extras`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t77ebp3ogdble) == 128 && _Alignof(gm_t77ebp3ogdble) == 8 && offsetof(gm_t77ebp3ogdble, f0) == 0 && offsetof(gm_t77ebp3ogdble, f1) == 8, "layout of `Failure!Parsed`");
_Static_assert(sizeof(gm_tcl27d4j9lskg) == 96 && _Alignof(gm_tcl27d4j9lskg) == 8 && offsetof(gm_tcl27d4j9lskg, f0) == 0 && offsetof(gm_tcl27d4j9lskg, f1) == 8, "layout of `string!ScaffoldReport`");
_Static_assert(sizeof(gm_tis0irsd01rfp) == 72 && _Alignof(gm_tis0irsd01rfp) == 8 && offsetof(gm_tis0irsd01rfp, f0) == 0 && offsetof(gm_tis0irsd01rfp, f1) == 8, "layout of `Failure!?Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t074vrl1hb4f6) == 56 && _Alignof(gm_t074vrl1hb4f6) == 8 && offsetof(gm_t074vrl1hb4f6, f0) == 0 && offsetof(gm_t074vrl1hb4f6, f1) == 8, "layout of `string!?Resolution`");
_Static_assert(sizeof(gm_t2jeshnsn30ep) == 88 && _Alignof(gm_t2jeshnsn30ep) == 8 && offsetof(gm_t2jeshnsn30ep, f0) == 0 && offsetof(gm_t2jeshnsn30ep, f1) == 8, "layout of `NewDep`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
struct gm_fr_add_04lv2cc {
    gm_frame h;
    gm_thr4tus9rimig l1; /* keys */
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_trqfvmjtt4sij l8; /* refs */
    gm_t0uuhaljsf410 l9; /* <tmp> */
    gm_tdmi4fk9nhqa7 l10; /* <tmp> */
    gm_tfeab279c16rr l15; /* <tmp> */
    gm_t9bmh80edou9c l16; /* <tmp> */
    gm_t2jeshnsn30ep l20; /* dep */
    gm_ttu09lij36f8c l26; /* reference */
    gm_tu1089hhijadf l30; /* v */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_cache_command_7uqmos8 {
    gm_frame h;
    gm_tr8dvlu9ik4gj l9; /* p */
    gm_t9j922kfqcdi9 l15; /* report */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_contains_a7cccd4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pe7uj7v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fail_o80k3rv {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_fetch_l6q1k6p {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_trqfvmjtt4sij l4; /* given */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_find_root_kjnm6hf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_for_body_0m0ntf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_flag_4n9tu40 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_package_command_ufjt5rp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g9fqnp2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_package_hf828qc {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* p */
    gm_b16_8 l5; /* dir */
    uint8_t l11; /* non_empty */
    uint8_t l12; /* missing */
    gm_tecsjmmgeeh52 l22; /* report */
    gm_t715ajl6o1ipo ret;
    gm_b0_1 w0;
};
struct gm_fr_newest_version_dsisujv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* tags */
    gm_tom1ad5bcqgi1 l3; /* best */
    gm_tu1089hhijadf l7; /* v */
    gm_tu1089hhijadf l8; /* b */
    gm_tis0irsd01rfp ret;
};
struct gm_fr_parse_args_7blm2mp {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t5detta1b4gnk ret;
};
struct gm_fr_relative_path_3klciqu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* f */
    gm_trqfvmjtt4sij l3; /* t */
    gm_trqfvmjtt4sij l5; /* parts */
    gm_b16_8 ret;
};
struct gm_fr_remove_bar6kga {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_root_of_cluda11 {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_run_command_s4fhqaq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_update_oi0ok0i {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_usage_failure_cgg6v1t {
    gm_frame h;
    gm_t5detta1b4gnk l0; /* r */
    gm_t77ebp3ogdble ret;
};
struct gm_fr_value_of_0km7vvp {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_04lv2cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_cache_command_7uqmos8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_clean_97pd952(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t553jgmqd1tlr a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_contains_a7cccd4(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pe7uj7v(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_suo216n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_fail_o80k3rv_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fetch_l6q1k6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_27unvt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6dqa07h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6ta43n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e2683lk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gl2aoqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_is5aam9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_udqe6i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ujusd29(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_has_flag_4n9tu40(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g9fqnp2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_all_q6phjan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_new_package_hf828qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_newest_version_dsisujv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_parse_args_7blm2mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_parse_int_8aueeu2(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_2tc790g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_text_g0qbj1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_relative_path_3klciqu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_remote_tags_rssa7s7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_remove_bar6kga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_root_of_cluda11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sanitize_name_3i9pjsp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scaffold_ansfc6m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_summary_s3h16om(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_sync_ht164ep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_update_oi0ok0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t77ebp3ogdble gm_f_usage_failure_cgg6v1t_c(gm_t5detta1b4gnk a0);
__attribute__((aligned(4))) void gm_f_value_of_0km7vvp(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_value_of_0km7vvp_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_write_atomic_v74ncu3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf70j0q3sgt1(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cftc9ncjfm7[16] __attribute__((aligned(8)));
extern uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
extern void *const gm_hl87701o8eo1[];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s2n0v309tod60[70];
extern const uint8_t gm_s2n3d0ogcjggg[14];
extern const uint8_t gm_s30cnsbbn0c4o[4];
extern const uint8_t gm_s3lbmpmt95hvh[7];
extern const uint8_t gm_s3v0kr2fdpfld[5];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s4uu8ali3icr8[40];
extern const uint8_t gm_s5fgj0n2t69gl[8];
extern const uint8_t gm_s6pnfrub0rj1t[6];
extern const uint8_t gm_s7ch91qn11t34[25];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7mqv7hf4fso0[27];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s9m6kd6gec368[14];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_saicih5tjo4fl[43];
extern const uint8_t gm_sankagtb3ecgn[58];
extern const uint8_t gm_saqbkgqthogiq[30];
extern const uint8_t gm_sb9fu6ufbt9g1[17];
extern const uint8_t gm_sbcpd47bv0ru9[63];
extern const uint8_t gm_sbvsnnbg6vc88[23];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se39cci44d9rt[35];
extern const uint8_t gm_se48mcej0nimj[16];
extern const uint8_t gm_seo2gq6ouvlf6[59];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgu3j1te831tl[24];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sivh46n0690a4[4];
extern const uint8_t gm_sjgng334veu6f[8];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slrtg04n254p4[19];
extern const uint8_t gm_slskjtl6alfrd[6];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_smsc5gshqt6cd[20];
extern const uint8_t gm_snboa22b96bbc[4];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snmm68ee1rq48[41];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_snvisme5hp379[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so6e0kl9gkq21[46];
extern const uint8_t gm_so87vf8a6o4mm[11];
extern const uint8_t gm_so9n4mmd960c7[6];
extern const uint8_t gm_sq99f4u8gmi6n[28];
extern const uint8_t gm_sqljkj8r253om[7];
extern const uint8_t gm_sr43s52rmecnf[33];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccr53ejqr1dde[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgao1ir81l50[14];
extern const uint8_t gm_ss9d51lhe1orq[46];
extern const uint8_t gm_sshhi6fn3vfaa[45];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_suass5sid4b6e[5];
extern const uint8_t gm_sueadt05fevtm[18];
extern const uint8_t gm_suq3bgh3anb87[58];
extern const uint8_t gm_svdospumf3o67[14];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pe7uj7v(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pe7uj7v F;
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
inline __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fail_o80k3rv F;
    gm_b16_8 gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_t3ujhu6qph7de t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 8), t1);
    F.ret = t2;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_name_em7v8lv F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tl7hpf5nlilqq t2;
    gm_f_split_cbk4ks8((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl7hpf5nlilqq t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&t3) + 16) + 0));
    gm_b16_8 t5 = t4;
    void * t6 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)));
    uint64_t t7 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
    uint64_t t8 = (t7 - 1);
    gm_th2lqbo77pvsc t9;
    if (t8 < t7) {
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t6 + t8 * 16));
    gm_th2lqbo77pvsc t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[16] = 1;
        t9 = t11;
    } else {
    gm_th2lqbo77pvsc t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_kjnm6hf F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_canonicalize_epo7cln((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[16] != 0)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 11, 8);
me7:;
        if (!t6) goto ie2;
        {
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13 = gm_l0;
        gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t13);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_2kkkskr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t5 = t4;
        gm_t5j14m8ru132p t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_t5j14m8ru132p t7 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)1ULL)));
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t1, t2);
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
inline __attribute__((aligned(4))) void gm_f_list_g9fqnp2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g9fqnp2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_pe7uj7v();
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
__attribute__((aligned(4))) void gm_f_add_04lv2cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_add_04lv2cc F;
    gm_b16_8 gm_l0; /* args */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* problem */
    gm_b16_8 gm_l6; /* e */
    gm_b16_8 gm_l7; /* root */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* k */
    uint64_t gm_l14; /* sources */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* k */
    gm_b16_8 gm_l21; /* url */
    gm_b16_8 gm_l22; /* path */
    gm_b16_8 gm_l23; /* abs */
    gm_b16_8 gm_l24; /* url */
    gm_b16_8 gm_l25; /* problem */
    gm_b16_8 gm_l27; /* k */
    gm_b16_8 gm_l28; /* v */
    gm_b16_8 gm_l29; /* e */
    gm_b16_8 gm_l31; /* manifest_path */
    gm_b16_8 gm_l32; /* e */
    gm_b16_8 gm_l33; /* old_text */
    gm_tf1bjg81u0glv gm_l34; /* e */
    gm_b16_8 gm_l35; /* e */
    gm_b16_8 gm_l36; /* new_text */
    gm_tf1bjg81u0glv gm_l37; /* e */
    gm_b16_8 gm_l38; /* e */
    gm_b16_8 gm_l39; /* e */
    gm_b16_8 gm_l40; /* e2 */
    GM_STACK_CHECK(&F, 10857u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 16, t3);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 48, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 64, t6);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 80, t7);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 96, t8);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 112, t9);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sfvspsnv23gbt));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 128, t10);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 144, t11);
    }
    {
    }
    {
        gm_b16_8 t12 = gm_l0;
        void * t13 = ((uint8_t *)&F.l1);
        if ((uintptr_t)0 > (uintptr_t)10 || (uintptr_t)10 > (uintptr_t)10) gm_trap_slice((uintptr_t)0, (uintptr_t)10, 10, gm_srccr53ejqr1dde, 189, 45);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t13 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)10 - (uintptr_t)0));
        gm_b16_8 t15 = t14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        {
            gm_b0_8 t18;
            gm_l2 = t18;
        }
        void * t19 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srccr53ejqr1dde, 189, 55);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t19 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
        gm_b16_8 t21 = t20;
        gm_t5detta1b4gnk t22;
        gm_f_parse_args_7blm2mp((void *)&t22, (void *)NULL, cx, NULL, t12, t16, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t77ebp3ogdble t23;
        gm_f_usage_failure_cgg6v1t((void *)&t23, &t22);
        gm_t77ebp3ogdble t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) {
            gm_t3ujhu6qph7de t25 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t24) + 8));
            gm_t715ajl6o1ipo t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            ((uint8_t *)&t26)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t26 + 8), t25);
            F.ret = t26;
            goto gm_fret;
        }
        gm_tr8dvlu9ik4gj t27 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t24) + 8));
        F.l3 = t27;
    }
    {
        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 0) + 0) + 8);
        uint8_t t30 = (!(t29 == ((uint64_t)1ULL)));
        if (!t30) goto ie28;
        {
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s4uu8ali3icr8));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(39));
            gm_t3ujhu6qph7de t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t32);
            gm_t715ajl6o1ipo t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            ((uint8_t *)&t34)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t34 + 8), t33);
            gm_t715ajl6o1ipo t35 = t34;
            uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
            F.ret = t34;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 0) + 0) + 8);
        if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srccr53ejqr1dde, 191, 13);
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l3) + 0) + 0)) + t37 * 16));
        gm_l4 = t39;
    }
    {
        gm_b16_8 t41 = gm_l4;
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s8f9rv6hrr024));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(10));
        gm_th2lqbo77pvsc t43;
        gm_f_name_problem_6r4mp11((void *)&t43, (void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t44 = t43;
        if (!((((uint8_t *)&t44))[16] != 0)) goto ie40;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        gm_l5 = t45;
        {
            gm_b16_8 t47 = gm_l5;
            gm_t3ujhu6qph7de t48;
            gm_f_fail_o80k3rv((void *)&t48, t47);
            gm_t715ajl6o1ipo t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            ((uint8_t *)&t49)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t49 + 8), t48);
            gm_t715ajl6o1ipo t50 = t49;
            uint8_t t51 = ((((uint8_t *)&t50))[0] != 0);
            F.ret = t49;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        void * t52 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t54;
        gm_f_value_of_0km7vvp((void *)&t54, t52, t53);
        gm_th2lqbo77pvsc t55 = t54;
        gm_b16_8 t56;
        if (((((uint8_t *)&t55))[16] != 0)) {
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 0));
            t56 = t57;
        } else {
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(1));
            t56 = t58;
        }
        gm_t3ujhu6qph7de t59;
        gm_f_root_of_cluda11((void *)&t59, (void *)NULL, cx, NULL, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t60 = t59;
        gm_b16_8 t61;
        if (!((((uint8_t *)&t60))[0] != 0)) {
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
            t61 = t62;
        } else {
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
            gm_l6 = t63;
            gm_b16_8 t65 = gm_l6;
            gm_t3ujhu6qph7de t66;
            gm_f_fail_o80k3rv((void *)&t66, t65);
            gm_t715ajl6o1ipo t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            ((uint8_t *)&t67)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t67 + 8), t66);
            gm_t715ajl6o1ipo t68 = t67;
            uint8_t t69 = ((((uint8_t *)&t68))[0] != 0);
            F.ret = t67;
            goto gm_fret;
        }
        gm_l7 = t61;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s420tm9ua7ggg));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 0, t71);
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s0jho24omhfui));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 16, t72);
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sf9ilivr4c86m));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 32, t73);
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sla3alt5340mf));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 48, t74);
        }
        {
            {
                {
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 0, t77);
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 16, t78);
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 32, t79);
                    gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 48));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 48, t80);
                }
                void * t81 = ((uint8_t *)&F.l10);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srccr53ejqr1dde, 195, 5);
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(((uint8_t *)t81 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_b16_8 t83 = t82;
                gm_l11 = t83;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t85 = gm_l12;
                    uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t87 = (t85 < t86);
                    if (!t87) goto lb84;
                    {
                        uint64_t t90 = gm_l12;
                        uint64_t t91 = (uintptr_t)t90;
                        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t91 * 16));
                        gm_l13 = t95;
                        {
                            void * t98 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t99 = gm_l13;
                            gm_th2lqbo77pvsc t100;
                            gm_f_value_of_0km7vvp((void *)&t100, t98, t99);
                            gm_th2lqbo77pvsc t101 = t100;
                            uint8_t t102;
                            if (((((uint8_t *)&t101))[16] != 0)) goto mn104;
                            t102 = ((uint8_t)0);
                            goto me103;
mn104:;
                            t102 = ((uint8_t)1);
                            goto me103;
mn105:;
                            gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 196, 12);
me103:;
                            if (!t102) goto ie97;
                            {
                                void * t107 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t108 = gm_l13;
                                gm_tftb1odc739t5 t109 = gm_f_push_3ef746b(t107, t108);
                                gm_tftb1odc739t5 t110 = t109;
                                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srccr53ejqr1dde, 196, 42);
                                gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
                            }
                            goto ix97;
ie97:;
ix97:;
                        }
                        goto me93;
mn94:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 195, 5);
me93:;
                    }
lc84:;
                    {
                        uint64_t t113 = gm_l12;
                        uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                        uint64_t t116 = r114;
                        gm_l12 = t116;
                    }
                }
lb84:;
            }
        }
    }
    {
        gm_l14 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t118;
            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_smimi08sfrg0n));
            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&F.l15 + 0, t118);
            gm_b16_8 t119;
            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s0k6ioi39f878));
            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l15 + 16, t119);
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sa2h9vonan0ma));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l15 + 32, t120);
        }
        {
            {
                {
                    gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 0, t123);
                    gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 16, t124);
                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 32, t125);
                }
                void * t126 = ((uint8_t *)&F.l16);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srccr53ejqr1dde, 199, 5);
                gm_b16_8 t127;
                GM_ST(void *, (uint8_t *)&t127, (void *)(((uint8_t *)t126 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t128 = t127;
                gm_l17 = t128;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t130 = gm_l18;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t132 = (t130 < t131);
                    if (!t132) goto lb129;
                    {
                        uint64_t t135 = gm_l18;
                        uint64_t t136 = (uintptr_t)t135;
                        uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t136 * 16));
                        gm_l19 = t140;
                        {
                            void * t143 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t144 = gm_l19;
                            gm_th2lqbo77pvsc t145;
                            gm_f_value_of_0km7vvp((void *)&t145, t143, t144);
                            gm_th2lqbo77pvsc t146 = t145;
                            uint8_t t147;
                            if (((((uint8_t *)&t146))[16] != 0)) goto mn149;
                            t147 = ((uint8_t)0);
                            goto me148;
mn149:;
                            t147 = ((uint8_t)1);
                            goto me148;
mn150:;
                            gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 200, 12);
me148:;
                            if (!t147) goto ie142;
                            {
                                uint64_t t152 = gm_l14;
                                uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                                if (o154) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 200, 42);
                                uint64_t t155 = r153;
                                gm_l14 = t155;
                            }
                            goto ix142;
ie142:;
ix142:;
                        }
                        goto me138;
mn139:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 199, 5);
me138:;
                    }
lc129:;
                    {
                        uint64_t t157 = gm_l18;
                        uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                        uint64_t t160 = r158;
                        gm_l18 = t160;
                    }
                }
lb129:;
            }
        }
    }
    {
        uint64_t t162 = gm_l14;
        uint8_t t163 = (t162 > ((uint64_t)1ULL));
        if (!t163) goto ie161;
        {
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(gm_so6e0kl9gkq21));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(45));
            gm_t3ujhu6qph7de t166;
            GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 8), t165);
            gm_t715ajl6o1ipo t167;
            GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
            ((uint8_t *)&t167)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t167 + 8), t166);
            gm_t715ajl6o1ipo t168 = t167;
            uint8_t t169 = ((((uint8_t *)&t168))[0] != 0);
            F.ret = t167;
            goto gm_fret;
        }
        goto ix161;
ie161:;
ix161:;
    }
    {
        void * t171 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t172;
        GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(6));
        gm_th2lqbo77pvsc t173;
        gm_f_value_of_0km7vvp((void *)&t173, t171, t172);
        gm_th2lqbo77pvsc t174 = t173;
        uint8_t t175;
        if (((((uint8_t *)&t174))[16] != 0)) goto mn177;
        t175 = ((uint8_t)0);
        goto me176;
mn177:;
        t175 = ((uint8_t)1);
        goto me176;
mn178:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 203, 8);
me176:;
        if (!t175) goto ie170;
        void * t179 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t180;
        GM_ST(void *, (uint8_t *)&t180, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t181;
        gm_f_value_of_0km7vvp((void *)&t181, t179, t180);
        gm_th2lqbo77pvsc t182 = t181;
        uint8_t t183;
        if (((((uint8_t *)&t182))[16] != 0)) goto mn185;
        t183 = ((uint8_t)1);
        goto me184;
mn185:;
        t183 = ((uint8_t)0);
        goto me184;
mn186:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 203, 47);
me184:;
        if (!t183) goto ie170;
        {
            gm_b16_8 t188;
            GM_ST(void *, (uint8_t *)&t188, (void *)(gm_s7ch91qn11t34));
            GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(24));
            gm_t3ujhu6qph7de t189;
            GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t189 + 8), t188);
            gm_t715ajl6o1ipo t190;
            GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
            ((uint8_t *)&t190)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t190 + 8), t189);
            gm_t715ajl6o1ipo t191 = t190;
            uint8_t t192 = ((((uint8_t *)&t191))[0] != 0);
            F.ret = t190;
            goto gm_fret;
        }
        goto ix170;
ie170:;
ix170:;
    }
    {
        gm_b16_8 t193;
        GM_ST(void *, (uint8_t *)&t193, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(0));
        gm_t2jeshnsn30ep t194;
        memset(&t194, 0, sizeof t194);
        GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t194 + 8), t193);
        F.l20 = t194;
    }
    {
        void * t196 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t197;
        GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t198;
        gm_f_value_of_0km7vvp((void *)&t198, t196, t197);
        gm_th2lqbo77pvsc t199 = t198;
        if (!((((uint8_t *)&t199))[16] != 0)) goto ie195;
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
        gm_l21 = t200;
        {
            uint64_t t203 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
            uint8_t t204 = (t203 > ((uint64_t)0ULL));
            if (!t204) goto ie202;
            {
                gm_b16_8 t206;
                GM_ST(void *, (uint8_t *)&t206, (void *)(gm_suq3bgh3anb87));
                GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(57));
                gm_t3ujhu6qph7de t207;
                GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t207 + 8), t206);
                gm_t715ajl6o1ipo t208;
                GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
                ((uint8_t *)&t208)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t208 + 8), t207);
                gm_t715ajl6o1ipo t209 = t208;
                uint8_t t210 = ((((uint8_t *)&t209))[0] != 0);
                F.ret = t208;
                goto gm_fret;
            }
            goto ix202;
ie202:;
ix202:;
        }
        {
            gm_tt008mgut8glb t211;
            gm_b16_8 t212 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t211 + 0, t212);
            void * t213 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t214;
            GM_ST(void *, (uint8_t *)&t214, (void *)(gm_sd1smnnii4cig));
            GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(6));
            gm_f_value_of_0km7vvp((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&t211 + 16)), t213, t214);
            void * t215 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t216;
            GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sfvspsnv23gbt));
            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(6));
            gm_f_value_of_0km7vvp((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&t211 + 40)), t215, t216);
            gm_t2jeshnsn30ep t217;
            GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t217 + 72, 0);
            GM_ST(uint64_t, (uint8_t *)&t217 + 80, 0);
            GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tt008mgut8glb, ((uint8_t *)&t217 + 8), t211);
            F.l20 = t217;
        }
        goto ix195;
ie195:;
        void * t219 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t220;
        GM_ST(void *, (uint8_t *)&t220, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(4));
        gm_th2lqbo77pvsc t221;
        gm_f_value_of_0km7vvp((void *)&t221, t219, t220);
        gm_th2lqbo77pvsc t222 = t221;
        if (!((((uint8_t *)&t222))[16] != 0)) goto ie218;
        gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
        gm_l22 = t223;
        {
            uint8_t t226 = 0;
            uint64_t t227 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
            uint8_t t228 = (t227 > ((uint64_t)0ULL));
            if (!t228) {
                void * t229 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(gm_sfvspsnv23gbt));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(6));
                gm_th2lqbo77pvsc t231;
                gm_f_value_of_0km7vvp((void *)&t231, t229, t230);
                gm_th2lqbo77pvsc t232 = t231;
                uint8_t t233;
                if (((((uint8_t *)&t232))[16] != 0)) goto mn235;
                t233 = ((uint8_t)0);
                goto me234;
mn235:;
                t233 = ((uint8_t)1);
                goto me234;
mn236:;
                gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 211, 34);
me234:;
                t226 = t233;
            } else t226 = 1;
            if (!t226) goto ie225;
            {
                gm_b16_8 t238;
                GM_ST(void *, (uint8_t *)&t238, (void *)(gm_s2n0v309tod60));
                GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(69));
                gm_t3ujhu6qph7de t239;
                GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t239 + 8), t238);
                gm_t715ajl6o1ipo t240;
                GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                ((uint8_t *)&t240)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t240 + 8), t239);
                gm_t715ajl6o1ipo t241 = t240;
                uint8_t t242 = ((((uint8_t *)&t241))[0] != 0);
                F.ret = t240;
                goto gm_fret;
            }
            goto ix225;
ie225:;
ix225:;
        }
        {
            gm_b16_8 t243 = gm_l22;
            gm_th2lqbo77pvsc t244;
            gm_f_canonicalize_epo7cln((void *)&t244, (void *)NULL, cx, NULL, t243);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t245 = t244;
            gm_b16_8 t246;
            if (((((uint8_t *)&t245))[16] != 0)) {
                gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&t245) + 0));
                t246 = t247;
            } else {
                gm_b16_8 t249 = gm_l22;
                gm_tnpifv20irnph t250;
                GM_ST(gm_b16_8, ((uint8_t *)&t250 + 0), t249);
                gm_tabb710d9mq2e t251;
                gm_f_format_udqe6i2((void *)&t251, (void *)NULL, cx, NULL, t250);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t252 = t251;
                if (((((uint8_t *)&t252))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t252) + 8, gm_srccr53ejqr1dde, 215, 26);
                gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&t252) + 8));
                gm_b16_8 t254 = t253;
                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                gm_t3ujhu6qph7de t256;
                gm_f_fail_o80k3rv((void *)&t256, t255);
                gm_t715ajl6o1ipo t257;
                GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                ((uint8_t *)&t257)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t257 + 8), t256);
                gm_t715ajl6o1ipo t258 = t257;
                uint8_t t259 = ((((uint8_t *)&t258))[0] != 0);
                F.ret = t257;
                goto gm_fret;
            }
            gm_l23 = t246;
        }
        {
            gm_b16_8 t260 = gm_l7;
            gm_b16_8 t261 = gm_l23;
            gm_b16_8 t262 = gm_f_relative_path_3klciqu((void *)NULL, cx, NULL, t260, t261);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2jeshnsn30ep t263;
            memset(&t263, 0, sizeof t263);
            GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t263 + 8), t262);
            F.l20 = t263;
        }
        goto ix218;
ie218:;
        void * t265 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t266;
        GM_ST(void *, (uint8_t *)&t266, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t267;
        gm_f_value_of_0km7vvp((void *)&t267, t265, t266);
        gm_th2lqbo77pvsc t268 = t267;
        if (!((((uint8_t *)&t268))[16] != 0)) goto ie264;
        gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
        gm_l24 = t269;
        {
            gm_b16_8 t272 = gm_l24;
            gm_th2lqbo77pvsc t273;
            gm_f_url_problem_38kdh3a((void *)&t273, (void *)NULL, cx, NULL, t272);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t274 = t273;
            if (!((((uint8_t *)&t274))[16] != 0)) goto ie271;
            gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&t274));
            gm_l25 = t275;
            {
                gm_b16_8 t277 = gm_l25;
                gm_t3ujhu6qph7de t278;
                GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t278 + 8), t277);
                gm_t715ajl6o1ipo t279;
                GM_ST(uint8_t, (uint8_t *)&t279 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t279 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t279 + 4, 0);
                ((uint8_t *)&t279)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t279 + 8), t278);
                gm_t715ajl6o1ipo t280 = t279;
                uint8_t t281 = ((((uint8_t *)&t280))[0] != 0);
                F.ret = t279;
                goto gm_fret;
            }
            goto ix271;
ie271:;
ix271:;
        }
        {
            uint64_t t283 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
            uint8_t t284 = (t283 > ((uint64_t)1ULL));
            if (!t284) goto ie282;
            {
                gm_b16_8 t286;
                GM_ST(void *, (uint8_t *)&t286, (void *)(gm_sankagtb3ecgn));
                GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(57));
                gm_t3ujhu6qph7de t287;
                GM_ST(uint8_t, (uint8_t *)&t287 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t287 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t287 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t287 + 8), t286);
                gm_t715ajl6o1ipo t288;
                GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                ((uint8_t *)&t288)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t288 + 8), t287);
                gm_t715ajl6o1ipo t289 = t288;
                uint8_t t290 = ((((uint8_t *)&t289))[0] != 0);
                F.ret = t288;
                goto gm_fret;
            }
            goto ix282;
ie282:;
ix282:;
        }
        {
            gm_ttu09lij36f8c t291;
            memset(&t291, 0, sizeof t291);
            F.l26 = t291;
        }
        {
            uint64_t t293 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
            uint8_t t294 = ((t293 == ((uint64_t)1ULL)));
            if (!t294) goto ie292;
            {
                uint64_t t296 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t297 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                if (t296 >= (uintptr_t)(t297)) gm_trap_index(t296, t297, gm_srccr53ejqr1dde, 223, 18);
                gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t296 * 16));
                gm_l27 = t298;
            }
            {
                void * t299 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t300 = gm_l27;
                gm_th2lqbo77pvsc t301;
                gm_f_value_of_0km7vvp((void *)&t301, t299, t300);
                gm_th2lqbo77pvsc t302 = t301;
                if (!((((uint8_t *)&t302))[16] != 0)) gm_trap("unwrap of null", gm_srccr53ejqr1dde, 224, 18);
                gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                gm_l28 = t303;
            }
            {
                uint8_t t305 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l27)), gm_sla3alt5340mf, 7) == 0));
                if (!t305) goto ie304;
                {
                    {
                        gm_b16_8 t308 = gm_l28;
                        gm_b16_8 t309 = gm_f_trim_ascii_c9j36d1(t308);
                        gm_t62efos6ssciq t310;
                        gm_f_parse_version_req_alipvfi((void *)&t310, (void *)NULL, cx, NULL, t309);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t62efos6ssciq t311 = t310;
                        gm_tin97tt4ud0s1 t312;
                        if (!((((uint8_t *)&t311))[0] != 0)) {
                            gm_tin97tt4ud0s1 t313 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)&t311) + 8));
                            t312 = t313;
                        } else {
                            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&t311) + 8));
                            gm_l29 = t314;
                            gm_t6j6smj7qoctu t316;
                            gm_b16_8 t317 = gm_l28;
                            GM_ST(gm_b16_8, (uint8_t *)&t316 + 0, t317);
                            gm_b16_8 t318 = gm_l29;
                            GM_ST(gm_b16_8, (uint8_t *)&t316 + 16, t318);
                            gm_tabb710d9mq2e t319;
                            gm_f_format_pah1p62((void *)&t319, (void *)NULL, cx, NULL, &t316);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tabb710d9mq2e t320 = t319;
                            if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 8, gm_srccr53ejqr1dde, 227, 34);
                            gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&t320) + 8));
                            gm_b16_8 t322 = t321;
                            gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
                            gm_t3ujhu6qph7de t324;
                            gm_f_fail_o80k3rv((void *)&t324, t323);
                            gm_t715ajl6o1ipo t325;
                            GM_ST(uint8_t, (uint8_t *)&t325 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t325 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
                            ((uint8_t *)&t325)[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t325 + 8), t324);
                            gm_t715ajl6o1ipo t326 = t325;
                            uint8_t t327 = ((((uint8_t *)&t326))[0] != 0);
                            F.ret = t325;
                            goto gm_fret;
                        }
                    }
                }
                goto ix304;
ie304:;
ix304:;
            }
            {
                gm_t6j6smj7qoctu t328;
                gm_b16_8 t329 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t328 + 0, t329);
                gm_b16_8 t330 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t328 + 16, t330);
                gm_ttu09lij36f8c t331;
                GM_ST(uint8_t, (uint8_t *)&t331 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t331 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t331 + 36, 0);
                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t331), t328);
                ((uint8_t *)&t331)[32] = 1;
                F.l26 = t331;
            }
            goto ix292;
ie292:;
            gm_b16_8 t333 = gm_l24;
            gm_tis0irsd01rfp t334;
            gm_f_newest_version_dsisujv((void *)&t334, (void *)NULL, cx, NULL, t333);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tis0irsd01rfp t335 = t334;
            if (((((uint8_t *)&t335))[0] != 0)) {
                gm_t3ujhu6qph7de t336 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t335) + 8));
                gm_t715ajl6o1ipo t337;
                GM_ST(uint8_t, (uint8_t *)&t337 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t337 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
                ((uint8_t *)&t337)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t337 + 8), t336);
                F.ret = t337;
                goto gm_fret;
            }
            gm_tom1ad5bcqgi1 t338 = GM_LD(gm_tom1ad5bcqgi1, (((uint8_t *)&t335) + 8));
            gm_tom1ad5bcqgi1 t339 = t338;
            if (!((((uint8_t *)&t339))[56] != 0)) goto ie332;
            gm_tu1089hhijadf t340 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&t339));
            F.l30 = t340;
            {
                gm_t6j6smj7qoctu t342;
                gm_b16_8 t343;
                GM_ST(void *, (uint8_t *)&t343, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(7));
                GM_ST(gm_b16_8, (uint8_t *)&t342 + 0, t343);
                gm_b16_8 t344 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l30)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t345;
                GM_ST(gm_b16_8, ((uint8_t *)&t345 + 0), t344);
                gm_tabb710d9mq2e t346;
                gm_f_format_dhjqjjt((void *)&t346, (void *)NULL, cx, NULL, t345);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t347 = t346;
                if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 8, gm_srccr53ejqr1dde, 232, 49);
                gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&t347) + 8));
                gm_b16_8 t349 = t348;
                gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&t349));
                GM_ST(gm_b16_8, (uint8_t *)&t342 + 16, t350);
                gm_ttu09lij36f8c t351;
                GM_ST(uint8_t, (uint8_t *)&t351 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t351 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t351 + 36, 0);
                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t351), t342);
                ((uint8_t *)&t351)[32] = 1;
                F.l26 = t351;
            }
            goto ix332;
ie332:;
ix332:;
ix292:;
        }
        {
            gm_t03ccdabio1fj t352;
            gm_b16_8 t353 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 0, t353);
            memcpy((uint8_t *)&t352 + 16, ((uint8_t *)&F.l26), 40);
            void * t354 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t355;
            GM_ST(void *, (uint8_t *)&t355, (void *)(gm_sfvspsnv23gbt));
            GM_ST(uintptr_t, (uint8_t *)&t355 + 8, (uintptr_t)(6));
            gm_f_value_of_0km7vvp((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&t352 + 56)), t354, t355);
            gm_t2jeshnsn30ep t356;
            GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t356 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t03ccdabio1fj, ((uint8_t *)&t356 + 8), t352);
            F.l20 = t356;
        }
        goto ix264;
ie264:;
        {
            gm_b16_8 t358;
            GM_ST(void *, (uint8_t *)&t358, (void *)(gm_seo2gq6ouvlf6));
            GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(58));
            gm_t3ujhu6qph7de t359;
            GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t359 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t359 + 8), t358);
            gm_t715ajl6o1ipo t360;
            GM_ST(uint8_t, (uint8_t *)&t360 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t360 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t360 + 4, 0);
            ((uint8_t *)&t360)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t360 + 8), t359);
            gm_t715ajl6o1ipo t361 = t360;
            uint8_t t362 = ((((uint8_t *)&t361))[0] != 0);
            F.ret = t360;
            goto gm_fret;
        }
ix264:;
ix218:;
ix195:;
    }
    {
        gm_b16_8 t363 = gm_l7;
        gm_b16_8 t364;
        GM_ST(void *, (uint8_t *)&t364, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t364 + 8, (uintptr_t)(10));
        gm_b16_8 t365 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t363, t364);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l31 = t365;
    }
    {
        gm_b16_8 t366 = gm_l31;
        gm_t3ujhu6qph7de t367;
        gm_f_read_text_g0qbj1t((void *)&t367, (void *)NULL, cx, NULL, t366);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t368 = t367;
        gm_b16_8 t369;
        if (!((((uint8_t *)&t368))[0] != 0)) {
            gm_b16_8 t370 = GM_LD(gm_b16_8, (((uint8_t *)&t368) + 8));
            t369 = t370;
        } else {
            gm_b16_8 t371 = GM_LD(gm_b16_8, (((uint8_t *)&t368) + 8));
            gm_l32 = t371;
            gm_b16_8 t373 = gm_l32;
            gm_t3ujhu6qph7de t374;
            gm_f_fail_o80k3rv((void *)&t374, t373);
            gm_t715ajl6o1ipo t375;
            GM_ST(uint8_t, (uint8_t *)&t375 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t375 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t375 + 4, 0);
            ((uint8_t *)&t375)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t375 + 8), t374);
            gm_t715ajl6o1ipo t376 = t375;
            uint8_t t377 = ((((uint8_t *)&t376))[0] != 0);
            F.ret = t375;
            goto gm_fret;
        }
        gm_l33 = t369;
    }
    {
        {
            gm_b16_8 t379 = gm_l33;
            gm_tjgmtdrk88t7m t380;
            gm_f_parse_manifest_b61s3ln((void *)&t380, (void *)NULL, cx, NULL, t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tjgmtdrk88t7m t381 = t380;
            gm_tm43kqe3e8b4a t382;
            if (!((((uint8_t *)&t381))[0] != 0)) {
                gm_tm43kqe3e8b4a t383 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t381) + 8));
                t382 = t383;
            } else {
                gm_tf1bjg81u0glv t384 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t381) + 8));
                gm_l34 = t384;
                gm_tq3m5qgvkpje8 t386;
                gm_b16_8 t387 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t386 + 0, t387);
                uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 0));
                GM_ST(uint64_t, (uint8_t *)&t386 + 16, t388);
                gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t386 + 24, t389);
                gm_tabb710d9mq2e t390;
                gm_f_format_n6eprd4((void *)&t390, (void *)NULL, cx, NULL, &t386);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t391 = t390;
                if (((((uint8_t *)&t391))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t391) + 8, gm_srccr53ejqr1dde, 241, 22);
                gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)&t391) + 8));
                gm_b16_8 t393 = t392;
                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                gm_t3ujhu6qph7de t395;
                gm_f_fail_o80k3rv((void *)&t395, t394);
                gm_t715ajl6o1ipo t396;
                GM_ST(uint8_t, (uint8_t *)&t396 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t396 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t396 + 4, 0);
                ((uint8_t *)&t396)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t396 + 8), t395);
                gm_t715ajl6o1ipo t397 = t396;
                uint8_t t398 = ((((uint8_t *)&t397))[0] != 0);
                F.ret = t396;
                goto gm_fret;
            }
        }
    }
    {
        gm_b16_8 t399 = gm_l33;
        gm_b16_8 t400 = gm_l4;
        gm_t3ujhu6qph7de t401;
        gm_f_add_dependency_jmjffrp((void *)&t401, (void *)NULL, cx, NULL, t399, t400, &(*(gm_t2jeshnsn30ep *)((uint8_t *)&F.l20)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t402 = t401;
        gm_b16_8 t403;
        if (!((((uint8_t *)&t402))[0] != 0)) {
            gm_b16_8 t404 = GM_LD(gm_b16_8, (((uint8_t *)&t402) + 8));
            t403 = t404;
        } else {
            gm_b16_8 t405 = GM_LD(gm_b16_8, (((uint8_t *)&t402) + 8));
            gm_l35 = t405;
            gm_b16_8 t407 = gm_l35;
            gm_t3ujhu6qph7de t408;
            gm_f_fail_o80k3rv((void *)&t408, t407);
            gm_t715ajl6o1ipo t409;
            GM_ST(uint8_t, (uint8_t *)&t409 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t409 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t409 + 4, 0);
            ((uint8_t *)&t409)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t409 + 8), t408);
            gm_t715ajl6o1ipo t410 = t409;
            uint8_t t411 = ((((uint8_t *)&t410))[0] != 0);
            F.ret = t409;
            goto gm_fret;
        }
        gm_l36 = t403;
    }
    {
        {
            gm_b16_8 t413 = gm_l36;
            gm_tjgmtdrk88t7m t414;
            gm_f_parse_manifest_b61s3ln((void *)&t414, (void *)NULL, cx, NULL, t413);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tjgmtdrk88t7m t415 = t414;
            gm_tm43kqe3e8b4a t416;
            if (!((((uint8_t *)&t415))[0] != 0)) {
                gm_tm43kqe3e8b4a t417 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t415) + 8));
                t416 = t417;
            } else {
                gm_tf1bjg81u0glv t418 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t415) + 8));
                gm_l37 = t418;
                gm_tq3m5qgvkpje8 t420;
                gm_b16_8 t421 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t420 + 0, t421);
                uint64_t t422 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 0));
                GM_ST(uint64_t, (uint8_t *)&t420 + 16, t422);
                gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t420 + 24, t423);
                gm_tabb710d9mq2e t424;
                gm_f_format_n6eprd4((void *)&t424, (void *)NULL, cx, NULL, &t420);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t425 = t424;
                if (((((uint8_t *)&t425))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t425) + 8, gm_srccr53ejqr1dde, 246, 22);
                gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&t425) + 8));
                gm_b16_8 t427 = t426;
                gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                gm_t3ujhu6qph7de t429;
                gm_f_fail_o80k3rv((void *)&t429, t428);
                gm_t715ajl6o1ipo t430;
                GM_ST(uint8_t, (uint8_t *)&t430 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t430 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t430 + 4, 0);
                ((uint8_t *)&t430)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t430 + 8), t429);
                gm_t715ajl6o1ipo t431 = t430;
                uint8_t t432 = ((((uint8_t *)&t431))[0] != 0);
                F.ret = t430;
                goto gm_fret;
            }
        }
    }
    {
        gm_b16_8 t433 = gm_l31;
        gm_b16_8 t434 = gm_l36;
        gm_tl572d7579hns t435;
        gm_f_write_atomic_v74ncu3((void *)&t435, (void *)NULL, cx, NULL, t433, t434);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t436 = t435;
        gm_b0_1 t437;
        if (!((((uint8_t *)&t436))[0] != 0)) {
            gm_b0_1 t438 = GM_LD(gm_b0_1, (((uint8_t *)&t436) + 8));
            t437 = t438;
        } else {
            gm_b16_8 t439 = GM_LD(gm_b16_8, (((uint8_t *)&t436) + 8));
            gm_l38 = t439;
            gm_b16_8 t441 = gm_l38;
            gm_t3ujhu6qph7de t442;
            gm_f_fail_o80k3rv((void *)&t442, t441);
            gm_t715ajl6o1ipo t443;
            GM_ST(uint8_t, (uint8_t *)&t443 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t443 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t443 + 4, 0);
            ((uint8_t *)&t443)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t443 + 8), t442);
            gm_t715ajl6o1ipo t444 = t443;
            uint8_t t445 = ((((uint8_t *)&t444))[0] != 0);
            F.ret = t443;
            goto gm_fret;
        }
    }
    {
        {
            gm_b16_8 t447 = gm_l7;
            gm_tqs5p4n2tq19v t448;
            GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t448 + 1), ((uint8_t)0));
            gm_tl572d7579hns t449;
            GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t449 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t449 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t449 + 0), ((uint8_t)0ULL));
            gm_t074vrl1hb4f6 t450;
            gm_f_sync_ht164ep((void *)&t450, (void *)NULL, cx, NULL, t447, t448, &t449, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t074vrl1hb4f6 t451 = t450;
            gm_tv37d3dpt2opu t452;
            if (!((((uint8_t *)&t451))[0] != 0)) {
                gm_tv37d3dpt2opu t453 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&t451) + 8));
                t452 = t453;
            } else {
                gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&t451) + 8));
                gm_l39 = t454;
                {
                    gm_b16_8 t456 = gm_l31;
                    gm_b16_8 t457 = gm_l33;
                    gm_tl572d7579hns t458;
                    gm_f_write_atomic_v74ncu3((void *)&t458, (void *)NULL, cx, NULL, t456, t457);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl572d7579hns t459 = t458;
                    gm_b0_1 t460;
                    if (!((((uint8_t *)&t459))[0] != 0)) {
                        gm_b0_1 t461 = GM_LD(gm_b0_1, (((uint8_t *)&t459) + 8));
                        t460 = t461;
                    } else {
                        gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&t459) + 8));
                        gm_l40 = t462;
                        gm_b16_8 t464 = gm_l40;
                        gm_t3ujhu6qph7de t465;
                        gm_f_fail_o80k3rv((void *)&t465, t464);
                        gm_t715ajl6o1ipo t466;
                        GM_ST(uint8_t, (uint8_t *)&t466 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t466 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t466 + 4, 0);
                        ((uint8_t *)&t466)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t466 + 8), t465);
                        gm_t715ajl6o1ipo t467 = t466;
                        uint8_t t468 = ((((uint8_t *)&t467))[0] != 0);
                        F.ret = t466;
                        goto gm_fret;
                    }
                }
                gm_b16_8 t469 = gm_l39;
                gm_t3ujhu6qph7de t470;
                gm_f_fail_o80k3rv((void *)&t470, t469);
                gm_t715ajl6o1ipo t471;
                GM_ST(uint8_t, (uint8_t *)&t471 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t471 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t471 + 4, 0);
                ((uint8_t *)&t471)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t471 + 8), t470);
                gm_t715ajl6o1ipo t472 = t471;
                uint8_t t473 = ((((uint8_t *)&t472))[0] != 0);
                F.ret = t471;
                goto gm_fret;
            }
        }
    }
    {
        gm_b16_8 t474 = gm_l4;
        gm_b16_8 t475 = t474;
        if (gm_pf454qfrpt6r(cx, gm_svdospumf3o67, 13, gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t475), gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sueadt05fevtm, 17, gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t476;
        gm_t715ajl6o1ipo t477;
        GM_ST(uint8_t, (uint8_t *)&t477 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t477 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t477 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t477 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t477 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t477 + 24, 0);
        ((uint8_t *)&t477)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t477 + 8), t476);
        gm_t715ajl6o1ipo t478 = t477;
        uint8_t t479 = ((((uint8_t *)&t478))[0] != 0);
        F.ret = t477;
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
__attribute__((aligned(4))) void gm_f_cache_command_7uqmos8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_cache_command_7uqmos8 F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* sub */
    gm_b16_8 gm_l2; /* rest */
    gm_th2lqbo77pvsc gm_l3; /* dir */
    gm_b0_8 gm_l4; /* <temporary array> */
    gm_b0_8 gm_l5; /* <temporary array> */
    gm_b16_8 gm_l6; /* d */
    gm_tpmdr2hmdhsqr gm_l7; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l8; /* <temporary array> */
    uint64_t gm_l10; /* days */
    gm_b16_8 gm_l11; /* v */
    gm_b16_8 gm_l12; /* d */
    uint64_t gm_l13; /* secs */
    gm_t553jgmqd1tlr gm_l14; /* policy */
    GM_STACK_CHECK(&F, 3842u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sshhi6fn3vfaa));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(44));
            gm_t3ujhu6qph7de t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
            gm_t715ajl6o1ipo t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t8 + 8), t7);
            gm_t715ajl6o1ipo t9 = t8;
            uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srccr53ejqr1dde, 292, 12);
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t11 * 16));
        gm_l1 = t13;
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t16 = (uintptr_t)((uint64_t)1ULL);
        if (t16 > (uintptr_t)t14 || (uintptr_t)t14 > (uintptr_t)t14) gm_trap_slice(t16, (uintptr_t)t14, t14, gm_srccr53ejqr1dde, 293, 13);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t15 + t16 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)((uintptr_t)t14 - t16));
        gm_l2 = t17;
    }
    {
        gm_f_cache_dir_81h90qu((void *)&gm_l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t18;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_snboa22b96bbc, 3) == 0)) goto mn20;
        {
        }
        {
        }
        {
            {
                gm_b16_8 t23 = gm_l2;
                {
                    gm_b0_8 t25;
                    gm_l4 = t25;
                }
                void * t26 = ((uint8_t *)&gm_l4);
                if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srccr53ejqr1dde, 298, 52);
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t26 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
                gm_b16_8 t28 = t27;
                {
                    gm_b0_8 t30;
                    gm_l5 = t30;
                }
                void * t31 = ((uint8_t *)&gm_l5);
                if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srccr53ejqr1dde, 298, 69);
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t31 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
                gm_b16_8 t33 = t32;
                gm_t5detta1b4gnk t34;
                gm_f_parse_args_7blm2mp((void *)&t34, (void *)NULL, cx, NULL, t23, t28, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t77ebp3ogdble t35;
                gm_f_usage_failure_cgg6v1t((void *)&t35, &t34);
                gm_t77ebp3ogdble t36 = t35;
                if (((((uint8_t *)&t36))[0] != 0)) {
                    gm_t3ujhu6qph7de t37 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t36) + 8));
                    gm_t715ajl6o1ipo t38;
                    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                    ((uint8_t *)&t38)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t38 + 8), t37);
                    F.ret = t38;
                    goto gm_fret;
                }
                gm_tr8dvlu9ik4gj t39 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t36) + 8));
            }
        }
        {
            gm_th2lqbo77pvsc t40 = gm_l3;
            gm_th2lqbo77pvsc t41 = t40;
            gm_b16_8 t42;
            if (((((uint8_t *)&t41))[16] != 0)) {
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 0));
                t42 = t43;
            } else {
                gm_b16_8 t44 = GM_LD(gm_b16_8, gm_cftc9ncjfm7);
                gm_t3ujhu6qph7de t45;
                gm_f_fail_o80k3rv((void *)&t45, t44);
                gm_t715ajl6o1ipo t46;
                GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                ((uint8_t *)&t46)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t46 + 8), t45);
                gm_t715ajl6o1ipo t47 = t46;
                uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
                F.ret = t46;
                goto gm_fret;
            }
            gm_l6 = t42;
        }
        {
            gm_b16_8 t49 = gm_l6;
            gm_b16_8 t50 = t49;
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t50), gm_srccr53ejqr1dde, 300, 13)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 300, 13)) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t51;
            gm_t715ajl6o1ipo t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
            ((uint8_t *)&t52)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t52 + 8), t51);
            gm_t715ajl6o1ipo t53 = t52;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t52;
            goto gm_fret;
        }
        goto me19;
mn20:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_slskjtl6alfrd, 5) == 0)) goto mn55;
        {
        }
        {
        }
        {
            gm_b16_8 t57 = gm_l2;
            {
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_so87vf8a6o4mm));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(10));
                gm_tpmdr2hmdhsqr t60;
                GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
                gm_l7 = t60;
            }
            void * t61 = ((uint8_t *)&gm_l7);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srccr53ejqr1dde, 304, 53);
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t61 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t63 = t62;
            {
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s0nf1rg04f3m5));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(3));
                gm_b16_8 t66;
                GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sjgng334veu6f));
                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(7));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l8)) + 0, t65);
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l8)) + 16, t66);
            }
            void * t67 = ((uint8_t *)&gm_l8);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srccr53ejqr1dde, 304, 82);
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t67 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t69 = t68;
            gm_t5detta1b4gnk t70;
            gm_f_parse_args_7blm2mp((void *)&t70, (void *)NULL, cx, NULL, t57, t63, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t77ebp3ogdble t71;
            gm_f_usage_failure_cgg6v1t((void *)&t71, &t70);
            gm_t77ebp3ogdble t72 = t71;
            if (((((uint8_t *)&t72))[0] != 0)) {
                gm_t3ujhu6qph7de t73 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t72) + 8));
                gm_t715ajl6o1ipo t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                ((uint8_t *)&t74)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t74 + 8), t73);
                F.ret = t74;
                goto gm_fret;
            }
            gm_tr8dvlu9ik4gj t75 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t72) + 8));
            F.l9 = t75;
        }
        {
            uint64_t t77 = GM_LD(uintptr_t, ((((uint8_t *)&F.l9) + 0) + 0) + 8);
            uint8_t t78 = (t77 > ((uint64_t)0ULL));
            if (!t78) goto ie76;
            {
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sr43s52rmecnf));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(32));
                gm_t3ujhu6qph7de t81;
                GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t81 + 8), t80);
                gm_t715ajl6o1ipo t82;
                GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                ((uint8_t *)&t82)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t82 + 8), t81);
                gm_t715ajl6o1ipo t83 = t82;
                uint8_t t84 = ((((uint8_t *)&t83))[0] != 0);
                F.ret = t82;
                goto gm_fret;
            }
            goto ix76;
ie76:;
ix76:;
        }
        {
            gm_l10 = ((uint64_t)30ULL);
        }
        {
            void * t86 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_so87vf8a6o4mm));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(10));
            gm_th2lqbo77pvsc t88;
            gm_f_value_of_0km7vvp((void *)&t88, t86, t87);
            gm_th2lqbo77pvsc t89 = t88;
            if (!((((uint8_t *)&t89))[16] != 0)) goto ie85;
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
            gm_l11 = t90;
            {
                gm_b16_8 t92 = gm_l11;
                gm_tutnubt6u4hoq t93;
                gm_f_parse_int_8aueeu2((void *)&t93, t92);
                gm_tutnubt6u4hoq t94 = t93;
                uint64_t t95;
                if (!((((uint8_t *)&t94))[0] != 0)) {
                    uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&t94) + 8));
                    t95 = t96;
                } else {
                    gm_b16_8 t98 = gm_l11;
                    gm_tnpifv20irnph t99;
                    GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t98);
                    gm_tabb710d9mq2e t100;
                    gm_f_format_da7bimt((void *)&t100, (void *)NULL, cx, NULL, t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t101 = t100;
                    if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_srccr53ejqr1dde, 309, 43);
                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
                    gm_b16_8 t103 = t102;
                    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                    gm_t3ujhu6qph7de t105;
                    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 8), t104);
                    gm_t715ajl6o1ipo t106;
                    GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                    ((uint8_t *)&t106)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t106 + 8), t105);
                    gm_t715ajl6o1ipo t107 = t106;
                    uint8_t t108 = ((((uint8_t *)&t107))[0] != 0);
                    F.ret = t106;
                    goto gm_fret;
                }
                gm_l10 = t95;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            void * t110 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_s0nf1rg04f3m5));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(3));
            uint8_t t112 = gm_f_has_flag_4n9tu40(t110, t111);
            if (!t112) goto ie109;
            void * t113 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_so87vf8a6o4mm));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(10));
            gm_th2lqbo77pvsc t115;
            gm_f_value_of_0km7vvp((void *)&t115, t113, t114);
            gm_th2lqbo77pvsc t116 = t115;
            uint8_t t117;
            if (((((uint8_t *)&t116))[16] != 0)) goto mn119;
            t117 = ((uint8_t)0);
            goto me118;
mn119:;
            t117 = ((uint8_t)1);
            goto me118;
mn120:;
            gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 312, 40);
me118:;
            if (!t117) goto ie109;
            {
                gm_b16_8 t122;
                GM_ST(void *, (uint8_t *)&t122, (void *)(gm_ss9d51lhe1orq));
                GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(45));
                gm_t3ujhu6qph7de t123;
                GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t123 + 8), t122);
                gm_t715ajl6o1ipo t124;
                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                ((uint8_t *)&t124)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t124 + 8), t123);
                gm_t715ajl6o1ipo t125 = t124;
                uint8_t t126 = ((((uint8_t *)&t125))[0] != 0);
                F.ret = t124;
                goto gm_fret;
            }
            goto ix109;
ie109:;
ix109:;
        }
        {
            gm_th2lqbo77pvsc t127 = gm_l3;
            gm_th2lqbo77pvsc t128 = t127;
            gm_b16_8 t129;
            if (((((uint8_t *)&t128))[16] != 0)) {
                gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 0));
                t129 = t130;
            } else {
                gm_b16_8 t131 = GM_LD(gm_b16_8, gm_cftc9ncjfm7);
                gm_t3ujhu6qph7de t132;
                gm_f_fail_o80k3rv((void *)&t132, t131);
                gm_t715ajl6o1ipo t133;
                GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                ((uint8_t *)&t133)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t133 + 8), t132);
                gm_t715ajl6o1ipo t134 = t133;
                uint8_t t135 = ((((uint8_t *)&t134))[0] != 0);
                F.ret = t133;
                goto gm_fret;
            }
            gm_l12 = t129;
        }
        {
            uint64_t t136 = gm_l10;
            uint64_t r137; bool o138 = __builtin_mul_overflow(t136, ((uint64_t)86400ULL), &r137);
            gm_tf5srh4mp4b3i t139;
            if (o138) {
            gm_tf5srh4mp4b3i t140;
            memset(&t140, 0, sizeof t140);
                t139 = t140;
            } else {
            uint64_t t141 = r137;
            gm_tf5srh4mp4b3i t142;
            GM_ST(uint8_t, (uint8_t *)&t142 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t142 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t142 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t142), t141);
            ((uint8_t *)&t142)[8] = 1;
                t139 = t142;
            }
            gm_tf5srh4mp4b3i t143 = t139;
            uint64_t t144;
            if (((((uint8_t *)&t143))[8] != 0)) {
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&t143) + 0));
                t144 = t145;
            } else {
                if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srccr53ejqr1dde, 316, 40);
                uint64_t t146 = (uint64_t)(((uint64_t)18446744073709551615ULL) / ((uint64_t)1000000000ULL));
                t144 = t146;
            }
            gm_l13 = t144;
        }
        {
            uint64_t t147 = gm_l13;
            uint64_t r148; bool o149 = __builtin_mul_overflow(t147, ((uint64_t)1000000000ULL), &r148);
            gm_tf5srh4mp4b3i t150;
            if (o149) {
            gm_tf5srh4mp4b3i t151;
            memset(&t151, 0, sizeof t151);
                t150 = t151;
            } else {
            uint64_t t152 = r148;
            gm_tf5srh4mp4b3i t153;
            GM_ST(uint8_t, (uint8_t *)&t153 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t153 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t153 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t153), t152);
            ((uint8_t *)&t153)[8] = 1;
                t150 = t153;
            }
            gm_tf5srh4mp4b3i t154 = t150;
            uint64_t t155;
            if (((((uint8_t *)&t154))[8] != 0)) {
                uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&t154) + 0));
                t155 = t156;
            } else {
                t155 = ((uint64_t)18446744073709551615ULL);
            }
            void * t157 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t158;
            GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s0nf1rg04f3m5));
            GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(3));
            uint8_t t159 = gm_f_has_flag_4n9tu40(t157, t158);
            void * t160 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sjgng334veu6f));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(7));
            uint8_t t162 = gm_f_has_flag_4n9tu40(t160, t161);
            gm_t553jgmqd1tlr t163;
            GM_ST(uint16_t, (uint8_t *)&t163 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t163 + 0), t155);
            GM_ST(uint8_t, ((uint8_t *)&t163 + 8), t159);
            GM_ST(uint8_t, ((uint8_t *)&t163 + 9), t162);
            gm_l14 = t163;
        }
        {
            gm_b16_8 t164 = gm_l12;
            gm_t553jgmqd1tlr t165 = gm_l14;
            gm_f_clean_97pd952((void *)&F.l15, (void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t166 = (void *)((uint8_t *)&F.l15);
            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 9));
            gm_b16_8 t168 = gm_f_summary_s3h16om((void *)NULL, cx, NULL, t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t169 = t168;
            gm_b16_8 t170 = gm_l12;
            gm_b16_8 t171 = t170;
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t169), gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7hkl5mqa15gm, 4, gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t171), gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
        }
        {
            uint64_t t173 = GM_LD(uintptr_t, ((((uint8_t *)&F.l15) + 32) + 0) + 8);
            uint8_t t174 = (t173 > ((uint64_t)0ULL));
            if (!t174) goto ie172;
            {
                gm_b16_8 t176 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l15) + 32) + 0));
                gm_b16_8 t177 = t176;
                gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                gm_b16_8 t179;
                GM_ST(void *, (uint8_t *)&t179, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(1));
                gm_tabb710d9mq2e t180;
                gm_f_join_sfknljb((void *)&t180, (void *)NULL, cx, NULL, t178, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t181 = t180;
                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_srccr53ejqr1dde, 324, 61);
                gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
                gm_b16_8 t183 = t182;
                gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
                gm_t3ujhu6qph7de t185;
                gm_f_fail_o80k3rv((void *)&t185, t184);
                gm_t715ajl6o1ipo t186;
                GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                ((uint8_t *)&t186)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t186 + 8), t185);
                gm_t715ajl6o1ipo t187 = t186;
                uint8_t t188 = ((((uint8_t *)&t187))[0] != 0);
                F.ret = t186;
                goto gm_fret;
            }
            goto ix172;
ie172:;
ix172:;
        }
        {
            gm_b0_1 t189;
            gm_t715ajl6o1ipo t190;
            GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t190 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t190 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t190 + 24, 0);
            ((uint8_t *)&t190)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t190 + 8), t189);
            gm_t715ajl6o1ipo t191 = t190;
            uint8_t t192 = ((((uint8_t *)&t191))[0] != 0);
            F.ret = t190;
            goto gm_fret;
        }
        goto me19;
mn55:;
        gm_b16_8 t194 = gm_l1;
        gm_tnpifv20irnph t195;
        GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t194);
        gm_tabb710d9mq2e t196;
        gm_f_format_6ta43n5((void *)&t196, (void *)NULL, cx, NULL, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t197 = t196;
        if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 8, gm_srccr53ejqr1dde, 327, 36);
        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&t197) + 8));
        gm_b16_8 t199 = t198;
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
        gm_t3ujhu6qph7de t201;
        GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t201 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t201 + 8), t200);
        gm_t715ajl6o1ipo t202;
        GM_ST(uint8_t, (uint8_t *)&t202 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t202 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
        ((uint8_t *)&t202)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t202 + 8), t201);
        gm_t715ajl6o1ipo t203 = t202;
        uint8_t t204 = ((((uint8_t *)&t203))[0] != 0);
        F.ret = t202;
        goto gm_fret;
        goto me19;
mn193:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 296, 5);
me19:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a7cccd4(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_a7cccd4 F;
    gm_b16_8 gm_l0; /* names */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 230u);
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
                        gm_b16_8 t20 = t18;
                        gm_b16_8 t21 = t19;
                        uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (!t22) goto ie17;
                        {
                            uint8_t t24 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 61, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_fail_o80k3rv_c(gm_b16_8 a0) { gm_t3ujhu6qph7de gm_r; gm_f_fail_o80k3rv(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_fetch_l6q1k6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fetch_l6q1k6p F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l2; /* <temporary array> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* d */
    gm_b16_8 gm_l8; /* d */
    gm_b16_8 gm_l9; /* dir */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* root */
    gm_tqs5p4n2tq19v gm_l12; /* opts */
    gm_b16_8 gm_l13; /* e */
    GM_STACK_CHECK(&F, 2174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
    }
    {
    }
    {
        gm_b16_8 t2 = gm_l0;
        {
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
            gm_tpmdr2hmdhsqr t5;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
            gm_l1 = t5;
        }
        void * t6 = ((uint8_t *)&gm_l1);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srccr53ejqr1dde, 153, 45);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t8 = t7;
        {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s5fgj0n2t69gl));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(7));
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_snvisme5hp379));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l2)) + 0, t10);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l2)) + 16, t11);
        }
        void * t12 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srccr53ejqr1dde, 153, 67);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t12 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t14 = t13;
        gm_t5detta1b4gnk t15;
        gm_f_parse_args_7blm2mp((void *)&t15, (void *)NULL, cx, NULL, t2, t8, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t77ebp3ogdble t16;
        gm_f_usage_failure_cgg6v1t((void *)&t16, &t15);
        gm_t77ebp3ogdble t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) {
            gm_t3ujhu6qph7de t18 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t17) + 8));
            gm_t715ajl6o1ipo t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t19 + 8), t18);
            F.ret = t19;
            goto gm_fret;
        }
        gm_tr8dvlu9ik4gj t20 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t17) + 8));
        F.l3 = t20;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 0) + 0));
            gm_l5 = t22;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t24 = gm_l6;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t26 = (t24 < t25);
                if (!t26) goto lb23;
                {
                    uint64_t t29 = gm_l6;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t30 * 16));
                    gm_l7 = t34;
                    {
                        void * t36 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t37 = gm_l7;
                        gm_tftb1odc739t5 t38 = gm_f_push_3ef746b(t36, t37);
                        gm_tftb1odc739t5 t39 = t38;
                        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srccr53ejqr1dde, 156, 28);
                        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                    }
                    goto me32;
mn33:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 156, 5);
me32:;
                }
lc23:;
                {
                    uint64_t t42 = gm_l6;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l6 = t45;
                }
            }
lb23:;
        }
    }
    {
        void * t47 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t49;
        gm_f_value_of_0km7vvp((void *)&t49, t47, t48);
        gm_th2lqbo77pvsc t50 = t49;
        if (!((((uint8_t *)&t50))[16] != 0)) goto ie46;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        gm_l8 = t51;
        {
            void * t53 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t54 = gm_l8;
            gm_tftb1odc739t5 t55 = gm_f_push_3ef746b(t53, t54);
            gm_tftb1odc739t5 t56 = t55;
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srccr53ejqr1dde, 157, 35);
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t60 = (t59 > ((uint64_t)1ULL));
        if (!t60) goto ie58;
        {
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_saqbkgqthogiq));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(29));
            gm_t3ujhu6qph7de t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t63 + 8), t62);
            gm_t715ajl6o1ipo t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            ((uint8_t *)&t64)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t64 + 8), t63);
            gm_t715ajl6o1ipo t65 = t64;
            uint8_t t66 = ((((uint8_t *)&t65))[0] != 0);
            F.ret = t64;
            goto gm_fret;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t68 = t67;
        void * t69 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t68)));
        uint64_t t70 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t68) + 8);
        uint64_t t71 = (uintptr_t)0;
        gm_th2lqbo77pvsc t72;
        if (t71 < t70) {
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)t69 + t71 * 16));
        gm_th2lqbo77pvsc t74;
        GM_ST(uint8_t, (uint8_t *)&t74 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t74 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t74), t73);
        ((uint8_t *)&t74)[16] = 1;
            t72 = t74;
        } else {
        gm_th2lqbo77pvsc t75;
        memset(&t75, 0, sizeof t75);
            t72 = t75;
        }
        gm_th2lqbo77pvsc t76 = t72;
        gm_b16_8 t77;
        if (((((uint8_t *)&t76))[16] != 0)) {
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 0));
            t77 = t78;
        } else {
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(1));
            t77 = t79;
        }
        gm_l9 = t77;
    }
    {
        gm_b16_8 t80 = gm_l9;
        gm_t3ujhu6qph7de t81;
        gm_f_root_of_cluda11((void *)&t81, (void *)NULL, cx, NULL, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t82 = t81;
        gm_b16_8 t83;
        if (!((((uint8_t *)&t82))[0] != 0)) {
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
            t83 = t84;
        } else {
            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
            gm_l10 = t85;
            gm_b16_8 t87 = gm_l10;
            gm_t3ujhu6qph7de t88;
            gm_f_fail_o80k3rv((void *)&t88, t87);
            gm_t715ajl6o1ipo t89;
            GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
            ((uint8_t *)&t89)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t89 + 8), t88);
            gm_t715ajl6o1ipo t90 = t89;
            uint8_t t91 = ((((uint8_t *)&t90))[0] != 0);
            F.ret = t89;
            goto gm_fret;
        }
        gm_l11 = t83;
    }
    {
        void * t92 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s5fgj0n2t69gl));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(7));
        uint8_t t94 = gm_f_has_flag_4n9tu40(t92, t93);
        void * t95 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_snvisme5hp379));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(6));
        uint8_t t97 = gm_f_has_flag_4n9tu40(t95, t96);
        gm_tqs5p4n2tq19v t98;
        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), t94);
        GM_ST(uint8_t, ((uint8_t *)&t98 + 1), t97);
        gm_l12 = t98;
    }
    {
        {
            gm_b16_8 t100 = gm_l11;
            gm_tqs5p4n2tq19v t101 = gm_l12;
            gm_tl572d7579hns t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t102 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t102 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)0ULL));
            gm_t074vrl1hb4f6 t103;
            gm_f_sync_ht164ep((void *)&t103, (void *)NULL, cx, NULL, t100, t101, &t102, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t074vrl1hb4f6 t104 = t103;
            gm_tv37d3dpt2opu t105;
            if (!((((uint8_t *)&t104))[0] != 0)) {
                gm_tv37d3dpt2opu t106 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&t104) + 8));
                t105 = t106;
            } else {
                gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
                gm_l13 = t107;
                gm_b16_8 t109 = gm_l13;
                gm_t3ujhu6qph7de t110;
                gm_f_fail_o80k3rv((void *)&t110, t109);
                gm_t715ajl6o1ipo t111;
                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                ((uint8_t *)&t111)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t111 + 8), t110);
                gm_t715ajl6o1ipo t112 = t111;
                uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
                F.ret = t111;
                goto gm_fret;
            }
        }
    }
    {
        gm_b0_1 t114;
        gm_t715ajl6o1ipo t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 24, 0);
        ((uint8_t *)&t115)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t115 + 8), t114);
        gm_t715ajl6o1ipo t116 = t115;
        uint8_t t117 = ((((uint8_t *)&t116))[0] != 0);
        F.ret = t115;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_0m0ntf2 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* e */
    GM_STACK_CHECK(&F, 482u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        {
            gm_te2n4j2vtm53d t6 = gm_l1;
            gm_te2n4j2vtm53d t7 = t6;
            gm_tibupt4dmcqai t8;
            if (!((((uint8_t *)&t7))[0] != 0)) {
                gm_tibupt4dmcqai t9 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t7) + 8));
                t8 = t9;
            } else {
                gm_toe5fmr3vvf4d t10 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t7) + 8));
                gm_l2 = t10;
                {
                    uint8_t t13 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
                    if (!t13) goto ie12;
                    {
                        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l12), ((uint8_t)1));
                    }
                    goto ix12;
ie12:;
                    {
                        gm_t6j6smj7qoctu t16;
                        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l5));
                        GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t17);
                        gm_toe5fmr3vvf4d t18 = gm_l2;
                        gm_b16_8 t19 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t19);
                        gm_tabb710d9mq2e t20;
                        gm_f_format_is5aam9((void *)&t20, (void *)NULL, cx, NULL, &t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srccr53ejqr1dde, 362, 34);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
                        gm_b16_8 t23 = t22;
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                        gm_t3ujhu6qph7de t25;
                        gm_f_fail_o80k3rv((void *)&t25, t24);
                        gm_t715ajl6o1ipo t26;
                        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                        ((uint8_t *)&t26)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t26 + 8), t25);
                        gm_t715ajl6o1ipo t27 = t26;
                        uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
                        ((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->ret = t26;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t28;
                        goto gm_lad;
                    }
ix12:;
                }
                gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
                goto gm_lad;
            }
        }
    }
    {
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l11), ((uint8_t)1));
    }
    {
        gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
        goto gm_lad;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 357, 9);
me1:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_flag_4n9tu40(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_flag_4n9tu40 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 80) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 16));
                    gm_l4 = t16;
                    {
                        gm_b16_8 t19 = gm_l4;
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            uint8_t t25 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 54, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t31 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_package_command_ufjt5rp F;
    gm_b16_8 gm_l0; /* command */
    GM_STACK_CHECK_SHALLOW(&F, 388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2d9c7aju8hp t2;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6pnfrub0rj1t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3lbmpmt95hvh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t4);
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s4o70d89183c2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t5);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sqljkj8r253om));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t6);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sivh46n0690a4));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 64, t7);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_suass5sid4b6e));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 80, t8);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_so9n4mmd960c7));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 96, t9);
    gm_th2d9c7aju8hp t10 = t2;
    uint8_t t11 = 0;
    gm_b16_8 t12 = t1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 0, ((uint8_t *)&t12))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 16, ((uint8_t *)&t12))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 32, ((uint8_t *)&t12))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 48, ((uint8_t *)&t12))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 64, ((uint8_t *)&t12))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 80, ((uint8_t *)&t12))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 96, ((uint8_t *)&t12))) t11 = 1;
    uint8_t t13 = (uint8_t)(t11);
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_new_package_hf828qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1) {
    struct gm_fr_new_package_hf828qc F;
    gm_b16_8 gm_l0; /* args */
    uint8_t gm_l1; /* create */
    gm_t0kl141g5m2qf gm_l2; /* <temporary array> */
    gm_b0_8 gm_l3; /* <temporary array> */
    uint8_t gm_l6; /* git_init */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* n */
    gm_b16_8 gm_l9; /* problem */
    uint8_t gm_l10; /* created_here */
    gm_b16_8 gm_l13; /* <tmp> */
    gm_toe5fmr3vvf4d gm_l14; /* e */
    gm_th2lqbo77pvsc gm_l15; /* derived_from */
    gm_b16_8 gm_l16; /* name */
    gm_b16_8 gm_l17; /* n */
    gm_b16_8 gm_l18; /* abs */
    gm_b16_8 gm_l19; /* dir_name */
    gm_b16_8 gm_l20; /* problem */
    gm_b16_8 gm_l21; /* e */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* f */
    uint8_t gm_l26; /* existing_manifest */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* f */
    gm_b16_8 gm_l30; /* d */
    GM_STACK_CHECK(&F, 5077u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
    }
    {
    }
    {
        gm_b16_8 t2 = gm_l0;
        {
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sius9pki5u4e3));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s30cnsbbn0c4o));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l2)) + 0, t4);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l2)) + 16, t5);
        }
        void * t6 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srccr53ejqr1dde, 332, 45);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t8 = t7;
        {
            gm_b0_8 t10;
            gm_l3 = t10;
        }
        void * t11 = ((uint8_t *)&gm_l3);
        if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srccr53ejqr1dde, 332, 75);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t11 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
        gm_b16_8 t13 = t12;
        gm_t5detta1b4gnk t14;
        gm_f_parse_args_7blm2mp((void *)&t14, (void *)NULL, cx, NULL, t2, t8, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t77ebp3ogdble t15;
        gm_f_usage_failure_cgg6v1t((void *)&t15, &t14);
        gm_t77ebp3ogdble t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_t3ujhu6qph7de t17 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t16) + 8));
            gm_t715ajl6o1ipo t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_tr8dvlu9ik4gj t19 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t16) + 8));
        F.l4 = t19;
    }
    {
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
        F.l5 = t20;
    }
    {
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 0) + 0) + 8);
        uint8_t t23 = ((t22 == ((uint64_t)1ULL)));
        if (!t23) goto ie21;
        {
            uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 0) + 0) + 8);
            if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srccr53ejqr1dde, 335, 15);
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l4) + 0) + 0)) + t25 * 16));
            F.l5 = t27;
        }
        goto ix21;
ie21:;
        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 0) + 0) + 8);
        uint8_t t30 = ((t29 == ((uint64_t)0ULL)));
        if (!t30) goto ie28;
        {
            uint8_t t33 = gm_l1;
            if (!t33) goto ie32;
            {
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sgu3j1te831tl));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(23));
                gm_t3ujhu6qph7de t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
                gm_t715ajl6o1ipo t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                ((uint8_t *)&t37)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t37 + 8), t36);
                gm_t715ajl6o1ipo t38 = t37;
                uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
                F.ret = t37;
                goto gm_fret;
            }
            goto ix32;
ie32:;
ix32:;
        }
        goto ix28;
ie28:;
        {
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_saqbkgqthogiq));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(29));
            gm_t3ujhu6qph7de t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 8), t41);
            gm_t715ajl6o1ipo t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t43 + 8), t42);
            gm_t715ajl6o1ipo t44 = t43;
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t43;
            goto gm_fret;
        }
ix28:;
ix21:;
    }
    {
        gm_l6 = ((uint8_t)1);
    }
    {
        void * t47 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s30cnsbbn0c4o));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t49;
        gm_f_value_of_0km7vvp((void *)&t49, t47, t48);
        gm_th2lqbo77pvsc t50 = t49;
        if (!((((uint8_t *)&t50))[16] != 0)) goto ie46;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        gm_l7 = t51;
        {
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_s0k6ioi39f878, 3) == 0)) goto mn54;
            goto me53;
mn54:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_s3v0kr2fdpfld, 4) == 0)) goto mn56;
            {
                gm_l6 = ((uint8_t)0);
            }
            goto me53;
mn56:;
            gm_b16_8 t59 = gm_l7;
            gm_tnpifv20irnph t60;
            GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
            gm_tabb710d9mq2e t61;
            gm_f_format_e2683lk((void *)&t61, (void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t62 = t61;
            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srccr53ejqr1dde, 346, 40);
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
            gm_b16_8 t64 = t63;
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_t3ujhu6qph7de t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
            gm_t715ajl6o1ipo t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            ((uint8_t *)&t67)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t67 + 8), t66);
            gm_t715ajl6o1ipo t68 = t67;
            uint8_t t69 = ((((uint8_t *)&t68))[0] != 0);
            F.ret = t67;
            goto gm_fret;
            goto me53;
mn58:;
            gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 343, 9);
me53:;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t71 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sius9pki5u4e3));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(4));
        gm_th2lqbo77pvsc t73;
        gm_f_value_of_0km7vvp((void *)&t73, t71, t72);
        gm_th2lqbo77pvsc t74 = t73;
        if (!((((uint8_t *)&t74))[16] != 0)) goto ie70;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        gm_l8 = t75;
        {
            gm_b16_8 t78 = gm_l8;
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_snqvo5bgc2fch));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t80;
            gm_f_name_problem_6r4mp11((void *)&t80, (void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t81 = t80;
            if (!((((uint8_t *)&t81))[16] != 0)) goto ie77;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            gm_l9 = t82;
            {
                gm_b16_8 t84 = gm_l9;
                gm_t3ujhu6qph7de t85;
                gm_f_fail_o80k3rv((void *)&t85, t84);
                gm_t715ajl6o1ipo t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                ((uint8_t *)&t86)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t86 + 8), t85);
                gm_t715ajl6o1ipo t87 = t86;
                uint8_t t88 = ((((uint8_t *)&t87))[0] != 0);
                F.ret = t86;
                goto gm_fret;
            }
            goto ix77;
ie77:;
ix77:;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_l10 = ((uint8_t)0);
    }
    {
        uint8_t t90 = gm_l1;
        if (!t90) goto ie89;
        {
            F.l11 = ((uint8_t)0);
        }
        {
            F.l12 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t93 = F.l5;
                gm_l13 = t93;
            }
            gm_hval t94 = ((gm_hval){(void *)&F.h, cx});
            gm_node wn95 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl87701o8eo1, .env = t94.env, .hctx = t94.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t98 = gm_l13;
            gm_b0_1 t99 = gm_f_entries_44o5n9f_suo216n((void *)NULL, (&wn95), NULL, t98);
            if (gm_unw.kind) {
                goto gm_ll97;
            }
            F.w0 = t99;
            goto gm_ls100;
gm_ll97:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we96; }
            goto gm_lad;
gm_ls100:;
we96:;
            gm_b0_1 t101 = F.w0;
            gm_b0_1 t102 = t101;
        }
        {
            uint8_t t104 = F.l11;
            if (!t104) goto ie103;
            {
                gm_b16_8 t106 = F.l5;
                gm_tnpifv20irnph t107;
                GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
                gm_tabb710d9mq2e t108;
                gm_f_format_7lopc28((void *)&t108, (void *)NULL, cx, NULL, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t109 = t108;
                if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srccr53ejqr1dde, 369, 37);
                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                gm_b16_8 t111 = t110;
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                gm_t3ujhu6qph7de t113;
                gm_f_fail_o80k3rv((void *)&t113, t112);
                gm_t715ajl6o1ipo t114;
                GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                ((uint8_t *)&t114)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t114 + 8), t113);
                gm_t715ajl6o1ipo t115 = t114;
                uint8_t t116 = ((((uint8_t *)&t115))[0] != 0);
                F.ret = t114;
                goto gm_fret;
            }
            goto ix103;
ie103:;
ix103:;
        }
        {
            uint8_t t118 = F.l12;
            if (!t118) goto ie117;
            {
                gm_b16_8 t120 = F.l5;
                gm_ttln0d0hd70en t121 = gm_f_make_dir_all_q6phjan((void *)NULL, cx, NULL, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttln0d0hd70en t122 = t121;
                gm_b0_1 t123;
                if (!((((uint8_t *)&t122))[0] != 0)) {
                    gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 4));
                    t123 = t124;
                } else {
                    gm_toe5fmr3vvf4d t125 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t122) + 4));
                    gm_l14 = t125;
                    gm_t6j6smj7qoctu t127;
                    gm_b16_8 t128 = F.l5;
                    GM_ST(gm_b16_8, (uint8_t *)&t127 + 0, t128);
                    gm_toe5fmr3vvf4d t129 = gm_l14;
                    gm_b16_8 t130 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t127 + 16, t130);
                    gm_tabb710d9mq2e t131;
                    gm_f_format_5uun4vl((void *)&t131, (void *)NULL, cx, NULL, &t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t132 = t131;
                    if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 8, gm_srccr53ejqr1dde, 371, 59);
                    gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
                    gm_b16_8 t134 = t133;
                    gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                    gm_t3ujhu6qph7de t136;
                    gm_f_fail_o80k3rv((void *)&t136, t135);
                    gm_t715ajl6o1ipo t137;
                    GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                    ((uint8_t *)&t137)[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t137 + 8), t136);
                    gm_t715ajl6o1ipo t138 = t137;
                    uint8_t t139 = ((((uint8_t *)&t138))[0] != 0);
                    F.ret = t137;
                    goto gm_fret;
                }
            }
            {
                gm_l10 = ((uint8_t)1);
            }
            goto ix117;
ie117:;
ix117:;
        }
        goto ix89;
ie89:;
        gm_b16_8 t141 = F.l5;
        uint8_t t142 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t143 = (uint8_t)!t142;
        if (!t143) goto ie140;
        {
            gm_b16_8 t145 = F.l5;
            gm_tnpifv20irnph t146;
            GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t145);
            gm_tabb710d9mq2e t147;
            gm_f_format_tffhbfn((void *)&t147, (void *)NULL, cx, NULL, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t148 = t147;
            if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_srccr53ejqr1dde, 375, 22);
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
            gm_b16_8 t150 = t149;
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
            gm_t3ujhu6qph7de t152;
            gm_f_fail_o80k3rv((void *)&t152, t151);
            gm_t715ajl6o1ipo t153;
            GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
            ((uint8_t *)&t153)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t153 + 8), t152);
            gm_t715ajl6o1ipo t154 = t153;
            uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
            F.ret = t153;
            goto gm_fret;
        }
        goto ix140;
ie140:;
ix140:;
ix89:;
    }
    {
        gm_th2lqbo77pvsc t156;
        memset(&t156, 0, sizeof t156);
        gm_l15 = t156;
    }
    {
        gm_b16_8 t157;
        GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(0));
        gm_l16 = t157;
    }
    {
        void * t159 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sius9pki5u4e3));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(4));
        gm_th2lqbo77pvsc t161;
        gm_f_value_of_0km7vvp((void *)&t161, t159, t160);
        gm_th2lqbo77pvsc t162 = t161;
        if (!((((uint8_t *)&t162))[16] != 0)) goto ie158;
        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
        gm_l17 = t163;
        {
            gm_b16_8 t165 = gm_l17;
            gm_l16 = t165;
        }
        goto ix158;
ie158:;
        {
            gm_b16_8 t167 = F.l5;
            gm_th2lqbo77pvsc t168;
            gm_f_canonicalize_epo7cln((void *)&t168, (void *)NULL, cx, NULL, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t169 = t168;
            gm_b16_8 t170;
            if (((((uint8_t *)&t169))[16] != 0)) {
                gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&t169) + 0));
                t170 = t171;
            } else {
                gm_b16_8 t173 = F.l5;
                gm_tnpifv20irnph t174;
                GM_ST(gm_b16_8, ((uint8_t *)&t174 + 0), t173);
                gm_tabb710d9mq2e t175;
                gm_f_format_14t1s38((void *)&t175, (void *)NULL, cx, NULL, t174);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t176 = t175;
                if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 8, gm_srccr53ejqr1dde, 383, 26);
                gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t176) + 8));
                gm_b16_8 t178 = t177;
                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                gm_t3ujhu6qph7de t180;
                gm_f_fail_o80k3rv((void *)&t180, t179);
                gm_t715ajl6o1ipo t181;
                GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                ((uint8_t *)&t181)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t181 + 8), t180);
                gm_t715ajl6o1ipo t182 = t181;
                uint8_t t183 = ((((uint8_t *)&t182))[0] != 0);
                F.ret = t181;
                goto gm_fret;
            }
            gm_l18 = t170;
        }
        {
            gm_b16_8 t184 = gm_l18;
            gm_th2lqbo77pvsc t185;
            gm_f_file_name_em7v8lv((void *)&t185, (void *)NULL, cx, NULL, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t186 = t185;
            gm_b16_8 t187;
            if (((((uint8_t *)&t186))[16] != 0)) {
                gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&t186) + 0));
                t187 = t188;
            } else {
                gm_b16_8 t189;
                GM_ST(void *, (uint8_t *)&t189, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(0));
                t187 = t189;
            }
            gm_l19 = t187;
        }
        {
            gm_b16_8 t190 = gm_l19;
            gm_b16_8 t191 = gm_f_sanitize_name_3i9pjsp((void *)NULL, cx, NULL, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t191;
        }
        {
            gm_b16_8 t193 = gm_l16;
            gm_b16_8 t194;
            GM_ST(void *, (uint8_t *)&t194, (void *)(gm_snqvo5bgc2fch));
            GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t195;
            gm_f_name_problem_6r4mp11((void *)&t195, (void *)NULL, cx, NULL, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t196 = t195;
            if (!((((uint8_t *)&t196))[16] != 0)) goto ie192;
            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
            gm_l20 = t197;
            {
                uint8_t t200 = gm_l10;
                if (!t200) goto ie199;
                {
                    {
                        gm_b16_8 t203 = F.l5;
                        gm_node *pn204 = gm_find(cx, 2126215853);
                        if (!pn204) gm_trap("no handler installed for effect `Fs`", gm_srccr53ejqr1dde, 388, 35);
                        gm_ttln0d0hd70en t205;
                        t205 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn204->clauses[9])(pn204->env, pn204->hctx, pn204, t203);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                goto ix199;
ie199:;
ix199:;
            }
            {
                gm_tfeab279c16rr t206;
                gm_b16_8 t207 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t206 + 0, t207);
                gm_b16_8 t208 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t208);
                gm_b16_8 t209 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t206 + 32, t209);
                gm_tabb710d9mq2e t210;
                gm_f_format_ujusd29((void *)&t210, (void *)NULL, cx, NULL, &t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t211 = t210;
                if (((((uint8_t *)&t211))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t211) + 8, gm_srccr53ejqr1dde, 389, 26);
                gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&t211) + 8));
                gm_b16_8 t213 = t212;
                gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                gm_t3ujhu6qph7de t215;
                gm_f_fail_o80k3rv((void *)&t215, t214);
                gm_t715ajl6o1ipo t216;
                GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
                ((uint8_t *)&t216)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t216 + 8), t215);
                gm_t715ajl6o1ipo t217 = t216;
                uint8_t t218 = ((((uint8_t *)&t217))[0] != 0);
                F.ret = t216;
                goto gm_fret;
            }
            goto ix192;
ie192:;
ix192:;
        }
        {
            gm_b16_8 t220 = gm_l16;
            gm_b16_8 t221 = gm_l19;
            gm_b16_8 t222 = t220;
            gm_b16_8 t223 = t221;
            uint8_t t224 = (!gm_eq0iqiubfm7e(((uint8_t *)&t222), ((uint8_t *)&t223)));
            if (!t224) goto ie219;
            {
                gm_b16_8 t226 = gm_l19;
                gm_th2lqbo77pvsc t227;
                GM_ST(uint8_t, (uint8_t *)&t227 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t227 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t227 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t227), t226);
                ((uint8_t *)&t227)[16] = 1;
                gm_l15 = t227;
            }
            goto ix219;
ie219:;
ix219:;
        }
ix158:;
    }
    {
        gm_b16_8 t228 = F.l5;
        gm_b16_8 t229 = gm_l16;
        uint8_t t230 = gm_l6;
        gm_tcl27d4j9lskg t231;
        gm_f_scaffold_ansfc6m((void *)&t231, (void *)NULL, cx, NULL, t228, t229, t230);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcl27d4j9lskg t232 = t231;
        gm_tecsjmmgeeh52 t233;
        if (!((((uint8_t *)&t232))[0] != 0)) {
            gm_tecsjmmgeeh52 t234 = GM_LD(gm_tecsjmmgeeh52, (((uint8_t *)&t232) + 8));
            t233 = t234;
        } else {
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t232) + 8));
            gm_l21 = t235;
            gm_b16_8 t237 = gm_l21;
            gm_t3ujhu6qph7de t238;
            gm_f_fail_o80k3rv((void *)&t238, t237);
            gm_t715ajl6o1ipo t239;
            GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
            ((uint8_t *)&t239)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t239 + 8), t238);
            gm_t715ajl6o1ipo t240 = t239;
            uint8_t t241 = ((((uint8_t *)&t240))[0] != 0);
            F.ret = t239;
            goto gm_fret;
        }
        F.l22 = t233;
    }
    {
        {
            gm_b16_8 t243 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l22) + 40) + 0));
            gm_l23 = t243;
        }
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t245 = gm_l24;
                uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                uint8_t t247 = (t245 < t246);
                if (!t247) goto lb244;
                {
                    uint64_t t250 = gm_l24;
                    uint64_t t251 = (uintptr_t)t250;
                    uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t251 * 16));
                    gm_l25 = t255;
                    {
                        gm_b16_8 t257 = gm_l25;
                        gm_b16_8 t258 = t257;
                        if (gm_pf1m4b8q4e78(cx, gm_srgao1ir81l50, 13, gm_srccr53ejqr1dde, 394, 37)) {
                            goto gm_ll248;
                        }
                        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t258), gm_srccr53ejqr1dde, 394, 37)) {
                            goto gm_ll248;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_slrtg04n254p4, 18, gm_srccr53ejqr1dde, 394, 37)) {
                            goto gm_ll248;
                        }
                    }
                    goto me253;
mn254:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 394, 5);
me253:;
                }
lc244:;
                {
                    uint64_t t260 = gm_l24;
                    uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                    uint64_t t263 = r261;
                    gm_l24 = t263;
                }
            }
gm_ll248:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb244; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc244; }
            }
            goto gm_lad;
lb244:;
        }
    }
    {
        uint64_t t265 = GM_LD(uintptr_t, ((((uint8_t *)&F.l22) + 0) + 0) + 8);
        uint8_t t266 = ((t265 == ((uint64_t)0ULL)));
        if (!t266) goto ie264;
        {
            gm_b16_8 t268 = F.l5;
            gm_b16_8 t269 = t268;
            if (gm_pf1m4b8q4e78(cx, gm_s7mqv7hf4fso0, 26, gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t269), gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t270;
            gm_t715ajl6o1ipo t271;
            GM_ST(uint8_t, (uint8_t *)&t271 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t271 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t271 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t271 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t271 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t271 + 24, 0);
            ((uint8_t *)&t271)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t271 + 8), t270);
            gm_t715ajl6o1ipo t272 = t271;
            uint8_t t273 = ((((uint8_t *)&t272))[0] != 0);
            F.ret = t271;
            goto gm_fret;
        }
        goto ix264;
ie264:;
ix264:;
    }
    {
        gm_l26 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t275 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l22) + 40) + 0));
            gm_l27 = t275;
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t277 = gm_l28;
                uint64_t t278 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                uint8_t t279 = (t277 < t278);
                if (!t279) goto lb276;
                {
                    uint64_t t282 = gm_l28;
                    uint64_t t283 = (uintptr_t)t282;
                    uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t283 * 16));
                    gm_l29 = t287;
                    {
                        uint8_t t290 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l29)), gm_se24c7hpp401n, 10) == 0));
                        if (!t290) goto ie289;
                        {
                            gm_l26 = ((uint8_t)1);
                        }
                        goto ix289;
ie289:;
ix289:;
                    }
                    goto me285;
mn286:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 400, 5);
me285:;
                }
lc276:;
                {
                    uint64_t t293 = gm_l28;
                    uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                    uint64_t t296 = r294;
                    gm_l28 = t296;
                }
            }
lb276:;
        }
    }
    {
        uint8_t t298 = gm_l26;
        if (!t298) goto ie297;
        {
            gm_b16_8 t300 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l22) + 0) + 0));
            gm_b16_8 t301 = t300;
            gm_b16_8 t302 = GM_LD(gm_b16_8, ((uint8_t *)&t301));
            gm_b16_8 t303;
            GM_ST(void *, (uint8_t *)&t303, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t303 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t304;
            gm_f_join_sfknljb((void *)&t304, (void *)NULL, cx, NULL, t302, t303);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t305 = t304;
            if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 8, gm_srccr53ejqr1dde, 404, 81);
            gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&t305) + 8));
            gm_b16_8 t307 = t306;
            gm_b16_8 t308 = F.l5;
            gm_b16_8 t309 = t308;
            if (gm_pf1m4b8q4e78(cx, gm_s2n3d0ogcjggg, 13, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_tybihgc69jv3, ((uint8_t *)&t307), gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7hkl5mqa15gm, 4, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t309), gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_snmm68ee1rq48, 40, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
        }
        goto ix297;
ie297:;
        {
            gm_b16_8 t311 = gm_l16;
            gm_b16_8 t312 = t311;
            gm_b16_8 t313 = F.l5;
            gm_b16_8 t314 = t313;
            if (gm_pf1m4b8q4e78(cx, gm_sbvsnnbg6vc88, 22, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t312), gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_sgc7crh43q9tj, 5, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t314), gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
        }
ix297:;
    }
    {
        if (!((((uint8_t *)&gm_l15))[16] != 0)) goto ie315;
        gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
        gm_l30 = t316;
        {
            gm_b16_8 t318 = gm_l30;
            gm_b16_8 t319 = t318;
            if (gm_pf1m4b8q4e78(cx, gm_se48mcej0nimj, 15, gm_srccr53ejqr1dde, 409, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t319), gm_srccr53ejqr1dde, 409, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_sbcpd47bv0ru9, 62, gm_srccr53ejqr1dde, 409, 9)) {
                goto gm_lad;
            }
        }
        goto ix315;
ie315:;
ix315:;
    }
    {
        uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
        if (!t321) goto ie320;
        {
            if (gm_pf1m4b8q4e78(cx, gm_se39cci44d9rt, 34, gm_srccr53ejqr1dde, 411, 33)) {
                goto gm_lad;
            }
        }
        goto ix320;
ie320:;
ix320:;
    }
    {
        gm_b16_8 t323 = F.l5;
        gm_b16_8 t324 = t323;
        gm_b16_8 t325 = F.l5;
        gm_b16_8 t326 = t325;
        if (gm_pf1m4b8q4e78(cx, gm_sq99f4u8gmi6n, 27, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t324), gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_sb9fu6ufbt9g1, 16, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t326), gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t327;
        gm_t715ajl6o1ipo t328;
        GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t328 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t328 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t328 + 24, 0);
        ((uint8_t *)&t328)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t328 + 8), t327);
        gm_t715ajl6o1ipo t329 = t328;
        uint8_t t330 = ((((uint8_t *)&t329))[0] != 0);
        F.ret = t328;
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
inline __attribute__((aligned(4))) void gm_f_newest_version_dsisujv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_newest_version_dsisujv F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* t */
    GM_STACK_CHECK(&F, 1273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t470aqkia5212 t3;
        gm_f_remote_tags_rssa7s7((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t470aqkia5212 t4 = t3;
        gm_trqfvmjtt4sij t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_trqfvmjtt4sij t6 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            gm_b16_8 t9 = gm_l1;
            gm_t3ujhu6qph7de t10;
            gm_f_fail_o80k3rv((void *)&t10, t9);
            gm_tis0irsd01rfp t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t11 + 8), t10);
            gm_tis0irsd01rfp t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        F.l2 = t5;
    }
    {
        gm_tom1ad5bcqgi1 t14;
        memset(&t14, 0, sizeof t14);
        F.l3 = t14;
    }
    {
        {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l4 = t16;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l5;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l5;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t24 * 16));
                    gm_l6 = t28;
                    {
                        gm_b16_8 t30 = gm_l6;
                        gm_tom1ad5bcqgi1 t31;
                        gm_f_tag_version_aloeltn((void *)&t31, (void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_tom1ad5bcqgi1 t32 = t31;
                        gm_tu1089hhijadf t33;
                        if (((((uint8_t *)&t32))[56] != 0)) {
                            gm_tu1089hhijadf t34 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t32) + 0));
                            t33 = t34;
                        } else {
                            goto lc17;
                        }
                        F.l7 = t33;
                    }
                    {
                        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 24) + 8);
                        uint8_t t37 = (t36 > ((uint64_t)0ULL));
                        if (!t37) goto ie35;
                        {
                            goto lc17;
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    {
                        if (!((((uint8_t *)&F.l3))[56] != 0)) goto ie39;
                        gm_tu1089hhijadf t40 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l3));
                        F.l8 = t40;
                        {
                            int32_t t43 = gm_f_cmp_version_0oibvro(&(*(gm_tu1089hhijadf *)((uint8_t *)&F.l7)), &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l8)));
                            uint8_t t44 = (t43 < ((int32_t)0LL));
                            if (!t44) goto ie42;
                            {
                                goto lc17;
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    {
                        gm_tu1089hhijadf t46 = F.l7;
                        gm_tom1ad5bcqgi1 t47;
                        GM_ST(uint8_t, (uint8_t *)&t47 + 57, 0);
                        GM_ST(uint16_t, (uint8_t *)&t47 + 58, 0);
                        GM_ST(uint32_t, (uint8_t *)&t47 + 60, 0);
                        GM_ST(gm_tu1089hhijadf, ((uint8_t *)&t47), t46);
                        ((uint8_t *)&t47)[56] = 1;
                        F.l3 = t47;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 261, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t49 = gm_l5;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l5 = t52;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        gm_tom1ad5bcqgi1 t53 = F.l3;
        gm_tis0irsd01rfp t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        ((uint8_t *)&t54)[0] = 0;
        GM_ST(gm_tom1ad5bcqgi1, ((uint8_t *)&t54 + 8), t53);
        gm_tis0irsd01rfp t55 = t54;
        uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
        F.ret = t54;
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
__attribute__((aligned(4))) void gm_f_parse_args_7blm2mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_parse_args_7blm2mp F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* value_opts */
    gm_b16_8 gm_l2; /* flag_opts */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* rest */
    gm_b16_8 gm_l9; /* opt */
    gm_b16_8 gm_l10; /* name */
    gm_th2lqbo77pvsc gm_l11; /* inline */
    gm_t6j6smj7qoctu gm_l12; /* split */
    gm_b16_8 gm_l13; /* v */
    GM_STACK_CHECK(&F, 3627u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_g9fqnp2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 40)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 80)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l4;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l4;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srccr53ejqr1dde, 71, 14);
                gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 16));
                gm_l5 = t11;
            }
            {
                uint64_t t12 = gm_l4;
                uint64_t r13; bool o14 = __builtin_add_overflow(t12, ((uint64_t)1ULL), &r13);
                if (o14) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 72, 9);
                uint64_t t15 = r13;
                gm_l4 = t15;
            }
            {
                uint8_t t17 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l5)), gm_s9pknf9g05okt, 2) == 0));
                if (!t17) goto ie16;
                {
                    {
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t22 = gm_l4;
                        uint64_t t23 = (uintptr_t)t22;
                        if (t23 > (uintptr_t)t20 || (uintptr_t)t20 > (uintptr_t)t20) gm_trap_slice(t23, (uintptr_t)t20, t20, gm_srccr53ejqr1dde, 75, 25);
                        gm_b16_8 t24;
                        GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t21 + t23 * 16)));
                        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)((uintptr_t)t20 - t23));
                        gm_l6 = t24;
                    }
                    {
                        gm_l7 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t26 = gm_l7;
                            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            uint8_t t28 = (t26 < t27);
                            if (!t28) goto lb25;
                            {
                                uint64_t t31 = gm_l7;
                                uint64_t t32 = (uintptr_t)t31;
                                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t32 * 16));
                                gm_l8 = t36;
                                {
                                    void * t38 = (void *)(((uint8_t *)&F.l3) + 0);
                                    gm_b16_8 t39 = gm_l8;
                                    gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t38, t39);
                                    gm_tftb1odc739t5 t41 = t40;
                                    if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srccr53ejqr1dde, 75, 37);
                                    gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                                }
                                goto me34;
mn35:;
                                gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 75, 13);
me34:;
                            }
lc25:;
                            {
                                uint64_t t44 = gm_l7;
                                uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                uint64_t t47 = r45;
                                gm_l7 = t47;
                            }
                        }
lb25:;
                    }
                }
                {
                    goto lb2;
                }
                goto ix16;
ie16:;
ix16:;
            }
            {
                gm_b16_8 t49 = gm_l5;
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s9pknf9g05okt));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(2));
                gm_th2lqbo77pvsc t51;
                gm_f_strip_prefix_b4949s8((void *)&t51, t49, t50);
                gm_th2lqbo77pvsc t52 = t51;
                if (!((((uint8_t *)&t52))[16] != 0)) goto ie48;
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                gm_l9 = t53;
                {
                    gm_b16_8 t55 = gm_l9;
                    gm_l10 = t55;
                }
                {
                    gm_th2lqbo77pvsc t56;
                    memset(&t56, 0, sizeof t56);
                    gm_l11 = t56;
                }
                {
                    gm_b16_8 t58 = gm_l9;
                    gm_b16_8 t59;
                    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sh0j2ggs1vvoa));
                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(1));
                    gm_ttu09lij36f8c t60;
                    gm_f_split_once_mqcu1ce((void *)&t60, t58, t59);
                    gm_ttu09lij36f8c t61 = t60;
                    if (!((((uint8_t *)&t61))[32] != 0)) goto ie57;
                    gm_t6j6smj7qoctu t62 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t61));
                    gm_l12 = t62;
                    {
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                        gm_l10 = t64;
                    }
                    {
                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                        gm_th2lqbo77pvsc t66;
                        GM_ST(uint8_t, (uint8_t *)&t66 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t66 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t66 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t66), t65);
                        ((uint8_t *)&t66)[16] = 1;
                        gm_l11 = t66;
                    }
                    goto ix57;
ie57:;
ix57:;
                }
                {
                    gm_b16_8 t68 = gm_l2;
                    gm_b16_8 t69 = gm_l10;
                    uint8_t t70 = gm_f_contains_a7cccd4(t68, t69);
                    if (!t70) goto ie67;
                    uint8_t t71;
                    if (((((uint8_t *)&gm_l11))[16] != 0)) goto mn73;
                    t71 = ((uint8_t)0);
                    goto me72;
mn73:;
                    t71 = ((uint8_t)1);
                    goto me72;
mn74:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 85, 46);
me72:;
                    if (!t71) goto ie67;
                    {
                        gm_b16_8 t76 = gm_l10;
                        gm_tnpifv20irnph t77;
                        GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                        gm_tabb710d9mq2e t78;
                        gm_f_format_6dqa07h((void *)&t78, (void *)NULL, cx, NULL, t77);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                        gm_tabb710d9mq2e t79 = t78;
                        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_srccr53ejqr1dde, 86, 25);
                        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
                        gm_b16_8 t81 = t80;
                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                        gm_t5detta1b4gnk t83;
                        memset(&t83, 0, sizeof t83);
                        ((uint8_t *)&t83)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 8), t82);
                        gm_t5detta1b4gnk t84 = t83;
                        uint8_t t85 = ((((uint8_t *)&t84))[0] != 0);
                        F.ret = t83;
                        goto gm_fret;
                    }
                    goto ix67;
ie67:;
                    gm_b16_8 t87 = gm_l2;
                    gm_b16_8 t88 = gm_l10;
                    uint8_t t89 = gm_f_contains_a7cccd4(t87, t88);
                    if (!t89) goto ie86;
                    {
                        void * t92 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t93 = gm_l10;
                        uint8_t t94 = gm_f_has_flag_4n9tu40(t92, t93);
                        uint8_t t95 = (uint8_t)!t94;
                        if (!t95) goto ie91;
                        {
                            void * t97 = (void *)(((uint8_t *)&F.l3) + 80);
                            gm_b16_8 t98 = gm_l10;
                            gm_tftb1odc739t5 t99 = gm_f_push_3ef746b(t97, t98);
                            gm_tftb1odc739t5 t100 = t99;
                            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srccr53ejqr1dde, 88, 45);
                            gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    goto ix86;
ie86:;
                    gm_b16_8 t103 = gm_l1;
                    gm_b16_8 t104 = gm_l10;
                    uint8_t t105 = gm_f_contains_a7cccd4(t103, t104);
                    if (!t105) goto ie102;
                    {
                        gm_th2lqbo77pvsc t107 = gm_l11;
                        gm_th2lqbo77pvsc t108 = t107;
                        gm_b16_8 t109;
                        if (((((uint8_t *)&t108))[16] != 0)) {
                            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t108) + 0));
                            t109 = t110;
                        } else {
                            {
                                uint64_t t113 = gm_l4;
                                uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                uint8_t t115 = (t113 >= t114);
                                if (!t115) goto ie112;
                                {
                                    gm_b16_8 t117 = gm_l10;
                                    gm_tnpifv20irnph t118;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t117);
                                    gm_tabb710d9mq2e t119;
                                    gm_f_format_n537vp2((void *)&t119, (void *)NULL, cx, NULL, t118);
                                    if (gm_unw.kind) {
                                        goto gm_ll6;
                                    }
                                    gm_tabb710d9mq2e t120 = t119;
                                    if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 8, gm_srccr53ejqr1dde, 91, 48);
                                    gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
                                    gm_b16_8 t122 = t121;
                                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                                    gm_t5detta1b4gnk t124;
                                    memset(&t124, 0, sizeof t124);
                                    ((uint8_t *)&t124)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t124 + 8), t123);
                                    gm_t5detta1b4gnk t125 = t124;
                                    uint8_t t126 = ((((uint8_t *)&t125))[0] != 0);
                                    F.ret = t124;
                                    goto gm_fret;
                                }
                                goto ix112;
ie112:;
ix112:;
                            }
                            {
                                uint64_t t127 = gm_l4;
                                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                                if (o129) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 92, 21);
                                uint64_t t130 = r128;
                                gm_l4 = t130;
                            }
                            uint64_t t131 = gm_l4;
                            uint64_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint64_t)1ULL), &r132);
                            if (o133) gm_trap("integer overflow in `-`", gm_srccr53ejqr1dde, 93, 26);
                            uint64_t t134 = r132;
                            uint64_t t135 = (uintptr_t)t134;
                            uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t135 >= (uintptr_t)(t136)) gm_trap_index(t135, t136, gm_srccr53ejqr1dde, 93, 21);
                            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t135 * 16));
                            gm_b16_8 t138 = t137;
                            t109 = t138;
                        }
                        gm_l13 = t109;
                    }
                    {
                        void * t140 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t141 = gm_l10;
                        gm_th2lqbo77pvsc t142;
                        gm_f_value_of_0km7vvp((void *)&t142, t140, t141);
                        gm_th2lqbo77pvsc t143 = t142;
                        uint8_t t144;
                        if (((((uint8_t *)&t143))[16] != 0)) goto mn146;
                        t144 = ((uint8_t)0);
                        goto me145;
mn146:;
                        t144 = ((uint8_t)1);
                        goto me145;
mn147:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 95, 20);
me145:;
                        if (!t144) goto ie139;
                        {
                            gm_b16_8 t149 = gm_l10;
                            gm_tnpifv20irnph t150;
                            GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t149);
                            gm_tabb710d9mq2e t151;
                            gm_f_format_fbigqms((void *)&t151, (void *)NULL, cx, NULL, t150);
                            if (gm_unw.kind) {
                                goto gm_ll6;
                            }
                            gm_tabb710d9mq2e t152 = t151;
                            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 8, gm_srccr53ejqr1dde, 95, 61);
                            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                            gm_b16_8 t154 = t153;
                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                            gm_t5detta1b4gnk t156;
                            memset(&t156, 0, sizeof t156);
                            ((uint8_t *)&t156)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t156 + 8), t155);
                            gm_t5detta1b4gnk t157 = t156;
                            uint8_t t158 = ((((uint8_t *)&t157))[0] != 0);
                            F.ret = t156;
                            goto gm_fret;
                        }
                        goto ix139;
ie139:;
ix139:;
                    }
                    {
                        void * t159 = (void *)(((uint8_t *)&F.l3) + 40);
                        gm_t6j6smj7qoctu t160;
                        gm_b16_8 t161 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t160 + 0, t161);
                        gm_b16_8 t162 = gm_l13;
                        GM_ST(gm_b16_8, (uint8_t *)&t160 + 16, t162);
                        gm_tftb1odc739t5 t163 = gm_f_push_2tc790g(t159, &t160);
                        gm_tftb1odc739t5 t164 = t163;
                        if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 1, gm_srccr53ejqr1dde, 96, 17);
                        gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 1));
                    }
                    goto ix102;
ie102:;
                    {
                        gm_b16_8 t167 = gm_l5;
                        gm_tnpifv20irnph t168;
                        GM_ST(gm_b16_8, ((uint8_t *)&t168 + 0), t167);
                        gm_tabb710d9mq2e t169;
                        gm_f_format_j0ggm04((void *)&t169, (void *)NULL, cx, NULL, t168);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                        gm_tabb710d9mq2e t170 = t169;
                        if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 8, gm_srccr53ejqr1dde, 98, 25);
                        gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&t170) + 8));
                        gm_b16_8 t172 = t171;
                        gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                        gm_t5detta1b4gnk t174;
                        memset(&t174, 0, sizeof t174);
                        ((uint8_t *)&t174)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t174 + 8), t173);
                        gm_t5detta1b4gnk t175 = t174;
                        uint8_t t176 = ((((uint8_t *)&t175))[0] != 0);
                        F.ret = t174;
                        goto gm_fret;
                    }
ix102:;
ix86:;
ix67:;
                }
                goto ix48;
ie48:;
                gm_b16_8 t178 = gm_l5;
                gm_b16_8 t179;
                GM_ST(void *, (uint8_t *)&t179, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(1));
                uint8_t t180 = gm_f_starts_with_6vvt01e(t178, t179);
                if (!t180) goto ie177;
                uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t182 = (t181 > ((uint64_t)1ULL));
                if (!t182) goto ie177;
                {
                    gm_b16_8 t184 = gm_l5;
                    gm_tnpifv20irnph t185;
                    GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t184);
                    gm_tabb710d9mq2e t186;
                    gm_f_format_j0ggm04((void *)&t186, (void *)NULL, cx, NULL, t185);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    gm_tabb710d9mq2e t187 = t186;
                    if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 8, gm_srccr53ejqr1dde, 101, 21);
                    gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&t187) + 8));
                    gm_b16_8 t189 = t188;
                    gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                    gm_t5detta1b4gnk t191;
                    memset(&t191, 0, sizeof t191);
                    ((uint8_t *)&t191)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t191 + 8), t190);
                    gm_t5detta1b4gnk t192 = t191;
                    uint8_t t193 = ((((uint8_t *)&t192))[0] != 0);
                    F.ret = t191;
                    goto gm_fret;
                }
                goto ix177;
ie177:;
                {
                    void * t195 = (void *)(((uint8_t *)&F.l3) + 0);
                    gm_b16_8 t196 = gm_l5;
                    gm_tftb1odc739t5 t197 = gm_f_push_3ef746b(t195, t196);
                    gm_tftb1odc739t5 t198 = t197;
                    if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 1, gm_srccr53ejqr1dde, 103, 13);
                    gm_b0_1 t199 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 1));
                }
ix177:;
ix48:;
            }
lc2:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        gm_tr8dvlu9ik4gj t200 = F.l3;
        gm_t5detta1b4gnk t201;
        GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
        ((uint8_t *)&t201)[0] = 0;
        GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t201 + 8), t200);
        gm_t5detta1b4gnk t202 = t201;
        uint8_t t203 = ((((uint8_t *)&t202))[0] != 0);
        F.ret = t201;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_relative_path_3klciqu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_relative_path_3klciqu F;
    gm_b16_8 gm_l0; /* from */
    gm_b16_8 gm_l1; /* target */
    uint64_t gm_l4; /* common */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* x */
    GM_STACK_CHECK(&F, 1056u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_tl7hpf5nlilqq t3;
        gm_f_split_cbk4ks8((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl7hpf5nlilqq t4 = t3;
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t4) + 16));
        F.l2 = t5;
    }
    {
        gm_b16_8 t6 = gm_l1;
        gm_tl7hpf5nlilqq t7;
        gm_f_split_cbk4ks8((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl7hpf5nlilqq t8 = t7;
        gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t8) + 16));
        F.l3 = t9;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t13 = (t11 < t12);
            if (!t13) goto lb10;
            uint64_t t14 = gm_l4;
            uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            uint8_t t16 = (t14 < t15);
            if (!t16) goto lb10;
            uint64_t t17 = gm_l4;
            uint64_t t18 = (uintptr_t)t17;
            uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srccr53ejqr1dde, 179, 61);
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t18 * 16));
            uint64_t t21 = gm_l4;
            uint64_t t22 = (uintptr_t)t21;
            uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srccr53ejqr1dde, 179, 80);
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t22 * 16));
            gm_b16_8 t25 = t20;
            gm_b16_8 t26 = t24;
            uint8_t t27 = (gm_eq0iqiubfm7e(((uint8_t *)&t25), ((uint8_t *)&t26)));
            if (!t27) goto lb10;
            {
                uint64_t t30 = gm_l4;
                uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 179, 98);
                uint64_t t33 = r31;
                gm_l4 = t33;
            }
lc10:;
        }
lb10:;
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t35 = gm_l4;
            gm_l7 = t35;
        }
        {
            uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            gm_l6 = t36;
        }
        {
            for (;;) {
                uint64_t t38 = gm_l7;
                uint64_t t39 = gm_l6;
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l7;
                    uint64_t t44 = t43;
                    {
                        void * t48 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_snenkhfev166s));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t50 = gm_f_push_3ef746b(t48, t49);
                        gm_tftb1odc739t5 t51 = t50;
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srccr53ejqr1dde, 181, 36);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                    }
                    goto me45;
mn46:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 181, 5);
me45:;
                }
lc37:;
                {
                    uint64_t t54 = gm_l7;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l7 = t57;
                }
            }
lb37:;
        }
    }
    {
        {
            uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            void * t60 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)));
            uint64_t t61 = gm_l4;
            uint64_t t62 = (uintptr_t)t61;
            if (t62 > (uintptr_t)t59 || (uintptr_t)t59 > (uintptr_t)t59) gm_trap_slice(t62, (uintptr_t)t59, t59, gm_srccr53ejqr1dde, 182, 14);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t60 + t62 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)((uintptr_t)t59 - t62));
            gm_l8 = t63;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t65 = gm_l9;
                uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t67 = (t65 < t66);
                if (!t67) goto lb64;
                {
                    uint64_t t70 = gm_l9;
                    uint64_t t71 = (uintptr_t)t70;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t71 * 16));
                    gm_l10 = t75;
                    {
                        void * t77 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t78 = gm_l10;
                        gm_tftb1odc739t5 t79 = gm_f_push_3ef746b(t77, t78);
                        gm_tftb1odc739t5 t80 = t79;
                        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srccr53ejqr1dde, 182, 34);
                        gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                    }
                    goto me73;
mn74:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 182, 5);
me73:;
                }
lc64:;
                {
                    uint64_t t83 = gm_l9;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l9 = t86;
                }
            }
lb64:;
        }
    }
    {
        uint64_t t88 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
        uint8_t t89 = ((t88 == ((uint64_t)0ULL)));
        if (!t89) goto ie87;
        {
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(1));
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t94 = t93;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t97;
        gm_f_join_sfknljb((void *)&t97, (void *)NULL, cx, NULL, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t98 = t97;
        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_srccr53ejqr1dde, 184, 12);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
        gm_b16_8 t100 = t99;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
        uint8_t t102 = 0;
        F.ret = t101;
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
__attribute__((aligned(4))) void gm_f_remove_bar6kga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_bar6kga F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l6; /* root */
    gm_b16_8 gm_l7; /* manifest_path */
    gm_b16_8 gm_l8; /* e */
    gm_b16_8 gm_l9; /* old_text */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* new_text */
    gm_b16_8 gm_l12; /* e */
    gm_b16_8 gm_l13; /* e */
    gm_b16_8 gm_l14; /* e2 */
    GM_STACK_CHECK(&F, 2731u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
    }
    {
    }
    {
        gm_b16_8 t2 = gm_l0;
        {
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
            gm_tpmdr2hmdhsqr t5;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
            gm_l1 = t5;
        }
        void * t6 = ((uint8_t *)&gm_l1);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srccr53ejqr1dde, 273, 45);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t8 = t7;
        {
            gm_b0_8 t10;
            gm_l2 = t10;
        }
        void * t11 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srccr53ejqr1dde, 273, 67);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t11 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
        gm_b16_8 t13 = t12;
        gm_t5detta1b4gnk t14;
        gm_f_parse_args_7blm2mp((void *)&t14, (void *)NULL, cx, NULL, t2, t8, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t77ebp3ogdble t15;
        gm_f_usage_failure_cgg6v1t((void *)&t15, &t14);
        gm_t77ebp3ogdble t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_t3ujhu6qph7de t17 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t16) + 8));
            gm_t715ajl6o1ipo t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_tr8dvlu9ik4gj t19 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t16) + 8));
        F.l3 = t19;
    }
    {
        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 0) + 0) + 8);
        uint8_t t22 = (!(t21 == ((uint64_t)1ULL)));
        if (!t22) goto ie20;
        {
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_saicih5tjo4fl));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(42));
            gm_t3ujhu6qph7de t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 8), t24);
            gm_t715ajl6o1ipo t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            ((uint8_t *)&t26)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t26 + 8), t25);
            gm_t715ajl6o1ipo t27 = t26;
            uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
            F.ret = t26;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 0) + 0) + 8);
        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srccr53ejqr1dde, 275, 13);
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l3) + 0) + 0)) + t29 * 16));
        gm_l4 = t31;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t34;
        gm_f_value_of_0km7vvp((void *)&t34, t32, t33);
        gm_th2lqbo77pvsc t35 = t34;
        gm_b16_8 t36;
        if (((((uint8_t *)&t35))[16] != 0)) {
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 0));
            t36 = t37;
        } else {
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(1));
            t36 = t38;
        }
        gm_t3ujhu6qph7de t39;
        gm_f_root_of_cluda11((void *)&t39, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t40 = t39;
        gm_b16_8 t41;
        if (!((((uint8_t *)&t40))[0] != 0)) {
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            t41 = t42;
        } else {
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_l5 = t43;
            gm_b16_8 t45 = gm_l5;
            gm_t3ujhu6qph7de t46;
            gm_f_fail_o80k3rv((void *)&t46, t45);
            gm_t715ajl6o1ipo t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            ((uint8_t *)&t47)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t47 + 8), t46);
            gm_t715ajl6o1ipo t48 = t47;
            uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        gm_l6 = t41;
    }
    {
        gm_b16_8 t50 = gm_l6;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(10));
        gm_b16_8 t52 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t52;
    }
    {
        gm_b16_8 t53 = gm_l7;
        gm_t3ujhu6qph7de t54;
        gm_f_read_text_g0qbj1t((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t55 = t54;
        gm_b16_8 t56;
        if (!((((uint8_t *)&t55))[0] != 0)) {
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            t56 = t57;
        } else {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            gm_l8 = t58;
            gm_b16_8 t60 = gm_l8;
            gm_t3ujhu6qph7de t61;
            gm_f_fail_o80k3rv((void *)&t61, t60);
            gm_t715ajl6o1ipo t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            ((uint8_t *)&t62)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t62 + 8), t61);
            gm_t715ajl6o1ipo t63 = t62;
            uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
            F.ret = t62;
            goto gm_fret;
        }
        gm_l9 = t56;
    }
    {
        gm_b16_8 t65 = gm_l9;
        gm_b16_8 t66 = gm_l4;
        gm_t3ujhu6qph7de t67;
        gm_f_remove_dependency_4ju7b0h((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t68 = t67;
        gm_b16_8 t69;
        if (!((((uint8_t *)&t68))[0] != 0)) {
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
            t69 = t70;
        } else {
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
            gm_l10 = t71;
            gm_b16_8 t73 = gm_l10;
            gm_t3ujhu6qph7de t74;
            gm_f_fail_o80k3rv((void *)&t74, t73);
            gm_t715ajl6o1ipo t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            ((uint8_t *)&t75)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t75 + 8), t74);
            gm_t715ajl6o1ipo t76 = t75;
            uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
            F.ret = t75;
            goto gm_fret;
        }
        gm_l11 = t69;
    }
    {
        gm_b16_8 t78 = gm_l7;
        gm_b16_8 t79 = gm_l11;
        gm_tl572d7579hns t80;
        gm_f_write_atomic_v74ncu3((void *)&t80, (void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t81 = t80;
        gm_b0_1 t82;
        if (!((((uint8_t *)&t81))[0] != 0)) {
            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 8));
            t82 = t83;
        } else {
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
            gm_l12 = t84;
            gm_b16_8 t86 = gm_l12;
            gm_t3ujhu6qph7de t87;
            gm_f_fail_o80k3rv((void *)&t87, t86);
            gm_t715ajl6o1ipo t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
            ((uint8_t *)&t88)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t88 + 8), t87);
            gm_t715ajl6o1ipo t89 = t88;
            uint8_t t90 = ((((uint8_t *)&t89))[0] != 0);
            F.ret = t88;
            goto gm_fret;
        }
    }
    {
        {
            gm_b16_8 t92 = gm_l6;
            gm_tqs5p4n2tq19v t93;
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t93 + 1), ((uint8_t)0));
            gm_tl572d7579hns t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t94 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t94 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
            gm_t074vrl1hb4f6 t95;
            gm_f_sync_ht164ep((void *)&t95, (void *)NULL, cx, NULL, t92, t93, &t94, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t074vrl1hb4f6 t96 = t95;
            gm_tv37d3dpt2opu t97;
            if (!((((uint8_t *)&t96))[0] != 0)) {
                gm_tv37d3dpt2opu t98 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&t96) + 8));
                t97 = t98;
            } else {
                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t96) + 8));
                gm_l13 = t99;
                {
                    gm_b16_8 t101 = gm_l7;
                    gm_b16_8 t102 = gm_l9;
                    gm_tl572d7579hns t103;
                    gm_f_write_atomic_v74ncu3((void *)&t103, (void *)NULL, cx, NULL, t101, t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl572d7579hns t104 = t103;
                    gm_b0_1 t105;
                    if (!((((uint8_t *)&t104))[0] != 0)) {
                        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 8));
                        t105 = t106;
                    } else {
                        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
                        gm_l14 = t107;
                        gm_b16_8 t109 = gm_l14;
                        gm_t3ujhu6qph7de t110;
                        gm_f_fail_o80k3rv((void *)&t110, t109);
                        gm_t715ajl6o1ipo t111;
                        GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                        ((uint8_t *)&t111)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t111 + 8), t110);
                        gm_t715ajl6o1ipo t112 = t111;
                        uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
                        F.ret = t111;
                        goto gm_fret;
                    }
                }
                gm_b16_8 t114 = gm_l13;
                gm_t3ujhu6qph7de t115;
                gm_f_fail_o80k3rv((void *)&t115, t114);
                gm_t715ajl6o1ipo t116;
                GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                ((uint8_t *)&t116)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t116 + 8), t115);
                gm_t715ajl6o1ipo t117 = t116;
                uint8_t t118 = ((((uint8_t *)&t117))[0] != 0);
                F.ret = t116;
                goto gm_fret;
            }
        }
    }
    {
        gm_b16_8 t119 = gm_l4;
        gm_b16_8 t120 = t119;
        if (gm_pf454qfrpt6r(cx, gm_s9m6kd6gec368, 13, gm_srccr53ejqr1dde, 285, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t120), gm_srccr53ejqr1dde, 285, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_smsc5gshqt6cd, 19, gm_srccr53ejqr1dde, 285, 5)) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t121;
        gm_t715ajl6o1ipo t122;
        GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t122 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t122 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t122 + 24, 0);
        ((uint8_t *)&t122)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t122 + 8), t121);
        gm_t715ajl6o1ipo t123 = t122;
        uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
        F.ret = t122;
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
inline __attribute__((aligned(4))) void gm_f_root_of_cluda11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_root_of_cluda11 F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 595u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srccr53ejqr1dde, 110, 8);
        gm_tih6h4avv5nrb t5;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[5])((void *)&t5, pn4->env, pn4->hctx, pn4, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t6 = t5;
        uint8_t t7;
        if (!((((uint8_t *)&t6))[0] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 110, 8);
me8:;
        if (!t7) goto ie2;
        {
            gm_b16_8 t12 = gm_l0;
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_tabb710d9mq2e t14;
            gm_f_format_27unvt9((void *)&t14, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srccr53ejqr1dde, 110, 37);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
            gm_b16_8 t17 = t16;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_t3ujhu6qph7de t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
            gm_t3ujhu6qph7de t20 = t19;
            uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t22 = gm_l0;
        gm_th2lqbo77pvsc t23;
        gm_f_find_root_kjnm6hf((void *)&t23, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t24 = t23;
        gm_t3ujhu6qph7de t25;
        if (((((uint8_t *)&t24))[16] != 0)) {
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 0));
            gm_t3ujhu6qph7de t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 8), t26);
            t25 = t27;
        } else {
            gm_b16_8 t28 = gm_l0;
            gm_tnpifv20irnph t29;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
            gm_tabb710d9mq2e t30;
            gm_f_format_v71csbq((void *)&t30, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srccr53ejqr1dde, 111, 33);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_t3ujhu6qph7de t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t35 + 8), t34);
            t25 = t35;
        }
        gm_t3ujhu6qph7de t36 = t25;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t25;
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
inline __attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_run_command_s4fhqaq F;
    gm_b16_8 gm_l0; /* tool */
    gm_b16_8 gm_l1; /* command */
    gm_b16_8 gm_l2; /* args */
    gm_t715ajl6o1ipo gm_l3; /* result */
    gm_b16_8 gm_l4; /* e */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 863u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_t715ajl6o1ipo t2;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s6pnfrub0rj1t, 5) == 0)) goto mn4;
        gm_b16_8 t5 = gm_l2;
        gm_t715ajl6o1ipo t6;
        gm_f_fetch_l6q1k6p((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t6;
        goto me3;
mn4:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s3lbmpmt95hvh, 6) == 0)) goto mn7;
        gm_b16_8 t8 = gm_l2;
        gm_t715ajl6o1ipo t9;
        gm_f_update_oi0ok0i((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t9;
        goto me3;
mn7:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s4o70d89183c2, 3) == 0)) goto mn10;
        gm_b16_8 t11 = gm_l2;
        gm_t715ajl6o1ipo t12;
        gm_f_add_04lv2cc((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t12;
        goto me3;
mn10:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sqljkj8r253om, 6) == 0)) goto mn13;
        gm_b16_8 t14 = gm_l2;
        gm_t715ajl6o1ipo t15;
        gm_f_remove_bar6kga((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t15;
        goto me3;
mn13:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sivh46n0690a4, 3) == 0)) goto mn16;
        gm_b16_8 t17 = gm_l2;
        gm_t715ajl6o1ipo t18;
        gm_f_new_package_hf828qc((void *)&t18, (void *)NULL, cx, NULL, t17, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t18;
        goto me3;
mn16:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_suass5sid4b6e, 4) == 0)) goto mn19;
        gm_b16_8 t20 = gm_l2;
        gm_t715ajl6o1ipo t21;
        gm_f_new_package_hf828qc((void *)&t21, (void *)NULL, cx, NULL, t20, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t21;
        goto me3;
mn19:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_so9n4mmd960c7, 5) == 0)) goto mn22;
        gm_b16_8 t23 = gm_l2;
        gm_t715ajl6o1ipo t24;
        gm_f_cache_command_7uqmos8((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t2 = t24;
        goto me3;
mn22:;
        gm_b16_8 t26 = gm_l1;
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_gl2aoqn((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srccr53ejqr1dde, 131, 29);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_t3ujhu6qph7de t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t32);
        gm_t715ajl6o1ipo t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        ((uint8_t *)&t34)[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t34 + 8), t33);
        t2 = t34;
        goto me3;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 123, 26);
me3:;
        gm_l3 = t2;
    }
    {
        if (!((((uint8_t *)&gm_l3))[0] != 0)) goto mn36;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
        if (t37 != ((uint8_t)0ULL)) goto mn36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l3) + 8) + 8));
        gm_l4 = t38;
        {
            gm_b16_8 t40 = gm_l0;
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = gm_l4;
            gm_b16_8 t43 = t42;
            gm_b16_8 t44 = GM_LD(gm_b16_8, gm_crqb3tjd618);
            gm_b16_8 t45 = t44;
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t41), gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t43), gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s8dbdqj1sqibj, 2, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t45), gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t46 = 0;
            F.ret = ((uint8_t)2ULL);
            goto gm_fret;
        }
        goto me35;
mn36:;
        if (!((((uint8_t *)&gm_l3))[0] != 0)) goto mn47;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
        if (t48 != ((uint8_t)1ULL)) goto mn47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l3) + 8) + 8));
        gm_l5 = t49;
        {
            gm_b16_8 t51 = gm_l0;
            gm_b16_8 t52 = t51;
            gm_b16_8 t53 = gm_l5;
            gm_b16_8 t54 = t53;
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t52), gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t54), gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t55 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        goto me35;
mn47:;
        uint8_t t57 = 0;
        F.ret = ((uint8_t)0ULL);
        goto gm_fret;
        goto me35;
mn56:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 133, 5);
me35:;
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
__attribute__((aligned(4))) void gm_f_update_oi0ok0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_update_oi0ok0i F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* e */
    gm_b16_8 gm_l5; /* root */
    gm_tl572d7579hns gm_l6; /* refresh */
    gm_b16_8 gm_l7; /* e */
    GM_STACK_CHECK(&F, 1702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
    }
    {
    }
    {
        gm_b16_8 t2 = gm_l0;
        {
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
            gm_tpmdr2hmdhsqr t5;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
            gm_l1 = t5;
        }
        void * t6 = ((uint8_t *)&gm_l1);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srccr53ejqr1dde, 167, 45);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t8 = t7;
        {
            gm_b0_8 t10;
            gm_l2 = t10;
        }
        void * t11 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srccr53ejqr1dde, 167, 67);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t11 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
        gm_b16_8 t13 = t12;
        gm_t5detta1b4gnk t14;
        gm_f_parse_args_7blm2mp((void *)&t14, (void *)NULL, cx, NULL, t2, t8, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t77ebp3ogdble t15;
        gm_f_usage_failure_cgg6v1t((void *)&t15, &t14);
        gm_t77ebp3ogdble t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_t3ujhu6qph7de t17 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&t16) + 8));
            gm_t715ajl6o1ipo t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_tr8dvlu9ik4gj t19 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t16) + 8));
        F.l3 = t19;
    }
    {
        void * t20 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
        gm_th2lqbo77pvsc t22;
        gm_f_value_of_0km7vvp((void *)&t22, t20, t21);
        gm_th2lqbo77pvsc t23 = t22;
        gm_b16_8 t24;
        if (((((uint8_t *)&t23))[16] != 0)) {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 0));
            t24 = t25;
        } else {
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
            t24 = t26;
        }
        gm_t3ujhu6qph7de t27;
        gm_f_root_of_cluda11((void *)&t27, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t28 = t27;
        gm_b16_8 t29;
        if (!((((uint8_t *)&t28))[0] != 0)) {
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            t29 = t30;
        } else {
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_l4 = t31;
            gm_b16_8 t33 = gm_l4;
            gm_t3ujhu6qph7de t34;
            gm_f_fail_o80k3rv((void *)&t34, t33);
            gm_t715ajl6o1ipo t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t35 + 8), t34);
            gm_t715ajl6o1ipo t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        gm_l5 = t29;
    }
    {
        gm_tl572d7579hns t38;
        uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 0) + 0) + 8);
        uint8_t t41 = ((t40 == ((uint64_t)0ULL)));
        if (!t41) goto ie39;
        gm_tl572d7579hns t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t43 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t43 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)1ULL));
        gm_tl572d7579hns t44 = t43;
        t38 = t44;
        goto ix39;
ie39:;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 0) + 0));
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_tl572d7579hns t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 8), t48);
        gm_tl572d7579hns t50 = t49;
        t38 = t50;
ix39:;
        gm_l6 = t38;
    }
    {
        {
            gm_b16_8 t52 = gm_l5;
            gm_tqs5p4n2tq19v t53;
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t53 + 1), ((uint8_t)0));
            gm_t074vrl1hb4f6 t54;
            gm_f_sync_ht164ep((void *)&t54, (void *)NULL, cx, NULL, t52, t53, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l6)), ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t074vrl1hb4f6 t55 = t54;
            gm_tv37d3dpt2opu t56;
            if (!((((uint8_t *)&t55))[0] != 0)) {
                gm_tv37d3dpt2opu t57 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&t55) + 8));
                t56 = t57;
            } else {
                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                gm_l7 = t58;
                gm_b16_8 t60 = gm_l7;
                gm_t3ujhu6qph7de t61;
                gm_f_fail_o80k3rv((void *)&t61, t60);
                gm_t715ajl6o1ipo t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                ((uint8_t *)&t62)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t62 + 8), t61);
                gm_t715ajl6o1ipo t63 = t62;
                uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
                F.ret = t62;
                goto gm_fret;
            }
        }
    }
    {
        gm_b0_1 t65;
        gm_t715ajl6o1ipo t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
        ((uint8_t *)&t66)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t66 + 8), t65);
        gm_t715ajl6o1ipo t67 = t66;
        uint8_t t68 = ((((uint8_t *)&t67))[0] != 0);
        F.ret = t66;
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
inline __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_usage_failure_cgg6v1t F;
    gm_b16_8 gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 961u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t5detta1b4gnk t2 = GM_LD(gm_t5detta1b4gnk, ((uint8_t *)a0));
        gm_t5detta1b4gnk t3 = t2;
        gm_t77ebp3ogdble t4;
        if (!((((uint8_t *)&t3))[0] != 0)) {
            gm_tr8dvlu9ik4gj t5 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t3) + 8));
            gm_t77ebp3ogdble t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            ((uint8_t *)&t6)[0] = 0;
            GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t6 + 8), t5);
            t4 = t6;
        } else {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            gm_t3ujhu6qph7de t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t77ebp3ogdble t10;
            memset(&t10, 0, sizeof t10);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t10 + 8), t9);
            t4 = t10;
        }
        gm_t77ebp3ogdble t11 = t4;
        uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
        F.ret = t4;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t77ebp3ogdble gm_f_usage_failure_cgg6v1t_c(gm_t5detta1b4gnk a0) { gm_t77ebp3ogdble gm_r; gm_f_usage_failure_cgg6v1t(&gm_r, &a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_value_of_0km7vvp(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_of_0km7vvp F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 40) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            gm_th2lqbo77pvsc t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[16] = 1;
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 47, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l3;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l3 = t32;
                }
            }
lb5:;
        }
    }
    {
        gm_th2lqbo77pvsc t33;
        memset(&t33, 0, sizeof t33);
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_value_of_0km7vvp_c(void * a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_value_of_0km7vvp(&gm_r, a0, a1); return gm_r; }
