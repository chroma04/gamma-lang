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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_t8er5olt969ip;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_b16_8 f11; } gm_th4d8o29g5urm;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { uint64_t f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tkb8hpcqf70t1;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_tjcj4j2gbh6c8 f0; uint8_t f1; uint8_t pe[1]; } gm_tp8mv052i18rg;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8er5olt969ip) == 28 && _Alignof(gm_t8er5olt969ip) == 4 && offsetof(gm_t8er5olt969ip, f0) == 0 && offsetof(gm_t8er5olt969ip, f1) == 4 && offsetof(gm_t8er5olt969ip, f2) == 8 && offsetof(gm_t8er5olt969ip, f3) == 12 && offsetof(gm_t8er5olt969ip, f4) == 16, "layout of `Deferred`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th4d8o29g5urm) == 192 && _Alignof(gm_th4d8o29g5urm) == 8 && offsetof(gm_th4d8o29g5urm, f0) == 0 && offsetof(gm_th4d8o29g5urm, f1) == 16 && offsetof(gm_th4d8o29g5urm, f2) == 32 && offsetof(gm_th4d8o29g5urm, f3) == 48 && offsetof(gm_th4d8o29g5urm, f4) == 64 && offsetof(gm_th4d8o29g5urm, f5) == 80 && offsetof(gm_th4d8o29g5urm, f6) == 96 && offsetof(gm_th4d8o29g5urm, f7) == 112 && offsetof(gm_th4d8o29g5urm, f8) == 128 && offsetof(gm_th4d8o29g5urm, f9) == 144 && offsetof(gm_th4d8o29g5urm, f10) == 160 && offsetof(gm_th4d8o29g5urm, f11) == 176, "layout of `(string, string, string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkb8hpcqf70t1) == 24 && _Alignof(gm_tkb8hpcqf70t1) == 8 && offsetof(gm_tkb8hpcqf70t1, f0) == 0 && offsetof(gm_tkb8hpcqf70t1, f1) == 8, "layout of `NamedDecl`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_tkb8hpcqf70t1 f0; uint8_t f1; uint8_t pe[7]; } gm_toblvhepnjqkh;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tp8mv052i18rg) == 6 && _Alignof(gm_tp8mv052i18rg) == 2 && offsetof(gm_tp8mv052i18rg, f0) == 0 && offsetof(gm_tp8mv052i18rg, f1) == 4, "layout of `?(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_toblvhepnjqkh) == 32 && _Alignof(gm_toblvhepnjqkh) == 8 && offsetof(gm_toblvhepnjqkh, f0) == 0 && offsetof(gm_toblvhepnjqkh, f1) == 24, "layout of `?NamedDecl`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_array_len_eielif2 {
    gm_frame h;
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_t5e6kkuq21nig ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_type_64h714c {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_calls_on_value_q0pa6h5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_len_exprs_m8s6m0h {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* names */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_component_type_q9ge5lo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_comptime_value_l5orc93 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_key_of_q36m6nm {
    gm_frame h;
    gm_t5vbkdq81qfs7 l9; /* r */
    gm_t0ialelq3ql1k l10; /* v */
    gm_tbci5ospk5nnn l11; /* i */
    gm_trqfvmjtt4sij l13; /* bytes */
    gm_trgmlgt2gmmjs ret;
};
struct gm_fr_construction_call_ktqtrpo {
    gm_frame h;
    void * ret;
};
struct gm_fr_decl_is_type_oh0cpn9 {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tv2uf4leil5hq l5; /* b */
    uint8_t ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_empty_slice_1l2vi9a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_enum_array_type_0sic6t0 {
    gm_frame h;
    gm_tckdobunaulee l8; /* v */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fn_ref_7alnaqh {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_tddtjfggdat0d l17; /* have */
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_ty_qaih7ts {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_generic_decl_oir9hm6 {
    gm_frame h;
    gm_toblvhepnjqkh ret;
};
struct gm_fr_generic_type_args_5b8bvov {
    gm_frame h;
    gm_t6art7bprjiqb l5; /* info */
    gm_trqfvmjtt4sij l8; /* given */
    gm_t7te2tdpebbt4 l14; /* a */
    gm_t0mfir12leg2g l16; /* ep */
    gm_tc3oht4ksth9d l17; /* x */
    gm_tc3oht4ksth9d l19; /* x */
    gm_trqfvmjtt4sij l25; /* out */
    gm_t0mfir12leg2g l30; /* ep */
    gm_tc3oht4ksth9d l32; /* g */
    gm_tc3oht4ksth9d l34; /* ex */
    gm_trqfvmjtt4sij l37; /* map */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_hoists_13k2a2s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_type_name_a77a0kf {
    gm_frame h;
    gm_tp8mv052i18rg ret;
};
struct gm_fr_is_builtin_type_name_v5ttoka {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_oversized_int_name_nr25haf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_path_qa34i5j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_len_param_names_cilm68f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_len_params_bound_f7mjvn9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_length_is_type_soi53ah {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9tqbks7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_const_idpokjh {
    gm_frame h;
    gm_tk1gen3hgkm51 ret;
};
struct gm_fr_local_fn_aji8qnd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_looks_like_type_ho5qv1k {
    gm_frame h;
    gm_tv1o1j44ii4db l11; /* el */
    uint8_t ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_names_error_oet8t6b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_names_type_rjdgib9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_prefix_operand_3n5jchh {
    gm_frame h;
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_down_hge098p {
    gm_frame h;
    gm_t2gtbh39ptekd l21; /* kind */
    void * ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pushes_down_0djaflp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_type_jjodbei {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_type_unchecked_psme7dn {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_trqfvmjtt4sij l13; /* edits */
    gm_trqfvmjtt4sij l18; /* ts */
    gm_tv1o1j44ii4db l21; /* el */
    gm_trqfvmjtt4sij l22; /* names */
    gm_tv1o1j44ii4db l25; /* el */
    gm_tqqhoivahirr0 l35; /* f */
    gm_tqqhoivahirr0 l53; /* f */
    gm_tkacitepg5fk0 l82; /* a */
    gm_tet9guhev5r4u l84; /* i */
    gm_tc3oht4ksth9d l86; /* x */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
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
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_syntax_is_type_pe6ugv2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_syntax_module_vho1001 {
    gm_frame h;
    gm_t6qooom8qa2do l9; /* v */
    gm_tv2uf4leil5hq l13; /* b */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
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
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_param_3av4r8i {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_u8_ty_afkvj25 {
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
struct gm_fr_value_param_tof1h6t {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_with_operand_ajbkgld {
    gm_frame h;
    gm_t2gtbh39ptekd l10; /* kind */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_il5kbp4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o2s8o9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o5o5km2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oq3rvor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pplcoh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uhvct7c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_um7orcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_up1v1ae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_afk8dfq(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k0n7eot(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3n1bpabq7f[40] __attribute__((aligned(8)));
extern uint8_t gm_k6r9cr7441u[40] __attribute__((aligned(8)));
extern uint8_t gm_k8b8p636a5m[40] __attribute__((aligned(8)));
extern uint8_t gm_kijoa5b5euq[40] __attribute__((aligned(8)));
extern uint8_t gm_klh5uk0bvu4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s2328gjukbu28[16];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9rkpo2ljmui9[50];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgrpmaenv85i1[73];
extern const uint8_t gm_sgva3apqq9jo2[68];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_sjdn5gs7lu39i[57];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_sldikqa9i70sm[52];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm7179tmr0nhi[27];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_sm9funvq1222p[44];
extern const uint8_t gm_smi7k9v6u7cmo[11];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_ssj1a8k9nspq4[82];
extern const uint8_t gm_stevggi5tvq7c[18];
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
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 116u);
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
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
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
    GM_STACK_CHECK(&F, 60u);
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
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 176, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
        void * t11 = gm_l3;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_l2;
        uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t1, t2, t3, t4, t5, ((uint8_t)0));
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
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1l2vi9a F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 96)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 0, (((uint8_t *)t2) + 1568), 40);
        void * t3 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 40, (((uint8_t *)t3) + 1608), 40);
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 1568);
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint8_t *t7 = (((uint8_t *)t6) + 1608);
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)t7), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_hoists_13k2a2s(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        void * t10 = gm_f_prefix_operand_3n5jchh(t9);
        void * t11 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = 0;
        F.ret = t11;
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
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1568), t3);
    }
    {
        void * t4 = gm_l0;
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 40));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t4) + 1608), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9tqbks7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1l2vi9a();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)5ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 256) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 256) + 0)) + t3 * 16));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        gm_tep2drsl9408b t4;
        memset((uint8_t *)&t4 + 20, 0, 4);
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_t88rcucg1b7uk t6 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcsffver18si54, 63, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 1488) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1608) + 0) + 8);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1568) + 0) + 8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0, t2);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16, t6);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24, t8);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tabb710d9mq2e t5;
        gm_f_into_slice_hq0te56((void *)&t5, t4);
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        uint8_t t8 = 0;
        F.ret = t7;
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
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)6ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 216u);
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
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t11);
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_array_len_eielif2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint32_t gm_l4; /* pid */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint64_t gm_l8; /* n */
    void * gm_l9; /* hook */
    GM_STACK_CHECK(&F, 1792u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l3 = t5;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_t9805qtonbsag t10 = gm_f_value_param_tof1h6t(t8, t9);
            gm_t9805qtonbsag t11 = t10;
            if (!((((uint8_t *)&t11))[4] != 0)) goto ie7;
            uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&t11));
            gm_l4 = t12;
            {
                uint32_t t15 = gm_l4;
                uint64_t t16 = (uint64_t)t15;
                uint64_t t17 = (uintptr_t)t16;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 1128) + 0) + 8);
                if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcsffver18si54, 634, 26);
                uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 1128) + 0)) + t17 * 48) + 0) + 0));
                if (t20 != ((uint8_t)2ULL)) goto ie14;
                gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 1128) + 0)) + t17 * 48) + 0) + 4));
                gm_l5 = t21;
                {
                    void * t24 = gm_l0;
                    void * t25 = (void *)(((uint8_t *)t24) + 8);
                    gm_t88rcucg1b7uk t26 = gm_l5;
                    uint8_t t27 = gm_f_is_integer_8d4f9ek(t25, GM_CONV(gm_csguankko4lmhd, t26));
                    uint8_t t28 = (uint8_t)!t27;
                    if (!t28) goto ie23;
                    {
                        void * t30 = gm_l0;
                        void * t31 = gm_l2;
                        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                        gm_tnpifv20irnph t34;
                        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
                        gm_tabb710d9mq2e t35;
                        gm_f_format_bunqlnl((void *)&t35, (void *)NULL, cx, NULL, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t36 = t35;
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcsffver18si54, 636, 33);
                        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                        gm_b16_8 t38 = t37;
                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                        gm_b0_1 t40 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t30, t32, t39);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_t5e6kkuq21nig t41;
                        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)2ULL));
                        uint8_t t42 = 0;
                        F.ret = t41;
                        goto gm_fret;
                    }
                    goto ix23;
ie23:;
ix23:;
                }
                {
                    void * t43 = gm_l0;
                    void * t44 = (void *)(((uint8_t *)t43) + 8);
                    uint32_t t45 = gm_l4;
                    gm_t88rcucg1b7uk t46 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t44, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5e6kkuq21nig t47;
                    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 8), t46);
                    uint8_t t48 = 0;
                    F.ret = t47;
                    goto gm_fret;
                }
                goto ix14;
ie14:;
                {
                    gm_t5e6kkuq21nig t50;
                    GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t50 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)2ULL));
                    uint8_t t51 = 0;
                    F.ret = t50;
                    goto gm_fret;
                }
ix14:;
            }
            goto ix7;
ie7:;
ix7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l1;
        void * t54 = gm_l2;
        gm_tkv1vap90vht2 t55;
        gm_f_try_untyped_fj0h8i5((void *)&t55, (void *)NULL, cx, NULL, t52, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkv1vap90vht2 t56 = t55;
        if (!((((uint8_t *)&t56))[208] != 0)) goto mn58;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t59 != ((uint8_t)0ULL)) goto mn58;
        gm_t0ialelq3ql1k t60 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t56) + 8));
        F.l6 = t60;
        {
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t63 != ((uint8_t)0ULL)) goto ie62;
            gm_tbci5ospk5nnn t64 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 8));
            F.l7 = t64;
            {
                gm_tf5srh4mp4b3i t67 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                gm_tf5srh4mp4b3i t68 = t67;
                if (!((((uint8_t *)&t68))[8] != 0)) goto ie66;
                uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&t68));
                gm_l8 = t69;
                {
                    uint64_t t71 = gm_l8;
                    gm_t5e6kkuq21nig t72;
                    GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t72 + 8), t71);
                    uint8_t t73 = 0;
                    F.ret = t72;
                    goto gm_fret;
                }
                goto ix66;
ie66:;
ix66:;
            }
            {
                void * t74 = gm_l0;
                void * t75 = gm_l2;
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                gm_b16_8 t77 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t78;
                GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t77);
                gm_tabb710d9mq2e t79;
                gm_f_format_uhvct7c((void *)&t79, (void *)NULL, cx, NULL, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t80 = t79;
                if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srcsffver18si54, 646, 33);
                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                gm_b16_8 t82 = t81;
                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                gm_b0_1 t84 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t74, t76, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t5e6kkuq21nig t85;
                GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)2ULL));
                uint8_t t86 = 0;
                F.ret = t85;
                goto gm_fret;
            }
            goto ix62;
ie62:;
ix62:;
        }
        {
            void * t87 = gm_l0;
            void * t88 = gm_l2;
            gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t88) + 72));
            void * t90 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t91 = gm_f_describe_bm129vh(t90);
            gm_tnpifv20irnph t92;
            GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
            gm_tabb710d9mq2e t93;
            gm_f_format_up1v1ae((void *)&t93, (void *)NULL, cx, NULL, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t94 = t93;
            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 8, gm_srcsffver18si54, 649, 29);
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
            gm_b16_8 t96 = t95;
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            gm_b0_1 t98 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t87, t89, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t5e6kkuq21nig t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t99 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)2ULL));
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
        }
        goto me57;
mn58:;
        if (!((((uint8_t *)&t56))[208] != 0)) goto mn101;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t102 != ((uint8_t)1ULL)) goto mn101;
        gm_t5e6kkuq21nig t103;
        GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t103 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)2ULL));
        uint8_t t104 = 0;
        F.ret = t103;
        goto gm_fret;
        goto me57;
mn101:;
        if (((((uint8_t *)&t56))[208] != 0)) goto mn105;
        {
            void * t107 = gm_l0;
            void * t108 = GM_LD(void *, (((uint8_t *)t107) + 2176));
            void * t109 = t108;
            void * t110;
            if ((GM_LD(void *, ((uint8_t *)&t109)) != 0)) {
                void * t111 = GM_LD(void *, (((uint8_t *)&t109) + 0));
                t110 = t111;
            } else {
                {
                    void * t113 = gm_l0;
                    void * t114 = gm_l2;
                    gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t114) + 72));
                    gm_b16_8 t116;
                    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s9rkpo2ljmui9));
                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(49));
                    gm_b0_1 t117 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t113, t115, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_t5e6kkuq21nig t118;
                GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t118 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)2ULL));
                uint8_t t119 = 0;
                F.ret = t118;
                goto gm_fret;
            }
            gm_l9 = t110;
        }
        {
            void * t120 = gm_l9;
            void * t121 = gm_l0;
            uint64_t t122 = gm_l1;
            void * t123 = gm_l2;
            void * t124 = t120;
            gm_t5e6kkuq21nig t125;
            if (GM_PTR_TAGGED(t124)) {
                t125 = ((gm_t5e6kkuq21nig (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t124))(t121, t122, t123);
            } else {
                t125 = ((gm_t5e6kkuq21nig (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t124)(NULL, cx, NULL, t121, t122, t123);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t126 = 0;
            F.ret = t125;
            goto gm_fret;
        }
        goto me57;
mn105:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 642, 5);
me57:;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_builtin_type_64h714c F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjcj4j2gbh6c8 gm_l2; /* i */
    GM_STACK_CHECK(&F, 761u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l1;
        gm_tp8mv052i18rg t4 = gm_f_int_type_name_a77a0kf(t3);
        gm_tp8mv052i18rg t5 = t4;
        if (!((((uint8_t *)&t5))[4] != 0)) goto ie2;
        gm_tjcj4j2gbh6c8 t6 = GM_LD(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint16_t t11 = GM_LD(uint16_t, (((uint8_t *)&gm_l2) + 2));
            gm_t88rcucg1b7uk t12 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13), t12);
            ((uint8_t *)&t13)[4] = 1;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tkn42u7na3t81 t15;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s6groq37ram3h, 5) == 0)) goto mn17;
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 8);
        gm_t88rcucg1b7uk t20 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[4] = 1;
        t15 = t21;
        goto me16;
mn17:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s0i3i86nphpsh, 5) == 0)) goto mn22;
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_t53at80pfdus3 t25;
        memset(&t25, 0, sizeof t25);
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)2ULL));
        gm_t88rcucg1b7uk t26 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t24, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
        ((uint8_t *)&t27)[4] = 1;
        t15 = t27;
        goto me16;
mn22:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_shnig5tqtdedi, 3) == 0)) goto mn28;
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 8);
        gm_t53at80pfdus3 t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t31 + 8), ((uint16_t)16ULL));
        gm_t88rcucg1b7uk t32 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t30, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33), t32);
        ((uint8_t *)&t33)[4] = 1;
        t15 = t33;
        goto me16;
mn28:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sm9e81djuncnu, 3) == 0)) goto mn34;
        void * t35 = gm_l0;
        void * t36 = (void *)(((uint8_t *)t35) + 8);
        gm_t53at80pfdus3 t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t37 + 8), ((uint16_t)32ULL));
        gm_t88rcucg1b7uk t38 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t36, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39), t38);
        ((uint8_t *)&t39)[4] = 1;
        t15 = t39;
        goto me16;
mn34:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sqqajgod2bb4o, 3) == 0)) goto mn40;
        void * t41 = gm_l0;
        void * t42 = (void *)(((uint8_t *)t41) + 8);
        gm_t53at80pfdus3 t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t43 + 8), ((uint16_t)64ULL));
        gm_t88rcucg1b7uk t44 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t42, &t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t45), t44);
        ((uint8_t *)&t45)[4] = 1;
        t15 = t45;
        goto me16;
mn40:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s929lennfd3g6, 4) == 0)) goto mn46;
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 8);
        gm_t88rcucg1b7uk t49 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
        ((uint8_t *)&t50)[4] = 1;
        t15 = t50;
        goto me16;
mn46:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s3cv29r83pgn4, 5) == 0)) goto mn51;
        void * t52 = gm_l0;
        void * t53 = (void *)(((uint8_t *)t52) + 8);
        gm_t88rcucg1b7uk t54 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t55), t54);
        ((uint8_t *)&t55)[4] = 1;
        t15 = t55;
        goto me16;
mn51:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s2737i1iq57nf, 4) == 0)) goto mn56;
        void * t57 = gm_l0;
        void * t58 = (void *)(((uint8_t *)t57) + 8);
        gm_t88rcucg1b7uk t59 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t60), t59);
        ((uint8_t *)&t60)[4] = 1;
        t15 = t60;
        goto me16;
mn56:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_slolcr6ikvg33, 6) == 0)) goto mn61;
        void * t62 = gm_l0;
        void * t63 = (void *)(((uint8_t *)t62) + 8);
        gm_t53at80pfdus3 t64;
        memset(&t64, 0, sizeof t64);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t65 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t63, &t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66), t65);
        ((uint8_t *)&t66)[4] = 1;
        t15 = t66;
        goto me16;
mn61:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s0iqiubfm7e7e, 6) == 0)) goto mn67;
        void * t68 = gm_l0;
        void * t69 = (void *)(((uint8_t *)t68) + 8);
        gm_t88rcucg1b7uk t70 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t71), t70);
        ((uint8_t *)&t71)[4] = 1;
        t15 = t71;
        goto me16;
mn67:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s47llp68ptjgu, 7) == 0)) goto mn72;
        void * t73 = gm_l0;
        void * t74 = (void *)(((uint8_t *)t73) + 8);
        void * t75 = gm_l0;
        void * t76 = (void *)(((uint8_t *)t75) + 8);
        gm_t88rcucg1b7uk t77 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t78 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t74, ((uint8_t)1), t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t79), t78);
        ((uint8_t *)&t79)[4] = 1;
        t15 = t79;
        goto me16;
mn72:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sflee3fpg0btv, 4) == 0)) goto mn80;
        void * t81 = gm_l0;
        void * t82 = (void *)(((uint8_t *)t81) + 8);
        gm_t88rcucg1b7uk t83 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t82, ((uint8_t)0), ((uint16_t)32ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t84;
        GM_ST(uint8_t, (uint8_t *)&t84 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t84 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t84), t83);
        ((uint8_t *)&t84)[4] = 1;
        t15 = t84;
        goto me16;
mn80:;
        gm_tkn42u7na3t81 t86;
        memset(&t86, 0, sizeof t86);
        t15 = t86;
        goto me16;
mn85:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 31, 12);
me16:;
        uint8_t t87 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_calls_on_value_q0pa6h5 F;
    void * gm_l0; /* callee */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma4; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)22ULL)) goto mn9;
    void * t11 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l1 = t11;
    void * t12 = gm_l1;
    uint8_t t13 = gm_f_is_path_qa34i5j(t12);
    uint8_t t14 = (uint8_t)!t13;
    t2 = t14;
    goto me3;
mn9:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)20ULL)) goto mn15;
    t2 = ((uint8_t)1);
    goto me3;
mn15:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn17:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 270, 58);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2) {
    struct gm_fr_check_len_exprs_m8s6m0h F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* map */
    gm_toceffb697p0u gm_l2; /* at */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t2i8ne5ibd7v4 gm_l6; /* le */
    gm_tl874t2atnlh3 gm_l9; /* a */
    GM_STACK_CHECK(&F, 682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2192) + 0) + 8);
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
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 2192) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcsffver18si54, 668, 15);
                        gm_t2i8ne5ibd7v4 t21 = GM_LD(gm_t2i8ne5ibd7v4, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 2192) + 0)) + t18 * 16));
                        gm_l6 = t21;
                    }
                    {
                        uint8_t t23 = 0;
                        void * t24 = gm_l0;
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t26 = gm_l1;
                        uint8_t t27 = gm_f_len_params_bound_f7mjvn9(t24, GM_CONV(gm_csguankko4lmhd, t25), t26);
                        uint8_t t28 = (uint8_t)!t27;
                        if (!t28) {
                            void * t29 = gm_l0;
                            void * t30 = (void *)(((uint8_t *)t29) + 8);
                            void * t31 = gm_l0;
                            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                            gm_b16_8 t33 = gm_l1;
                            gm_t88rcucg1b7uk t34 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t31, t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            uint8_t t35 = gm_f_is_error_8k27gd1(t30, GM_CONV(gm_csguankko4lmhd, t34));
                            uint8_t t36 = (uint8_t)!t35;
                            t23 = t36;
                        } else t23 = 1;
                        if (!t23) goto ie22;
                        {
                            goto lc5;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t38 = gm_l0;
                        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t40 = gm_l1;
                        void * t41 = (void *)((uint8_t *)&F.l7);
                        gm_b0_1 t42 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t38, t39, t40, t41);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
                        gm_t6j6smj7qoctu t44;
                        void * t45 = gm_l0;
                        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t47 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t47);
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t49 = t48;
                        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                        gm_b16_8 t51;
                        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(2));
                        gm_tabb710d9mq2e t52;
                        gm_f_join_sfknljb((void *)&t52, (void *)NULL, cx, NULL, t50, t51);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tabb710d9mq2e t53 = t52;
                        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcsffver18si54, 672, 129);
                        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t44 + 16, t54);
                        gm_tabb710d9mq2e t55;
                        gm_f_format_rd9dflp((void *)&t55, (void *)NULL, cx, NULL, &t44);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tabb710d9mq2e t56 = t55;
                        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcsffver18si54, 672, 34);
                        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                        gm_b16_8 t58 = t57;
                        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                        gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t43, t59);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        if (!((((uint8_t *)&gm_l2))[12] != 0)) goto ie60;
                        gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l2));
                        gm_l9 = t61;
                        {
                            void * t63 = (void *)((uint8_t *)&F.l8);
                            gm_tl874t2atnlh3 t64 = gm_l9;
                            gm_b16_8 t65;
                            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_stevggi5tvq7c));
                            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(17));
                            gm_f_add_label_50fj7vv(t63, GM_CONV(gm_cseahvg8v7p0vs, t64), t65);
                        }
                        goto ix60;
ie60:;
ix60:;
                    }
                    {
                        void * t66 = gm_l0;
                        gm_b0_1 t67 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t66, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 667, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t69 = gm_l4;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l4 = t72;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_component_type_q9ge5lo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* text */
    GM_STACK_CHECK(&F, 435u);
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
        gm_t88rcucg1b7uk t5 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l3;
        gm_t53at80pfdus3 t10;
        gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
        gm_t53at80pfdus3 t11 = t10;
        uint8_t t12;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t15 != ((uint8_t)15ULL)) goto mn14;
        t12 = ((uint8_t)1);
        goto me13;
mn14:;
        t12 = ((uint8_t)0);
        goto me13;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 546, 8);
me13:;
        if (!t12) goto ie6;
        {
            void * t18 = gm_l0;
            gm_t88rcucg1b7uk t19 = gm_l3;
            gm_b16_8 t20 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t20;
        }
        {
            void * t21 = gm_l0;
            void * t22 = gm_l2;
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
            gm_t6j6smj7qoctu t24;
            gm_b16_8 t25 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
            gm_b16_8 t26 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t24 + 16, t26);
            gm_tabb710d9mq2e t27;
            gm_f_format_um7orcc((void *)&t27, (void *)NULL, cx, NULL, &t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t28 = t27;
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcsffver18si54, 548, 25);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_b16_8 t30 = t29;
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_b0_1 t32 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t21, t23, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_t88rcucg1b7uk t33 = gm_l3;
        uint8_t t34 = 0;
        F.ret = t33;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_comptime_value_l5orc93 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* e */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint32_t gm_l5; /* pid */
    gm_t88rcucg1b7uk gm_l6; /* have */
    gm_t428ue2u3hf5g gm_l7; /* key */
    void * gm_l8; /* hook */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* cell */
    gm_t428ue2u3hf5g gm_l11; /* key */
    GM_STACK_CHECK(&F, 1515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l3;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l4 = t5;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_t9805qtonbsag t10 = gm_f_value_param_tof1h6t(t8, t9);
            gm_t9805qtonbsag t11 = t10;
            if (!((((uint8_t *)&t11))[4] != 0)) goto ie7;
            uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&t11));
            gm_l5 = t12;
            {
                uint32_t t15 = gm_l5;
                uint64_t t16 = (uint64_t)t15;
                uint64_t t17 = (uintptr_t)t16;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 1128) + 0) + 8);
                if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcsffver18si54, 800, 29);
                uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 1128) + 0)) + t17 * 48) + 0) + 0));
                if (t20 != ((uint8_t)2ULL)) goto ie14;
                gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 1128) + 0)) + t17 * 48) + 0) + 4));
                gm_l6 = t21;
                {
                    gm_t88rcucg1b7uk t24 = gm_l6;
                    gm_t88rcucg1b7uk t25 = gm_l2;
                    gm_t88rcucg1b7uk t26 = t24;
                    gm_t88rcucg1b7uk t27 = t25;
                    uint8_t t28 = (!gm_eq9m6mvtmgde(((uint8_t *)&t26), ((uint8_t *)&t27)));
                    if (!t28) goto ie23;
                    gm_t88rcucg1b7uk t29 = gm_l2;
                    void * t30 = gm_l0;
                    void * t31 = (void *)(((uint8_t *)t30) + 8);
                    gm_t88rcucg1b7uk t32 = gm_l6;
                    gm_t88rcucg1b7uk t33 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t31, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t34 = t29;
                    gm_t88rcucg1b7uk t35 = t33;
                    uint8_t t36 = (!gm_eq9m6mvtmgde(((uint8_t *)&t34), ((uint8_t *)&t35)));
                    if (!t36) goto ie23;
                    {
                        void * t38 = gm_l0;
                        void * t39 = gm_l3;
                        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
                        gm_tfeab279c16rr t41;
                        void * t42 = gm_l0;
                        gm_t88rcucg1b7uk t43 = gm_l2;
                        gm_b16_8 t44 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t44);
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t45);
                        void * t46 = gm_l0;
                        gm_t88rcucg1b7uk t47 = gm_l6;
                        gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t41 + 32, t48);
                        gm_tabb710d9mq2e t49;
                        gm_f_format_c3isg16((void *)&t49, (void *)NULL, cx, NULL, &t41);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t50 = t49;
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcsffver18si54, 802, 33);
                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                        gm_b16_8 t52 = t51;
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                        gm_b0_1 t54 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t38, t40, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_tkn42u7na3t81 t55;
                        memset(&t55, 0, sizeof t55);
                        uint8_t t56 = 0;
                        F.ret = t55;
                        goto gm_fret;
                    }
                    goto ix23;
ie23:;
ix23:;
                }
                {
                    void * t57 = gm_l0;
                    void * t58 = (void *)(((uint8_t *)t57) + 8);
                    uint32_t t59 = gm_l5;
                    gm_t88rcucg1b7uk t60 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t58, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkn42u7na3t81 t61;
                    GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61), t60);
                    ((uint8_t *)&t61)[4] = 1;
                    uint8_t t62 = 0;
                    F.ret = t61;
                    goto gm_fret;
                }
                goto ix14;
ie14:;
                {
                    gm_tkn42u7na3t81 t64;
                    memset(&t64, 0, sizeof t64);
                    uint8_t t65 = 0;
                    F.ret = t64;
                    goto gm_fret;
                }
ix14:;
            }
            goto ix7;
ie7:;
ix7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t67 = gm_l0;
        uint64_t t68 = gm_l1;
        void * t69 = gm_l3;
        gm_t88rcucg1b7uk t70 = gm_l2;
        gm_trgmlgt2gmmjs t71;
        gm_f_const_key_of_q36m6nm((void *)&t71, (void *)NULL, cx, NULL, t67, t68, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trgmlgt2gmmjs t72 = t71;
        if (!((((uint8_t *)&t72))[32] != 0)) goto ie66;
        gm_t428ue2u3hf5g t73 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&t72));
        gm_l7 = t73;
        {
            void * t75 = gm_l0;
            void * t76 = (void *)(((uint8_t *)t75) + 8);
            gm_t428ue2u3hf5g t77 = gm_l7;
            gm_t53at80pfdus3 t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t78 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t78 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t78 + 8), t77);
            gm_t88rcucg1b7uk t79 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t76, &t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t80), t79);
            ((uint8_t *)&t80)[4] = 1;
            uint8_t t81 = 0;
            F.ret = t80;
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        void * t83 = gm_l0;
        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 2168));
        void * t85 = t84;
        if (!(GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto ie82;
        void * t86 = GM_LD(void *, ((uint8_t *)&t85));
        gm_l8 = t86;
        void * t87 = gm_l0;
        void * t88 = (void *)(((uint8_t *)t87) + 8);
        gm_t88rcucg1b7uk t89 = gm_l2;
        uint8_t t90 = gm_f_has_param_u9t0efs(t88, GM_CONV(gm_csguankko4lmhd, t89));
        uint8_t t91 = (uint8_t)!t90;
        if (!t91) goto ie82;
        {
            void * t93 = gm_l8;
            void * t94 = gm_l0;
            uint64_t t95 = gm_l1;
            void * t96 = gm_l3;
            gm_t88rcucg1b7uk t97 = gm_l2;
            gm_tkn42u7na3t81 t98;
            GM_ST(uint8_t, (uint8_t *)&t98 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t98 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t98), t97);
            ((uint8_t *)&t98)[4] = 1;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s33hvuvcdjs1b));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(10));
            void * t100 = gm_l3;
            gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 72));
            void * t102 = t93;
            gm_t9805qtonbsag t103;
            if (GM_PTR_TAGGED(t102)) {
                t103 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t102))(t94, t95, t96, t98, t99, GM_CONV(gm_cseahvg8v7p0vs, t101));
            } else {
                t103 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t102)(NULL, cx, NULL, t94, t95, t96, t98, t99, t101);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t104 = t103;
            uint32_t t105;
            if (((((uint8_t *)&t104))[4] != 0)) {
                uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&t104) + 0));
                t105 = t106;
            } else {
                gm_tkn42u7na3t81 t107;
                memset(&t107, 0, sizeof t107);
                uint8_t t108 = 0;
                F.ret = t107;
                goto gm_fret;
            }
            gm_l9 = t105;
        }
        {
            uint32_t t109 = gm_l9;
            uint64_t t110 = (uint64_t)t109;
            uint64_t t111 = (uintptr_t)t110;
            void * t112 = gm_l0;
            uint64_t t113 = GM_LD(uintptr_t, ((((uint8_t *)t112) + 1968) + 0) + 8);
            if (t111 >= (uintptr_t)(t113)) gm_trap_index(t111, t113, gm_srcsffver18si54, 811, 17);
            void * t114 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t112) + 1968) + 0)) + t111 * 88) + 80));
            void * t115 = t114;
            void * t116;
            if ((GM_LD(void *, ((uint8_t *)&t115)) != 0)) {
                void * t117 = GM_LD(void *, (((uint8_t *)&t115) + 0));
                t116 = t117;
            } else {
                gm_tkn42u7na3t81 t118;
                memset(&t118, 0, sizeof t118);
                uint8_t t119 = 0;
                F.ret = t118;
                goto gm_fret;
            }
            gm_l10 = t116;
        }
        {
            void * t120 = gm_l0;
            void * t121 = gm_l10;
            gm_t88rcucg1b7uk t122 = gm_l2;
            void * t123 = gm_l3;
            gm_tl874t2atnlh3 t124 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t123) + 72));
            gm_trgmlgt2gmmjs t125;
            gm_f_value_key_vki574i((void *)&t125, (void *)NULL, cx, NULL, t120, &(*(gm_t6evimt84lndq *)(((uint8_t *)t121) + 0)), t122, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trgmlgt2gmmjs t126 = t125;
            gm_t428ue2u3hf5g t127;
            if (((((uint8_t *)&t126))[32] != 0)) {
                gm_t428ue2u3hf5g t128 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t126) + 0));
                t127 = t128;
            } else {
                gm_tkn42u7na3t81 t129;
                memset(&t129, 0, sizeof t129);
                uint8_t t130 = 0;
                F.ret = t129;
                goto gm_fret;
            }
            gm_l11 = t127;
        }
        {
            void * t131 = gm_l0;
            void * t132 = (void *)(((uint8_t *)t131) + 8);
            gm_t428ue2u3hf5g t133 = gm_l11;
            gm_t53at80pfdus3 t134;
            GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t134 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t134 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t134 + 8), t133);
            gm_t88rcucg1b7uk t135 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t132, &t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t136;
            GM_ST(uint8_t, (uint8_t *)&t136 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t136 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136), t135);
            ((uint8_t *)&t136)[4] = 1;
            uint8_t t137 = 0;
            F.ret = t136;
            goto gm_fret;
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        void * t138 = gm_l0;
        void * t139 = gm_l3;
        gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t139) + 72));
        gm_b16_8 t141;
        GM_ST(void *, (uint8_t *)&t141, (void *)(gm_sm7179tmr0nhi));
        GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(26));
        gm_b0_1 t142 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t138, t140, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t143;
        memset(&t143, 0, sizeof t143);
        uint8_t t144 = 0;
        F.ret = t143;
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
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_key_of_q36m6nm F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    uint32_t gm_l5; /* f */
    void * gm_l6; /* x */
    void * gm_l7; /* inner */
    gm_tibupt4dmcqai gm_l8; /* lit */
    uint8_t gm_l12; /* b */
    GM_STACK_CHECK(&F, 2584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l2;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 8) + 0) == ((uint8_t)4ULL)));
        if (!t4) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l3;
            gm_t53at80pfdus3 t10;
            gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
            gm_t53at80pfdus3 t11 = t10;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
            if (t12 != ((uint8_t)13ULL)) goto ie6;
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t11) + 8));
            gm_l4 = t13;
            void * t14 = gm_l0;
            gm_t88rcucg1b7uk t15 = gm_l4;
            uint8_t t16 = gm_f_is_nullable_pointer_nkie06s(t14, GM_CONV(gm_csguankko4lmhd, t15));
            if (!t16) goto ie6;
            {
                gm_t428ue2u3hf5g t18;
                GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)2ULL));
                gm_trgmlgt2gmmjs t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t19), t18);
                ((uint8_t *)&t19)[32] = 1;
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_trgmlgt2gmmjs t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = gm_l1;
        void * t26 = gm_l2;
        gm_t88rcucg1b7uk t27 = gm_l3;
        gm_t9805qtonbsag t28 = gm_f_fn_ref_7alnaqh((void *)NULL, cx, NULL, t24, t25, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t29 = t28;
        if (!((((uint8_t *)&t29))[4] != 0)) goto ie23;
        uint32_t t30 = GM_LD(uint32_t, ((uint8_t *)&t29));
        gm_l5 = t30;
        {
            gm_tiilmfd16v3fc t32;
            memset((uint8_t *)&t32 + 4, 0, 4);
            uint32_t t33 = gm_l5;
            GM_ST(uint32_t, (uint8_t *)&t32 + 0, t33);
            uint32_t t34 = gm_l5;
            uint64_t t35 = (uint64_t)t34;
            uint64_t t36 = (uintptr_t)t35;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 344) + 0) + 8);
            if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_srcsffver18si54, 826, 53);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t37) + 344) + 0)) + t36 * 136) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t32 + 8, t39);
            gm_t428ue2u3hf5g t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t40 + 8), t32);
            gm_trgmlgt2gmmjs t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t41), t40);
            ((uint8_t *)&t41)[32] = 1;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_t88rcucg1b7uk t46 = gm_l3;
        uint8_t t47 = gm_f_has_param_u9t0efs(t45, GM_CONV(gm_csguankko4lmhd, t46));
        if (!t47) goto ie43;
        {
            gm_trgmlgt2gmmjs t49;
            memset(&t49, 0, sizeof t49);
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t51 = gm_l2;
        gm_l6 = t51;
    }
    {
        for (;;) {
            void * t53 = gm_l6;
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
            if (t54 != ((uint8_t)9ULL)) goto lb52;
            void * t55 = GM_LD(void *, ((((uint8_t *)t53) + 8) + 8));
            gm_l7 = t55;
            {
                void * t58 = gm_l7;
                gm_l6 = t58;
            }
lc52:;
        }
lb52:;
    }
    {
        void * t60 = gm_l6;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
        if (t61 != ((uint8_t)2ULL)) goto ie59;
        gm_tibupt4dmcqai t62 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t60) + 8) + 8));
        gm_l8 = t62;
        uint8_t t63 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 16) + 0) == ((uint8_t)2ULL)));
        if (!t63) goto ie59;
        gm_t88rcucg1b7uk t64 = gm_l3;
        void * t65 = gm_l0;
        void * t66 = (void *)(((uint8_t *)t65) + 8);
        gm_t88rcucg1b7uk t67 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t68 = t64;
        gm_t88rcucg1b7uk t69 = t67;
        uint8_t t70 = (gm_eq9m6mvtmgde(((uint8_t *)&t68), ((uint8_t *)&t69)));
        if (!t70) goto ie59;
        {
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
            gm_b16_8 t73 = t72;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_t428ue2u3hf5g t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t75 + 8), t74);
            gm_trgmlgt2gmmjs t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t76), t75);
            ((uint8_t *)&t76)[32] = 1;
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t78 = gm_l0;
        uint64_t t79 = gm_l1;
        void * t80 = gm_l2;
        gm_tkv1vap90vht2 t81;
        gm_f_untyped_if_plain_lbrp24p((void *)&t81, (void *)NULL, cx, NULL, t78, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkv1vap90vht2 t82 = t81;
        gm_t5vbkdq81qfs7 t83;
        if (((((uint8_t *)&t82))[208] != 0)) {
            gm_t5vbkdq81qfs7 t84 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t82) + 0));
            t83 = t84;
        } else {
            gm_trgmlgt2gmmjs t85;
            memset(&t85, 0, sizeof t85);
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        F.l9 = t83;
    }
    {
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        if (t88 != ((uint8_t)0ULL)) goto ie87;
        gm_t0ialelq3ql1k t89 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l9) + 8));
        F.l10 = t89;
        {
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t96) { case ((uint8_t)0ULL): goto ma92; case ((uint8_t)1ULL): goto ma94; case ((uint8_t)2ULL): goto ma94; case ((uint8_t)3ULL): goto ma93; case ((uint8_t)4ULL): goto ma94; default: goto ma94; }
ma92:;
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t98 != ((uint8_t)0ULL)) goto mn97;
            gm_tbci5ospk5nnn t99 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l10) + 8));
            F.l11 = t99;
            {
                uint8_t t102 = 0;
                void * t103 = gm_l0;
                void * t104 = (void *)(((uint8_t *)t103) + 8);
                gm_t88rcucg1b7uk t105 = gm_l3;
                uint8_t t106 = gm_f_is_integer_8d4f9ek(t104, GM_CONV(gm_csguankko4lmhd, t105));
                uint8_t t107 = (uint8_t)!t106;
                if (!t107) {
                    void * t108 = gm_l0;
                    gm_t88rcucg1b7uk t109 = gm_l3;
                    uint8_t t110 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t108, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), t109);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t111 = (uint8_t)!t110;
                    t102 = t111;
                } else t102 = 1;
                if (!t102) goto ie101;
                {
                    gm_trgmlgt2gmmjs t113;
                    memset(&t113, 0, sizeof t113);
                    uint8_t t114 = 0;
                    F.ret = t113;
                    goto gm_fret;
                }
                goto ix101;
ie101:;
ix101:;
            }
            {
                void * t115 = gm_l0;
                gm_t88rcucg1b7uk t116 = gm_l3;
                gm_t428ue2u3hf5g t117;
                gm_f_int_key_dmcraup((void *)&t117, (void *)NULL, cx, NULL, t115, t116, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trgmlgt2gmmjs t118;
                GM_ST(uint8_t, (uint8_t *)&t118 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t118 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t118 + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t118), t117);
                ((uint8_t *)&t118)[32] = 1;
                uint8_t t119 = 0;
                F.ret = t118;
                goto gm_fret;
            }
            goto me91;
mn97:;
ma93:;
            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t121 != ((uint8_t)3ULL)) goto mn120;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 8));
            gm_l12 = t122;
            {
                void * t125 = gm_l0;
                void * t126 = (void *)(((uint8_t *)t125) + 8);
                gm_t88rcucg1b7uk t127 = gm_l3;
                gm_t53at80pfdus3 t128;
                gm_f_kind_jcnt4hm((void *)&t128, t126, GM_CONV(gm_csguankko4lmhd, t127));
                gm_t53at80pfdus3 t129 = t128;
                uint8_t t130 = (!(GM_LD(uint8_t, ((uint8_t *)&t129) + 0) == ((uint8_t)4ULL)));
                if (!t130) goto ie124;
                {
                    gm_trgmlgt2gmmjs t132;
                    memset(&t132, 0, sizeof t132);
                    uint8_t t133 = 0;
                    F.ret = t132;
                    goto gm_fret;
                }
                goto ix124;
ie124:;
ix124:;
            }
            {
                gm_f_list_uvfu1k1((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t134 = (void *)((uint8_t *)&F.l13);
                uint8_t t135;
                uint8_t t137 = gm_l12;
                if (!t137) goto ie136;
                uint8_t t139 = ((uint8_t)1ULL);
                t135 = t139;
                goto ix136;
ie136:;
                uint8_t t141 = ((uint8_t)0ULL);
                t135 = t141;
ix136:;
                gm_tftb1odc739t5 t142 = gm_f_push_t83rg2m(t134, t135);
                gm_tftb1odc739t5 t143 = t142;
                if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcsffver18si54, 842, 13);
                gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
            }
            {
                gm_tcmkompbl09il t145;
                memset((uint8_t *)&t145 + 4, 0, 4);
                gm_t88rcucg1b7uk t146 = gm_l3;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t145 + 0, t146);
                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t148 = t147;
                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                GM_ST(gm_b16_8, (uint8_t *)&t145 + 8, t149);
                gm_t428ue2u3hf5g t150;
                GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t150 + 8), t145);
                gm_trgmlgt2gmmjs t151;
                GM_ST(uint8_t, (uint8_t *)&t151 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t151 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t151 + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t151), t150);
                ((uint8_t *)&t151)[32] = 1;
                uint8_t t152 = 0;
                F.ret = t151;
                goto gm_fret;
            }
            goto me91;
mn120:;
ma94:;
            gm_trgmlgt2gmmjs t154;
            memset(&t154, 0, sizeof t154);
            uint8_t t155 = 0;
            F.ret = t154;
            goto gm_fret;
            goto me91;
mn153:;
ma95:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 834, 5);
me91:;
        }
        goto ix87;
ie87:;
        {
            gm_trgmlgt2gmmjs t157;
            memset(&t157, 0, sizeof t157);
            uint8_t t158 = 0;
            F.ret = t157;
            goto gm_fret;
        }
ix87:;
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
inline __attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_construction_call_ktqtrpo F;
    void * gm_l0; /* e */
    void * gm_l1; /* callee */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    void * gm_l6; /* inner */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)19ULL)) goto mn11;
        void * t13 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t13;
        void * t14;
        void * t16 = gm_l1;
        uint8_t t17 = gm_f_calls_on_value_q0pa6h5(t16);
        if (!t17) goto ie15;
        void * t19 = ((void *)0);
        t14 = t19;
        goto ix15;
ie15:;
        void * t21 = gm_l0;
        void * t22 = t21;
        t14 = t22;
ix15:;
        uint8_t t23 = 0;
        F.ret = t14;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)28ULL)) goto mn24;
        gm_t3mf58gv9kf3n t26 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t26;
        void * t27 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t28 = gm_f_construction_call_ktqtrpo(t27);
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
        goto me3;
mn24:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)24ULL)) goto mn30;
        void * t32 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t32;
        void * t33 = gm_l3;
        void * t34 = gm_f_construction_call_ktqtrpo(t33);
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me3;
mn30:;
ma7:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)25ULL)) goto pa38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t40;
        goto po37;
pa38:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t42 != ((uint8_t)26ULL)) goto pa41;
        void * t43 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t43;
        goto po37;
pa41:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)27ULL)) goto pa44;
        void * t46 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t46;
        goto po37;
pa44:;
        goto mn36;
po37:;
        void * t47 = gm_l4;
        void * t48 = gm_f_construction_call_ktqtrpo(t47);
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
        goto me3;
mn36:;
ma8:;
        uint8_t t51 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me3;
mn50:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 258, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_decl_is_type_oh0cpn9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint32_t gm_l2; /* depth */
    void * gm_l4; /* decl */
    GM_STACK_CHECK(&F, 886u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        gm_tagrkbqh054h7 t5;
        gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tagrkbqh054h7 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)2ULL)) goto ie2;
        gm_t6qooom8qa2do t8 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&t6) + 8));
        F.l3 = t8;
        {
            uint8_t t10;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t16) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; default: goto ma14; }
ma12:;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t18 != ((uint8_t)0ULL)) goto mn17;
            t10 = ((uint8_t)1);
            goto me11;
mn17:;
ma13:;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t20 != ((uint8_t)1ULL)) goto mn19;
            t10 = ((uint8_t)1);
            goto me11;
mn19:;
ma14:;
            t10 = ((uint8_t)0);
            goto me11;
mn21:;
ma15:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 144, 40);
me11:;
            uint8_t t22 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t24 = gm_l2;
        uint8_t t25 = (t24 > ((uint32_t)16ULL));
        if (!t25) goto ie23;
        {
            uint8_t t27 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = (uintptr_t)t28;
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 240) + 0) + 8);
        if (t29 >= (uintptr_t)(t32)) gm_trap_index(t29, t32, gm_srcsffver18si54, 146, 14);
        void * t33 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 240) + 0)) + t29 * 216);
        gm_l4 = t33;
    }
    {
        void * t35 = gm_l4;
        uint8_t t36 = GM_LD(uint8_t, (((((uint8_t *)t35) + 40) + 64) + 0));
        if (t36 != ((uint8_t)0ULL)) goto ie34;
        gm_tv2uf4leil5hq t37 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t35) + 40) + 64) + 8));
        F.l5 = t37;
        {
            uint8_t t40 = 0;
            void * t41 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
            void * t42 = t41;
            uint8_t t43;
            if ((GM_LD(void *, ((uint8_t *)&t42)) != 0)) goto mn45;
            t43 = ((uint8_t)0);
            goto me44;
mn45:;
            t43 = ((uint8_t)1);
            goto me44;
mn46:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 148, 8);
me44:;
            if (!t43) {
                uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 8) + 8);
                uint8_t t48 = (!(t47 == ((uint64_t)1ULL)));
                t40 = t48;
            } else t40 = 1;
            if (!t40) goto ie39;
            {
                uint8_t t50 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            void * t51 = gm_l0;
            void * t52 = gm_l4;
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 32));
            void * t54 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint32_t t55 = gm_l2;
            uint8_t t56 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t51, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix34;
ie34:;
        {
            uint8_t t59 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix34:;
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
extern __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_enum_array_type_0sic6t0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* index */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* u */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 1220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        uint8_t t5 = 0;
        void * t6 = gm_l4;
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint8_t t8 = gm_f_is_error_8k27gd1(t6, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) {
            void * t9 = gm_l4;
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = gm_f_is_error_8k27gd1(t9, GM_CONV(gm_csguankko4lmhd, t10));
            t5 = t11;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            void * t13 = gm_l4;
            gm_t88rcucg1b7uk t14 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_t53at80pfdus3 t19;
        gm_f_kind_jcnt4hm((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
        gm_t53at80pfdus3 t20 = t19;
        uint8_t t21;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t24 != ((uint8_t)19ULL)) goto mn23;
        t21 = ((uint8_t)1);
        goto me22;
mn23:;
        t21 = ((uint8_t)0);
        goto me22;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 603, 8);
me22:;
        if (!t21) goto ie16;
        {
            void * t27 = gm_l0;
            gm_tl874t2atnlh3 t28 = gm_l3;
            void * t29 = gm_l0;
            gm_t88rcucg1b7uk t30 = gm_l1;
            gm_b16_8 t31 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t32;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
            gm_tabb710d9mq2e t33;
            gm_f_format_o2s8o9q((void *)&t33, (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcsffver18si54, 604, 23);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_b0_1 t38 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t27, t28, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t39 = gm_l4;
            gm_t88rcucg1b7uk t40 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t42 = gm_l0;
        gm_t88rcucg1b7uk t43 = gm_l1;
        gm_tdlpv0q69lov1 t44;
        gm_f_union_variants_a7khaqp((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
        gm_tdlpv0q69lov1 t45 = t44;
        gm_tep2drsl9408b t46;
        if (((((uint8_t *)&t45))[24] != 0)) {
            gm_tep2drsl9408b t47 = GM_LD(gm_tep2drsl9408b, (((uint8_t *)&t45) + 0));
            t46 = t47;
        } else {
            {
                void * t49 = gm_l0;
                gm_tl874t2atnlh3 t50 = gm_l3;
                void * t51 = gm_l0;
                gm_t88rcucg1b7uk t52 = gm_l1;
                gm_b16_8 t53 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t54;
                GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
                gm_tabb710d9mq2e t55;
                gm_f_format_qmamljh((void *)&t55, (void *)NULL, cx, NULL, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcsffver18si54, 608, 23);
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                gm_b16_8 t58 = t57;
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                gm_b0_1 t60 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t49, t50, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t61 = gm_l4;
            gm_t88rcucg1b7uk t62 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        gm_l5 = t46;
    }
    {
        {
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_l6 = t65;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t67 = gm_l7;
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t69 = (t67 < t68);
                if (!t69) goto lb66;
                {
                    uint64_t t72 = gm_l7;
                    uint64_t t73 = (uintptr_t)t72;
                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tckdobunaulee t77 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t73 * 72));
                    F.l8 = t77;
                    {
                        uint8_t t80;
                        if ((((((uint8_t *)&F.l8) + 16))[4] != 0)) goto mn82;
                        t80 = ((uint8_t)0);
                        goto me81;
mn82:;
                        t80 = ((uint8_t)1);
                        goto me81;
mn83:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 612, 12);
me81:;
                        if (!t80) goto ie79;
                        {
                            void * t85 = gm_l0;
                            gm_tl874t2atnlh3 t86 = gm_l3;
                            gm_t6j6smj7qoctu t87;
                            void * t88 = gm_l0;
                            gm_t88rcucg1b7uk t89 = gm_l1;
                            gm_b16_8 t90 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t88, t89);
                            if (gm_unw.kind) {
                                goto gm_ll70;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t90);
                            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t91);
                            gm_tabb710d9mq2e t92;
                            gm_f_format_il5kbp4((void *)&t92, (void *)NULL, cx, NULL, &t87);
                            if (gm_unw.kind) {
                                goto gm_ll70;
                            }
                            gm_tabb710d9mq2e t93 = t92;
                            if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 8, gm_srcsffver18si54, 613, 27);
                            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
                            gm_b16_8 t95 = t94;
                            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                            gm_b0_1 t97 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t85, t86, t96);
                            if (gm_unw.kind) {
                                goto gm_ll70;
                            }
                        }
                        {
                            void * t98 = gm_l4;
                            gm_t88rcucg1b7uk t99 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t98);
                            if (gm_unw.kind) {
                                goto gm_ll70;
                            }
                            uint8_t t100 = 0;
                            F.ret = t99;
                            goto gm_fret;
                        }
                        goto ix79;
ie79:;
ix79:;
                    }
                    goto me75;
mn76:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 611, 5);
me75:;
                }
lc66:;
                {
                    uint64_t t102 = gm_l7;
                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                    uint64_t t105 = r103;
                    gm_l7 = t105;
                }
            }
gm_ll70:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
            }
            goto gm_lad;
lb66:;
        }
    }
    {
        void * t106 = gm_l4;
        gm_t8nckeqfenl8a t107;
        memset((uint8_t *)&t107 + 20, 0, 4);
        uint64_t t108 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
        uint64_t t109 = (uint64_t)t108;
        GM_ST(uint64_t, (uint8_t *)&t107 + 0, t109);
        gm_t88rcucg1b7uk t110 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t107 + 8, t110);
        gm_t88rcucg1b7uk t111 = gm_l1;
        gm_tnnjeerolos0a t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112 + 4), t111);
        GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t107 + 12, t112);
        gm_t53at80pfdus3 t113;
        GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t113 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t113 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t113 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)11ULL));
        GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t113 + 8), t107);
        gm_t88rcucg1b7uk t114 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t106, &t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t115 = 0;
        F.ret = t114;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_fn_ref_7alnaqh F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* want */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    void * gm_l12; /* base */
    gm_t08olq97rscn3 gm_l13; /* name */
    uint64_t gm_l14; /* m */
    uint32_t gm_l15; /* g */
    void * gm_l16; /* info */
    GM_STACK_CHECK(&F, 2206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)13ULL)) goto ie3;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 8));
        gm_l5 = t10;
        {
            gm_t88rcucg1b7uk t12 = gm_l5;
            gm_l4 = t12;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l4;
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
        gm_t53at80pfdus3 t18 = t17;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t19 != ((uint8_t)16ULL)) goto ie13;
        gm_tddtjfggdat0d t20 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t18) + 8));
        F.l6 = t20;
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            gm_l8 = ((uint32_t)0ULL);
        }
        {
            void * t22 = gm_l2;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma26; case ((uint8_t)13ULL): goto ma26; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; case ((uint8_t)27ULL): goto ma26; case ((uint8_t)28ULL): goto ma26; case ((uint8_t)29ULL): goto ma26; case ((uint8_t)30ULL): goto ma26; case ((uint8_t)31ULL): goto ma26; case ((uint8_t)32ULL): goto ma26; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma26; case ((uint8_t)35ULL): goto ma26; case ((uint8_t)36ULL): goto ma26; case ((uint8_t)37ULL): goto ma26; case ((uint8_t)38ULL): goto ma26; case ((uint8_t)39ULL): goto ma26; case ((uint8_t)40ULL): goto ma26; case ((uint8_t)41ULL): goto ma26; case ((uint8_t)42ULL): goto ma26; case ((uint8_t)43ULL): goto ma26; case ((uint8_t)44ULL): goto ma26; case ((uint8_t)45ULL): goto ma26; case ((uint8_t)46ULL): goto ma26; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma26; case ((uint8_t)49ULL): goto ma26; case ((uint8_t)50ULL): goto ma26; default: goto ma26; }
ma24:;
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            if (t30 != ((uint8_t)6ULL)) goto mn29;
            gm_t08olq97rscn3 t31 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t22) + 8) + 8));
            gm_l9 = t31;
            {
                void * t34 = gm_l0;
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                gm_t9805qtonbsag t36 = gm_f_local_fn_aji8qnd(t34, t35);
                gm_t9805qtonbsag t37 = t36;
                if (!((((uint8_t *)&t37))[4] != 0)) goto ie33;
                uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&t37));
                gm_l10 = t38;
                {
                    uint32_t t40 = gm_l10;
                    gm_l8 = t40;
                }
                goto ix33;
ie33:;
                {
                    void * t42 = gm_l0;
                    uint64_t t43 = gm_l1;
                    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                    gm_tf5srh4mp4b3i t45 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t42, t43, t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tf5srh4mp4b3i t46 = t45;
                    uint64_t t47;
                    if (((((uint8_t *)&t46))[8] != 0)) {
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&t46) + 0));
                        t47 = t48;
                    } else {
                        gm_t9805qtonbsag t49;
                        memset(&t49, 0, sizeof t49);
                        uint8_t t50 = 0;
                        F.ret = t49;
                        goto gm_fret;
                    }
                    gm_l7 = t47;
                }
                {
                    void * t52 = gm_l0;
                    uint64_t t53 = gm_l7;
                    gm_t6qooom8qa2do t54;
                    gm_f_resolve_decl_pus042v((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6qooom8qa2do t55 = t54;
                    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
                    if (t56 != ((uint8_t)5ULL)) goto ie51;
                    uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&t55) + 8));
                    gm_l11 = t57;
                    {
                        uint32_t t59 = gm_l11;
                        gm_l8 = t59;
                    }
                    goto ix51;
ie51:;
                    {
                        gm_t9805qtonbsag t61;
                        memset(&t61, 0, sizeof t61);
                        uint8_t t62 = 0;
                        F.ret = t61;
                        goto gm_fret;
                    }
ix51:;
                }
ix33:;
            }
            goto me23;
mn29:;
ma25:;
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            if (t64 != ((uint8_t)22ULL)) goto mn63;
            void * t65 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 0));
            gm_l12 = t65;
            gm_t08olq97rscn3 t66 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t22) + 8) + 8) + 8));
            gm_l13 = t66;
            {
                void * t68 = gm_l0;
                uint64_t t69 = gm_l1;
                void * t70 = gm_l12;
                gm_tf5srh4mp4b3i t71 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t68, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t72 = t71;
                uint64_t t73;
                if (((((uint8_t *)&t72))[8] != 0)) {
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&t72) + 0));
                    t73 = t74;
                } else {
                    gm_t9805qtonbsag t75;
                    memset(&t75, 0, sizeof t75);
                    uint8_t t76 = 0;
                    F.ret = t75;
                    goto gm_fret;
                }
                gm_l14 = t73;
            }
            {
                void * t77 = gm_l0;
                void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
                uint64_t t79 = gm_l14;
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                gm_tf5srh4mp4b3i t81 = gm_f_lookup_59juqoo(t78, t79, t80);
                gm_tf5srh4mp4b3i t82 = t81;
                uint64_t t83;
                if (((((uint8_t *)&t82))[8] != 0)) {
                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&t82) + 0));
                    t83 = t84;
                } else {
                    gm_t9805qtonbsag t85;
                    memset(&t85, 0, sizeof t85);
                    uint8_t t86 = 0;
                    F.ret = t85;
                    goto gm_fret;
                }
                gm_l7 = t83;
            }
            {
                void * t88 = gm_l0;
                uint64_t t89 = gm_l7;
                uint64_t t90 = gm_l1;
                uint8_t t91 = gm_f_decl_visible_bd168ti(t88, t89, t90);
                uint8_t t92 = (uint8_t)!t91;
                if (!t92) goto ie87;
                {
                    gm_t9805qtonbsag t94;
                    memset(&t94, 0, sizeof t94);
                    uint8_t t95 = 0;
                    F.ret = t94;
                    goto gm_fret;
                }
                goto ix87;
ie87:;
ix87:;
            }
            {
                void * t97 = gm_l0;
                uint64_t t98 = gm_l7;
                gm_t6qooom8qa2do t99;
                gm_f_resolve_decl_pus042v((void *)&t99, (void *)NULL, cx, NULL, t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t100 = t99;
                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t100) + 0));
                if (t101 != ((uint8_t)5ULL)) goto ie96;
                uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&t100) + 8));
                gm_l15 = t102;
                {
                    uint32_t t104 = gm_l15;
                    gm_l8 = t104;
                }
                goto ix96;
ie96:;
                {
                    gm_t9805qtonbsag t106;
                    memset(&t106, 0, sizeof t106);
                    uint8_t t107 = 0;
                    F.ret = t106;
                    goto gm_fret;
                }
ix96:;
            }
            goto me23;
mn63:;
ma26:;
            gm_t9805qtonbsag t109;
            memset(&t109, 0, sizeof t109);
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
            goto me23;
mn108:;
ma27:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 857, 5);
me23:;
        }
        {
            uint32_t t111 = gm_l8;
            uint64_t t112 = (uint64_t)t111;
            uint64_t t113 = (uintptr_t)t112;
            void * t114 = gm_l0;
            uint64_t t115 = GM_LD(uintptr_t, ((((uint8_t *)t114) + 344) + 0) + 8);
            if (t113 >= (uintptr_t)(t115)) gm_trap_index(t113, t115, gm_srcsffver18si54, 876, 14);
            void * t116 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t114) + 344) + 0)) + t113 * 136);
            gm_l16 = t116;
        }
        {
            void * t118 = gm_l16;
            uint64_t t119 = GM_LD(uintptr_t, (((uint8_t *)t118) + 80) + 8);
            uint8_t t120 = (t119 > ((uint64_t)0ULL));
            if (!t120) goto ie117;
            {
                gm_t9805qtonbsag t122;
                memset(&t122, 0, sizeof t122);
                uint8_t t123 = 0;
                F.ret = t122;
                goto gm_fret;
            }
            goto ix117;
ie117:;
ix117:;
        }
        {
            void * t124 = gm_l0;
            void * t125 = gm_l16;
            gm_f_fn_ty_qaih7ts((void *)&F.l17, (void *)NULL, cx, NULL, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t127 = gm_l0;
            void * t128 = (void *)(((uint8_t *)t127) + 8);
            gm_tddtjfggdat0d t129 = F.l17;
            gm_t53at80pfdus3 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t130 + 8), t129);
            gm_t88rcucg1b7uk t131 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t128, &t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t132 = gm_l0;
            void * t133 = (void *)(((uint8_t *)t132) + 8);
            gm_tddtjfggdat0d t134 = F.l6;
            gm_t53at80pfdus3 t135;
            GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t135 + 8), t134);
            gm_t88rcucg1b7uk t136 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t133, &t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t137 = t131;
            gm_t88rcucg1b7uk t138 = t136;
            uint8_t t139 = (!gm_eq9m6mvtmgde(((uint8_t *)&t137), ((uint8_t *)&t138)));
            if (!t139) goto ie126;
            {
                gm_t9805qtonbsag t141;
                memset(&t141, 0, sizeof t141);
                uint8_t t142 = 0;
                F.ret = t141;
                goto gm_fret;
            }
            goto ix126;
ie126:;
ix126:;
        }
        {
            uint32_t t143 = gm_l8;
            gm_t9805qtonbsag t144;
            GM_ST(uint8_t, (uint8_t *)&t144 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t144 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t144), t143);
            ((uint8_t *)&t144)[4] = 1;
            uint8_t t145 = 0;
            F.ret = t144;
            goto gm_fret;
        }
        goto ix13;
ie13:;
        {
            gm_t9805qtonbsag t147;
            memset(&t147, 0, sizeof t147);
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
ix13:;
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
inline __attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_ty_qaih7ts F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 286u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_nof7vfj((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 40));
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
                    gm_t5v0c8ia6a5mo t16 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 56));
                    F.l5 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t20 = gm_f_push_k3l9nic(t18, GM_CONV(gm_csguankko4lmhd, t19));
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcsffver18si54, 538, 25);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 538, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t24 = gm_l4;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l4 = t27;
                }
            }
lb5:;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0, 4);
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0, 7);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        void * t31 = gm_l1;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 56));
        void * t33 = gm_l1;
        gm_tnpifv20irnph t34 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t33) + 64));
        void * t35 = gm_l1;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t35) + 128));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0, t30);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16, t32);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24, t34);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40, t36);
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
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3) {
    struct gm_fr_generic_decl_oir9hm6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* callee */
    uint8_t gm_l3; /* report */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l9; /* d */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 1004u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t11;
        {
            void * t13 = gm_l0;
            uint64_t t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t16 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t17 = t16;
            uint64_t t18;
            if (((((uint8_t *)&t17))[8] != 0)) {
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                t18 = t19;
            } else {
                gm_toblvhepnjqkh t20;
                memset(&t20, 0, sizeof t20);
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            gm_l5 = t18;
        }
        {
            gm_tkb8hpcqf70t1 t22;
            memset((uint8_t *)&t22 + 20, 0, 4);
            uint64_t t23 = gm_l5;
            GM_ST(uint64_t, (uint8_t *)&t22 + 0, t23);
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t22 + 8, t24);
            gm_toblvhepnjqkh t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 28, 0);
            GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&t25), t22);
            ((uint8_t *)&t25)[24] = 1;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)22ULL)) goto mn27;
        void * t29 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t29;
        gm_t08olq97rscn3 t30 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t30;
        {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l1;
            void * t34 = gm_l6;
            gm_tf5srh4mp4b3i t35 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t32, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t36 = t35;
            uint64_t t37;
            if (((((uint8_t *)&t36))[8] != 0)) {
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&t36) + 0));
                t37 = t38;
            } else {
                gm_toblvhepnjqkh t39;
                memset(&t39, 0, sizeof t39);
                uint8_t t40 = 0;
                F.ret = t39;
                goto gm_fret;
            }
            gm_l8 = t37;
        }
        {
            uint8_t t42 = gm_l3;
            if (!t42) goto ie41;
            {
                void * t44 = gm_l0;
                uint64_t t45 = gm_l8;
                uint64_t t46 = gm_l1;
                gm_tf5srh4mp4b3i t47 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t44, t45, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t48 = t47;
                uint64_t t49;
                if (((((uint8_t *)&t48))[8] != 0)) {
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
                    t49 = t50;
                } else {
                    gm_toblvhepnjqkh t51;
                    memset(&t51, 0, sizeof t51);
                    uint8_t t52 = 0;
                    F.ret = t51;
                    goto gm_fret;
                }
                gm_l9 = t49;
            }
            {
                gm_tkb8hpcqf70t1 t53;
                memset((uint8_t *)&t53 + 20, 0, 4);
                uint64_t t54 = gm_l9;
                GM_ST(uint64_t, (uint8_t *)&t53 + 0, t54);
                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t53 + 8, t55);
                gm_toblvhepnjqkh t56;
                GM_ST(uint8_t, (uint8_t *)&t56 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 28, 0);
                GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&t56), t53);
                ((uint8_t *)&t56)[24] = 1;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            void * t58 = gm_l0;
            void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
            uint64_t t60 = gm_l8;
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t62 = gm_f_lookup_59juqoo(t59, t60, t61);
            gm_tf5srh4mp4b3i t63 = t62;
            uint64_t t64;
            if (((((uint8_t *)&t63))[8] != 0)) {
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&t63) + 0));
                t64 = t65;
            } else {
                gm_toblvhepnjqkh t66;
                memset(&t66, 0, sizeof t66);
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            gm_l10 = t64;
        }
        {
            void * t69 = gm_l0;
            uint64_t t70 = gm_l10;
            uint64_t t71 = gm_l1;
            uint8_t t72 = gm_f_decl_visible_bd168ti(t69, t70, t71);
            uint8_t t73 = (uint8_t)!t72;
            if (!t73) goto ie68;
            {
                gm_toblvhepnjqkh t75;
                memset(&t75, 0, sizeof t75);
                uint8_t t76 = 0;
                F.ret = t75;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        {
            void * t77 = gm_l0;
            uint64_t t78 = gm_l10;
            uint64_t t79 = gm_l1;
            gm_b0_1 t80 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t77, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tkb8hpcqf70t1 t81;
            memset((uint8_t *)&t81 + 20, 0, 4);
            uint64_t t82 = gm_l10;
            GM_ST(uint64_t, (uint8_t *)&t81 + 0, t82);
            gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t81 + 8, t83);
            gm_toblvhepnjqkh t84;
            GM_ST(uint8_t, (uint8_t *)&t84 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t84 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t84 + 28, 0);
            GM_ST(gm_tkb8hpcqf70t1, ((uint8_t *)&t84), t81);
            ((uint8_t *)&t84)[24] = 1;
            uint8_t t85 = 0;
            F.ret = t84;
            goto gm_fret;
        }
        goto me3;
mn27:;
ma6:;
        gm_toblvhepnjqkh t87;
        memset(&t87, 0, sizeof t87);
        uint8_t t88 = 0;
        F.ret = t87;
        goto gm_fret;
        goto me3;
mn86:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 562, 5);
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
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_type_args_5b8bvov F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* template */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* next */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t08olq97rscn3 gm_l18; /* n */
    gm_tf5srh4mp4b3i gm_l20; /* found */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* j */
    uint32_t gm_l24; /* p */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    uint32_t gm_l29; /* pid */
    uint64_t gm_l31; /* m */
    void * gm_l33; /* d */
    void * gm_l35; /* e */
    gm_t88rcucg1b7uk gm_l36; /* vt */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* j */
    uint32_t gm_l41; /* q */
    gm_t88rcucg1b7uk gm_l42; /* t */
    GM_STACK_CHECK(&F, 5035u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l2;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcsffver18si54, 715, 13);
        gm_t6art7bprjiqb t7 = GM_LD(gm_t6art7bprjiqb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184));
        F.l5 = t7;
    }
    {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 120));
        gm_l6 = t8;
    }
    {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_l7 = t9;
    }
    {
        gm_f_list_9tqbks7((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t11 = gm_l6;
            gm_l9 = t11;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l10;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l10;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t19 * 4));
                    uint32_t t22 = t21;
                    {
                        void * t26 = (void *)((uint8_t *)&F.l8);
                        gm_t0mfir12leg2g t27;
                        memset(&t27, 0, sizeof t27);
                        gm_tftb1odc739t5 t28 = gm_f_push_k0n7eot(t26, &t27);
                        gm_tftb1odc739t5 t29 = t28;
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcsffver18si54, 720, 23);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 720, 5);
me23:;
                }
lc12:;
                {
                    uint64_t t32 = gm_l10;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l10 = t35;
                }
            }
lb12:;
        }
    }
    {
        gm_l11 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t37 = gm_l3;
            gm_l12 = t37;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t39 = gm_l13;
                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t41 = (t39 < t40);
                if (!t41) goto lb38;
                {
                    uint64_t t44 = gm_l13;
                    uint64_t t45 = (uintptr_t)t44;
                    uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t7te2tdpebbt4 t49 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t45 * 144));
                    F.l14 = t49;
                    {
                        gm_l15 = ((uint64_t)0ULL);
                    }
                    {
                        gm_t0mfir12leg2g t51;
                        memset(&t51, 0, sizeof t51);
                        F.l16 = t51;
                    }
                    {
                        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        switch (t57) { case ((uint8_t)0ULL): goto ma53; case ((uint8_t)1ULL): goto ma54; case ((uint8_t)2ULL): goto ma55; default: goto ma56; }
ma53:;
                        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        if (t59 != ((uint8_t)0ULL)) goto mn58;
                        gm_tc3oht4ksth9d t60 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l14) + 0) + 8));
                        F.l17 = t60;
                        {
                            uint64_t t62 = gm_l11;
                            gm_l15 = t62;
                        }
                        {
                            uint64_t t63 = gm_l11;
                            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                            if (o65) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 728, 17);
                            uint64_t t66 = r64;
                            gm_l11 = t66;
                        }
                        {
                            gm_tc3oht4ksth9d t67 = F.l17;
                            gm_t0mfir12leg2g t68;
                            GM_ST(uint8_t, (uint8_t *)&t68 + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&t68 + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&t68 + 92, 0);
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t68), t67);
                            ((uint8_t *)&t68)[88] = 1;
                            F.l16 = t68;
                        }
                        goto me52;
mn58:;
ma54:;
                        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        if (t70 != ((uint8_t)1ULL)) goto mn69;
                        gm_t08olq97rscn3 t71 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l14) + 0) + 8) + 0));
                        gm_l18 = t71;
                        gm_tc3oht4ksth9d t72 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l14) + 0) + 8) + 32));
                        F.l19 = t72;
                        {
                            gm_tf5srh4mp4b3i t74;
                            memset(&t74, 0, sizeof t74);
                            gm_l20 = t74;
                        }
                        {
                            {
                                gm_b16_8 t76 = gm_l6;
                                gm_b16_8 t77 = t76;
                                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                                gm_l21 = t78;
                            }
                            {
                                gm_l22 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t80 = gm_l22;
                                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                    uint8_t t82 = (t80 < t81);
                                    if (!t82) goto lb79;
                                    {
                                        uint64_t t85 = gm_l22;
                                        uint64_t t86 = gm_l22;
                                        uint64_t t87 = (uintptr_t)t86;
                                        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                        uint32_t t89 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t87 * 4));
                                        gm_tn68el8d14vku t90;
                                        GM_ST(uint32_t, (uint8_t *)&t90 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t90 + 0), t85);
                                        GM_ST(uint32_t, ((uint8_t *)&t90 + 8), t89);
                                        gm_tn68el8d14vku t91 = t90;
                                        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&t91) + 0));
                                        gm_l23 = t94;
                                        uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&t91) + 8));
                                        gm_l24 = t95;
                                        {
                                            void * t98 = gm_l0;
                                            void * t99 = (void *)(((uint8_t *)t98) + 8);
                                            uint32_t t100 = gm_l24;
                                            gm_b16_8 t101 = gm_f_param_name_rbnop5l(t99, t100);
                                            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                            gm_b16_8 t103 = t101;
                                            gm_b16_8 t104 = t102;
                                            uint8_t t105 = (gm_eq0iqiubfm7e(((uint8_t *)&t103), ((uint8_t *)&t104)));
                                            if (!t105) goto ie97;
                                            {
                                                uint64_t t107 = gm_l23;
                                                gm_tf5srh4mp4b3i t108;
                                                GM_ST(uint8_t, (uint8_t *)&t108 + 9, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t108 + 10, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t108 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t108), t107);
                                                ((uint8_t *)&t108)[8] = 1;
                                                gm_l20 = t108;
                                            }
                                            {
                                                goto lb79;
                                            }
                                            goto ix97;
ie97:;
ix97:;
                                        }
                                        goto me92;
mn93:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 733, 17);
me92:;
                                    }
lc79:;
                                    {
                                        uint64_t t110 = gm_l22;
                                        uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                                        uint64_t t113 = r111;
                                        gm_l22 = t113;
                                    }
                                }
lb79:;
                            }
                        }
                        {
                            gm_tf5srh4mp4b3i t114 = gm_l20;
                            gm_tf5srh4mp4b3i t115 = t114;
                            uint64_t t116;
                            if (((((uint8_t *)&t115))[8] != 0)) {
                                uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&t115) + 0));
                                t116 = t117;
                            } else {
                                {
                                    void * t119 = gm_l0;
                                    gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 16));
                                    gm_t6j6smj7qoctu t121;
                                    gm_b16_8 t122 = gm_l7;
                                    GM_ST(gm_b16_8, (uint8_t *)&t121 + 0, t122);
                                    gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t121 + 16, t123);
                                    gm_tabb710d9mq2e t124;
                                    gm_f_format_bnsf58k((void *)&t124, (void *)NULL, cx, NULL, &t121);
                                    if (gm_unw.kind) {
                                        goto gm_ll42;
                                    }
                                    gm_tabb710d9mq2e t125 = t124;
                                    if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 8, gm_srcsffver18si54, 740, 37);
                                    gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t125) + 8));
                                    gm_b16_8 t127 = t126;
                                    gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                                    gm_b0_1 t129 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t119, t120, t128);
                                    if (gm_unw.kind) {
                                        goto gm_ll42;
                                    }
                                }
                                gm_th2lqbo77pvsc t130;
                                memset(&t130, 0, sizeof t130);
                                uint8_t t131 = 0;
                                F.ret = t130;
                                goto gm_fret;
                            }
                            gm_l15 = t116;
                        }
                        {
                            gm_tc3oht4ksth9d t132 = F.l19;
                            gm_t0mfir12leg2g t133;
                            GM_ST(uint8_t, (uint8_t *)&t133 + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&t133 + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&t133 + 92, 0);
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t133), t132);
                            ((uint8_t *)&t133)[88] = 1;
                            F.l16 = t133;
                        }
                        goto me52;
mn69:;
ma55:;
                        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        if (t135 != ((uint8_t)2ULL)) goto mn134;
                        {
                            void * t137 = gm_l0;
                            gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                            gm_b16_8 t139;
                            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sldikqa9i70sm));
                            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(51));
                            gm_b0_1 t140 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t137, t138, t139);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t141;
                            memset(&t141, 0, sizeof t141);
                            uint8_t t142 = 0;
                            F.ret = t141;
                            goto gm_fret;
                        }
                        goto me52;
mn134:;
ma56:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 725, 9);
me52:;
                    }
                    {
                        uint64_t t144 = gm_l15;
                        uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t146 = (t144 >= t145);
                        if (!t146) goto ie143;
                        {
                            void * t148 = gm_l0;
                            gm_tl874t2atnlh3 t149 = gm_l4;
                            gm_t3877pk7lv748 t150;
                            gm_b16_8 t151 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t150 + 0, t151);
                            uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            GM_ST(uint64_t, (uint8_t *)&t150 + 16, t152);
                            gm_tabb710d9mq2e t153;
                            gm_f_format_n6tblha((void *)&t153, (void *)NULL, cx, NULL, &t150);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                            gm_tabb710d9mq2e t154 = t153;
                            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 8, gm_srcsffver18si54, 751, 27);
                            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
                            gm_b16_8 t156 = t155;
                            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                            gm_b0_1 t158 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t148, t149, t157);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t159;
                            memset(&t159, 0, sizeof t159);
                            uint8_t t160 = 0;
                            F.ret = t159;
                            goto gm_fret;
                        }
                        goto ix143;
ie143:;
ix143:;
                    }
                    {
                        uint64_t t162 = gm_l15;
                        uint64_t t163 = (uintptr_t)t162;
                        uint64_t t164 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_srcsffver18si54, 754, 12);
                        uint8_t t165;
                        if (((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t163 * 96))[88] != 0)) goto mn167;
                        t165 = ((uint8_t)0);
                        goto me166;
mn167:;
                        t165 = ((uint8_t)1);
                        goto me166;
mn168:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 754, 12);
me166:;
                        if (!t165) goto ie161;
                        {
                            void * t170 = gm_l0;
                            gm_tl874t2atnlh3 t171 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                            void * t172 = gm_l0;
                            void * t173 = (void *)(((uint8_t *)t172) + 8);
                            uint64_t t174 = gm_l15;
                            uint64_t t175 = (uintptr_t)t174;
                            uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srcsffver18si54, 755, 94);
                            uint32_t t177 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t175 * 4));
                            gm_b16_8 t178 = gm_f_param_name_rbnop5l(t173, t177);
                            gm_tnpifv20irnph t179;
                            GM_ST(gm_b16_8, ((uint8_t *)&t179 + 0), t178);
                            gm_tabb710d9mq2e t180;
                            gm_f_format_nbjjmv4((void *)&t180, (void *)NULL, cx, NULL, t179);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                            gm_tabb710d9mq2e t181 = t180;
                            if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_srcsffver18si54, 755, 29);
                            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
                            gm_b16_8 t183 = t182;
                            gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
                            gm_b0_1 t185 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t170, t171, t184);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t186;
                            memset(&t186, 0, sizeof t186);
                            uint8_t t187 = 0;
                            F.ret = t186;
                            goto gm_fret;
                        }
                        goto ix161;
ie161:;
ix161:;
                    }
                    {
                        uint64_t t188 = gm_l15;
                        uint64_t t189 = (uintptr_t)t188;
                        uint64_t t190 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        if (t189 >= (uintptr_t)(t190)) gm_trap_index(t189, t190, gm_srcsffver18si54, 758, 9);
                        gm_t0mfir12leg2g t191 = F.l16;
                        GM_ST(gm_t0mfir12leg2g, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t189 * 96), t191);
                    }
                    goto me47;
mn48:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 722, 5);
me47:;
                }
lc38:;
                {
                    uint64_t t193 = gm_l13;
                    uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                    uint64_t t196 = r194;
                    gm_l13 = t196;
                }
            }
gm_ll42:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
            }
            goto gm_lad;
lb38:;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t198 = gm_l6;
            gm_b16_8 t199 = t198;
            gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
            gm_l26 = t200;
        }
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t202 = gm_l27;
                uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                uint8_t t204 = (t202 < t203);
                if (!t204) goto lb201;
                {
                    uint64_t t207 = gm_l27;
                    uint64_t t208 = gm_l27;
                    uint64_t t209 = (uintptr_t)t208;
                    uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    uint32_t t211 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t209 * 4));
                    gm_tn68el8d14vku t212;
                    GM_ST(uint32_t, (uint8_t *)&t212 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t212 + 0), t207);
                    GM_ST(uint32_t, ((uint8_t *)&t212 + 8), t211);
                    gm_tn68el8d14vku t213 = t212;
                    uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&t213) + 0));
                    gm_l28 = t216;
                    uint32_t t217 = GM_LD(uint32_t, (((uint8_t *)&t213) + 8));
                    gm_l29 = t217;
                    {
                        gm_t0mfir12leg2g t219;
                        memset(&t219, 0, sizeof t219);
                        F.l30 = t219;
                    }
                    {
                        uint64_t t220 = gm_l1;
                        gm_l31 = t220;
                    }
                    {
                        uint64_t t222 = gm_l28;
                        uint64_t t223 = (uintptr_t)t222;
                        uint64_t t224 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        if (t223 >= (uintptr_t)(t224)) gm_trap_index(t223, t224, gm_srcsffver18si54, 764, 17);
                        if (!((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t223 * 96))[88] != 0)) goto ie221;
                        gm_tc3oht4ksth9d t225 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t223 * 96));
                        F.l32 = t225;
                        {
                            gm_tc3oht4ksth9d t227 = F.l32;
                            gm_t0mfir12leg2g t228;
                            GM_ST(uint8_t, (uint8_t *)&t228 + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&t228 + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&t228 + 92, 0);
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t228), t227);
                            ((uint8_t *)&t228)[88] = 1;
                            F.l30 = t228;
                        }
                        goto ix221;
ie221:;
                        uint64_t t230 = gm_l28;
                        uint64_t t231 = (uintptr_t)t230;
                        uint64_t t232 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 136) + 8);
                        if (t231 >= (uintptr_t)(t232)) gm_trap_index(t231, t232, gm_srcsffver18si54, 766, 24);
                        void * t233 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 136)) + t231 * 8));
                        void * t234 = t233;
                        if (!(GM_LD(void *, ((uint8_t *)&t234)) != 0)) goto ie229;
                        void * t235 = GM_LD(void *, ((uint8_t *)&t234));
                        gm_l33 = t235;
                        {
                            void * t237 = gm_l33;
                            gm_tc3oht4ksth9d t238 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t237));
                            gm_t0mfir12leg2g t239;
                            GM_ST(uint8_t, (uint8_t *)&t239 + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&t239 + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&t239 + 92, 0);
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t239), t238);
                            ((uint8_t *)&t239)[88] = 1;
                            F.l30 = t239;
                        }
                        {
                            uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
                            gm_l31 = t240;
                        }
                        goto ix229;
ie229:;
                        uint32_t t242 = gm_l29;
                        uint64_t t243 = (uint64_t)t242;
                        uint64_t t244 = (uintptr_t)t243;
                        void * t245 = gm_l0;
                        uint64_t t246 = GM_LD(uintptr_t, ((((uint8_t *)t245) + 1128) + 0) + 8);
                        if (t244 >= (uintptr_t)(t246)) gm_trap_index(t244, t246, gm_srcsffver18si54, 770, 19);
                        uint8_t t247 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t245) + 1128) + 0)) + t244 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                        if (!t247) goto ie241;
                        {
                            void * t249 = (void *)((uint8_t *)&F.l25);
                            void * t250 = gm_l0;
                            void * t251 = (void *)(((uint8_t *)t250) + 8);
                            gm_tnpifv20irnph t252 = gm_f_empty_effects_r0iv3pv();
                            gm_t53at80pfdus3 t253;
                            memset(&t253, 0, sizeof t253);
                            GM_ST(uint8_t, ((uint8_t *)&t253 + 0), ((uint8_t)24ULL));
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t253 + 8), t252);
                            gm_t88rcucg1b7uk t254 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t251, &t253);
                            if (gm_unw.kind) {
                                goto gm_ll205;
                            }
                            gm_tftb1odc739t5 t255 = gm_f_push_k3l9nic(t249, GM_CONV(gm_csguankko4lmhd, t254));
                            gm_tftb1odc739t5 t256 = t255;
                            if (((((uint8_t *)&t256))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t256) + 1, gm_srcsffver18si54, 772, 13);
                            gm_b0_1 t257 = GM_LD(gm_b0_1, (((uint8_t *)&t256) + 1));
                        }
                        {
                            goto lc201;
                        }
                        goto ix241;
ie241:;
                        {
                            void * t259 = gm_l0;
                            gm_tl874t2atnlh3 t260 = gm_l4;
                            gm_t6j6smj7qoctu t261;
                            void * t262 = gm_l0;
                            void * t263 = (void *)(((uint8_t *)t262) + 8);
                            uint32_t t264 = gm_l29;
                            gm_b16_8 t265 = gm_f_param_name_rbnop5l(t263, t264);
                            GM_ST(gm_b16_8, (uint8_t *)&t261 + 0, t265);
                            gm_b16_8 t266 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t261 + 16, t266);
                            gm_tabb710d9mq2e t267;
                            gm_f_format_l0aimgq((void *)&t267, (void *)NULL, cx, NULL, &t261);
                            if (gm_unw.kind) {
                                goto gm_ll205;
                            }
                            gm_tabb710d9mq2e t268 = t267;
                            if (((((uint8_t *)&t268))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t268) + 8, gm_srcsffver18si54, 775, 27);
                            gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&t268) + 8));
                            gm_b16_8 t270 = t269;
                            gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                            gm_b0_1 t272 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t259, t260, t271);
                            if (gm_unw.kind) {
                                goto gm_ll205;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t273;
                            memset(&t273, 0, sizeof t273);
                            uint8_t t274 = 0;
                            F.ret = t273;
                            goto gm_fret;
                        }
ix241:;
ix229:;
ix221:;
                    }
                    {
                        gm_t0mfir12leg2g t275 = F.l30;
                        gm_t0mfir12leg2g t276 = t275;
                        gm_tc3oht4ksth9d t277;
                        if (((((uint8_t *)&t276))[88] != 0)) {
                            gm_tc3oht4ksth9d t278 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t276) + 0));
                            t277 = t278;
                        } else {
                            gm_th2lqbo77pvsc t279;
                            memset(&t279, 0, sizeof t279);
                            uint8_t t280 = 0;
                            F.ret = t279;
                            goto gm_fret;
                        }
                        F.l34 = t277;
                    }
                    {
                        void * t281 = (void *)((uint8_t *)&F.l34);
                        gm_l35 = t281;
                    }
                    {
                        uint32_t t282 = gm_l29;
                        uint64_t t283 = (uint64_t)t282;
                        uint64_t t284 = (uintptr_t)t283;
                        void * t285 = gm_l0;
                        uint64_t t286 = GM_LD(uintptr_t, ((((uint8_t *)t285) + 1128) + 0) + 8);
                        if (t284 >= (uintptr_t)(t286)) gm_trap_index(t284, t286, gm_srcsffver18si54, 780, 20);
                        gm_t88rcucg1b7uk t287;
                        uint8_t t293 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t285) + 1128) + 0)) + t284 * 48) + 0) + 0));
                        switch (t293) { case ((uint8_t)0ULL): goto ma291; case ((uint8_t)1ULL): goto ma290; case ((uint8_t)2ULL): goto ma289; default: goto ma292; }
ma289:;
                        uint8_t t295 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t285) + 1128) + 0)) + t284 * 48) + 0) + 0));
                        if (t295 != ((uint8_t)2ULL)) goto mn294;
                        gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t285) + 1128) + 0)) + t284 * 48) + 0) + 4));
                        gm_l36 = t296;
                        {
                            gm_f_list_oalq03d((void *)&F.l37, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll205;
                            }
                        }
                        {
                            {
                                uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                void * t300 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                                uint64_t t301 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t302 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                                uint64_t t303 = (uintptr_t)t302;
                                if (t301 > t303 || t303 > (uintptr_t)t299) gm_trap_slice(t301, t303, t299, gm_srcsffver18si54, 783, 39);
                                gm_b16_8 t304;
                                GM_ST(void *, (uint8_t *)&t304, (void *)(((uint8_t *)t300 + t301 * 4)));
                                GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(t303 - t301));
                                gm_b16_8 t305 = t304;
                                gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&t305));
                                gm_l38 = t306;
                            }
                            {
                                gm_l39 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t308 = gm_l39;
                                    uint64_t t309 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                    uint8_t t310 = (t308 < t309);
                                    if (!t310) goto lb307;
                                    {
                                        uint64_t t313 = gm_l39;
                                        uint64_t t314 = gm_l39;
                                        uint64_t t315 = (uintptr_t)t314;
                                        uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                        uint32_t t317 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t315 * 4));
                                        gm_tn68el8d14vku t318;
                                        GM_ST(uint32_t, (uint8_t *)&t318 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t318 + 0), t313);
                                        GM_ST(uint32_t, ((uint8_t *)&t318 + 8), t317);
                                        gm_tn68el8d14vku t319 = t318;
                                        uint64_t t322 = GM_LD(uint64_t, (((uint8_t *)&t319) + 0));
                                        gm_l40 = t322;
                                        uint32_t t323 = GM_LD(uint32_t, (((uint8_t *)&t319) + 8));
                                        gm_l41 = t323;
                                        {
                                            void * t325 = (void *)((uint8_t *)&F.l37);
                                            uint32_t t326 = gm_l41;
                                            uint64_t t327 = gm_l40;
                                            uint64_t t328 = (uintptr_t)t327;
                                            uint64_t t329 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                                            if (t328 >= (uintptr_t)(t329)) gm_trap_index(t328, t329, gm_srcsffver18si54, 783, 96);
                                            gm_t88rcucg1b7uk t330 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t328 * 4));
                                            gm_tplpbqf3duvn3 t331;
                                            GM_ST(uint32_t, ((uint8_t *)&t331 + 0), t326);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t331 + 4), t330);
                                            gm_tftb1odc739t5 t332 = gm_f_push_h69n5fi(t325, GM_CONV(gm_cs7e9ppsd384bj, t331));
                                            gm_tftb1odc739t5 t333 = t332;
                                            if (((((uint8_t *)&t333))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t333) + 1, gm_srcsffver18si54, 783, 67);
                                            gm_b0_1 t334 = GM_LD(gm_b0_1, (((uint8_t *)&t333) + 1));
                                        }
                                        goto me320;
mn321:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 783, 17);
me320:;
                                    }
lc307:;
                                    {
                                        uint64_t t336 = gm_l39;
                                        uint64_t r337; bool o338 = __builtin_add_overflow(t336, ((uint64_t)1ULL), &r337);
                                        uint64_t t339 = r337;
                                        gm_l39 = t339;
                                    }
                                }
lb307:;
                            }
                        }
                        void * t340 = gm_l0;
                        uint64_t t341 = gm_l31;
                        void * t342 = gm_l0;
                        gm_t88rcucg1b7uk t343 = gm_l36;
                        gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                        gm_b16_8 t345 = t344;
                        gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&t345));
                        gm_t88rcucg1b7uk t347 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t342, t343, t346);
                        if (gm_unw.kind) {
                            goto gm_ll205;
                        }
                        void * t348 = gm_l35;
                        gm_tkn42u7na3t81 t349 = gm_f_comptime_value_l5orc93((void *)NULL, cx, NULL, t340, t341, t347, t348);
                        if (gm_unw.kind) {
                            goto gm_ll205;
                        }
                        gm_tkn42u7na3t81 t350 = t349;
                        gm_t88rcucg1b7uk t351;
                        if (((((uint8_t *)&t350))[4] != 0)) {
                            gm_t88rcucg1b7uk t352 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t350) + 0));
                            t351 = t352;
                        } else {
                            gm_th2lqbo77pvsc t353;
                            memset(&t353, 0, sizeof t353);
                            uint8_t t354 = 0;
                            F.ret = t353;
                            goto gm_fret;
                        }
                        gm_t88rcucg1b7uk t355 = t351;
                        t287 = t355;
                        goto me288;
mn294:;
ma290:;
                        uint8_t t357 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t285) + 1128) + 0)) + t284 * 48) + 0) + 0));
                        if (t357 != ((uint8_t)1ULL)) goto mn356;
                        void * t358 = gm_l0;
                        uint64_t t359 = gm_l31;
                        void * t360 = gm_l35;
                        gm_t88rcucg1b7uk t361 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t358, t359, t360);
                        if (gm_unw.kind) {
                            goto gm_ll205;
                        }
                        t287 = t361;
                        goto me288;
mn356:;
ma291:;
                        uint8_t t363 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t285) + 1128) + 0)) + t284 * 48) + 0) + 0));
                        if (t363 != ((uint8_t)0ULL)) goto mn362;
                        void * t364 = gm_l0;
                        uint64_t t365 = gm_l31;
                        void * t366 = gm_l35;
                        gm_t88rcucg1b7uk t367 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t364, t365, t366);
                        if (gm_unw.kind) {
                            goto gm_ll205;
                        }
                        t287 = t367;
                        goto me288;
mn362:;
ma292:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 780, 14);
me288:;
                        gm_l42 = t287;
                    }
                    {
                        void * t369 = gm_l0;
                        void * t370 = (void *)(((uint8_t *)t369) + 8);
                        gm_t88rcucg1b7uk t371 = gm_l42;
                        uint8_t t372 = gm_f_is_error_8k27gd1(t370, GM_CONV(gm_csguankko4lmhd, t371));
                        if (!t372) goto ie368;
                        {
                            gm_th2lqbo77pvsc t374;
                            memset(&t374, 0, sizeof t374);
                            uint8_t t375 = 0;
                            F.ret = t374;
                            goto gm_fret;
                        }
                        goto ix368;
ie368:;
ix368:;
                    }
                    {
                        void * t376 = (void *)((uint8_t *)&F.l25);
                        gm_t88rcucg1b7uk t377 = gm_l42;
                        gm_tftb1odc739t5 t378 = gm_f_push_k3l9nic(t376, GM_CONV(gm_csguankko4lmhd, t377));
                        gm_tftb1odc739t5 t379 = t378;
                        if (((((uint8_t *)&t379))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t379) + 1, gm_srcsffver18si54, 790, 9);
                        gm_b0_1 t380 = GM_LD(gm_b0_1, (((uint8_t *)&t379) + 1));
                    }
                    goto me214;
mn215:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 761, 5);
me214:;
                }
lc201:;
                {
                    uint64_t t382 = gm_l27;
                    uint64_t r383; bool o384 = __builtin_add_overflow(t382, ((uint64_t)1ULL), &r383);
                    uint64_t t385 = r383;
                    gm_l27 = t385;
                }
            }
gm_ll205:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb201; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc201; }
            }
            goto gm_lad;
lb201:;
        }
    }
    {
        gm_b16_8 t386 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
        gm_th2lqbo77pvsc t387;
        GM_ST(uint8_t, (uint8_t *)&t387 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t387 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t387 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t387), t386);
        ((uint8_t *)&t387)[16] = 1;
        uint8_t t388 = 0;
        F.ret = t387;
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
extern __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hoists_13k2a2s F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* x */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma5:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)28ULL)) goto mn10;
        gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t12;
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
        t3 = t13;
        goto me4;
mn10:;
ma6:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)24ULL)) goto mn14;
        void * t16 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t16;
        void * t17 = gm_l2;
        t3 = t17;
        goto me4;
mn14:;
ma7:;
        uint8_t t19 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn18:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 285, 14);
me4:;
        gm_l3 = t3;
    }
    {
        void * t21 = gm_l3;
        void * t22 = gm_f_construction_call_ktqtrpo(t21);
        void * t23 = t22;
        uint8_t t24;
        if ((GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto mn26;
        t24 = ((uint8_t)0);
        goto me25;
mn26:;
        t24 = ((uint8_t)1);
        goto me25;
mn27:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 290, 8);
me25:;
        if (!t24) goto ie20;
        {
            uint8_t t29 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t30 = gm_l0;
        void * t31 = gm_f_prefix_operand_3n5jchh(t30);
        uint8_t t32 = gm_f_pushes_down_0djaflp(t31);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_type_name_a77a0kf F;
    gm_b16_8 gm_l0; /* name */
    uint32_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t7 = (t6 < ((uint64_t)2ULL));
        if (!t7) {
            uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcsffver18si54, 5, 24);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            gm_tqs5p4n2tq19v t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t11 + 1), ((uint8_t)105ULL));
            gm_tqs5p4n2tq19v t12 = t11;
            uint8_t t13 = 0;
            uint8_t t14 = t10;
            if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 0, ((uint8_t *)&t14))) t13 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 1, ((uint8_t *)&t14))) t13 = 1;
            uint8_t t15 = (uint8_t)(!t13);
            t5 = t15;
        } else t5 = 1;
        if (!t5) {
            uint64_t t16 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcsffver18si54, 5, 53);
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t16 * 1));
            uint8_t t19 = ((t18 == ((uint8_t)48ULL)));
            t4 = t19;
        } else t4 = 1;
        if (!t4) {
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t21 = (t20 > ((uint64_t)4ULL));
            t3 = t21;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_tp8mv052i18rg t23;
            memset(&t23, 0, sizeof t23);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l1 = ((uint32_t)0ULL);
    }
    {
        {
            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t27 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t28 = (uintptr_t)((uint64_t)1ULL);
            if (t28 > (uintptr_t)t26 || (uintptr_t)t26 > (uintptr_t)t26) gm_trap_slice(t28, (uintptr_t)t26, t26, gm_srcsffver18si54, 7, 14);
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t27 + t28 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)((uintptr_t)t26 - t28));
            gm_l2 = t29;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t31 = gm_l3;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l3;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t37 * 1));
                    uint8_t t40 = t39;
                    uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&t40));
                    gm_l4 = t43;
                    {
                        {
                            uint8_t t47 = gm_l4;
                            gm_l5 = t47;
                        }
                        uint8_t t48 = 0;
                        uint8_t t49 = gm_l5;
                        uint8_t t50 = (((uint8_t)48ULL) <= t49);
                        if (t50) {
                            uint8_t t51 = gm_l5;
                            uint8_t t52 = (t51 <= ((uint8_t)57ULL));
                            t48 = t52;
                        } else t48 = 0;
                        uint8_t t53 = (uint8_t)!t48;
                        uint8_t t54 = t53;
                        if (!t54) goto ie45;
                        {
                            gm_tp8mv052i18rg t56;
                            memset(&t56, 0, sizeof t56);
                            uint8_t t57 = 0;
                            F.ret = t56;
                            goto gm_fret;
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    {
                        uint32_t t58 = gm_l1;
                        uint32_t r59; bool o60 = __builtin_mul_overflow(t58, ((uint32_t)10ULL), &r59);
                        if (o60) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 9, 16);
                        uint32_t t61 = r59;
                        uint8_t t62 = gm_l4;
                        uint8_t r63; bool o64 = __builtin_sub_overflow(t62, ((uint8_t)48ULL), &r63);
                        if (o64) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 9, 32);
                        uint8_t t65 = r63;
                        uint32_t t66 = (uint32_t)t65;
                        uint32_t r67; bool o68 = __builtin_add_overflow(t61, t66, &r67);
                        if (o68) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 9, 16);
                        uint32_t t69 = r67;
                        gm_l1 = t69;
                    }
                    goto me41;
mn42:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 7, 5);
me41:;
                }
lc30:;
                {
                    uint64_t t71 = gm_l3;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l3 = t74;
                }
            }
lb30:;
        }
    }
    {
        {
            uint32_t t77 = gm_l1;
            gm_l6 = t77;
        }
        uint8_t t78 = 0;
        uint32_t t79 = gm_l6;
        uint8_t t80 = (((uint32_t)1ULL) <= t79);
        if (t80) {
            uint32_t t81 = gm_l6;
            uint8_t t82 = (t81 <= ((uint32_t)128ULL));
            t78 = t82;
        } else t78 = 0;
        uint8_t t83 = (uint8_t)!t78;
        uint8_t t84 = t83;
        if (!t84) goto ie75;
        {
            gm_tp8mv052i18rg t86;
            memset(&t86, 0, sizeof t86);
            uint8_t t87 = 0;
            F.ret = t86;
            goto gm_fret;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        uint64_t t88 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcsffver18si54, 12, 21);
        uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t88 * 1));
        uint8_t t91 = ((t90 == ((uint8_t)105ULL)));
        uint32_t t92 = gm_l1;
        uint16_t t93 = (uint16_t)t92;
        gm_tjcj4j2gbh6c8 t94;
        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), t91);
        GM_ST(uint16_t, ((uint8_t *)&t94 + 2), t93);
        gm_tp8mv052i18rg t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 5, 0);
        GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t95), t94);
        ((uint8_t *)&t95)[4] = 1;
        uint8_t t96 = 0;
        F.ret = t95;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_builtin_type_name_v5ttoka F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        gm_b16_8 t3 = gm_l0;
        gm_tp8mv052i18rg t4 = gm_f_int_type_name_a77a0kf(t3);
        gm_tp8mv052i18rg t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 49, 12);
me7:;
        if (!t6) {
            gm_b16_8 t10 = gm_l0;
            gm_th4d8o29g5urm t11;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s6groq37ram3h));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s0i3i86nphpsh));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t13);
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 32, t14);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sm9e81djuncnu));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 48, t15);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sqqajgod2bb4o));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 64, t16);
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s929lennfd3g6));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 80, t17);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3cv29r83pgn4));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 96, t18);
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 112, t19);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_slolcr6ikvg33));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 128, t20);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s0iqiubfm7e7e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 144, t21);
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s47llp68ptjgu));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 160, t22);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sflee3fpg0btv));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 176, t23);
            gm_th4d8o29g5urm t24 = t11;
            uint8_t t25 = 0;
            gm_b16_8 t26 = t10;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 0, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 16, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 32, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 48, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 64, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 80, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 96, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 112, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 128, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 144, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 160, ((uint8_t *)&t26))) t25 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t24) + 176, ((uint8_t *)&t26))) t25 = 1;
            uint8_t t27 = (uint8_t)(t25);
            t2 = t27;
        } else t2 = 1;
        uint8_t t28 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_oversized_int_name_nr25haf F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* bits */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t6 = (t5 < ((uint64_t)2ULL));
        if (!t6) {
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcsffver18si54, 17, 24);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            gm_tqs5p4n2tq19v t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t10 + 1), ((uint8_t)105ULL));
            gm_tqs5p4n2tq19v t11 = t10;
            uint8_t t12 = 0;
            uint8_t t13 = t9;
            if (gm_eqjmccqdb90f(((uint8_t *)&t11) + 0, ((uint8_t *)&t13))) t12 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t11) + 1, ((uint8_t *)&t13))) t12 = 1;
            uint8_t t14 = (uint8_t)(!t12);
            t4 = t14;
        } else t4 = 1;
        if (!t4) {
            uint64_t t15 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcsffver18si54, 17, 53);
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
            uint8_t t18 = ((t17 == ((uint8_t)48ULL)));
            t3 = t18;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t20 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t24 = (uintptr_t)((uint64_t)1ULL);
            if (t24 > (uintptr_t)t22 || (uintptr_t)t22 > (uintptr_t)t22) gm_trap_slice(t24, (uintptr_t)t22, t22, gm_srcsffver18si54, 18, 14);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t23 + t24 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)((uintptr_t)t22 - t24));
            gm_l1 = t25;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l2;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l2;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t33 * 1));
                    uint8_t t36 = t35;
                    uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)&t36));
                    gm_l3 = t39;
                    {
                        {
                            uint8_t t43 = gm_l3;
                            gm_l4 = t43;
                        }
                        uint8_t t44 = 0;
                        uint8_t t45 = gm_l4;
                        uint8_t t46 = (((uint8_t)48ULL) <= t45);
                        if (t46) {
                            uint8_t t47 = gm_l4;
                            uint8_t t48 = (t47 <= ((uint8_t)57ULL));
                            t44 = t48;
                        } else t44 = 0;
                        uint8_t t49 = (uint8_t)!t44;
                        uint8_t t50 = t49;
                        if (!t50) goto ie41;
                        {
                            uint8_t t52 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix41;
ie41:;
ix41:;
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 18, 5);
me37:;
                }
lc26:;
                {
                    uint64_t t54 = gm_l2;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l2 = t57;
                }
            }
lb26:;
        }
    }
    {
        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t60 = (t59 > ((uint64_t)4ULL));
        if (!t60) goto ie58;
        {
            uint8_t t62 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
        {
            uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t65 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t66 = (uintptr_t)((uint64_t)1ULL);
            if (t66 > (uintptr_t)t64 || (uintptr_t)t64 > (uintptr_t)t64) gm_trap_slice(t66, (uintptr_t)t64, t64, gm_srcsffver18si54, 23, 14);
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t65 + t66 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)((uintptr_t)t64 - t66));
            gm_l6 = t67;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t69 = gm_l7;
                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t71 = (t69 < t70);
                if (!t71) goto lb68;
                {
                    uint64_t t74 = gm_l7;
                    uint64_t t75 = (uintptr_t)t74;
                    uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t75 * 1));
                    uint8_t t78 = t77;
                    uint8_t t81 = GM_LD(uint8_t, ((uint8_t *)&t78));
                    gm_l8 = t81;
                    {
                        uint32_t t83 = gm_l5;
                        uint32_t r84; bool o85 = __builtin_mul_overflow(t83, ((uint32_t)10ULL), &r84);
                        if (o85) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 23, 33);
                        uint32_t t86 = r84;
                        uint8_t t87 = gm_l8;
                        uint8_t r88; bool o89 = __builtin_sub_overflow(t87, ((uint8_t)48ULL), &r88);
                        if (o89) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 23, 49);
                        uint8_t t90 = r88;
                        uint32_t t91 = (uint32_t)t90;
                        uint32_t r92; bool o93 = __builtin_add_overflow(t86, t91, &r92);
                        if (o93) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 23, 33);
                        uint32_t t94 = r92;
                        gm_l5 = t94;
                    }
                    goto me79;
mn80:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 23, 5);
me79:;
                }
lc68:;
                {
                    uint64_t t96 = gm_l7;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    uint64_t t99 = r97;
                    gm_l7 = t99;
                }
            }
lb68:;
        }
    }
    {
        uint32_t t100 = gm_l5;
        uint8_t t101 = (t100 > ((uint32_t)128ULL));
        uint8_t t102 = 0;
        F.ret = t101;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_path_qa34i5j F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)6ULL)) goto mn9;
    t2 = ((uint8_t)1);
    goto me3;
mn9:;
ma5:;
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t12 != ((uint8_t)22ULL)) goto mn11;
    void * t13 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l1 = t13;
    void * t14 = gm_l1;
    uint8_t t15 = gm_f_is_path_qa34i5j(t14);
    t2 = t15;
    goto me3;
mn11:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn16:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 276, 46);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1) { gm_f_leave_module_scope_4hp5mc4(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_len_param_names_cilm68f F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* out */
    uint32_t gm_l4; /* p */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l7; /* m */
    gm_b16_8 gm_l8; /* n */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_t88rcucg1b7uk gm_l10; /* b */
    GM_STACK_CHECK(&F, 699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)19ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l4 = t15;
        {
            gm_b16_8 t17 = gm_l2;
            gm_l5 = t17;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l6;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l6;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tplpbqf3duvn3 t29 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t25 * 8));
                    gm_l7 = t29;
                    {
                        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
                        uint32_t t33 = gm_l4;
                        uint8_t t34 = ((t32 == t33));
                        if (!t34) goto ie31;
                        {
                            gm_t6j6smj7qoctu t36;
                            void * t37 = gm_l0;
                            void * t38 = (void *)(((uint8_t *)t37) + 8);
                            uint32_t t39 = gm_l4;
                            gm_b16_8 t40 = gm_f_param_name_rbnop5l(t38, t39);
                            GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t40);
                            void * t41 = gm_l0;
                            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 4));
                            gm_b16_8 t43 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t43);
                            gm_tabb710d9mq2e t44;
                            gm_f_format_vom8db6((void *)&t44, (void *)NULL, cx, NULL, &t36);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_tabb710d9mq2e t45 = t44;
                            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcsffver18si54, 700, 29);
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                            gm_b16_8 t47 = t46;
                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                            gm_l8 = t48;
                        }
                        {
                            gm_b16_8 t50 = gm_l8;
                            void * t51 = gm_l3;
                            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 0));
                            gm_b16_8 t53 = t52;
                            uint8_t t54 = 0;
                            gm_b16_8 t55 = t50;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t53) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t53))) + i * 16, ((uint8_t *)&t55))) { t54 = 1; break; }
                            uint8_t t56 = (uint8_t)(!t54);
                            if (!t56) goto ie49;
                            {
                                void * t58 = gm_l3;
                                gm_b16_8 t59 = gm_l8;
                                gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t58, t59);
                                gm_tftb1odc739t5 t61 = t60;
                                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srcsffver18si54, 701, 41);
                                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
                            }
                            goto ix49;
ie49:;
ix49:;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 698, 22);
me27:;
                }
lc18:;
                {
                    uint64_t t64 = gm_l6;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l6 = t67;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t69 != ((uint8_t)22ULL)) goto mn68;
        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l9 = t70;
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l10 = t71;
        {
            void * t73 = gm_l0;
            gm_t88rcucg1b7uk t74 = gm_l9;
            gm_b16_8 t75 = gm_l2;
            void * t76 = gm_l3;
            gm_b0_1 t77 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t73, t74, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l10;
            gm_b16_8 t80 = gm_l2;
            void * t81 = gm_l3;
            gm_b0_1 t82 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t78, t79, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me7;
mn68:;
ma10:;
        goto me7;
mn83:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 697, 5);
me7:;
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
__attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_len_params_bound_f7mjvn9 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    uint32_t gm_l3; /* p */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l6; /* m */
    gm_t428ue2u3hf5g gm_l7; /* k */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* b */
    GM_STACK_CHECK(&F, 605u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)19ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t15;
        {
            {
                gm_b16_8 t18 = gm_l2;
                gm_l4 = t18;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t22 = (t20 < t21);
                    if (!t22) goto lb19;
                    {
                        uint64_t t25 = gm_l5;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tplpbqf3duvn3 t30 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t26 * 8));
                        gm_l6 = t30;
                        {
                            uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                            uint32_t t34 = gm_l3;
                            uint8_t t35 = ((t33 == t34));
                            if (!t35) goto ie32;
                            {
                                void * t38 = gm_l0;
                                void * t39 = (void *)(((uint8_t *)t38) + 8);
                                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
                                gm_t53at80pfdus3 t41;
                                gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
                                gm_t53at80pfdus3 t42 = t41;
                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
                                if (t43 != ((uint8_t)21ULL)) goto ie37;
                                gm_t428ue2u3hf5g t44 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t42) + 8));
                                gm_l7 = t44;
                                {
                                    gm_tf5srh4mp4b3i t46 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l7)));
                                    gm_tf5srh4mp4b3i t47 = t46;
                                    uint8_t t48;
                                    if (((((uint8_t *)&t47))[8] != 0)) goto mn50;
                                    t48 = ((uint8_t)0);
                                    goto me49;
mn50:;
                                    t48 = ((uint8_t)1);
                                    goto me49;
mn51:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 684, 63);
me49:;
                                    uint8_t t52 = 0;
                                    F.ret = t48;
                                    goto gm_fret;
                                }
                                goto ix37;
ie37:;
ix37:;
                            }
                            {
                                uint8_t t53 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix32;
ie32:;
ix32:;
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 682, 13);
me28:;
                    }
lc19:;
                    {
                        uint64_t t55 = gm_l5;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                        uint64_t t58 = r56;
                        gm_l5 = t58;
                    }
                }
lb19:;
            }
        }
        {
            uint8_t t59 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t61 != ((uint8_t)22ULL)) goto mn60;
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l8 = t62;
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l9 = t63;
        uint8_t t64 = 0;
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l8;
        gm_b16_8 t67 = gm_l2;
        uint8_t t68 = gm_f_len_params_bound_f7mjvn9(t65, GM_CONV(gm_csguankko4lmhd, t66), t67);
        if (t68) {
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l9;
            gm_b16_8 t71 = gm_l2;
            uint8_t t72 = gm_f_len_params_bound_f7mjvn9(t69, GM_CONV(gm_csguankko4lmhd, t70), t71);
            t64 = t72;
        } else t64 = 0;
        uint8_t t73 = 0;
        F.ret = t64;
        goto gm_fret;
        goto me7;
mn60:;
ma10:;
        uint8_t t75 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me7;
mn74:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 680, 5);
me7:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_length_is_type_soi53ah F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* len */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t11;
        uint8_t t12 = 0;
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_t9805qtonbsag t15 = gm_f_value_param_tof1h6t(t13, t14);
        gm_t9805qtonbsag t16 = t15;
        uint8_t t17;
        if (((((uint8_t *)&t16))[4] != 0)) goto mn19;
        t17 = ((uint8_t)1);
        goto me18;
mn19:;
        t17 = ((uint8_t)0);
        goto me18;
mn20:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 593, 30);
me18:;
        if (t17) {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l1;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint8_t t24 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t12 = t24;
        } else t12 = 0;
        uint8_t t25 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me3;
mn9:;
ma5:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)22ULL)) goto mn26;
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        void * t30 = gm_l2;
        uint8_t t31 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t28, t29, t30, ((uint32_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
        goto me3;
mn26:;
ma6:;
        uint8_t t34 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn33:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 592, 5);
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
inline __attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_const_idpokjh F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 892u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1568) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 109, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1568) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 110, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1568) + 0)) + t15 * 224) + 0));
                gm_b16_8 t19 = gm_l1;
                gm_b16_8 t20 = t18;
                gm_b16_8 t21 = t19;
                uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                if (!t22) goto ie13;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t t25 = (uintptr_t)t24;
                    void * t26 = gm_l0;
                    uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1568) + 0) + 8);
                    if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcsffver18si54, 110, 58);
                    gm_tb07pp6njg0bk t28 = GM_LD(gm_tb07pp6njg0bk, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1568) + 0)) + t25 * 224));
                    gm_tk1gen3hgkm51 t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 225, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 226, 0);
                    GM_ST(uint32_t, (uint8_t *)&t29 + 228, 0);
                    GM_ST(gm_tb07pp6njg0bk, ((uint8_t *)&t29), t28);
                    ((uint8_t *)&t29)[224] = 1;
                    uint8_t t30 = 0;
                    F.ret = t29;
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
        gm_tk1gen3hgkm51 t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1) { gm_tk1gen3hgkm51 gm_r; gm_f_local_const_idpokjh(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_fn_aji8qnd F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1608) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 119, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1608) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 120, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1608) + 0)) + t15 * 24) + 0));
                gm_b16_8 t19 = gm_l1;
                gm_b16_8 t20 = t18;
                gm_b16_8 t21 = t19;
                uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                if (!t22) goto ie13;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t t25 = (uintptr_t)t24;
                    void * t26 = gm_l0;
                    uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1608) + 0) + 8);
                    if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcsffver18si54, 120, 55);
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1608) + 0)) + t25 * 24) + 16));
                    gm_t9805qtonbsag t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t29), t28);
                    ((uint8_t *)&t29)[4] = 1;
                    uint8_t t30 = 0;
                    F.ret = t29;
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
        gm_t9805qtonbsag t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_looks_like_type_ho5qv1k F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    gm_t3mf58gv9kf3n gm_l6; /* a */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l12; /* lhs */
    void * gm_l13; /* callee */
    gm_tkb8hpcqf70t1 gm_l14; /* g */
    void * gm_l15; /* inner */
    GM_STACK_CHECK(&F, 1855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma13; default: goto ma13; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)6ULL)) goto mn16;
        gm_t08olq97rscn3 t18 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t18;
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l1;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tf5srh4mp4b3i t24 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t25 = t24;
            if (!((((uint8_t *)&t25))[8] != 0)) goto ie20;
            uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&t25));
            gm_l4 = t26;
            {
                void * t28 = gm_l0;
                uint64_t t29 = gm_l4;
                gm_t6qooom8qa2do t30;
                gm_f_resolve_decl_pus042v((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t31 = t30;
                uint8_t t32;
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                if (t35 != ((uint8_t)0ULL)) goto mn34;
                t32 = ((uint8_t)1);
                goto me33;
mn34:;
                t32 = ((uint8_t)0);
                goto me33;
mn36:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 221, 68);
me33:;
                uint8_t t37 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint8_t t39 = gm_f_is_builtin_type_name_v5ttoka(t38);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto me3;
mn16:;
ma5:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t42 != ((uint8_t)9ULL)) goto mn41;
        void * t43 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t43;
        void * t44 = gm_l0;
        uint64_t t45 = gm_l1;
        void * t46 = gm_l5;
        uint8_t t47 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t44, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
        goto me3;
mn41:;
ma6:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t50 != ((uint8_t)28ULL)) goto mn49;
        gm_t3mf58gv9kf3n t51 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t51;
        {
            void * t54 = gm_l2;
            uint8_t t55 = gm_f_hoists_13k2a2s(t54);
            if (!t55) goto ie53;
            {
                uint8_t t57 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix53;
ie53:;
ix53:;
        }
        {
            void * t59 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
            void * t60 = gm_f_construction_call_ktqtrpo(t59);
            void * t61 = t60;
            uint8_t t62;
            if ((GM_LD(void *, ((uint8_t *)&t61)) != 0)) goto mn64;
            t62 = ((uint8_t)0);
            goto me63;
mn64:;
            t62 = ((uint8_t)1);
            goto me63;
mn65:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 230, 16);
me63:;
            if (!t62) goto ie58;
            {
                void * t67 = gm_l0;
                uint64_t t68 = gm_l1;
                void * t69 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                uint8_t t70 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t67, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint8_t t72 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn49:;
ma7:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t74 != ((uint8_t)24ULL)) goto mn73;
        void * t75 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t75;
        {
            void * t78 = gm_l2;
            uint8_t t79 = gm_f_hoists_13k2a2s(t78);
            if (!t79) goto ie77;
            {
                uint8_t t81 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix77;
ie77:;
ix77:;
        }
        {
            void * t83 = gm_l7;
            void * t84 = gm_f_construction_call_ktqtrpo(t83);
            void * t85 = t84;
            uint8_t t86;
            if ((GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto mn88;
            t86 = ((uint8_t)0);
            goto me87;
mn88:;
            t86 = ((uint8_t)1);
            goto me87;
mn89:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 235, 16);
me87:;
            if (!t86) goto ie82;
            {
                void * t91 = gm_l0;
                uint64_t t92 = gm_l1;
                void * t93 = gm_l7;
                uint8_t t94 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t91, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix82;
ie82:;
ix82:;
        }
        {
            uint8_t t96 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn73:;
ma8:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t98 != ((uint8_t)10ULL)) goto mn97;
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t99;
        {
            {
                gm_b16_8 t102 = gm_l8;
                gm_l9 = t102;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t104 = gm_l10;
                    uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t106 = (t104 < t105);
                    if (!t106) goto lb103;
                    {
                        uint64_t t109 = gm_l10;
                        uint64_t t110 = (uintptr_t)t109;
                        uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tv1o1j44ii4db t114 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t110 * 136));
                        F.l11 = t114;
                        {
                            uint8_t t117 = 0;
                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 128));
                            if (!t118) {
                                void * t119 = gm_l0;
                                uint64_t t120 = gm_l1;
                                void * t121 = (void *)(((uint8_t *)&F.l11) + 40);
                                uint8_t t122 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t119, t120, t121);
                                if (gm_unw.kind) {
                                    goto gm_ll107;
                                }
                                uint8_t t123 = (uint8_t)!t122;
                                t117 = t123;
                            } else t117 = 1;
                            if (!t117) goto ie116;
                            {
                                uint8_t t125 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix116;
ie116:;
ix116:;
                        }
                        goto me112;
mn113:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 239, 13);
me112:;
                    }
lc103:;
                    {
                        uint64_t t127 = gm_l10;
                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                        uint64_t t130 = r128;
                        gm_l10 = t130;
                    }
                }
gm_ll107:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb103; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc103; }
                }
                goto gm_lad;
lb103:;
            }
        }
        {
            uint8_t t131 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn97:;
ma9:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t135 != ((uint8_t)25ULL)) goto pa134;
        goto po133;
pa134:;
        uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t137 != ((uint8_t)26ULL)) goto pa136;
        goto po133;
pa136:;
        uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t139 != ((uint8_t)27ULL)) goto pa138;
        goto po133;
pa138:;
        uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t141 != ((uint8_t)29ULL)) goto pa140;
        goto po133;
pa140:;
        uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t143 != ((uint8_t)30ULL)) goto pa142;
        goto po133;
pa142:;
        uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t145 != ((uint8_t)31ULL)) goto pa144;
        goto po133;
pa144:;
        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t147 != ((uint8_t)32ULL)) goto pa146;
        goto po133;
pa146:;
        uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t149 != ((uint8_t)33ULL)) goto pa148;
        goto po133;
pa148:;
        goto mn132;
po133:;
        uint8_t t150 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn132:;
ma10:;
        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t152 != ((uint8_t)17ULL)) goto mn151;
        void * t153 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l12 = t153;
        void * t154 = gm_l0;
        uint64_t t155 = gm_l1;
        void * t156 = gm_l12;
        uint8_t t157 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t154, t155, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t158 = 0;
        F.ret = t157;
        goto gm_fret;
        goto me3;
mn151:;
ma11:;
        uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t160 != ((uint8_t)19ULL)) goto mn159;
        void * t161 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l13 = t161;
        {
            void * t163 = gm_l0;
            uint64_t t164 = gm_l1;
            void * t165 = gm_l13;
            gm_toblvhepnjqkh t166;
            gm_f_generic_decl_oir9hm6((void *)&t166, (void *)NULL, cx, NULL, t163, t164, t165, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_toblvhepnjqkh t167 = t166;
            gm_tkb8hpcqf70t1 t168;
            if (((((uint8_t *)&t167))[24] != 0)) {
                gm_tkb8hpcqf70t1 t169 = GM_LD(gm_tkb8hpcqf70t1, (((uint8_t *)&t167) + 0));
                t168 = t169;
            } else {
                uint8_t t170 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l14 = t168;
        }
        {
            void * t171 = gm_l0;
            uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 0));
            gm_t6qooom8qa2do t173;
            gm_f_resolve_decl_pus042v((void *)&t173, (void *)NULL, cx, NULL, t171, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t174 = t173;
            uint8_t t175;
            uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t174) + 0));
            if (t178 != ((uint8_t)1ULL)) goto mn177;
            t175 = ((uint8_t)1);
            goto me176;
mn177:;
            t175 = ((uint8_t)0);
            goto me176;
mn179:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 248, 20);
me176:;
            uint8_t t180 = 0;
            F.ret = t175;
            goto gm_fret;
        }
        goto me3;
mn159:;
ma12:;
        uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t182 != ((uint8_t)35ULL)) goto mn181;
        void * t183 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l15 = t183;
        void * t184 = gm_l0;
        uint64_t t185 = gm_l1;
        void * t186 = gm_l15;
        uint8_t t187 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t184, t185, t186);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t188 = 0;
        F.ret = t187;
        goto gm_fret;
        goto me3;
mn181:;
ma13:;
        uint8_t t190 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn189:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 219, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_names_error_oet8t6b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 623u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l3 = t5;
        {
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tf5srh4mp4b3i t10 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t11 = t10;
            uint64_t t12;
            if (((((uint8_t *)&t11))[8] != 0)) {
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                t12 = t13;
            } else {
                uint8_t t14 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l4 = t12;
        }
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l4;
            gm_t6qooom8qa2do t17;
            gm_f_resolve_decl_pus042v((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t18 = t17;
            uint8_t t19 = ((GM_LD(uint8_t, ((uint8_t *)&t18) + 0) == ((uint8_t)11ULL)));
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t22 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_names_type_rjdgib9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_l2;
        gm_tkn42u7na3t81 t5 = gm_f_type_param_3av4r8i(t3, t4);
        gm_tkn42u7na3t81 t6 = t5;
        uint8_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) goto mn9;
        t7 = ((uint8_t)0);
        goto me8;
mn9:;
        t7 = ((uint8_t)1);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 138, 8);
me8:;
        if (!t7) goto ie2;
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        uint64_t t15 = gm_l1;
        gm_b16_8 t16 = gm_l2;
        gm_tf5srh4mp4b3i t17 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t18 = t17;
        if (!((((uint8_t *)&t18))[8] != 0)) goto ie13;
        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
        gm_l3 = t19;
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l3;
            uint8_t t23 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t21, t22, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b16_8 t25 = gm_l2;
        uint8_t t26 = gm_f_is_builtin_type_name_v5ttoka(t25);
        uint8_t t27 = 0;
        F.ret = t26;
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
inline __attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_operand_3n5jchh F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* inner */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma4; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t11 != ((uint8_t)28ULL)) goto mn10;
    gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t12;
    void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
    void * t14 = gm_f_prefix_operand_3n5jchh(t13);
    t2 = t14;
    goto me3;
mn10:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)24ULL)) goto mn15;
    void * t17 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l2 = t17;
    void * t18 = gm_l2;
    void * t19 = gm_f_prefix_operand_3n5jchh(t18);
    t2 = t19;
    goto me3;
mn15:;
ma6:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)25ULL)) goto pa22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t24;
    goto po21;
pa22:;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t26 != ((uint8_t)26ULL)) goto pa25;
    void * t27 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t27;
    goto po21;
pa25:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t29 != ((uint8_t)27ULL)) goto pa28;
    void * t30 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t30;
    goto po21;
pa28:;
    goto mn20;
po21:;
    void * t31 = gm_l3;
    void * t32 = gm_f_prefix_operand_3n5jchh(t31);
    t2 = t32;
    goto me3;
mn20:;
ma7:;
    void * t34 = gm_l0;
    t2 = t34;
    goto me3;
mn33:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 295, 64);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_push_down_hge098p F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* inner */
    void * gm_l2; /* b */
    void * gm_l3; /* b */
    void * gm_l4; /* b */
    void * gm_l5; /* b */
    void * gm_l6; /* b */
    void * gm_l7; /* b */
    gm_tao63pk3r18v5 gm_l8; /* x */
    void * gm_l9; /* callee */
    void * gm_l10; /* base */
    void * gm_l11; /* next */
    void * gm_l12; /* callee */
    void * gm_l13; /* down */
    void * gm_l14; /* i */
    gm_t08olq97rscn3 gm_l15; /* name */
    uint32_t gm_l16; /* i */
    gm_tl874t2atnlh3 gm_l17; /* at */
    gm_tao63pk3r18v5 gm_l18; /* x */
    gm_b16_8 gm_l19; /* args */
    uint8_t gm_l20; /* trailing */
    GM_STACK_CHECK(&F, 1819u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; default: goto ma9; }
ma5:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)20ULL)) goto pa14;
        void * t16 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t16;
        goto po13;
pa14:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)22ULL)) goto pa17;
        void * t19 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t19;
        goto po13;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)15ULL)) goto pa20;
        void * t22 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t22;
        goto po13;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)16ULL)) goto pa23;
        void * t25 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t25;
        goto po13;
pa23:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)18ULL)) goto pa26;
        void * t28 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t28;
        goto po13;
pa26:;
        goto mn12;
po13:;
        void * t29 = gm_l2;
        t3 = t29;
        goto me4;
mn12:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)23ULL)) goto mn30;
        void * t32 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t32;
        void * t33 = gm_l7;
        t3 = t33;
        goto me4;
mn30:;
ma7:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)21ULL)) goto mn34;
        gm_tao63pk3r18v5 t36 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t36;
        void * t37 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
        t3 = t37;
        goto me4;
mn34:;
ma8:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)19ULL)) goto mn38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t40;
        void * t41;
        void * t43 = gm_l9;
        uint8_t t44 = gm_f_calls_on_value_q0pa6h5(t43);
        if (!t44) goto ie42;
        void * t46 = gm_l9;
        void * t47 = t46;
        t41 = t47;
        goto ix42;
ie42:;
        uint8_t t49 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        t41 = ((void *)0);
ix42:;
        t3 = t41;
        goto me4;
mn38:;
ma9:;
        uint8_t t51 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me4;
mn50:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 894, 13);
me4:;
        gm_l10 = t3;
    }
    {
        void * t52 = gm_l10;
        gm_tq7s98ihonkju t53 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t52)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8b8p636a5m)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcsffver18si54, 901, 23);
        void * t55 = GM_LD(void *, (((uint8_t *)&t54) + 8));
        gm_l11 = t55;
    }
    {
        void * t57 = gm_l10;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
        if (t58 != ((uint8_t)19ULL)) goto ie56;
        void * t59 = GM_LD(void *, (((((uint8_t *)t57) + 8) + 8) + 0));
        gm_l12 = t59;
        void * t60 = gm_l12;
        uint8_t t61 = gm_f_calls_on_value_q0pa6h5(t60);
        uint8_t t62 = (uint8_t)!t61;
        if (!t62) goto ie56;
        {
            void * t64 = gm_l0;
            void * t65 = gm_l10;
            void * t66 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t66;
        }
        {
            void * t67 = gm_l11;
            uint8_t *t68 = (((uint8_t *)t67) + 72);
            void * t69 = gm_l0;
            gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
            void * t71 = gm_l10;
            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
            gm_tl874t2atnlh3 t73 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t70), GM_CONV(gm_cseahvg8v7p0vs, t72)));
            GM_ST(gm_tl874t2atnlh3, t68, t73);
        }
        goto ix56;
ie56:;
        {
            void * t75 = gm_l0;
            void * t76 = gm_l10;
            void * t77 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t78 = t77;
            void * t79;
            if ((GM_LD(void *, ((uint8_t *)&t78)) != 0)) {
                void * t80 = GM_LD(void *, (((uint8_t *)&t78) + 0));
                t79 = t80;
            } else {
                uint8_t t81 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l13 = t79;
        }
        {
            void * t82 = gm_l13;
            gm_tq7s98ihonkju t83 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t82)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klh5uk0bvu4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t84 = t83;
            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 8, gm_srcsffver18si54, 907, 16);
            void * t85 = GM_LD(void *, (((uint8_t *)&t84) + 8));
            gm_l11 = t85;
        }
ix56:;
    }
    {
        void * t86 = gm_l1;
        gm_t2gtbh39ptekd t87;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        switch (t99) { case ((uint8_t)0ULL): goto ma97; case ((uint8_t)1ULL): goto ma97; case ((uint8_t)2ULL): goto ma97; case ((uint8_t)3ULL): goto ma97; case ((uint8_t)4ULL): goto ma97; case ((uint8_t)5ULL): goto ma97; case ((uint8_t)6ULL): goto ma97; case ((uint8_t)7ULL): goto ma97; case ((uint8_t)8ULL): goto ma97; case ((uint8_t)9ULL): goto ma97; case ((uint8_t)10ULL): goto ma97; case ((uint8_t)11ULL): goto ma97; case ((uint8_t)12ULL): goto ma97; case ((uint8_t)13ULL): goto ma97; case ((uint8_t)14ULL): goto ma97; case ((uint8_t)15ULL): goto ma91; case ((uint8_t)16ULL): goto ma92; case ((uint8_t)17ULL): goto ma97; case ((uint8_t)18ULL): goto ma93; case ((uint8_t)19ULL): goto ma96; case ((uint8_t)20ULL): goto ma89; case ((uint8_t)21ULL): goto ma95; case ((uint8_t)22ULL): goto ma90; case ((uint8_t)23ULL): goto ma94; case ((uint8_t)24ULL): goto ma97; case ((uint8_t)25ULL): goto ma97; case ((uint8_t)26ULL): goto ma97; case ((uint8_t)27ULL): goto ma97; case ((uint8_t)28ULL): goto ma97; case ((uint8_t)29ULL): goto ma97; case ((uint8_t)30ULL): goto ma97; case ((uint8_t)31ULL): goto ma97; case ((uint8_t)32ULL): goto ma97; case ((uint8_t)33ULL): goto ma97; case ((uint8_t)34ULL): goto ma97; case ((uint8_t)35ULL): goto ma97; case ((uint8_t)36ULL): goto ma97; case ((uint8_t)37ULL): goto ma97; case ((uint8_t)38ULL): goto ma97; case ((uint8_t)39ULL): goto ma97; case ((uint8_t)40ULL): goto ma97; case ((uint8_t)41ULL): goto ma97; case ((uint8_t)42ULL): goto ma97; case ((uint8_t)43ULL): goto ma97; case ((uint8_t)44ULL): goto ma97; case ((uint8_t)45ULL): goto ma97; case ((uint8_t)46ULL): goto ma97; case ((uint8_t)47ULL): goto ma97; case ((uint8_t)48ULL): goto ma97; case ((uint8_t)49ULL): goto ma97; case ((uint8_t)50ULL): goto ma97; default: goto ma97; }
ma89:;
        uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t101 != ((uint8_t)20ULL)) goto mn100;
        void * t102 = GM_LD(void *, (((((uint8_t *)t86) + 8) + 8) + 8));
        gm_l14 = t102;
        void * t103 = gm_l11;
        void * t104 = gm_l14;
        gm_t3mf58gv9kf3n t105;
        GM_ST(void *, ((uint8_t *)&t105 + 0), t103);
        GM_ST(void *, ((uint8_t *)&t105 + 8), t104);
        gm_t2gtbh39ptekd t106;
        memset(&t106, 0, sizeof t106);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)20ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t106 + 8), t105);
        t87 = t106;
        goto me88;
mn100:;
ma90:;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t108 != ((uint8_t)22ULL)) goto mn107;
        gm_t08olq97rscn3 t109 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t86) + 8) + 8) + 8));
        gm_l15 = t109;
        gm_tioie2ju3q4ul t110;
        void * t111 = gm_l11;
        GM_ST(void *, (uint8_t *)&t110 + 0, t111);
        memcpy((uint8_t *)&t110 + 8, ((uint8_t *)&gm_l15), 32);
        gm_t2gtbh39ptekd t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t112 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t112 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)22ULL));
        GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t112 + 8), t110);
        t87 = t112;
        goto me88;
mn107:;
ma91:;
        uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t114 != ((uint8_t)15ULL)) goto mn113;
        void * t115 = gm_l11;
        gm_t2gtbh39ptekd t116;
        memset(&t116, 0, sizeof t116);
        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)15ULL));
        GM_ST(void *, ((uint8_t *)&t116 + 8), t115);
        t87 = t116;
        goto me88;
mn113:;
ma92:;
        uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t118 != ((uint8_t)16ULL)) goto mn117;
        void * t119 = gm_l11;
        gm_t2gtbh39ptekd t120;
        memset(&t120, 0, sizeof t120);
        GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)16ULL));
        GM_ST(void *, ((uint8_t *)&t120 + 8), t119);
        t87 = t120;
        goto me88;
mn117:;
ma93:;
        uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t122 != ((uint8_t)18ULL)) goto mn121;
        void * t123 = gm_l11;
        gm_t2gtbh39ptekd t124;
        memset(&t124, 0, sizeof t124);
        GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)18ULL));
        GM_ST(void *, ((uint8_t *)&t124 + 8), t123);
        t87 = t124;
        goto me88;
mn121:;
ma94:;
        uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t126 != ((uint8_t)23ULL)) goto mn125;
        uint32_t t127 = GM_LD(uint32_t, (((((uint8_t *)t86) + 8) + 8) + 8));
        gm_l16 = t127;
        gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t86) + 8) + 8) + 12));
        gm_l17 = t128;
        gm_tupk62cr6lbdu t129;
        void * t130 = gm_l11;
        GM_ST(void *, (uint8_t *)&t129 + 0, t130);
        uint32_t t131 = gm_l16;
        GM_ST(uint32_t, (uint8_t *)&t129 + 8, t131);
        gm_tl874t2atnlh3 t132 = gm_l17;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t129 + 12, t132);
        gm_t2gtbh39ptekd t133;
        memset(&t133, 0, sizeof t133);
        GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)23ULL));
        GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t133 + 8), t129);
        t87 = t133;
        goto me88;
mn125:;
ma95:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t135 != ((uint8_t)21ULL)) goto mn134;
        gm_tao63pk3r18v5 t136 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t86) + 8) + 8));
        gm_l18 = t136;
        gm_tao63pk3r18v5 t137;
        memset((uint8_t *)&t137 + 25, 0, 7);
        void * t138 = gm_l11;
        GM_ST(void *, (uint8_t *)&t137 + 0, t138);
        void * t139 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
        GM_ST(void *, (uint8_t *)&t137 + 8, t139);
        void * t140 = GM_LD(void *, (((uint8_t *)&gm_l18) + 16));
        GM_ST(void *, (uint8_t *)&t137 + 16, t140);
        uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 24));
        GM_ST(uint8_t, (uint8_t *)&t137 + 24, t141);
        gm_t2gtbh39ptekd t142;
        GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t142 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t142 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t142 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t142 + 8), t137);
        t87 = t142;
        goto me88;
mn134:;
ma96:;
        uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t144 != ((uint8_t)19ULL)) goto mn143;
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((((uint8_t *)t86) + 8) + 8) + 8));
        gm_l19 = t145;
        uint8_t t146 = GM_LD(uint8_t, (((((uint8_t *)t86) + 8) + 8) + 24));
        gm_l20 = t146;
        gm_tlljg61a2ctgq t147;
        memset((uint8_t *)&t147 + 25, 0, 7);
        void * t148 = gm_l11;
        GM_ST(void *, (uint8_t *)&t147 + 0, t148);
        gm_b16_8 t149 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t147 + 8, t149);
        uint8_t t150 = gm_l20;
        GM_ST(uint8_t, (uint8_t *)&t147 + 24, t150);
        gm_t2gtbh39ptekd t151;
        GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t151 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t151 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t151 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t151 + 8), t147);
        t87 = t151;
        goto me88;
mn143:;
ma97:;
        uint8_t t153 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me88;
mn152:;
ma98:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 909, 26);
me88:;
        F.l21 = t87;
    }
    {
        gm_tc3oht4ksth9d t154;
        memset((uint8_t *)&t154 + 4, 0, 4);
        memset((uint8_t *)&t154 + 84, 0, 4);
        void * t155 = gm_l1;
        uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)t155) + 0));
        GM_ST(uint32_t, (uint8_t *)&t154 + 0, t156);
        memcpy((uint8_t *)&t154 + 8, ((uint8_t *)&F.l21), 64);
        void * t157 = gm_l1;
        gm_tl874t2atnlh3 t158 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t157) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t154 + 72, t158);
        gm_tq7s98ihonkju t159 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t154, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kijoa5b5euq)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t160 = t159;
        if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_srcsffver18si54, 920, 12);
        void * t161 = GM_LD(void *, (((uint8_t *)&t160) + 8));
        uint8_t t162 = 0;
        F.ret = t161;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pushes_down_0djaflp F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    void * gm_l5; /* base */
    void * gm_l6; /* base */
    gm_tao63pk3r18v5 gm_l7; /* x */
    void * gm_l8; /* callee */
    void * gm_l9; /* next */
    void * gm_l10; /* callee */
    GM_STACK_CHECK(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; default: goto ma9; }
ma5:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)20ULL)) goto pa14;
        void * t16 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t16;
        goto po13;
pa14:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)22ULL)) goto pa17;
        void * t19 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t19;
        goto po13;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)15ULL)) goto pa20;
        void * t22 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t22;
        goto po13;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)16ULL)) goto pa23;
        void * t25 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t25;
        goto po13;
pa23:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)18ULL)) goto pa26;
        void * t28 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t28;
        goto po13;
pa26:;
        goto mn12;
po13:;
        void * t29 = gm_l1;
        t3 = t29;
        goto me4;
mn12:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)23ULL)) goto mn30;
        void * t32 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t32;
        void * t33 = gm_l6;
        t3 = t33;
        goto me4;
mn30:;
ma7:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)21ULL)) goto mn34;
        gm_tao63pk3r18v5 t36 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t36;
        void * t37 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
        t3 = t37;
        goto me4;
mn34:;
ma8:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)19ULL)) goto mn38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t40;
        void * t41;
        void * t43 = gm_l8;
        uint8_t t44 = gm_f_calls_on_value_q0pa6h5(t43);
        if (!t44) goto ie42;
        void * t46 = gm_l8;
        void * t47 = t46;
        t41 = t47;
        goto ix42;
ie42:;
        uint8_t t49 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        t41 = ((void *)0);
ix42:;
        t3 = t41;
        goto me4;
mn38:;
ma9:;
        uint8_t t51 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn50:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 303, 13);
me4:;
        gm_l9 = t3;
    }
    {
        void * t53 = gm_l9;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
        if (t54 != ((uint8_t)19ULL)) goto ie52;
        void * t55 = GM_LD(void *, (((((uint8_t *)t53) + 8) + 8) + 0));
        gm_l10 = t55;
        void * t56 = gm_l10;
        uint8_t t57 = gm_f_calls_on_value_q0pa6h5(t56);
        uint8_t t58 = (uint8_t)!t57;
        if (!t58) goto ie52;
        {
            uint8_t t60 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        void * t61 = gm_l9;
        uint8_t t62 = gm_f_pushes_down_0djaflp(t61);
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_jjodbei F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint8_t gm_l4; /* sized */
    GM_STACK_CHECK(&F, 293u);
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
        gm_t88rcucg1b7uk t5 = gm_f_resolve_type_unchecked_psme7dn((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_t53at80pfdus3 t9;
        gm_f_kind_jcnt4hm((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_t53at80pfdus3 t10 = t9;
        uint8_t t11;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t16 != ((uint8_t)11ULL)) goto pa15;
        goto po14;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t18 != ((uint8_t)12ULL)) goto pa17;
        goto po14;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t20 != ((uint8_t)13ULL)) goto pa19;
        goto po14;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t22 != ((uint8_t)14ULL)) goto pa21;
        goto po14;
pa21:;
        goto mn13;
po14:;
        t11 = ((uint8_t)1);
        goto me12;
mn13:;
        t11 = ((uint8_t)0);
        goto me12;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 320, 14);
me12:;
        gm_l4 = t11;
    }
    {
        uint8_t t25 = gm_l4;
        if (!t25) goto ie24;
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1728) + 0) + 8);
        uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
        if (!t28) goto ie24;
        void * t29 = gm_l0;
        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t29) + 1816));
        uint8_t t31 = ((t30 == ((uint32_t)0ULL)));
        if (!t31) goto ie24;
        void * t32 = gm_l0;
        gm_t88rcucg1b7uk t33 = gm_l3;
        void * t34 = gm_l2;
        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
        uint8_t t36 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t32, t33, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t37 = (uint8_t)!t36;
        if (!t37) goto ie24;
        {
            void * t39 = gm_l0;
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_t88rcucg1b7uk t41 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_t88rcucg1b7uk t43 = gm_l3;
        uint8_t t44 = 0;
        F.ret = t43;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_unchecked_psme7dn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* tys */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint32_t gm_l6; /* p */
    uint64_t gm_l7; /* d */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* msg */
    gm_b16_8 gm_l12; /* hint */
    gm_b16_8 gm_l14; /* last */
    void * gm_l15; /* inner */
    gm_b16_8 gm_l16; /* elems */
    uint8_t gm_l17; /* named */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t08olq97rscn3 gm_l26; /* n */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    gm_b16_8 gm_l30; /* n */
    void * gm_l31; /* inner */
    uint8_t gm_l32; /* is_const */
    void * gm_l33; /* inner */
    void * gm_l34; /* sig */
    uint8_t gm_l36; /* is_const */
    void * gm_l37; /* inner */
    uint8_t gm_l38; /* is_const */
    void * gm_l39; /* inner */
    gm_t3mf58gv9kf3n gm_l40; /* a */
    gm_t88rcucg1b7uk gm_l41; /* elem */
    void * gm_l42; /* len */
    gm_t88rcucg1b7uk gm_l43; /* index */
    uint64_t gm_l44; /* n */
    gm_t88rcucg1b7uk gm_l45; /* l */
    void * gm_l46; /* err */
    void * gm_l47; /* ok */
    void * gm_l48; /* err */
    void * gm_l49; /* ok */
    gm_t88rcucg1b7uk gm_l50; /* et */
    gm_t88rcucg1b7uk gm_l51; /* ot */
    void * gm_l52; /* sig */
    void * gm_l54; /* inner */
    void * gm_l55; /* callee */
    gm_b16_8 gm_l56; /* args */
    void * gm_l57; /* base */
    gm_t9805qtonbsag gm_l58; /* template */
    gm_tkb8hpcqf70t1 gm_l59; /* g */
    uint32_t gm_l60; /* t */
    uint32_t gm_l61; /* t */
    gm_b16_8 gm_l62; /* targs */
    gm_b16_8 gm_l63; /* params */
    gm_b16_8 gm_l64; /* <tmp> */
    uint64_t gm_l65; /* <tmp> */
    uint64_t gm_l66; /* i */
    uint32_t gm_l67; /* pid */
    gm_t88rcucg1b7uk gm_l68; /* a */
    gm_b16_8 gm_l69; /* <tmp> */
    uint64_t gm_l70; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l71; /* c */
    gm_b16_8 gm_l72; /* <tmp> */
    uint64_t gm_l73; /* <tmp> */
    gm_t88rcucg1b7uk gm_l74; /* a */
    gm_t88rcucg1b7uk gm_l75; /* part */
    gm_t88rcucg1b7uk gm_l76; /* inst */
    gm_b16_8 gm_l77; /* attrs */
    void * gm_l78; /* inner */
    void * gm_l79; /* f */
    gm_b16_8 gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    gm_b16_8 gm_l83; /* name */
    gm_b16_8 gm_l85; /* args */
    void * gm_l87; /* hook */
    void * gm_l88; /* base */
    gm_t08olq97rscn3 gm_l89; /* name */
    uint64_t gm_l90; /* m */
    uint64_t gm_l91; /* d */
    gm_t88rcucg1b7uk gm_l92; /* t */
    GM_STACK_CHECK(&F, 10905u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l2;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t25) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma23; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma23; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma23; case ((uint8_t)15ULL): goto ma23; case ((uint8_t)16ULL): goto ma23; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma23; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma23; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma23; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma15; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma19; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma23; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma23; case ((uint8_t)37ULL): goto ma23; case ((uint8_t)38ULL): goto ma23; case ((uint8_t)39ULL): goto ma23; case ((uint8_t)40ULL): goto ma23; case ((uint8_t)41ULL): goto ma23; case ((uint8_t)42ULL): goto ma23; case ((uint8_t)43ULL): goto ma23; case ((uint8_t)44ULL): goto ma23; case ((uint8_t)45ULL): goto ma23; case ((uint8_t)46ULL): goto ma23; case ((uint8_t)47ULL): goto ma23; case ((uint8_t)48ULL): goto ma23; case ((uint8_t)49ULL): goto ma21; case ((uint8_t)50ULL): goto ma23; default: goto ma23; }
ma6:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t27 != ((uint8_t)6ULL)) goto mn26;
        gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t4) + 8) + 8));
        gm_l4 = t28;
        {
            void * t31 = gm_l0;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tkn42u7na3t81 t33 = gm_f_type_param_3av4r8i(t31, t32);
            gm_tkn42u7na3t81 t34 = t33;
            if (!((((uint8_t *)&t34))[4] != 0)) goto ie30;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t34));
            gm_l5 = t35;
            {
                void * t38 = gm_l3;
                gm_t88rcucg1b7uk t39 = gm_l5;
                gm_t53at80pfdus3 t40;
                gm_f_kind_jcnt4hm((void *)&t40, t38, GM_CONV(gm_csguankko4lmhd, t39));
                gm_t53at80pfdus3 t41 = t40;
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
                if (t42 != ((uint8_t)19ULL)) goto ie37;
                uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&t41) + 8));
                gm_l6 = t43;
                uint32_t t44 = gm_l6;
                uint64_t t45 = (uint64_t)t44;
                uint64_t t46 = (uintptr_t)t45;
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 1128) + 0) + 8);
                if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_srcsffver18si54, 335, 49);
                uint8_t t49 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t47) + 1128) + 0)) + t46 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                if (!t49) goto ie37;
                {
                    void * t51 = gm_l0;
                    void * t52 = gm_l2;
                    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
                    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                    gm_tnpifv20irnph t55;
                    GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                    gm_tabb710d9mq2e t56;
                    gm_f_format_o5o5km2((void *)&t56, (void *)NULL, cx, NULL, t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t57 = t56;
                    if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_srcsffver18si54, 336, 37);
                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
                    gm_b16_8 t59 = t58;
                    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                    gm_b0_1 t61 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t51, t53, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t62 = gm_l3;
                    gm_t88rcucg1b7uk t63 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t64 = 0;
                    F.ret = t63;
                    goto gm_fret;
                }
                goto ix37;
ie37:;
ix37:;
            }
            {
                gm_t88rcucg1b7uk t65 = gm_l5;
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t68 = gm_l0;
            uint64_t t69 = gm_l1;
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t71 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t68, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t72 = t71;
            if (!((((uint8_t *)&t72))[8] != 0)) goto ie67;
            uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t72));
            gm_l7 = t73;
            {
                void * t75 = gm_l0;
                uint64_t t76 = gm_l7;
                gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b0_1 t78 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t75, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t79 = gm_l0;
                uint64_t t80 = gm_l7;
                gm_t6qooom8qa2do t81;
                gm_f_resolve_decl_pus042v((void *)&t81, (void *)NULL, cx, NULL, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t82 = t81;
                uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t82) + 0));
                switch (t88) { case ((uint8_t)0ULL): goto ma84; case ((uint8_t)1ULL): goto ma86; case ((uint8_t)2ULL): goto ma86; case ((uint8_t)3ULL): goto ma86; case ((uint8_t)4ULL): goto ma86; case ((uint8_t)5ULL): goto ma86; case ((uint8_t)6ULL): goto ma86; case ((uint8_t)7ULL): goto ma86; case ((uint8_t)8ULL): goto ma86; case ((uint8_t)9ULL): goto ma86; case ((uint8_t)10ULL): goto ma86; case ((uint8_t)11ULL): goto ma85; default: goto ma86; }
ma84:;
                uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t82) + 0));
                if (t90 != ((uint8_t)0ULL)) goto mn89;
                gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t82) + 8));
                gm_l8 = t91;
                gm_t88rcucg1b7uk t92 = gm_l8;
                uint8_t t93 = 0;
                F.ret = t92;
                goto gm_fret;
                goto me83;
mn89:;
ma85:;
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t82) + 0));
                if (t95 != ((uint8_t)11ULL)) goto mn94;
                void * t96 = gm_l3;
                gm_t88rcucg1b7uk t97 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
                goto me83;
mn94:;
ma86:;
                {
                    void * t101 = gm_l0;
                    void * t102 = gm_l2;
                    gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t102) + 72));
                    gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                    gm_tnpifv20irnph t105;
                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t104);
                    gm_tabb710d9mq2e t106;
                    gm_f_format_sv91363((void *)&t106, (void *)NULL, cx, NULL, t105);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t107 = t106;
                    if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srcsffver18si54, 347, 41);
                    gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
                    gm_b16_8 t109 = t108;
                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                    gm_b0_1 t111 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t101, t103, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t112 = gm_l3;
                    gm_t88rcucg1b7uk t113 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t114 = 0;
                    F.ret = t113;
                    goto gm_fret;
                }
                goto me83;
mn99:;
ma87:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 343, 17);
me83:;
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            void * t116 = gm_l0;
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tkn42u7na3t81 t118 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t116, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t119 = t118;
            if (!((((uint8_t *)&t119))[4] != 0)) goto ie115;
            gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t119));
            gm_l9 = t120;
            {
                gm_t88rcucg1b7uk t122 = gm_l9;
                uint8_t t123 = 0;
                F.ret = t122;
                goto gm_fret;
            }
            goto ix115;
ie115:;
ix115:;
        }
        {
            gm_b16_8 t124;
            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t127 = gm_f_is_oversized_int_name_nr25haf(t126);
            if (!t127) goto ie125;
            gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tnpifv20irnph t130;
            GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t129);
            gm_tabb710d9mq2e t131;
            gm_f_format_oq3rvor((void *)&t131, (void *)NULL, cx, NULL, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t132 = t131;
            if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 8, gm_srcsffver18si54, 354, 17);
            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
            gm_b16_8 t134 = t133;
            t124 = t134;
            goto ix125;
ie125:;
            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tnpifv20irnph t137;
            GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
            gm_tabb710d9mq2e t138;
            gm_f_format_pplcoh0((void *)&t138, (void *)NULL, cx, NULL, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t139 = t138;
            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_srcsffver18si54, 356, 17);
            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
            gm_b16_8 t141 = t140;
            t124 = t141;
ix125:;
            gm_l10 = t124;
        }
        {
            void * t142 = gm_l2;
            gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
            gm_b16_8 t144 = gm_l10;
            gm_b16_8 t145 = t144;
            gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
            gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t143, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t148 = gm_l0;
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_th2lqbo77pvsc t150;
            gm_f_universe_hint_ams3f10((void *)&t150, (void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t151 = t150;
            if (!((((uint8_t *)&t151))[16] != 0)) goto ie147;
            gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
            gm_l12 = t152;
            {
                void * t154 = (void *)((uint8_t *)&F.l11);
                gm_b16_8 t155 = gm_l12;
                gm_f_add_note_46v502s(t154, t155);
            }
            {
                void * t157 = gm_l0;
                uint64_t t158 = gm_l1;
                gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                void * t160 = gm_l2;
                gm_tl874t2atnlh3 t161 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t160) + 72));
                gm_tqetrgo0q24b5 t162;
                gm_f_import_fix_7822666((void *)&t162, (void *)NULL, cx, NULL, t157, t158, t159, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqetrgo0q24b5 t163 = t162;
                if (!((((uint8_t *)&t163))[40] != 0)) goto ie156;
                gm_trqfvmjtt4sij t164 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t163));
                F.l13 = t164;
                {
                    uint64_t t166 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                    uint64_t r167; bool o168 = __builtin_sub_overflow(t166, ((uint64_t)1ULL), &r167);
                    if (o168) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 362, 41);
                    uint64_t t169 = r167;
                    uint64_t t170 = (uintptr_t)t169;
                    uint64_t t171 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                    if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_srcsffver18si54, 362, 29);
                    gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t170 * 32) + 16));
                    gm_l14 = t172;
                }
                {
                    void * t173 = (void *)((uint8_t *)&F.l11);
                    gm_b16_8 t174 = gm_l14;
                    gm_tnpifv20irnph t175;
                    GM_ST(gm_b16_8, ((uint8_t *)&t175 + 0), t174);
                    gm_tabb710d9mq2e t176;
                    gm_f_format_ooibcq2((void *)&t176, (void *)NULL, cx, NULL, t175);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t177 = t176;
                    if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 8, gm_srcsffver18si54, 363, 37);
                    gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&t177) + 8));
                    gm_b16_8 t179 = t178;
                    gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
                    gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    gm_b16_8 t182 = t181;
                    gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
                    gm_b0_1 t184 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t173, t180, t183);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix156;
ie156:;
ix156:;
            }
            goto ix147;
ie147:;
ix147:;
        }
        {
            void * t185 = gm_l0;
            gm_b0_1 t186 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t185, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t187 = gm_l3;
            gm_t88rcucg1b7uk t188 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t189 = 0;
            F.ret = t188;
            goto gm_fret;
        }
        goto me5;
mn26:;
ma7:;
        uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t191 != ((uint8_t)9ULL)) goto mn190;
        void * t192 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l15 = t192;
        void * t193 = gm_l0;
        uint64_t t194 = gm_l1;
        void * t195 = gm_l15;
        gm_t88rcucg1b7uk t196 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t193, t194, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t197 = 0;
        F.ret = t196;
        goto gm_fret;
        goto me5;
mn190:;
ma8:;
        uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t199 != ((uint8_t)10ULL)) goto mn198;
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l16 = t200;
        {
            gm_b16_8 t203 = gm_l16;
            gm_b16_8 t204 = t203;
            gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
            uint8_t t206 = gm_f_has_spread_bndtpd6(t205);
            if (!t206) goto ie202;
            {
                void * t208 = gm_l0;
                void * t209 = gm_l2;
                gm_tl874t2atnlh3 t210 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t209) + 72));
                gm_b16_8 t211;
                GM_ST(void *, (uint8_t *)&t211, (void *)(gm_shbaullahutpe));
                GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(36));
                gm_b0_1 t212 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t208, t210, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t213 = gm_l3;
                gm_t88rcucg1b7uk t214 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t215 = 0;
                F.ret = t214;
                goto gm_fret;
            }
            goto ix202;
ie202:;
ix202:;
        }
        {
            uint8_t t216 = 0;
            uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
            uint8_t t218 = (t217 > ((uint64_t)0ULL));
            if (t218) {
                uint64_t t219 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                if (t219 >= (uintptr_t)(t220)) gm_trap_index(t219, t220, gm_srcsffver18si54, 375, 40);
                uint8_t t221;
                if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t219 * 136) + 0))[32] != 0)) goto mn223;
                t221 = ((uint8_t)0);
                goto me222;
mn223:;
                t221 = ((uint8_t)1);
                goto me222;
mn224:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 375, 40);
me222:;
                t216 = t221;
            } else t216 = 0;
            gm_l17 = t216;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t226 = gm_l16;
                gm_l19 = t226;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t228 = gm_l20;
                    uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t230 = (t228 < t229);
                    if (!t230) goto lb227;
                    {
                        uint64_t t233 = gm_l20;
                        uint64_t t234 = (uintptr_t)t233;
                        uint64_t t235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tv1o1j44ii4db t238 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t234 * 136));
                        F.l21 = t238;
                        {
                            void * t240 = (void *)((uint8_t *)&F.l18);
                            void * t241 = gm_l0;
                            uint64_t t242 = gm_l1;
                            void * t243 = (void *)(((uint8_t *)&F.l21) + 40);
                            gm_t88rcucg1b7uk t244 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t241, t242, t243);
                            if (gm_unw.kind) {
                                goto gm_ll231;
                            }
                            gm_tftb1odc739t5 t245 = gm_f_push_k3l9nic(t240, GM_CONV(gm_csguankko4lmhd, t244));
                            gm_tftb1odc739t5 t246 = t245;
                            if (((((uint8_t *)&t246))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t246) + 1, gm_srcsffver18si54, 377, 31);
                            gm_b0_1 t247 = GM_LD(gm_b0_1, (((uint8_t *)&t246) + 1));
                        }
                        goto me236;
mn237:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 377, 13);
me236:;
                    }
lc227:;
                    {
                        uint64_t t249 = gm_l20;
                        uint64_t r250; bool o251 = __builtin_add_overflow(t249, ((uint64_t)1ULL), &r250);
                        uint64_t t252 = r250;
                        gm_l20 = t252;
                    }
                }
gm_ll231:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb227; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc227; }
                }
                goto gm_lad;
lb227:;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t254 = gm_l17;
            if (!t254) goto ie253;
            {
                {
                    gm_b16_8 t257 = gm_l16;
                    gm_l23 = t257;
                }
                {
                    gm_l24 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t259 = gm_l24;
                        uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint8_t t261 = (t259 < t260);
                        if (!t261) goto lb258;
                        {
                            uint64_t t264 = gm_l24;
                            uint64_t t265 = (uintptr_t)t264;
                            uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            gm_tv1o1j44ii4db t269 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t265 * 136));
                            F.l25 = t269;
                            {
                                gm_t1s75i08qtptd t271 = GM_LD(gm_t1s75i08qtptd, (((uint8_t *)&F.l25) + 0));
                                gm_t1s75i08qtptd t272 = t271;
                                gm_t08olq97rscn3 t273;
                                if (((((uint8_t *)&t272))[32] != 0)) {
                                    gm_t08olq97rscn3 t274 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t272) + 0));
                                    t273 = t274;
                                } else {
                                    {
                                        void * t276 = gm_l0;
                                        void * t277 = gm_l2;
                                        gm_tl874t2atnlh3 t278 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t277) + 72));
                                        gm_b16_8 t279;
                                        GM_ST(void *, (uint8_t *)&t279, (void *)(gm_sjdn5gs7lu39i));
                                        GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(56));
                                        gm_b0_1 t280 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t276, t278, t279);
                                        if (gm_unw.kind) {
                                            goto gm_ll262;
                                        }
                                    }
                                    void * t281 = gm_l3;
                                    gm_t88rcucg1b7uk t282 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t281);
                                    if (gm_unw.kind) {
                                        goto gm_ll262;
                                    }
                                    uint8_t t283 = 0;
                                    F.ret = t282;
                                    goto gm_fret;
                                }
                                gm_l26 = t273;
                            }
                            {
                                void * t284 = (void *)((uint8_t *)&F.l22);
                                gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 0));
                                gm_tftb1odc739t5 t286 = gm_f_push_3ef746b(t284, t285);
                                gm_tftb1odc739t5 t287 = t286;
                                if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 1, gm_srcsffver18si54, 385, 21);
                                gm_b0_1 t288 = GM_LD(gm_b0_1, (((uint8_t *)&t287) + 1));
                            }
                            goto me267;
mn268:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 380, 17);
me267:;
                        }
lc258:;
                        {
                            uint64_t t290 = gm_l24;
                            uint64_t r291; bool o292 = __builtin_add_overflow(t290, ((uint64_t)1ULL), &r291);
                            uint64_t t293 = r291;
                            gm_l24 = t293;
                        }
                    }
gm_ll262:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb258; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc258; }
                    }
                    goto gm_lad;
lb258:;
                }
            }
            {
                {
                    gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                    gm_b16_8 t296 = t295;
                    gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&t296));
                    gm_l27 = t297;
                }
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t299 = gm_l28;
                        uint64_t t300 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        uint8_t t301 = (t299 < t300);
                        if (!t301) goto lb298;
                        {
                            gm_tf1bjg81u0glv t304;
                            uint64_t t305 = gm_l28;
                            GM_ST(uint64_t, (uint8_t *)&t304 + 0, t305);
                            uint64_t t306 = gm_l28;
                            uint64_t t307 = (uintptr_t)t306;
                            uint64_t t308 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t307 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t304 + 8, t309);
                            gm_tf1bjg81u0glv t310 = t304;
                            uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&t310) + 0));
                            gm_l29 = t313;
                            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&t310) + 8));
                            gm_l30 = t314;
                            {
                                gm_b16_8 t317 = gm_l30;
                                uint64_t t318 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                                void * t319 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l22) + 0)));
                                uint64_t t320 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t321 = gm_l29;
                                uint64_t t322 = (uintptr_t)t321;
                                if (t320 > t322 || t322 > (uintptr_t)t318) gm_trap_slice(t320, t322, t318, gm_srcsffver18si54, 388, 29);
                                gm_b16_8 t323;
                                GM_ST(void *, (uint8_t *)&t323, (void *)(((uint8_t *)t319 + t320 * 16)));
                                GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(t322 - t320));
                                gm_b16_8 t324 = t323;
                                uint8_t t325 = 0;
                                gm_b16_8 t326 = t317;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t324) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t324))) + i * 16, ((uint8_t *)&t326))) { t325 = 1; break; }
                                uint8_t t327 = (uint8_t)(t325);
                                if (!t327) goto ie316;
                                {
                                    void * t329 = gm_l0;
                                    void * t330 = gm_l2;
                                    gm_tl874t2atnlh3 t331 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t330) + 72));
                                    gm_b16_8 t332 = gm_l30;
                                    gm_tnpifv20irnph t333;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t333 + 0), t332);
                                    gm_tabb710d9mq2e t334;
                                    gm_f_format_6nmb1bf((void *)&t334, (void *)NULL, cx, NULL, t333);
                                    if (gm_unw.kind) {
                                        goto gm_ll302;
                                    }
                                    gm_tabb710d9mq2e t335 = t334;
                                    if (((((uint8_t *)&t335))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t335) + 8, gm_srcsffver18si54, 389, 41);
                                    gm_b16_8 t336 = GM_LD(gm_b16_8, (((uint8_t *)&t335) + 8));
                                    gm_b16_8 t337 = t336;
                                    gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
                                    gm_b0_1 t339 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t329, t331, t338);
                                    if (gm_unw.kind) {
                                        goto gm_ll302;
                                    }
                                }
                                {
                                    void * t340 = gm_l3;
                                    gm_t88rcucg1b7uk t341 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t340);
                                    if (gm_unw.kind) {
                                        goto gm_ll302;
                                    }
                                    uint8_t t342 = 0;
                                    F.ret = t341;
                                    goto gm_fret;
                                }
                                goto ix316;
ie316:;
ix316:;
                            }
                            goto me311;
mn312:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 387, 17);
me311:;
                        }
lc298:;
                        {
                            uint64_t t344 = gm_l28;
                            uint64_t r345; bool o346 = __builtin_add_overflow(t344, ((uint64_t)1ULL), &r345);
                            uint64_t t347 = r345;
                            gm_l28 = t347;
                        }
                    }
gm_ll302:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb298; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc298; }
                    }
                    goto gm_lad;
lb298:;
                }
            }
            goto ix253;
ie253:;
ix253:;
        }
        {
            void * t348 = gm_l3;
            gm_t6j6smj7qoctu t349;
            gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            gm_b16_8 t351 = t350;
            gm_b16_8 t352 = GM_LD(gm_b16_8, ((uint8_t *)&t351));
            GM_ST(gm_b16_8, (uint8_t *)&t349 + 0, t352);
            gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_b16_8 t354 = t353;
            gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
            GM_ST(gm_b16_8, (uint8_t *)&t349 + 16, t355);
            gm_t53at80pfdus3 t356;
            GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t356 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t356 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t356 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t356 + 8), t349);
            gm_t88rcucg1b7uk t357 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t348, &t356);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t358 = 0;
            F.ret = t357;
            goto gm_fret;
        }
        goto me5;
mn198:;
ma9:;
        uint8_t t360 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t360 != ((uint8_t)24ULL)) goto mn359;
        void * t361 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l31 = t361;
        void * t362 = gm_l3;
        void * t363 = gm_l0;
        uint64_t t364 = gm_l1;
        void * t365 = gm_l31;
        gm_t88rcucg1b7uk t366 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t363, t364, t365);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t367 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t362, t366);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t368 = 0;
        F.ret = t367;
        goto gm_fret;
        goto me5;
mn359:;
ma10:;
        uint8_t t370 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t370 != ((uint8_t)25ULL)) goto mn369;
        uint8_t t371 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l32 = t371;
        void * t372 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l33 = t372;
        {
            void * t375 = gm_l33;
            uint8_t t376 = GM_LD(uint8_t, ((((uint8_t *)t375) + 8) + 0));
            if (t376 != ((uint8_t)30ULL)) goto ie374;
            void * t377 = GM_LD(void *, ((((uint8_t *)t375) + 8) + 8));
            gm_l34 = t377;
            {
                uint8_t t380 = gm_l32;
                if (!t380) goto ie379;
                {
                    void * t382 = gm_l0;
                    void * t383 = gm_l2;
                    gm_tl874t2atnlh3 t384 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t383) + 72));
                    gm_b16_8 t385;
                    GM_ST(void *, (uint8_t *)&t385, (void *)(gm_sgva3apqq9jo2));
                    GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(67));
                    gm_b0_1 t386 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t382, t384, t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix379;
ie379:;
ix379:;
            }
            {
                void * t387 = gm_l0;
                uint64_t t388 = gm_l1;
                void * t389 = gm_l34;
                gm_b16_8 t390 = gm_f_empty_slice_3jt8vgk();
                gm_b16_8 t391 = t390;
                gm_b16_8 t392 = GM_LD(gm_b16_8, ((uint8_t *)&t391));
                gm_tg93b272d11vo t393;
                gm_f_resolve_fn_sig_pamuhve((void *)&t393, (void *)NULL, cx, NULL, t387, t388, t389, ((uint8_t)0), t392);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tg93b272d11vo t394 = t393;
                gm_tqqhoivahirr0 t395;
                if (((((uint8_t *)&t394))[136] != 0)) {
                    gm_tqqhoivahirr0 t396 = GM_LD(gm_tqqhoivahirr0, (((uint8_t *)&t394) + 0));
                    t395 = t396;
                } else {
                    void * t397 = gm_l3;
                    gm_t88rcucg1b7uk t398 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t397);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t399 = 0;
                    F.ret = t398;
                    goto gm_fret;
                }
                F.l35 = t395;
            }
            {
                void * t400 = gm_l3;
                void * t401 = gm_l0;
                void * t402 = (void *)((uint8_t *)&F.l35);
                gm_tddtjfggdat0d t403;
                gm_f_fn_ty_qaih7ts((void *)&t403, (void *)NULL, cx, NULL, t401, t402);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t53at80pfdus3 t404;
                GM_ST(uint8_t, (uint8_t *)&t404 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t404 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t404 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t404 + 0), ((uint8_t)16ULL));
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t404 + 8), t403);
                gm_t88rcucg1b7uk t405 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t400, &t404);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t406 = 0;
                F.ret = t405;
                goto gm_fret;
            }
            goto ix374;
ie374:;
ix374:;
        }
        {
            void * t407 = gm_l3;
            uint8_t t408 = gm_l32;
            void * t409 = gm_l0;
            uint64_t t410 = gm_l1;
            void * t411 = gm_l33;
            gm_t88rcucg1b7uk t412 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t409, t410, t411);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t413 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t407, t408, t412);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t414 = 0;
            F.ret = t413;
            goto gm_fret;
        }
        goto me5;
mn369:;
ma11:;
        uint8_t t416 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t416 != ((uint8_t)26ULL)) goto mn415;
        uint8_t t417 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l36 = t417;
        void * t418 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l37 = t418;
        void * t419 = gm_l3;
        uint8_t t420 = gm_l36;
        void * t421 = gm_l0;
        uint64_t t422 = gm_l1;
        void * t423 = gm_l37;
        gm_t88rcucg1b7uk t424 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t421, t422, t423);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t425 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t419, t420, t424);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t426 = 0;
        F.ret = t425;
        goto gm_fret;
        goto me5;
mn415:;
ma12:;
        uint8_t t428 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t428 != ((uint8_t)27ULL)) goto mn427;
        uint8_t t429 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l38 = t429;
        void * t430 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l39 = t430;
        void * t431 = gm_l3;
        uint8_t t432 = gm_l38;
        void * t433 = gm_l0;
        uint64_t t434 = gm_l1;
        void * t435 = gm_l39;
        gm_t88rcucg1b7uk t436 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t433, t434, t435);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t437 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t431, t432, t436);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t438 = 0;
        F.ret = t437;
        goto gm_fret;
        goto me5;
mn427:;
ma13:;
        uint8_t t440 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t440 != ((uint8_t)28ULL)) goto mn439;
        gm_t3mf58gv9kf3n t441 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t4) + 8) + 8));
        gm_l40 = t441;
        {
            void * t443 = gm_l0;
            uint64_t t444 = gm_l1;
            void * t445 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
            gm_t88rcucg1b7uk t446 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t443, t444, t445);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l41 = t446;
        }
        {
            void * t447 = GM_LD(void *, (((uint8_t *)&gm_l40) + 0));
            void * t448 = t447;
            void * t449;
            if ((GM_LD(void *, ((uint8_t *)&t448)) != 0)) {
                void * t450 = GM_LD(void *, (((uint8_t *)&t448) + 0));
                t449 = t450;
            } else {
                {
                    void * t452 = gm_l0;
                    void * t453 = gm_l2;
                    gm_tl874t2atnlh3 t454 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t453) + 72));
                    gm_b16_8 t455;
                    GM_ST(void *, (uint8_t *)&t455, (void *)(gm_sm9funvq1222p));
                    GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(43));
                    gm_b0_1 t456 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t452, t454, t455);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t457 = gm_l3;
                gm_t88rcucg1b7uk t458 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t457);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t459 = 0;
                F.ret = t458;
                goto gm_fret;
            }
            gm_l42 = t449;
        }
        {
            void * t461 = gm_l0;
            uint64_t t462 = gm_l1;
            void * t463 = gm_l42;
            uint8_t t464 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t461, t462, t463);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t464) goto ie460;
            {
                void * t466 = gm_l0;
                uint64_t t467 = gm_l1;
                void * t468 = gm_l42;
                gm_t88rcucg1b7uk t469 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t466, t467, t468);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l43 = t469;
            }
            {
                void * t470 = gm_l0;
                gm_t88rcucg1b7uk t471 = gm_l43;
                gm_t88rcucg1b7uk t472 = gm_l41;
                void * t473 = gm_l42;
                gm_tl874t2atnlh3 t474 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t473) + 72));
                gm_t88rcucg1b7uk t475 = gm_f_enum_array_type_0sic6t0((void *)NULL, cx, NULL, t470, t471, t472, t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t476 = 0;
                F.ret = t475;
                goto gm_fret;
            }
            goto ix460;
ie460:;
ix460:;
        }
        {
            void * t477 = gm_l0;
            uint64_t t478 = gm_l1;
            void * t479 = gm_l42;
            gm_t5e6kkuq21nig t480 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t477, t478, t479);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5e6kkuq21nig t481 = t480;
            uint8_t t487 = GM_LD(uint8_t, (((uint8_t *)&t481) + 0));
            switch (t487) { case ((uint8_t)0ULL): goto ma483; case ((uint8_t)1ULL): goto ma484; case ((uint8_t)2ULL): goto ma485; default: goto ma486; }
ma483:;
            uint8_t t489 = GM_LD(uint8_t, (((uint8_t *)&t481) + 0));
            if (t489 != ((uint8_t)0ULL)) goto mn488;
            uint64_t t490 = GM_LD(uint64_t, (((uint8_t *)&t481) + 8));
            gm_l44 = t490;
            void * t491 = gm_l3;
            uint64_t t492 = gm_l44;
            gm_t88rcucg1b7uk t493 = gm_l41;
            gm_t88rcucg1b7uk t494 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t491, t492, t493);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t495 = 0;
            F.ret = t494;
            goto gm_fret;
            goto me482;
mn488:;
ma484:;
            uint8_t t497 = GM_LD(uint8_t, (((uint8_t *)&t481) + 0));
            if (t497 != ((uint8_t)1ULL)) goto mn496;
            gm_t88rcucg1b7uk t498 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t481) + 8));
            gm_l45 = t498;
            void * t499 = gm_l3;
            gm_t88rcucg1b7uk t500 = gm_l45;
            gm_t88rcucg1b7uk t501 = gm_l41;
            gm_tgglga8ro3isc t502;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t502 + 0), t500);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t502 + 4), t501);
            gm_t53at80pfdus3 t503;
            memset(&t503, 0, sizeof t503);
            GM_ST(uint8_t, ((uint8_t *)&t503 + 0), ((uint8_t)20ULL));
            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t503 + 8), t502);
            gm_t88rcucg1b7uk t504 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t499, &t503);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t505 = 0;
            F.ret = t504;
            goto gm_fret;
            goto me482;
mn496:;
ma485:;
            uint8_t t507 = GM_LD(uint8_t, (((uint8_t *)&t481) + 0));
            if (t507 != ((uint8_t)2ULL)) goto mn506;
            void * t508 = gm_l3;
            gm_t88rcucg1b7uk t509 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t508);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t510 = 0;
            F.ret = t509;
            goto gm_fret;
            goto me482;
mn506:;
ma486:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 417, 13);
me482:;
        }
        goto me5;
mn439:;
ma14:;
        uint8_t t514 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t514 != ((uint8_t)29ULL)) goto pa513;
        void * t515 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l46 = t515;
        void * t516 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l47 = t516;
        goto po512;
pa513:;
        uint8_t t518 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t518 != ((uint8_t)17ULL)) goto pa517;
        void * t519 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l46 = t519;
        void * t520 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l47 = t520;
        goto po512;
pa517:;
        goto mn511;
po512:;
        {
            void * t522 = gm_l0;
            uint64_t t523 = gm_l1;
            void * t524 = gm_l46;
            gm_t88rcucg1b7uk t525 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t522, t523, t524);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l50 = t525;
        }
        {
            void * t526 = gm_l0;
            uint64_t t527 = gm_l1;
            void * t528 = gm_l47;
            gm_t88rcucg1b7uk t529 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t526, t527, t528);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l51 = t529;
        }
        {
            void * t530 = gm_l3;
            gm_t88rcucg1b7uk t531 = gm_l50;
            gm_t88rcucg1b7uk t532 = gm_l51;
            gm_t88rcucg1b7uk t533 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t530, t531, t532);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t534 = 0;
            F.ret = t533;
            goto gm_fret;
        }
        goto me5;
mn511:;
ma15:;
        uint8_t t536 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t536 != ((uint8_t)30ULL)) goto mn535;
        void * t537 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l52 = t537;
        {
            void * t539 = gm_l0;
            uint64_t t540 = gm_l1;
            void * t541 = gm_l52;
            gm_b16_8 t542 = gm_f_empty_slice_3jt8vgk();
            gm_b16_8 t543 = t542;
            gm_b16_8 t544 = GM_LD(gm_b16_8, ((uint8_t *)&t543));
            gm_tg93b272d11vo t545;
            gm_f_resolve_fn_sig_pamuhve((void *)&t545, (void *)NULL, cx, NULL, t539, t540, t541, ((uint8_t)0), t544);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg93b272d11vo t546 = t545;
            gm_tqqhoivahirr0 t547;
            if (((((uint8_t *)&t546))[136] != 0)) {
                gm_tqqhoivahirr0 t548 = GM_LD(gm_tqqhoivahirr0, (((uint8_t *)&t546) + 0));
                t547 = t548;
            } else {
                void * t549 = gm_l3;
                gm_t88rcucg1b7uk t550 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t549);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t551 = 0;
                F.ret = t550;
                goto gm_fret;
            }
            F.l53 = t547;
        }
        {
            void * t552 = gm_l3;
            void * t553 = gm_l0;
            void * t554 = (void *)((uint8_t *)&F.l53);
            gm_tddtjfggdat0d t555;
            gm_f_fn_ty_qaih7ts((void *)&t555, (void *)NULL, cx, NULL, t553, t554);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t556;
            GM_ST(uint8_t, (uint8_t *)&t556 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t556 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t556 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t556 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t556 + 8), t555);
            gm_t88rcucg1b7uk t557 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t552, &t556);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t558 = 0;
            F.ret = t557;
            goto gm_fret;
        }
        goto me5;
mn535:;
ma16:;
        uint8_t t560 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t560 != ((uint8_t)31ULL)) goto mn559;
        void * t561 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l54 = t561;
        void * t562 = gm_l3;
        void * t563 = gm_l0;
        uint64_t t564 = gm_l1;
        void * t565 = gm_l54;
        gm_t88rcucg1b7uk t566 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t563, t564, t565);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t567;
        memset(&t567, 0, sizeof t567);
        GM_ST(uint8_t, ((uint8_t *)&t567 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t567 + 8), t566);
        gm_t88rcucg1b7uk t568 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t562, &t567);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t569 = 0;
        F.ret = t568;
        goto gm_fret;
        goto me5;
mn559:;
ma17:;
        uint8_t t571 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t571 != ((uint8_t)19ULL)) goto mn570;
        void * t572 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l55 = t572;
        gm_b16_8 t573 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l56 = t573;
        {
            void * t576 = gm_l55;
            uint8_t t577 = GM_LD(uint8_t, ((((uint8_t *)t576) + 8) + 0));
            if (t577 != ((uint8_t)22ULL)) goto ie575;
            void * t578 = GM_LD(void *, (((((uint8_t *)t576) + 8) + 8) + 0));
            gm_l57 = t578;
            void * t579 = gm_l0;
            uint64_t t580 = gm_l1;
            void * t581 = gm_l57;
            uint8_t t582 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t579, t580, t581);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t582) goto ie575;
            {
                void * t584 = gm_l3;
                gm_t88rcucg1b7uk t585 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t584);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t586 = 0;
                F.ret = t585;
                goto gm_fret;
            }
            goto ix575;
ie575:;
ix575:;
        }
        {
            gm_t9805qtonbsag t587;
            memset(&t587, 0, sizeof t587);
            gm_l58 = t587;
        }
        {
            void * t589 = gm_l0;
            uint64_t t590 = gm_l1;
            void * t591 = gm_l55;
            gm_toblvhepnjqkh t592;
            gm_f_generic_decl_oir9hm6((void *)&t592, (void *)NULL, cx, NULL, t589, t590, t591, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_toblvhepnjqkh t593 = t592;
            if (!((((uint8_t *)&t593))[24] != 0)) goto ie588;
            gm_tkb8hpcqf70t1 t594 = GM_LD(gm_tkb8hpcqf70t1, ((uint8_t *)&t593));
            gm_l59 = t594;
            {
                void * t596 = gm_l0;
                uint64_t t597 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 0));
                gm_t6qooom8qa2do t598;
                gm_f_resolve_decl_pus042v((void *)&t598, (void *)NULL, cx, NULL, t596, t597);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t599 = t598;
                uint8_t t605 = GM_LD(uint8_t, (((uint8_t *)&t599) + 0));
                switch (t605) { case ((uint8_t)0ULL): goto ma603; case ((uint8_t)1ULL): goto ma601; case ((uint8_t)2ULL): goto ma603; case ((uint8_t)3ULL): goto ma603; case ((uint8_t)4ULL): goto ma603; case ((uint8_t)5ULL): goto ma603; case ((uint8_t)6ULL): goto ma603; case ((uint8_t)7ULL): goto ma603; case ((uint8_t)8ULL): goto ma603; case ((uint8_t)9ULL): goto ma603; case ((uint8_t)10ULL): goto ma603; case ((uint8_t)11ULL): goto ma602; default: goto ma603; }
ma601:;
                uint8_t t607 = GM_LD(uint8_t, (((uint8_t *)&t599) + 0));
                if (t607 != ((uint8_t)1ULL)) goto mn606;
                uint32_t t608 = GM_LD(uint32_t, (((uint8_t *)&t599) + 8));
                gm_l60 = t608;
                {
                    void * t610 = gm_l0;
                    uint64_t t611 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 0));
                    gm_tl874t2atnlh3 t612 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l59) + 8));
                    gm_b0_1 t613 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t610, t611, t612);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint32_t t614 = gm_l60;
                    gm_t9805qtonbsag t615;
                    GM_ST(uint8_t, (uint8_t *)&t615 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t615 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t615), t614);
                    ((uint8_t *)&t615)[4] = 1;
                    gm_l58 = t615;
                }
                goto me600;
mn606:;
ma602:;
                uint8_t t617 = GM_LD(uint8_t, (((uint8_t *)&t599) + 0));
                if (t617 != ((uint8_t)11ULL)) goto mn616;
                void * t618 = gm_l3;
                gm_t88rcucg1b7uk t619 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t618);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t620 = 0;
                F.ret = t619;
                goto gm_fret;
                goto me600;
mn616:;
ma603:;
                goto me600;
mn621:;
ma604:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 438, 17);
me600:;
            }
            goto ix588;
ie588:;
            void * t624 = gm_l55;
            uint8_t t625;
            uint8_t t628 = GM_LD(uint8_t, ((((uint8_t *)t624) + 8) + 0));
            if (t628 != ((uint8_t)22ULL)) goto mn627;
            t625 = ((uint8_t)1);
            goto me626;
mn627:;
            t625 = ((uint8_t)0);
            goto me626;
mn629:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 446, 23);
me626:;
            if (!t625) goto ie623;
            {
                void * t631 = gm_l3;
                gm_t88rcucg1b7uk t632 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t631);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t633 = 0;
                F.ret = t632;
                goto gm_fret;
            }
            goto ix623;
ie623:;
ix623:;
ix588:;
        }
        {
            gm_t9805qtonbsag t634 = gm_l58;
            gm_t9805qtonbsag t635 = t634;
            uint32_t t636;
            if (((((uint8_t *)&t635))[4] != 0)) {
                uint32_t t637 = GM_LD(uint32_t, (((uint8_t *)&t635) + 0));
                t636 = t637;
            } else {
                {
                    void * t639 = gm_l0;
                    void * t640 = gm_l2;
                    gm_tl874t2atnlh3 t641 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t640) + 72));
                    gm_b16_8 t642;
                    GM_ST(void *, (uint8_t *)&t642, (void *)(gm_s2328gjukbu28));
                    GM_ST(uintptr_t, (uint8_t *)&t642 + 8, (uintptr_t)(15));
                    gm_b0_1 t643 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t639, t641, t642);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t644 = gm_l3;
                gm_t88rcucg1b7uk t645 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t644);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t646 = 0;
                F.ret = t645;
                goto gm_fret;
            }
            gm_l61 = t636;
        }
        {
            void * t647 = gm_l0;
            uint64_t t648 = gm_l1;
            uint32_t t649 = gm_l61;
            gm_b16_8 t650 = gm_l56;
            gm_b16_8 t651 = t650;
            gm_b16_8 t652 = GM_LD(gm_b16_8, ((uint8_t *)&t651));
            void * t653 = gm_l2;
            gm_tl874t2atnlh3 t654 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t653) + 72));
            gm_th2lqbo77pvsc t655;
            gm_f_generic_type_args_5b8bvov((void *)&t655, (void *)NULL, cx, NULL, t647, t648, t649, t652, t654);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t656 = t655;
            gm_b16_8 t657;
            if (((((uint8_t *)&t656))[16] != 0)) {
                gm_b16_8 t658 = GM_LD(gm_b16_8, (((uint8_t *)&t656) + 0));
                t657 = t658;
            } else {
                void * t659 = gm_l3;
                gm_t88rcucg1b7uk t660 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t659);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t661 = 0;
                F.ret = t660;
                goto gm_fret;
            }
            gm_l62 = t657;
        }
        {
            uint32_t t662 = gm_l61;
            uint64_t t663 = (uint64_t)t662;
            uint64_t t664 = (uintptr_t)t663;
            void * t665 = gm_l0;
            uint64_t t666 = GM_LD(uintptr_t, ((((uint8_t *)t665) + 1088) + 0) + 8);
            if (t664 >= (uintptr_t)(t666)) gm_trap_index(t664, t666, gm_srcsffver18si54, 454, 23);
            gm_b16_8 t667 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t665) + 1088) + 0)) + t664 * 184) + 120));
            gm_l63 = t667;
        }
        {
            {
                gm_b16_8 t669 = gm_l63;
                gm_b16_8 t670 = t669;
                gm_b16_8 t671 = GM_LD(gm_b16_8, ((uint8_t *)&t670));
                gm_l64 = t671;
            }
            {
                gm_l65 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t673 = gm_l65;
                    uint64_t t674 = GM_LD(uintptr_t, ((uint8_t *)&gm_l64) + 8);
                    uint8_t t675 = (t673 < t674);
                    if (!t675) goto lb672;
                    {
                        uint64_t t678 = gm_l65;
                        uint64_t t679 = gm_l65;
                        uint64_t t680 = (uintptr_t)t679;
                        uint64_t t681 = GM_LD(uintptr_t, ((uint8_t *)&gm_l64) + 8);
                        uint32_t t682 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l64)) + t680 * 4));
                        gm_tn68el8d14vku t683;
                        GM_ST(uint32_t, (uint8_t *)&t683 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t683 + 0), t678);
                        GM_ST(uint32_t, ((uint8_t *)&t683 + 8), t682);
                        gm_tn68el8d14vku t684 = t683;
                        uint64_t t687 = GM_LD(uint64_t, (((uint8_t *)&t684) + 0));
                        gm_l66 = t687;
                        uint32_t t688 = GM_LD(uint32_t, (((uint8_t *)&t684) + 8));
                        gm_l67 = t688;
                        {
                            uint64_t t691 = gm_l66;
                            uint64_t t692 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                            uint8_t t693 = (t691 >= t692);
                            if (!t693) goto ie690;
                            {
                                goto lb672;
                            }
                            goto ix690;
ie690:;
ix690:;
                        }
                        {
                            uint64_t t695 = gm_l66;
                            uint64_t t696 = (uintptr_t)t695;
                            uint64_t t697 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                            if (t696 >= (uintptr_t)(t697)) gm_trap_index(t696, t697, gm_srcsffver18si54, 457, 22);
                            gm_t88rcucg1b7uk t698 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t696 * 4));
                            gm_l68 = t698;
                        }
                        {
                            {
                                uint32_t t700 = gm_l67;
                                uint64_t t701 = (uint64_t)t700;
                                uint64_t t702 = (uintptr_t)t701;
                                void * t703 = gm_l0;
                                uint64_t t704 = GM_LD(uintptr_t, ((((uint8_t *)t703) + 1128) + 0) + 8);
                                if (t702 >= (uintptr_t)(t704)) gm_trap_index(t702, t704, gm_srcsffver18si54, 458, 26);
                                gm_b16_8 t705 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t703) + 1128) + 0)) + t702 * 48) + 8) + 0));
                                gm_l69 = t705;
                            }
                            {
                                gm_l70 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t707 = gm_l70;
                                    uint64_t t708 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                    uint8_t t709 = (t707 < t708);
                                    if (!t709) goto lb706;
                                    {
                                        uint64_t t712 = gm_l70;
                                        uint64_t t713 = (uintptr_t)t712;
                                        uint64_t t714 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                        gm_t9qkh0rsooc8d t717 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t713 * 1));
                                        gm_l71 = t717;
                                        {
                                            void * t720 = gm_l0;
                                            gm_t88rcucg1b7uk t721 = gm_l68;
                                            gm_t9qkh0rsooc8d t722 = gm_l71;
                                            uint8_t t723 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t720, t721, t722);
                                            if (gm_unw.kind) {
                                                goto gm_ll710;
                                            }
                                            uint8_t t724 = (uint8_t)!t723;
                                            if (!t724) goto ie719;
                                            {
                                                void * t727 = gm_l0;
                                                uint32_t t728 = GM_LD(uint32_t, (((uint8_t *)t727) + 1768));
                                                uint8_t t729 = (t728 > ((uint32_t)0ULL));
                                                if (!t729) goto ie726;
                                                void * t730 = gm_l3;
                                                gm_t88rcucg1b7uk t731 = gm_l68;
                                                uint8_t t732 = gm_f_has_param_u9t0efs(t730, GM_CONV(gm_csguankko4lmhd, t731));
                                                if (!t732) goto ie726;
                                                {
                                                    void * t734 = gm_l0;
                                                    void * t735 = (void *)(((uint8_t *)t734) + 1776);
                                                    gm_t8er5olt969ip t736;
                                                    memset((uint8_t *)&t736 + 5, 0, 3);
                                                    gm_t88rcucg1b7uk t737 = gm_l68;
                                                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t736 + 0, t737);
                                                    gm_t9qkh0rsooc8d t738 = gm_l71;
                                                    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t736 + 4, t738);
                                                    uint32_t t739 = gm_l67;
                                                    GM_ST(uint32_t, (uint8_t *)&t736 + 8, t739);
                                                    uint32_t t740 = gm_l61;
                                                    GM_ST(uint32_t, (uint8_t *)&t736 + 12, t740);
                                                    void * t741 = gm_l2;
                                                    gm_tl874t2atnlh3 t742 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t741) + 72));
                                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t736 + 16, t742);
                                                    gm_tftb1odc739t5 t743 = gm_f_push_afk8dfq(t735, &t736);
                                                    gm_tftb1odc739t5 t744 = t743;
                                                    if (((((uint8_t *)&t744))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t744) + 1, gm_srcsffver18si54, 463, 29);
                                                    gm_b0_1 t745 = GM_LD(gm_b0_1, (((uint8_t *)&t744) + 1));
                                                }
                                                goto ix726;
ie726:;
                                                {
                                                    void * t747 = gm_l0;
                                                    void * t748 = gm_l2;
                                                    gm_tl874t2atnlh3 t749 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t748) + 72));
                                                    void * t750 = gm_l0;
                                                    gm_t88rcucg1b7uk t751 = gm_l68;
                                                    gm_t9qkh0rsooc8d t752 = gm_l71;
                                                    uint32_t t753 = gm_l67;
                                                    uint32_t t754 = gm_l61;
                                                    gm_b16_8 t755 = gm_f_unsatisfied_jnmtj8v((void *)NULL, cx, NULL, t750, t751, t752, t753, t754);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll710;
                                                    }
                                                    gm_b0_1 t756 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t747, t749, t755);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll710;
                                                    }
                                                }
ix726:;
                                            }
                                            goto ix719;
ie719:;
ix719:;
                                        }
                                        goto me715;
mn716:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 458, 17);
me715:;
                                    }
lc706:;
                                    {
                                        uint64_t t758 = gm_l70;
                                        uint64_t r759; bool o760 = __builtin_add_overflow(t758, ((uint64_t)1ULL), &r759);
                                        uint64_t t761 = r759;
                                        gm_l70 = t761;
                                    }
                                }
gm_ll710:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb706; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc706; }
                                }
                                goto gm_ll676;
lb706:;
                            }
                        }
                        goto me685;
mn686:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 455, 13);
me685:;
                    }
lc672:;
                    {
                        uint64_t t763 = gm_l65;
                        uint64_t r764; bool o765 = __builtin_add_overflow(t763, ((uint64_t)1ULL), &r764);
                        uint64_t t766 = r764;
                        gm_l65 = t766;
                    }
                }
gm_ll676:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb672; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc672; }
                }
                goto gm_lad;
lb672:;
            }
        }
        {
            {
                gm_b16_8 t768 = gm_l62;
                gm_l72 = t768;
            }
            {
                gm_l73 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t770 = gm_l73;
                    uint64_t t771 = GM_LD(uintptr_t, ((uint8_t *)&gm_l72) + 8);
                    uint8_t t772 = (t770 < t771);
                    if (!t772) goto lb769;
                    {
                        uint64_t t775 = gm_l73;
                        uint64_t t776 = (uintptr_t)t775;
                        uint64_t t777 = GM_LD(uintptr_t, ((uint8_t *)&gm_l72) + 8);
                        gm_t88rcucg1b7uk t780 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l72)) + t776 * 4));
                        gm_l74 = t780;
                        {
                            void * t783 = gm_l0;
                            gm_t88rcucg1b7uk t784 = gm_l74;
                            gm_tkn42u7na3t81 t785 = gm_f_second_class_part_k6i54mk(t783, GM_CONV(gm_csguankko4lmhd, t784));
                            gm_tkn42u7na3t81 t786 = t785;
                            if (!((((uint8_t *)&t786))[4] != 0)) goto ie782;
                            gm_t88rcucg1b7uk t787 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t786));
                            gm_l75 = t787;
                            {
                                void * t789 = gm_l0;
                                void * t790 = gm_l2;
                                gm_tl874t2atnlh3 t791 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t790) + 72));
                                void * t792 = gm_l0;
                                gm_t88rcucg1b7uk t793 = gm_l74;
                                gm_t88rcucg1b7uk t794 = gm_l75;
                                gm_b16_8 t795 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t792, t793, t794);
                                if (gm_unw.kind) {
                                    goto gm_ll773;
                                }
                                gm_b0_1 t796 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t789, t791, t795);
                                if (gm_unw.kind) {
                                    goto gm_ll773;
                                }
                            }
                            goto ix782;
ie782:;
ix782:;
                        }
                        goto me778;
mn779:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 470, 13);
me778:;
                    }
lc769:;
                    {
                        uint64_t t798 = gm_l73;
                        uint64_t r799; bool o800 = __builtin_add_overflow(t798, ((uint64_t)1ULL), &r799);
                        uint64_t t801 = r799;
                        gm_l73 = t801;
                    }
                }
gm_ll773:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb769; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc769; }
                }
                goto gm_lad;
lb769:;
            }
        }
        {
            void * t802 = gm_l0;
            uint32_t t803 = gm_l61;
            gm_b16_8 t804 = gm_l62;
            gm_b16_8 t805 = t804;
            gm_b16_8 t806 = GM_LD(gm_b16_8, ((uint8_t *)&t805));
            gm_t88rcucg1b7uk t807 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t802, t803, t806);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l76 = t807;
        }
        {
            void * t808 = gm_l0;
            void * t809 = gm_l2;
            gm_tl874t2atnlh3 t810 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t809) + 72));
            gm_b0_1 t811 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t808, t810);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t88rcucg1b7uk t812 = gm_l76;
            uint8_t t813 = 0;
            F.ret = t812;
            goto gm_fret;
        }
        goto me5;
mn570:;
ma18:;
        uint8_t t815 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t815 != ((uint8_t)35ULL)) goto mn814;
        gm_b16_8 t816 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l77 = t816;
        void * t817 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l78 = t817;
        {
            void * t820 = gm_l78;
            uint8_t t821 = GM_LD(uint8_t, ((((uint8_t *)t820) + 8) + 0));
            if (t821 != ((uint8_t)25ULL)) goto ie819;
            void * t822 = GM_LD(void *, (((((uint8_t *)t820) + 8) + 8) + 8));
            gm_l79 = t822;
            void * t823 = gm_l79;
            uint8_t t824;
            uint8_t t827 = GM_LD(uint8_t, ((((uint8_t *)t823) + 8) + 0));
            if (t827 != ((uint8_t)30ULL)) goto mn826;
            t824 = ((uint8_t)1);
            goto me825;
mn826:;
            t824 = ((uint8_t)0);
            goto me825;
mn828:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 478, 49);
me825:;
            if (!t824) goto ie819;
            {
                void * t830 = gm_l0;
                uint64_t t831 = gm_l1;
                gm_b16_8 t832 = gm_l77;
                gm_b16_8 t833 = t832;
                gm_b16_8 t834 = GM_LD(gm_b16_8, ((uint8_t *)&t833));
                void * t835 = gm_l78;
                void * t836 = gm_l2;
                gm_tl874t2atnlh3 t837 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t836) + 72));
                gm_t88rcucg1b7uk t838 = gm_f_callconv_fn_ptr_l9kavc3((void *)NULL, cx, NULL, t830, t831, t834, t835, t837);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t839 = 0;
                F.ret = t838;
                goto gm_fret;
            }
            goto ix819;
ie819:;
ix819:;
        }
        {
            {
                gm_b16_8 t841 = gm_l77;
                gm_l80 = t841;
            }
            {
                gm_l81 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t843 = gm_l81;
                    uint64_t t844 = GM_LD(uintptr_t, ((uint8_t *)&gm_l80) + 8);
                    uint8_t t845 = (t843 < t844);
                    if (!t845) goto lb842;
                    {
                        uint64_t t848 = gm_l81;
                        uint64_t t849 = (uintptr_t)t848;
                        uint64_t t850 = GM_LD(uintptr_t, ((uint8_t *)&gm_l80) + 8);
                        gm_tkacitepg5fk0 t853 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l80)) + t849 * 72));
                        F.l82 = t853;
                        {
                            uint8_t t856 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l82) + 0) + 0) + 8) == 3) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l82) + 0) + 0)), gm_sqes10usq4q5e, 3) == 0));
                            if (!t856) goto ie855;
                            void * t857 = gm_l78;
                            uint8_t t858;
                            uint8_t t864 = GM_LD(uint8_t, ((((uint8_t *)t857) + 8) + 0));
                            switch (t864) { case ((uint8_t)0ULL): goto ma862; case ((uint8_t)1ULL): goto ma862; case ((uint8_t)2ULL): goto ma862; case ((uint8_t)3ULL): goto ma862; case ((uint8_t)4ULL): goto ma862; case ((uint8_t)5ULL): goto ma862; case ((uint8_t)6ULL): goto ma862; case ((uint8_t)7ULL): goto ma862; case ((uint8_t)8ULL): goto ma862; case ((uint8_t)9ULL): goto ma862; case ((uint8_t)10ULL): goto ma862; case ((uint8_t)11ULL): goto ma862; case ((uint8_t)12ULL): goto ma862; case ((uint8_t)13ULL): goto ma862; case ((uint8_t)14ULL): goto ma862; case ((uint8_t)15ULL): goto ma862; case ((uint8_t)16ULL): goto ma862; case ((uint8_t)17ULL): goto ma862; case ((uint8_t)18ULL): goto ma862; case ((uint8_t)19ULL): goto ma862; case ((uint8_t)20ULL): goto ma862; case ((uint8_t)21ULL): goto ma862; case ((uint8_t)22ULL): goto ma862; case ((uint8_t)23ULL): goto ma862; case ((uint8_t)24ULL): goto ma862; case ((uint8_t)25ULL): goto ma862; case ((uint8_t)26ULL): goto ma861; case ((uint8_t)27ULL): goto ma862; case ((uint8_t)28ULL): goto ma860; case ((uint8_t)29ULL): goto ma862; case ((uint8_t)30ULL): goto ma862; case ((uint8_t)31ULL): goto ma862; case ((uint8_t)32ULL): goto ma862; case ((uint8_t)33ULL): goto ma862; case ((uint8_t)34ULL): goto ma862; case ((uint8_t)35ULL): goto ma862; case ((uint8_t)36ULL): goto ma862; case ((uint8_t)37ULL): goto ma862; case ((uint8_t)38ULL): goto ma862; case ((uint8_t)39ULL): goto ma862; case ((uint8_t)40ULL): goto ma862; case ((uint8_t)41ULL): goto ma862; case ((uint8_t)42ULL): goto ma862; case ((uint8_t)43ULL): goto ma862; case ((uint8_t)44ULL): goto ma862; case ((uint8_t)45ULL): goto ma862; case ((uint8_t)46ULL): goto ma862; case ((uint8_t)47ULL): goto ma862; case ((uint8_t)48ULL): goto ma862; case ((uint8_t)49ULL): goto ma862; case ((uint8_t)50ULL): goto ma862; default: goto ma862; }
ma860:;
                            uint8_t t866 = GM_LD(uint8_t, ((((uint8_t *)t857) + 8) + 0));
                            if (t866 != ((uint8_t)28ULL)) goto mn865;
                            t858 = ((uint8_t)1);
                            goto me859;
mn865:;
ma861:;
                            uint8_t t868 = GM_LD(uint8_t, ((((uint8_t *)t857) + 8) + 0));
                            if (t868 != ((uint8_t)26ULL)) goto mn867;
                            t858 = ((uint8_t)1);
                            goto me859;
mn867:;
ma862:;
                            t858 = ((uint8_t)0);
                            goto me859;
mn869:;
ma863:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 482, 45);
me859:;
                            if (!t858) goto ie855;
                            {
                                uint8_t t872 = 0;
                                uint64_t t873 = GM_LD(uintptr_t, ((uint8_t *)&gm_l77) + 8);
                                uint8_t t874 = (t873 > ((uint64_t)1ULL));
                                if (!t874) {
                                    uint64_t t875 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t876 = GM_LD(uintptr_t, ((uint8_t *)&gm_l77) + 8);
                                    if (t875 >= (uintptr_t)(t876)) gm_trap_index(t875, t876, gm_srcsffver18si54, 483, 41);
                                    uint8_t t877;
                                    if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l77)) + t875 * 72) + 32))[16] != 0)) goto mn879;
                                    t877 = ((uint8_t)0);
                                    goto me878;
mn879:;
                                    t877 = ((uint8_t)1);
                                    goto me878;
mn880:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 483, 41);
me878:;
                                    t872 = t877;
                                } else t872 = 1;
                                if (!t872) goto ie871;
                                {
                                    void * t882 = gm_l0;
                                    void * t883 = gm_l2;
                                    gm_tl874t2atnlh3 t884 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t883) + 72));
                                    gm_b16_8 t885;
                                    GM_ST(void *, (uint8_t *)&t885, (void *)(gm_sgrpmaenv85i1));
                                    GM_ST(uintptr_t, (uint8_t *)&t885 + 8, (uintptr_t)(72));
                                    gm_b0_1 t886 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t882, t884, t885);
                                    if (gm_unw.kind) {
                                        goto gm_ll846;
                                    }
                                }
                                {
                                    void * t887 = gm_l3;
                                    gm_t88rcucg1b7uk t888 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t887);
                                    if (gm_unw.kind) {
                                        goto gm_ll846;
                                    }
                                    uint8_t t889 = 0;
                                    F.ret = t888;
                                    goto gm_fret;
                                }
                                goto ix871;
ie871:;
ix871:;
                            }
                            {
                                void * t890 = gm_l0;
                                uint64_t t891 = gm_l1;
                                void * t892 = gm_l78;
                                void * t893 = gm_l2;
                                gm_tl874t2atnlh3 t894 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t893) + 72));
                                gm_t88rcucg1b7uk t895 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t890, t891, t892, t894);
                                if (gm_unw.kind) {
                                    goto gm_ll846;
                                }
                                uint8_t t896 = 0;
                                F.ret = t895;
                                goto gm_fret;
                            }
                            goto ix855;
ie855:;
ix855:;
                        }
                        goto me851;
mn852:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 481, 13);
me851:;
                    }
lc842:;
                    {
                        uint64_t t898 = gm_l81;
                        uint64_t r899; bool o900 = __builtin_add_overflow(t898, ((uint64_t)1ULL), &r899);
                        uint64_t t901 = r899;
                        gm_l81 = t901;
                    }
                }
gm_ll846:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb842; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc842; }
                }
                goto gm_lad;
lb842:;
            }
        }
        {
            void * t903 = gm_l78;
            uint8_t t904;
            uint8_t t907 = GM_LD(uint8_t, ((((uint8_t *)t903) + 8) + 0));
            if (t907 != ((uint8_t)28ULL)) goto mn906;
            t904 = ((uint8_t)1);
            goto me905;
mn906:;
            t904 = ((uint8_t)0);
            goto me905;
mn908:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 490, 16);
me905:;
            if (!t904) goto ie902;
            {
                void * t910 = gm_l0;
                uint64_t t911 = gm_l1;
                gm_b16_8 t912 = gm_l77;
                gm_b16_8 t913 = t912;
                gm_b16_8 t914 = GM_LD(gm_b16_8, ((uint8_t *)&t913));
                void * t915 = gm_l78;
                void * t916 = gm_l2;
                gm_tl874t2atnlh3 t917 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t916) + 72));
                gm_t88rcucg1b7uk t918 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t910, t911, t914, t915, t917);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t919 = 0;
                F.ret = t918;
                goto gm_fret;
            }
            goto ix902;
ie902:;
ix902:;
        }
        {
            void * t921 = gm_l78;
            uint8_t t922;
            uint8_t t928 = GM_LD(uint8_t, ((((uint8_t *)t921) + 8) + 0));
            switch (t928) { case ((uint8_t)0ULL): goto ma926; case ((uint8_t)1ULL): goto ma926; case ((uint8_t)2ULL): goto ma926; case ((uint8_t)3ULL): goto ma926; case ((uint8_t)4ULL): goto ma926; case ((uint8_t)5ULL): goto ma926; case ((uint8_t)6ULL): goto ma926; case ((uint8_t)7ULL): goto ma926; case ((uint8_t)8ULL): goto ma926; case ((uint8_t)9ULL): goto ma926; case ((uint8_t)10ULL): goto ma926; case ((uint8_t)11ULL): goto ma926; case ((uint8_t)12ULL): goto ma926; case ((uint8_t)13ULL): goto ma926; case ((uint8_t)14ULL): goto ma926; case ((uint8_t)15ULL): goto ma926; case ((uint8_t)16ULL): goto ma926; case ((uint8_t)17ULL): goto ma926; case ((uint8_t)18ULL): goto ma926; case ((uint8_t)19ULL): goto ma926; case ((uint8_t)20ULL): goto ma926; case ((uint8_t)21ULL): goto ma926; case ((uint8_t)22ULL): goto ma926; case ((uint8_t)23ULL): goto ma926; case ((uint8_t)24ULL): goto ma926; case ((uint8_t)25ULL): goto ma926; case ((uint8_t)26ULL): goto ma926; case ((uint8_t)27ULL): goto ma926; case ((uint8_t)28ULL): goto ma926; case ((uint8_t)29ULL): goto ma926; case ((uint8_t)30ULL): goto ma926; case ((uint8_t)31ULL): goto ma926; case ((uint8_t)32ULL): goto ma924; case ((uint8_t)33ULL): goto ma925; case ((uint8_t)34ULL): goto ma926; case ((uint8_t)35ULL): goto ma926; case ((uint8_t)36ULL): goto ma926; case ((uint8_t)37ULL): goto ma926; case ((uint8_t)38ULL): goto ma926; case ((uint8_t)39ULL): goto ma926; case ((uint8_t)40ULL): goto ma926; case ((uint8_t)41ULL): goto ma926; case ((uint8_t)42ULL): goto ma926; case ((uint8_t)43ULL): goto ma926; case ((uint8_t)44ULL): goto ma926; case ((uint8_t)45ULL): goto ma926; case ((uint8_t)46ULL): goto ma926; case ((uint8_t)47ULL): goto ma926; case ((uint8_t)48ULL): goto ma926; case ((uint8_t)49ULL): goto ma926; case ((uint8_t)50ULL): goto ma926; default: goto ma926; }
ma924:;
            uint8_t t930 = GM_LD(uint8_t, ((((uint8_t *)t921) + 8) + 0));
            if (t930 != ((uint8_t)32ULL)) goto mn929;
            t922 = ((uint8_t)0);
            goto me923;
mn929:;
ma925:;
            uint8_t t932 = GM_LD(uint8_t, ((((uint8_t *)t921) + 8) + 0));
            if (t932 != ((uint8_t)33ULL)) goto mn931;
            t922 = ((uint8_t)0);
            goto me923;
mn931:;
ma926:;
            t922 = ((uint8_t)1);
            goto me923;
mn933:;
ma927:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 491, 16);
me923:;
            if (!t922) goto ie920;
            {
                void * t935 = gm_l0;
                void * t936 = gm_l2;
                gm_tl874t2atnlh3 t937 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t936) + 72));
                gm_b16_8 t938;
                GM_ST(void *, (uint8_t *)&t938, (void *)(gm_ssj1a8k9nspq4));
                GM_ST(uintptr_t, (uint8_t *)&t938 + 8, (uintptr_t)(81));
                gm_b0_1 t939 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t935, t937, t938);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t940 = gm_l3;
                gm_t88rcucg1b7uk t941 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t940);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t942 = 0;
                F.ret = t941;
                goto gm_fret;
            }
            goto ix920;
ie920:;
ix920:;
        }
        {
            gm_b16_8 t943;
            void * t945 = gm_l78;
            uint8_t t946;
            uint8_t t949 = GM_LD(uint8_t, ((((uint8_t *)t945) + 8) + 0));
            if (t949 != ((uint8_t)33ULL)) goto mn948;
            t946 = ((uint8_t)1);
            goto me947;
mn948:;
            t946 = ((uint8_t)0);
            goto me947;
mn950:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 495, 24);
me947:;
            if (!t946) goto ie944;
            gm_b16_8 t952;
            GM_ST(void *, (uint8_t *)&t952, (void *)(gm_s8p1h96ji9kh0));
            GM_ST(uintptr_t, (uint8_t *)&t952 + 8, (uintptr_t)(5));
            gm_b16_8 t953 = t952;
            t943 = t953;
            goto ix944;
ie944:;
            gm_b16_8 t955;
            GM_ST(void *, (uint8_t *)&t955, (void *)(gm_sc49g68j2cekh));
            GM_ST(uintptr_t, (uint8_t *)&t955 + 8, (uintptr_t)(6));
            gm_b16_8 t956 = t955;
            t943 = t956;
ix944:;
            gm_l83 = t943;
        }
        {
            void * t957 = gm_l0;
            uint64_t t958 = gm_l1;
            gm_tf5srh4mp4b3i t959;
            memset(&t959, 0, sizeof t959);
            gm_b16_8 t960 = gm_l83;
            void * t961 = gm_l2;
            gm_t88rcucg1b7uk t962 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t957, t958, t959, t960, t961);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t963 = 0;
            F.ret = t962;
            goto gm_fret;
        }
        goto me5;
mn814:;
ma19:;
        uint8_t t965 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t965 != ((uint8_t)32ULL)) goto mn964;
        void * t966 = gm_l0;
        uint64_t t967 = gm_l1;
        gm_tf5srh4mp4b3i t968;
        memset(&t968, 0, sizeof t968);
        gm_b16_8 t969;
        GM_ST(void *, (uint8_t *)&t969, (void *)(gm_sc49g68j2cekh));
        GM_ST(uintptr_t, (uint8_t *)&t969 + 8, (uintptr_t)(6));
        void * t970 = gm_l2;
        gm_t88rcucg1b7uk t971 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t966, t967, t968, t969, t970);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t972 = 0;
        F.ret = t971;
        goto gm_fret;
        goto me5;
mn964:;
ma20:;
        uint8_t t974 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t974 != ((uint8_t)33ULL)) goto mn973;
        void * t975 = gm_l0;
        uint64_t t976 = gm_l1;
        gm_tf5srh4mp4b3i t977;
        memset(&t977, 0, sizeof t977);
        gm_b16_8 t978;
        GM_ST(void *, (uint8_t *)&t978, (void *)(gm_s8p1h96ji9kh0));
        GM_ST(uintptr_t, (uint8_t *)&t978 + 8, (uintptr_t)(5));
        void * t979 = gm_l2;
        gm_t88rcucg1b7uk t980 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t975, t976, t977, t978, t979);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t981 = 0;
        F.ret = t980;
        goto gm_fret;
        goto me5;
mn973:;
ma21:;
        uint8_t t983 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t983 != ((uint8_t)49ULL)) goto mn982;
        gm_tet9guhev5r4u t984 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t4) + 8) + 8));
        F.l84 = t984;
        {
            uint8_t t987 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l84) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l84) + 0) + 0)), gm_slbgk65sat4bm, 7) == 0));
            if (!t987) goto ie986;
            {
                if (!(((((uint8_t *)&F.l84) + 32))[16] != 0)) goto ie989;
                gm_b16_8 t990 = GM_LD(gm_b16_8, (((uint8_t *)&F.l84) + 32));
                gm_l85 = t990;
                uint64_t t991 = GM_LD(uintptr_t, ((uint8_t *)&gm_l85) + 8);
                uint8_t t992 = ((t991 == ((uint64_t)1ULL)));
                if (!t992) goto ie989;
                uint64_t t993 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t994 = GM_LD(uintptr_t, ((uint8_t *)&gm_l85) + 8);
                if (t993 >= (uintptr_t)(t994)) gm_trap_index(t993, t994, gm_srcsffver18si54, 502, 75);
                uint8_t t995 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l85)) + t993 * 144) + 0) + 0));
                if (t995 != ((uint8_t)0ULL)) goto ie989;
                gm_tc3oht4ksth9d t996 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l85)) + t993 * 144) + 0) + 8));
                F.l86 = t996;
                {
                    void * t998 = gm_l0;
                    void * t999 = GM_LD(void *, (((uint8_t *)t998) + 2184));
                    void * t1000 = t999;
                    void * t1001;
                    if ((GM_LD(void *, ((uint8_t *)&t1000)) != 0)) {
                        void * t1002 = GM_LD(void *, (((uint8_t *)&t1000) + 0));
                        t1001 = t1002;
                    } else {
                        {
                            void * t1004 = gm_l0;
                            void * t1005 = gm_l2;
                            gm_tl874t2atnlh3 t1006 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1005) + 72));
                            gm_b16_8 t1007;
                            GM_ST(void *, (uint8_t *)&t1007, (void *)(gm_smi7k9v6u7cmo));
                            GM_ST(uintptr_t, (uint8_t *)&t1007 + 8, (uintptr_t)(10));
                            gm_b0_1 t1008 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t1004, t1006, t1007);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t1009 = gm_l3;
                        gm_t88rcucg1b7uk t1010 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1009);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t1011 = 0;
                        F.ret = t1010;
                        goto gm_fret;
                    }
                    gm_l87 = t1001;
                }
                {
                    void * t1012 = gm_l87;
                    void * t1013 = gm_l0;
                    uint64_t t1014 = gm_l1;
                    void * t1015 = (void *)((uint8_t *)&F.l86);
                    void * t1016 = t1012;
                    gm_t88rcucg1b7uk t1017;
                    if (GM_PTR_TAGGED(t1016)) {
                        t1017 = GM_CONV(gm_t88rcucg1b7uk, ((gm_csguankko4lmhd (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t1016))(t1013, t1014, t1015));
                    } else {
                        t1017 = ((gm_t88rcucg1b7uk (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t1016)(NULL, cx, NULL, t1013, t1014, t1015);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1018 = 0;
                    F.ret = t1017;
                    goto gm_fret;
                }
                goto ix989;
ie989:;
ix989:;
            }
            {
                void * t1019 = gm_l0;
                void * t1020 = gm_l2;
                gm_tl874t2atnlh3 t1021 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1020) + 72));
                gm_b16_8 t1022;
                GM_ST(void *, (uint8_t *)&t1022, (void *)(gm_sfpopfqbubj89));
                GM_ST(uintptr_t, (uint8_t *)&t1022 + 8, (uintptr_t)(34));
                gm_b0_1 t1023 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t1019, t1021, t1022);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t1024 = gm_l3;
                gm_t88rcucg1b7uk t1025 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1024);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t1026 = 0;
                F.ret = t1025;
                goto gm_fret;
            }
            goto ix986;
ie986:;
ix986:;
        }
        goto me5;
mn982:;
ma22:;
        uint8_t t1028 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t1028 != ((uint8_t)22ULL)) goto mn1027;
        void * t1029 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l88 = t1029;
        gm_t08olq97rscn3 t1030 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l89 = t1030;
        {
            void * t1033 = gm_l0;
            uint64_t t1034 = gm_l1;
            void * t1035 = gm_l88;
            gm_tf5srh4mp4b3i t1036 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t1033, t1034, t1035);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t1037 = t1036;
            if (!((((uint8_t *)&t1037))[8] != 0)) goto ie1032;
            uint64_t t1038 = GM_LD(uint64_t, ((uint8_t *)&t1037));
            gm_l90 = t1038;
            {
                void * t1040 = gm_l0;
                uint64_t t1041 = gm_l90;
                uint64_t t1042 = gm_l1;
                gm_tf5srh4mp4b3i t1043 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t1040, t1041, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l89)), t1042);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t1044 = t1043;
                uint64_t t1045;
                if (((((uint8_t *)&t1044))[8] != 0)) {
                    uint64_t t1046 = GM_LD(uint64_t, (((uint8_t *)&t1044) + 0));
                    t1045 = t1046;
                } else {
                    void * t1047 = gm_l3;
                    gm_t88rcucg1b7uk t1048 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1047);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1049 = 0;
                    F.ret = t1048;
                    goto gm_fret;
                }
                gm_l91 = t1045;
            }
            {
                void * t1050 = gm_l0;
                uint64_t t1051 = gm_l91;
                gm_t6qooom8qa2do t1052;
                gm_f_resolve_decl_pus042v((void *)&t1052, (void *)NULL, cx, NULL, t1050, t1051);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t1053 = t1052;
                uint8_t t1059 = GM_LD(uint8_t, (((uint8_t *)&t1053) + 0));
                switch (t1059) { case ((uint8_t)0ULL): goto ma1055; case ((uint8_t)1ULL): goto ma1057; case ((uint8_t)2ULL): goto ma1057; case ((uint8_t)3ULL): goto ma1057; case ((uint8_t)4ULL): goto ma1057; case ((uint8_t)5ULL): goto ma1057; case ((uint8_t)6ULL): goto ma1057; case ((uint8_t)7ULL): goto ma1057; case ((uint8_t)8ULL): goto ma1057; case ((uint8_t)9ULL): goto ma1057; case ((uint8_t)10ULL): goto ma1057; case ((uint8_t)11ULL): goto ma1056; default: goto ma1057; }
ma1055:;
                uint8_t t1061 = GM_LD(uint8_t, (((uint8_t *)&t1053) + 0));
                if (t1061 != ((uint8_t)0ULL)) goto mn1060;
                gm_t88rcucg1b7uk t1062 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t1053) + 8));
                gm_l92 = t1062;
                gm_t88rcucg1b7uk t1063 = gm_l92;
                uint8_t t1064 = 0;
                F.ret = t1063;
                goto gm_fret;
                goto me1054;
mn1060:;
ma1056:;
                uint8_t t1066 = GM_LD(uint8_t, (((uint8_t *)&t1053) + 0));
                if (t1066 != ((uint8_t)11ULL)) goto mn1065;
                void * t1067 = gm_l3;
                gm_t88rcucg1b7uk t1068 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1067);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t1069 = 0;
                F.ret = t1068;
                goto gm_fret;
                goto me1054;
mn1065:;
ma1057:;
                {
                    void * t1072 = gm_l0;
                    void * t1073 = gm_l2;
                    gm_tl874t2atnlh3 t1074 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1073) + 72));
                    gm_b16_8 t1075 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 0));
                    gm_tnpifv20irnph t1076;
                    GM_ST(gm_b16_8, ((uint8_t *)&t1076 + 0), t1075);
                    gm_tabb710d9mq2e t1077;
                    gm_f_format_sv91363((void *)&t1077, (void *)NULL, cx, NULL, t1076);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t1078 = t1077;
                    if (((((uint8_t *)&t1078))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1078) + 8, gm_srcsffver18si54, 521, 41);
                    gm_b16_8 t1079 = GM_LD(gm_b16_8, (((uint8_t *)&t1078) + 8));
                    gm_b16_8 t1080 = t1079;
                    gm_b16_8 t1081 = GM_LD(gm_b16_8, ((uint8_t *)&t1080));
                    gm_b0_1 t1082 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t1072, t1074, t1081);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t1083 = gm_l3;
                    gm_t88rcucg1b7uk t1084 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1083);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1085 = 0;
                    F.ret = t1084;
                    goto gm_fret;
                }
                goto me1054;
mn1070:;
ma1058:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 517, 17);
me1054:;
            }
            goto ix1032;
ie1032:;
ix1032:;
        }
        {
            void * t1087 = gm_l0;
            uint64_t t1088 = gm_l1;
            void * t1089 = gm_l88;
            uint8_t t1090 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t1087, t1088, t1089);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t1090) goto ie1086;
            {
                void * t1092 = gm_l3;
                gm_t88rcucg1b7uk t1093 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1092);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t1094 = 0;
                F.ret = t1093;
                goto gm_fret;
            }
            goto ix1086;
ie1086:;
ix1086:;
        }
        goto me5;
mn1027:;
ma23:;
        goto me5;
mn1095:;
ma24:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 332, 5);
me5:;
    }
    {
        void * t1097 = gm_l0;
        void * t1098 = gm_l2;
        gm_tl874t2atnlh3 t1099 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1098) + 72));
        gm_b16_8 t1100;
        GM_ST(void *, (uint8_t *)&t1100, (void *)(gm_s2328gjukbu28));
        GM_ST(uintptr_t, (uint8_t *)&t1100 + 8, (uintptr_t)(15));
        gm_b0_1 t1101 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t1097, t1099, t1100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t1102 = gm_l3;
        gm_t88rcucg1b7uk t1103 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t1104 = 0;
        F.ret = t1103;
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
extern __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1) { gm_f_restore_scope_edqq1v8(a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0) { gm_t66j1im9ip128 gm_r; gm_f_scope_mark_i3b2mvo(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_is_type_pe6ugv2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* inner */
    void * gm_l7; /* inner */
    void * gm_l8; /* callee */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint64_t gm_l10; /* d */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    uint64_t gm_l13; /* m */
    uint64_t gm_l14; /* d */
    GM_STACK_CHECK(&F, 899u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)6ULL)) goto mn14;
        gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t16;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t22 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t23 = t22;
            if (!((((uint8_t *)&t23))[8] != 0)) goto ie18;
            uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&t23));
            gm_l5 = t24;
            {
                void * t26 = gm_l0;
                uint64_t t27 = gm_l5;
                uint32_t t28 = gm_l3;
                uint32_t r29; bool o30 = __builtin_add_overflow(t28, ((uint32_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 155, 86);
                uint32_t t31 = r29;
                uint8_t t32 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t26, t27, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t35 = gm_f_is_builtin_type_name_v5ttoka(t34);
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto me3;
mn14:;
ma5:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t38 != ((uint8_t)9ULL)) goto mn37;
        void * t39 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t39;
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        void * t42 = gm_l6;
        uint32_t t43 = gm_l3;
        uint8_t t44 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t40, t41, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t45 = 0;
        F.ret = t44;
        goto gm_fret;
        goto me3;
mn37:;
ma6:;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t47 != ((uint8_t)35ULL)) goto mn46;
        void * t48 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l7 = t48;
        void * t49 = gm_l0;
        uint64_t t50 = gm_l1;
        void * t51 = gm_l7;
        uint32_t t52 = gm_l3;
        uint8_t t53 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t49, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t54 = 0;
        F.ret = t53;
        goto gm_fret;
        goto me3;
mn46:;
ma7:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)32ULL)) goto pa57;
        goto po56;
pa57:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)33ULL)) goto pa59;
        goto po56;
pa59:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t62 != ((uint8_t)25ULL)) goto pa61;
        goto po56;
pa61:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t64 != ((uint8_t)26ULL)) goto pa63;
        goto po56;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t66 != ((uint8_t)27ULL)) goto pa65;
        goto po56;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t68 != ((uint8_t)29ULL)) goto pa67;
        goto po56;
pa67:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t70 != ((uint8_t)30ULL)) goto pa69;
        goto po56;
pa69:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t72 != ((uint8_t)31ULL)) goto pa71;
        goto po56;
pa71:;
        goto mn55;
po56:;
        uint8_t t73 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn55:;
ma8:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t77 != ((uint8_t)24ULL)) goto pa76;
        goto po75;
pa76:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t79 != ((uint8_t)28ULL)) goto pa78;
        goto po75;
pa78:;
        goto mn74;
po75:;
        void * t80 = gm_l2;
        uint8_t t81 = gm_f_hoists_13k2a2s(t80);
        uint8_t t82 = (uint8_t)!t81;
        uint8_t t83 = 0;
        F.ret = t82;
        goto gm_fret;
        goto me3;
mn74:;
ma9:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t85 != ((uint8_t)19ULL)) goto mn84;
        void * t86 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t86;
        void * t87 = gm_l8;
        uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t87) + 8) + 0));
        switch (t93) { case ((uint8_t)0ULL): goto ma91; case ((uint8_t)1ULL): goto ma91; case ((uint8_t)2ULL): goto ma91; case ((uint8_t)3ULL): goto ma91; case ((uint8_t)4ULL): goto ma91; case ((uint8_t)5ULL): goto ma91; case ((uint8_t)6ULL): goto ma89; case ((uint8_t)7ULL): goto ma91; case ((uint8_t)8ULL): goto ma91; case ((uint8_t)9ULL): goto ma91; case ((uint8_t)10ULL): goto ma91; case ((uint8_t)11ULL): goto ma91; case ((uint8_t)12ULL): goto ma91; case ((uint8_t)13ULL): goto ma91; case ((uint8_t)14ULL): goto ma91; case ((uint8_t)15ULL): goto ma91; case ((uint8_t)16ULL): goto ma91; case ((uint8_t)17ULL): goto ma91; case ((uint8_t)18ULL): goto ma91; case ((uint8_t)19ULL): goto ma91; case ((uint8_t)20ULL): goto ma91; case ((uint8_t)21ULL): goto ma91; case ((uint8_t)22ULL): goto ma90; case ((uint8_t)23ULL): goto ma91; case ((uint8_t)24ULL): goto ma91; case ((uint8_t)25ULL): goto ma91; case ((uint8_t)26ULL): goto ma91; case ((uint8_t)27ULL): goto ma91; case ((uint8_t)28ULL): goto ma91; case ((uint8_t)29ULL): goto ma91; case ((uint8_t)30ULL): goto ma91; case ((uint8_t)31ULL): goto ma91; case ((uint8_t)32ULL): goto ma91; case ((uint8_t)33ULL): goto ma91; case ((uint8_t)34ULL): goto ma91; case ((uint8_t)35ULL): goto ma91; case ((uint8_t)36ULL): goto ma91; case ((uint8_t)37ULL): goto ma91; case ((uint8_t)38ULL): goto ma91; case ((uint8_t)39ULL): goto ma91; case ((uint8_t)40ULL): goto ma91; case ((uint8_t)41ULL): goto ma91; case ((uint8_t)42ULL): goto ma91; case ((uint8_t)43ULL): goto ma91; case ((uint8_t)44ULL): goto ma91; case ((uint8_t)45ULL): goto ma91; case ((uint8_t)46ULL): goto ma91; case ((uint8_t)47ULL): goto ma91; case ((uint8_t)48ULL): goto ma91; case ((uint8_t)49ULL): goto ma91; case ((uint8_t)50ULL): goto ma91; default: goto ma91; }
ma89:;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t87) + 8) + 0));
        if (t95 != ((uint8_t)6ULL)) goto mn94;
        gm_t08olq97rscn3 t96 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t87) + 8) + 8));
        gm_l9 = t96;
        {
            void * t98 = gm_l0;
            uint64_t t99 = gm_l1;
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
            gm_tf5srh4mp4b3i t101 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t102 = t101;
            uint64_t t103;
            if (((((uint8_t *)&t102))[8] != 0)) {
                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&t102) + 0));
                t103 = t104;
            } else {
                uint8_t t105 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l10 = t103;
        }
        {
            void * t106 = gm_l0;
            uint64_t t107 = gm_l10;
            uint32_t t108 = gm_l3;
            uint32_t r109; bool o110 = __builtin_add_overflow(t108, ((uint32_t)1ULL), &r109);
            if (o110) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 165, 42);
            uint32_t t111 = r109;
            uint8_t t112 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t106, t107, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t113 = 0;
            F.ret = t112;
            goto gm_fret;
        }
        goto me88;
mn94:;
ma90:;
        uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t87) + 8) + 0));
        if (t115 != ((uint8_t)22ULL)) goto mn114;
        void * t116 = gm_l0;
        uint64_t t117 = gm_l1;
        void * t118 = gm_l8;
        uint32_t t119 = gm_l3;
        uint8_t t120 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t116, t117, t118, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t121 = 0;
        F.ret = t120;
        goto gm_fret;
        goto me88;
mn114:;
ma91:;
        uint8_t t123 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me88;
mn122:;
ma92:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 162, 32);
me88:;
        goto me3;
mn84:;
ma10:;
        uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t125 != ((uint8_t)22ULL)) goto mn124;
        void * t126 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l11 = t126;
        gm_t08olq97rscn3 t127 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l12 = t127;
        {
            void * t129 = gm_l0;
            uint64_t t130 = gm_l1;
            void * t131 = gm_l11;
            uint32_t t132 = gm_l3;
            gm_tf5srh4mp4b3i t133 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t129, t130, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t134 = t133;
            uint64_t t135;
            if (((((uint8_t *)&t134))[8] != 0)) {
                uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&t134) + 0));
                t135 = t136;
            } else {
                uint8_t t137 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l13 = t135;
        }
        {
            void * t138 = gm_l0;
            void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
            uint64_t t140 = gm_l13;
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
            gm_tf5srh4mp4b3i t142 = gm_f_lookup_59juqoo(t139, t140, t141);
            gm_tf5srh4mp4b3i t143 = t142;
            uint64_t t144;
            if (((((uint8_t *)&t143))[8] != 0)) {
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&t143) + 0));
                t144 = t145;
            } else {
                uint8_t t146 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l14 = t144;
        }
        {
            void * t148 = gm_l0;
            uint64_t t149 = gm_l14;
            uint64_t t150 = gm_l1;
            uint8_t t151 = gm_f_decl_visible_bd168ti(t148, t149, t150);
            uint8_t t152 = (uint8_t)!t151;
            if (!t152) goto ie147;
            {
                uint8_t t154 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix147;
ie147:;
ix147:;
        }
        {
            void * t155 = gm_l0;
            uint64_t t156 = gm_l14;
            uint32_t t157 = gm_l3;
            uint32_t r158; bool o159 = __builtin_add_overflow(t157, ((uint32_t)1ULL), &r158);
            if (o159) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 175, 38);
            uint32_t t160 = r158;
            uint8_t t161 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t155, t156, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t162 = 0;
            F.ret = t161;
            goto gm_fret;
        }
        goto me3;
mn124:;
ma11:;
        uint8_t t164 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn163:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 153, 5);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_module_vho1001 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    uint64_t gm_l4; /* d */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l10; /* m */
    void * gm_l11; /* decl */
    uint64_t gm_l12; /* dm */
    uint64_t gm_l14; /* m */
    GM_STACK_CHECK(&F, 2111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t3 = gm_l3;
        uint8_t t4 = (t3 > ((uint32_t)16ULL));
        if (!t4) goto ie2;
        {
            gm_tf5srh4mp4b3i t6;
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
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t8 = gm_l2;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; default: goto ma12; }
ma10:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t16 != ((uint8_t)6ULL)) goto mn15;
        gm_t08olq97rscn3 t17 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t8) + 8) + 8));
        gm_l5 = t17;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tf5srh4mp4b3i t22 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t23 = t22;
            uint64_t t24;
            if (((((uint8_t *)&t23))[8] != 0)) {
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&t23) + 0));
                t24 = t25;
            } else {
                gm_tf5srh4mp4b3i t26;
                memset(&t26, 0, sizeof t26);
                uint8_t t27 = 0;
                F.ret = t26;
                goto gm_fret;
            }
            gm_l4 = t24;
        }
        goto me9;
mn15:;
ma11:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t29 != ((uint8_t)22ULL)) goto mn28;
        void * t30 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l6 = t30;
        gm_t08olq97rscn3 t31 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l7 = t31;
        {
            void * t33 = gm_l0;
            uint64_t t34 = gm_l1;
            void * t35 = gm_l6;
            uint32_t t36 = gm_l3;
            uint32_t r37; bool o38 = __builtin_add_overflow(t36, ((uint32_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 189, 48);
            uint32_t t39 = r37;
            gm_tf5srh4mp4b3i t40 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t33, t34, t35, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t41 = t40;
            uint64_t t42;
            if (((((uint8_t *)&t41))[8] != 0)) {
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&t41) + 0));
                t42 = t43;
            } else {
                gm_tf5srh4mp4b3i t44;
                memset(&t44, 0, sizeof t44);
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            gm_l8 = t42;
        }
        {
            void * t46 = gm_l0;
            void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
            uint64_t t48 = gm_l8;
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t50 = gm_f_lookup_59juqoo(t47, t48, t49);
            gm_tf5srh4mp4b3i t51 = t50;
            uint64_t t52;
            if (((((uint8_t *)&t51))[8] != 0)) {
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t51) + 0));
                t52 = t53;
            } else {
                gm_tf5srh4mp4b3i t54;
                memset(&t54, 0, sizeof t54);
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            gm_l4 = t52;
        }
        {
            void * t57 = gm_l0;
            uint64_t t58 = gm_l4;
            uint64_t t59 = gm_l1;
            uint8_t t60 = gm_f_decl_visible_bd168ti(t57, t58, t59);
            uint8_t t61 = (uint8_t)!t60;
            if (!t61) goto ie56;
            {
                gm_tf5srh4mp4b3i t63;
                memset(&t63, 0, sizeof t63);
                uint8_t t64 = 0;
                F.ret = t63;
                goto gm_fret;
            }
            goto ix56;
ie56:;
ix56:;
        }
        goto me9;
mn28:;
ma12:;
        gm_tf5srh4mp4b3i t66;
        memset(&t66, 0, sizeof t66);
        uint8_t t67 = 0;
        F.ret = t66;
        goto gm_fret;
        goto me9;
mn65:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 186, 5);
me9:;
    }
    {
        void * t68 = gm_l0;
        uint64_t t69 = gm_l4;
        gm_tagrkbqh054h7 t70;
        gm_f_state_a5bnsjb((void *)&t70, (void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tagrkbqh054h7 t71 = t70;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t71) + 0));
        if (t74 != ((uint8_t)2ULL)) goto mn73;
        gm_t6qooom8qa2do t75 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&t71) + 8));
        F.l9 = t75;
        {
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            if (t78 != ((uint8_t)9ULL)) goto ie77;
            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
            gm_l10 = t79;
            {
                uint64_t t81 = gm_l10;
                gm_tf5srh4mp4b3i t82;
                GM_ST(uint8_t, (uint8_t *)&t82 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t82 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t82), t81);
                ((uint8_t *)&t82)[8] = 1;
                uint8_t t83 = 0;
                F.ret = t82;
                goto gm_fret;
            }
            goto ix77;
ie77:;
            {
                gm_tf5srh4mp4b3i t85;
                memset(&t85, 0, sizeof t85);
                uint8_t t86 = 0;
                F.ret = t85;
                goto gm_fret;
            }
ix77:;
        }
        goto me72;
mn73:;
        goto me72;
mn87:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 195, 5);
me72:;
    }
    {
        uint64_t t89 = gm_l4;
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = gm_l0;
        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
        uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 240) + 0) + 8);
        if (t90 >= (uintptr_t)(t93)) gm_trap_index(t90, t93, gm_srcsffver18si54, 202, 14);
        void * t94 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 240) + 0)) + t90 * 216);
        gm_l11 = t94;
    }
    {
        void * t95 = gm_l11;
        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)t95) + 32));
        gm_l12 = t96;
    }
    {
        void * t98 = gm_l11;
        uint8_t t99 = GM_LD(uint8_t, (((((uint8_t *)t98) + 40) + 64) + 0));
        if (t99 != ((uint8_t)0ULL)) goto ie97;
        gm_tv2uf4leil5hq t100 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t98) + 40) + 64) + 8));
        F.l13 = t100;
        {
            uint8_t t103 = 0;
            void * t104 = GM_LD(void *, (((uint8_t *)&F.l13) + 24));
            void * t105 = t104;
            uint8_t t106;
            if ((GM_LD(void *, ((uint8_t *)&t105)) != 0)) goto mn108;
            t106 = ((uint8_t)0);
            goto me107;
mn108:;
            t106 = ((uint8_t)1);
            goto me107;
mn109:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 205, 8);
me107:;
            if (!t106) {
                uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 8) + 8);
                uint8_t t111 = (!(t110 == ((uint64_t)1ULL)));
                t103 = t111;
            } else t103 = 1;
            if (!t103) goto ie102;
            {
                gm_tf5srh4mp4b3i t113;
                memset(&t113, 0, sizeof t113);
                uint8_t t114 = 0;
                F.ret = t113;
                goto gm_fret;
            }
            goto ix102;
ie102:;
ix102:;
        }
        {
            void * t115 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
            uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t115) + 8) + 0));
            switch (t121) { case ((uint8_t)0ULL): goto ma119; case ((uint8_t)1ULL): goto ma119; case ((uint8_t)2ULL): goto ma119; case ((uint8_t)3ULL): goto ma119; case ((uint8_t)4ULL): goto ma119; case ((uint8_t)5ULL): goto ma119; case ((uint8_t)6ULL): goto ma118; case ((uint8_t)7ULL): goto ma119; case ((uint8_t)8ULL): goto ma119; case ((uint8_t)9ULL): goto ma119; case ((uint8_t)10ULL): goto ma119; case ((uint8_t)11ULL): goto ma119; case ((uint8_t)12ULL): goto ma119; case ((uint8_t)13ULL): goto ma119; case ((uint8_t)14ULL): goto ma119; case ((uint8_t)15ULL): goto ma119; case ((uint8_t)16ULL): goto ma119; case ((uint8_t)17ULL): goto ma119; case ((uint8_t)18ULL): goto ma119; case ((uint8_t)19ULL): goto ma119; case ((uint8_t)20ULL): goto ma119; case ((uint8_t)21ULL): goto ma119; case ((uint8_t)22ULL): goto ma118; case ((uint8_t)23ULL): goto ma119; case ((uint8_t)24ULL): goto ma119; case ((uint8_t)25ULL): goto ma119; case ((uint8_t)26ULL): goto ma119; case ((uint8_t)27ULL): goto ma119; case ((uint8_t)28ULL): goto ma119; case ((uint8_t)29ULL): goto ma119; case ((uint8_t)30ULL): goto ma119; case ((uint8_t)31ULL): goto ma119; case ((uint8_t)32ULL): goto ma119; case ((uint8_t)33ULL): goto ma119; case ((uint8_t)34ULL): goto ma119; case ((uint8_t)35ULL): goto ma119; case ((uint8_t)36ULL): goto ma119; case ((uint8_t)37ULL): goto ma119; case ((uint8_t)38ULL): goto ma117; case ((uint8_t)39ULL): goto ma119; case ((uint8_t)40ULL): goto ma119; case ((uint8_t)41ULL): goto ma119; case ((uint8_t)42ULL): goto ma119; case ((uint8_t)43ULL): goto ma119; case ((uint8_t)44ULL): goto ma119; case ((uint8_t)45ULL): goto ma119; case ((uint8_t)46ULL): goto ma119; case ((uint8_t)47ULL): goto ma119; case ((uint8_t)48ULL): goto ma119; case ((uint8_t)49ULL): goto ma119; case ((uint8_t)50ULL): goto ma119; default: goto ma119; }
ma117:;
            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t115) + 8) + 0));
            if (t123 != ((uint8_t)38ULL)) goto mn122;
            {
                void * t126 = gm_l0;
                uint64_t t127 = gm_l4;
                gm_t6qooom8qa2do t128;
                gm_f_resolve_decl_pus042v((void *)&t128, (void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t129 = t128;
                uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t129) + 0));
                if (t130 != ((uint8_t)9ULL)) goto ie125;
                uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&t129) + 8));
                gm_l14 = t131;
                {
                    uint64_t t133 = gm_l14;
                    gm_tf5srh4mp4b3i t134;
                    GM_ST(uint8_t, (uint8_t *)&t134 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t134 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t134 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t134), t133);
                    ((uint8_t *)&t134)[8] = 1;
                    uint8_t t135 = 0;
                    F.ret = t134;
                    goto gm_fret;
                }
                goto ix125;
ie125:;
                {
                    gm_tf5srh4mp4b3i t137;
                    memset(&t137, 0, sizeof t137);
                    uint8_t t138 = 0;
                    F.ret = t137;
                    goto gm_fret;
                }
ix125:;
            }
            goto me116;
mn122:;
ma118:;
            uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t115) + 8) + 0));
            if (t142 != ((uint8_t)6ULL)) goto pa141;
            goto po140;
pa141:;
            uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t115) + 8) + 0));
            if (t144 != ((uint8_t)22ULL)) goto pa143;
            goto po140;
pa143:;
            goto mn139;
po140:;
            void * t145 = gm_l0;
            uint64_t t146 = gm_l12;
            void * t147 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
            uint32_t t148 = gm_l3;
            uint32_t r149; bool o150 = __builtin_add_overflow(t148, ((uint32_t)1ULL), &r149);
            if (o150) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 212, 68);
            uint32_t t151 = r149;
            gm_tf5srh4mp4b3i t152 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t145, t146, t147, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
            goto me116;
mn139:;
ma119:;
            gm_tf5srh4mp4b3i t155;
            memset(&t155, 0, sizeof t155);
            uint8_t t156 = 0;
            F.ret = t155;
            goto gm_fret;
            goto me116;
mn154:;
ma120:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 206, 5);
me116:;
        }
        goto ix97;
ie97:;
        {
            gm_tf5srh4mp4b3i t158;
            memset(&t158, 0, sizeof t158);
            uint8_t t159 = 0;
            F.ret = t158;
            goto gm_fret;
        }
ix97:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_param_3av4r8i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1488) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 55, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1488) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 56, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1488) + 0)) + t15 * 24) + 0));
                gm_b16_8 t19 = gm_l1;
                gm_b16_8 t20 = t18;
                gm_b16_8 t21 = t19;
                uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                if (!t22) goto ie13;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t t25 = (uintptr_t)t24;
                    void * t26 = gm_l0;
                    uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1488) + 0) + 8);
                    if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcsffver18si54, 56, 57);
                    gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1488) + 0)) + t25 * 24) + 16));
                    gm_tkn42u7na3t81 t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29), t28);
                    ((uint8_t *)&t29)[4] = 1;
                    uint8_t t30 = 0;
                    F.ret = t29;
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
        gm_tkn42u7na3t81 t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_param_tof1h6t F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1528) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 128, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1528) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 129, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1528) + 0)) + t15 * 24) + 0));
                gm_b16_8 t19 = gm_l1;
                gm_b16_8 t20 = t18;
                gm_b16_8 t21 = t19;
                uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                if (!t22) goto ie13;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t t25 = (uintptr_t)t24;
                    void * t26 = gm_l0;
                    uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1528) + 0) + 8);
                    if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcsffver18si54, 129, 57);
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1528) + 0)) + t25 * 24) + 16));
                    gm_t9805qtonbsag t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t29), t28);
                    ((uint8_t *)&t29)[4] = 1;
                    uint8_t t30 = 0;
                    F.ret = t29;
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
        gm_t9805qtonbsag t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_with_operand_ajbkgld F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* operand */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    uint8_t gm_l4; /* c */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* inner */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* inner */
    GM_STACK_CHECK(&F, 937u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t2gtbh39ptekd t3;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; default: goto ma10; }
ma5:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)28ULL)) goto mn13;
        gm_t3mf58gv9kf3n t15 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        void * t16 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t17 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t18 = gm_l1;
        void * t19 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t20;
        GM_ST(void *, ((uint8_t *)&t20 + 0), t16);
        GM_ST(void *, ((uint8_t *)&t20 + 8), t19);
        gm_t2gtbh39ptekd t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)28ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t21 + 8), t20);
        t3 = t21;
        goto me4;
mn13:;
ma6:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)24ULL)) goto mn22;
        void * t24 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t24;
        void * t25 = gm_l3;
        void * t26 = gm_l1;
        void * t27 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2gtbh39ptekd t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
        t3 = t28;
        goto me4;
mn22:;
ma7:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)25ULL)) goto mn29;
        uint8_t t31 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t31;
        void * t32 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t32;
        uint8_t t33 = gm_l4;
        void * t34 = gm_l5;
        void * t35 = gm_l1;
        void * t36 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), t33);
        GM_ST(void *, ((uint8_t *)&t37 + 8), t36);
        gm_t2gtbh39ptekd t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)25ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t38 + 8), t37);
        t3 = t38;
        goto me4;
mn29:;
ma8:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)26ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t41;
        void * t42 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t42;
        uint8_t t43 = gm_l6;
        void * t44 = gm_l7;
        void * t45 = gm_l1;
        void * t46 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), t43);
        GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
        gm_t2gtbh39ptekd t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)26ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t48 + 8), t47);
        t3 = t48;
        goto me4;
mn39:;
ma9:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t50 != ((uint8_t)27ULL)) goto mn49;
        uint8_t t51 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t51;
        void * t52 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t52;
        uint8_t t53 = gm_l8;
        void * t54 = gm_l9;
        void * t55 = gm_l1;
        void * t56 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), t53);
        GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
        gm_t2gtbh39ptekd t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t58 + 8), t57);
        t3 = t58;
        goto me4;
mn49:;
ma10:;
        void * t60 = gm_l1;
        gm_tq7s98ihonkju t61 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t60)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3n1bpabq7f)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t62 = t61;
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srcsffver18si54, 931, 21);
        void * t63 = GM_LD(void *, (((uint8_t *)&t62) + 8));
        uint8_t t64 = 0;
        F.ret = t63;
        goto gm_fret;
        goto me4;
mn59:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 925, 26);
me4:;
        F.l10 = t3;
    }
    {
        gm_tc3oht4ksth9d t65;
        memset((uint8_t *)&t65 + 4, 0, 4);
        memset((uint8_t *)&t65 + 84, 0, 4);
        void * t66 = gm_l0;
        uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)t66) + 0));
        GM_ST(uint32_t, (uint8_t *)&t65 + 0, t67);
        memcpy((uint8_t *)&t65 + 8, ((uint8_t *)&F.l10), 64);
        void * t68 = gm_l0;
        gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t65 + 72, t69);
        gm_tq7s98ihonkju t70 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t65, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6r9cr7441u)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t71 = t70;
        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcsffver18si54, 933, 12);
        void * t72 = GM_LD(void *, (((uint8_t *)&t71) + 8));
        uint8_t t73 = 0;
        F.ret = t72;
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
