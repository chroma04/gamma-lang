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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { gm_b16_8 f0; int32_t f1; uint8_t pe[4]; } gm_t0bf4lga76ktk;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_t0bf4lga76ktk) == 24 && _Alignof(gm_t0bf4lga76ktk) == 8 && offsetof(gm_t0bf4lga76ktk, f0) == 0 && offsetof(gm_t0bf4lga76ktk, f1) == 16, "layout of `(string, i32)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint32_t v1; } f1; } gm_tssfa0col9q01;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_tssfa0col9q01) == 24 && _Alignof(gm_tssfa0col9q01) == 8 && offsetof(gm_tssfa0col9q01, f0) == 0 && offsetof(gm_tssfa0col9q01, f1) == 8, "layout of `ParseError!u32`");
struct gm_fr_digits_br9h428 {
    gm_frame h;
    gm_tssfa0col9q01 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_057m2j5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_05go7ic {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_05jebu4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0af9jdh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0cu6em8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_103pjna {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_15lco53 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1et6tkr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1lb8oku {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1mlu301 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1n0hdi5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1s44o6q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_23j123c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2k5hcs1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_320ocof {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3sfh3cs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_48ghii1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4c88gjc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4dfj0mb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4jf0q0u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4q7fa3j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_507jnu8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_65m9k9g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_68un0e5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7bcuctm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7c6rrmg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7dm3ok7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7en1q5j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7o0ino2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_881vrts {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8fdq0j2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8qlbihr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8r57c5b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_90cv517 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_97k8lr5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9h0rhbh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9o505uv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aa36nmv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_atni201 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bfqp4iu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bgvaums {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bsjjs4q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cmpeusk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_djm7a7b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_elnenje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eq52hho {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eq52igv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fa6dgsl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fe8dlnp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fmt0nd3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gjsb0o3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gmjqp49 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gsrnq92 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gtdu4sv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i1mdkd1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i1s1tes {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i6f520e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ia3m98c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jh3ht86 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jjf260s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jv1pdfe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l1ol1gh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lc3l553 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lcl6k04 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mo08ias {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mqpg3i3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n3ad29p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_naiohrt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nmdqfj1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nqqt1q3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o6rmrv9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o78l9p5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_oqlb297 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pbpu4nu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pvvaqhh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q0ab64e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qi7m0ev {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sro7ivu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tbto4cb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_toihk94 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uqn7075 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ut99gho {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vcmvfau {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vhghpja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vnbkcp2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_b05vb48 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b2nifvt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b3e91bl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b46cbct {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b4oiuht {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b6aggpq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b6qg4rq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b946o7j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bbsjt91 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bc9gr0b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_be3fee1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_beqekat {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bi036vc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bia01cc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_biupho2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bjrmbam {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bkdctpk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bl27tn4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bl6khrb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_blcbfcc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_blrf705 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bm4rhr0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bm6js5o {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bmv30a9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bnsf58k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bofepup {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_booci4l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bovj9ap {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bpl33sh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bs5ht1e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bsjq7um {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bt4apif {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bul917c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bunqlnl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_buut5mk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bv13gdl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bvitrsu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bvot0a7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r0u4lpe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r1s7qdu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r31rqga {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r3el627 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r40dmo0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r4p6poi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r4smojn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r57fu0b {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r6aoskm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r6pm0oj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r8c6rs0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r8e1h45 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r8pfaos {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r9ba7g9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r9dvh2e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra2b58j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra7pont {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra91g4n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rb39nel {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rb3ud4d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rbr6trv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rd4d2vn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rd9dflp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_reauhlh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ree2v5u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rgk51pp {
    gm_frame h;
    gm_tmr5oep1578tb l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tmr5oep1578tb l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rh16oh6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rjqt2eo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rkrg0f2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rlkig7n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rlu0tid {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rmu2e6q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rn1vtpg {
    gm_frame h;
    gm_t7neromnu8t62 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t7neromnu8t62 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ro2n9te {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rp4le08 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rp57m7o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rpma0gd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rpqj2mb {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rqvn133 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rrko99s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rse2efa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rslrdtj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rtv6jsp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ru16275 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ru8569b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rvm8ibl {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rvu96av {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_digit_value_2b3md79(uint8_t a0);
__attribute__((aligned(4))) void gm_f_digits_br9h428(void *gm_out, gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3);
__attribute__((aligned(4))) gm_tssfa0col9q01 gm_f_digits_br9h428_c(gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_057m2j5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05go7ic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05jebu4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0af9jdh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0cu6em8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_103pjna(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_15lco53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1et6tkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lb8oku(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1mlu301(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1n0hdi5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1s44o6q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_23j123c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2k5hcs1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_320ocof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sfh3cs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_48ghii1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4c88gjc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4dfj0mb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4jf0q0u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4q7fa3j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_507jnu8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_65m9k9g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_68un0e5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7bcuctm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7c6rrmg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7dm3ok7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7en1q5j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7o0ino2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_881vrts(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8fdq0j2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8qlbihr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8r57c5b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_90cv517(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_97k8lr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9h0rhbh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9o505uv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aa36nmv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_atni201(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfqp4iu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bgvaums(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bsjjs4q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmpeusk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djm7a7b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_elnenje(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eq52hho(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eq52igv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fa6dgsl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fe8dlnp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fmt0nd3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gjsb0o3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gmjqp49(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gsrnq92(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gtdu4sv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i1mdkd1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i1s1tes(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i6f520e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ia3m98c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jh3ht86(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjf260s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jv1pdfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l1ol1gh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lc3l553(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lcl6k04(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo08ias(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mqpg3i3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n3ad29p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_naiohrt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nmdqfj1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nqqt1q3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6rmrv9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o78l9p5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oqlb297(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbpu4nu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pvvaqhh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q0ab64e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qi7m0ev(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sro7ivu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tbto4cb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_toihk94(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uqn7075(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ut99gho(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vcmvfau(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhghpja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vnbkcp2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_b05vb48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b2nifvt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b3e91bl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b4oiuht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b6aggpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b6qg4rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b946o7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bc9gr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_beqekat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bi036vc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bia01cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_biupho2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bjrmbam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bkdctpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bl27tn4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bl6khrb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_blcbfcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_blrf705(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bm6js5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bmv30a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bofepup(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_booci4l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_bovj9ap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_bpl33sh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bs5ht1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_bsjq7um(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bt4apif(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bul917c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_buut5mk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bvot0a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r0u4lpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r1s7qdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r31rqga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r3el627(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r40dmo0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r4smojn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r57fu0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r6aoskm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r6pm0oj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r8c6rs0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r8e1h45(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r8pfaos(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r9ba7g9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r9dvh2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ra2b58j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_ra91g4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rb39nel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rb3ud4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rd4d2vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_reauhlh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ree2v5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rgk51pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rh16oh6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rjqt2eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rkrg0f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rlkig7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rlu0tid(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rmu2e6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rn1vtpg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ro2n9te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rp4le08(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rp57m7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rpma0gd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rpqj2mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rqvn133(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rse2efa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rslrdtj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rtv6jsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ru16275(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ru8569b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rvm8ibl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rvu96av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s000up2l16pib[9];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s0ajhhgcemkg1[18];
extern const uint8_t gm_s0iioal6odpu7[11];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m3gh47554ab[6];
extern const uint8_t gm_s0n03nrf296lf[33];
extern const uint8_t gm_s12khqa42i680[29];
extern const uint8_t gm_s1a10cqdk6nil[19];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1e7k26kdaoj3[31];
extern const uint8_t gm_s1fshvu7vb9i4[16];
extern const uint8_t gm_s1lj31vo6lvqe[26];
extern const uint8_t gm_s1s57v200fem0[6];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s27jprdv4pn6e[10];
extern const uint8_t gm_s2aq94adogrjl[35];
extern const uint8_t gm_s2esennel4fsa[38];
extern const uint8_t gm_s2haaj13v41fd[25];
extern const uint8_t gm_s2hut6059od59[42];
extern const uint8_t gm_s2kuocg9pm4qc[63];
extern const uint8_t gm_s2o9ogopuec6c[108];
extern const uint8_t gm_s2pmug7pm7rdg[31];
extern const uint8_t gm_s2rbapbbig1pe[19];
extern const uint8_t gm_s3347hvco05di[6];
extern const uint8_t gm_s389kh4hkfd99[6];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3a91i4ku76eo[81];
extern const uint8_t gm_s3ajneg0jdfi2[15];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3j69epdb3egk[26];
extern const uint8_t gm_s3jflb7gdk32k[32];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4ccl84nrk27k[55];
extern const uint8_t gm_s4emnk8r9sirq[11];
extern const uint8_t gm_s4l3p0f0c6mmc[44];
extern const uint8_t gm_s4oil3epcv041[3];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4rugtmn8jarn[57];
extern const uint8_t gm_s4vgbnvmvj0st[34];
extern const uint8_t gm_s542rn9shphg8[16];
extern const uint8_t gm_s5dhdddu73608[25];
extern const uint8_t gm_s5e527dkpbusp[5];
extern const uint8_t gm_s5ndb61nne1fg[58];
extern const uint8_t gm_s5pq4t98troao[34];
extern const uint8_t gm_s5r4hcdoqsklk[53];
extern const uint8_t gm_s5r4lidhg0cfv[64];
extern const uint8_t gm_s62t7qmf3ajh5[69];
extern const uint8_t gm_s6f3urtqqu41d[16];
extern const uint8_t gm_s6g8mt9e5cjjr[11];
extern const uint8_t gm_s6kp8voc0etjv[60];
extern const uint8_t gm_s6m48vdr7h6mr[77];
extern const uint8_t gm_s6p6u3ogotq4f[50];
extern const uint8_t gm_s6qe25ct4l9uk[34];
extern const uint8_t gm_s71712nhgrg2u[15];
extern const uint8_t gm_s7c8ep4trp7bk[7];
extern const uint8_t gm_s7g0chs5u30do[21];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7nnecs6i9eki[41];
extern const uint8_t gm_s7pnhdrn5iusl[53];
extern const uint8_t gm_s7st0fkehmujt[69];
extern const uint8_t gm_s8do3iqjvck3j[45];
extern const uint8_t gm_s8l5u1ln9m2k0[58];
extern const uint8_t gm_s8tadn8f0l2a8[21];
extern const uint8_t gm_s9659cpb4brmg[36];
extern const uint8_t gm_s966nqk7bt67q[19];
extern const uint8_t gm_s99scb3gc7vkm[29];
extern const uint8_t gm_s9da4sg04ql4e[60];
extern const uint8_t gm_s9dh9d8gs6a16[9];
extern const uint8_t gm_s9grefqkso86a[3];
extern const uint8_t gm_s9nk06pmq8hl1[34];
extern const uint8_t gm_s9r71kmnlqpv8[17];
extern const uint8_t gm_s9rhhj2j5hs2v[36];
extern const uint8_t gm_s9rujqsrrjcho[110];
extern const uint8_t gm_s9uf4t24m7c3t[31];
extern const uint8_t gm_s9vfcoh7jj30b[17];
extern const uint8_t gm_sa8k1i3b5phk8[24];
extern const uint8_t gm_sa9pve8r9407o[35];
extern const uint8_t gm_safiul2d9a6fl[14];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_sb3o3ij0n2htb[5];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_sbfc2b8kquvq8[9];
extern const uint8_t gm_sblao71d2av8h[22];
extern const uint8_t gm_sbm4560lv1lt5[25];
extern const uint8_t gm_sbo7sr73hvg7d[10];
extern const uint8_t gm_sbo8mn3eri83c[23];
extern const uint8_t gm_sbvqgqcoh1qhn[56];
extern const uint8_t gm_sc149ns1jm5on[9];
extern const uint8_t gm_sc19lloml9nb7[17];
extern const uint8_t gm_scavc1uf2hs8l[7];
extern const uint8_t gm_sckeklr60tlq3[34];
extern const uint8_t gm_scnb9tl13g4lt[8];
extern const uint8_t gm_scon24vlgg660[15];
extern const uint8_t gm_scstobchrdgiu[67];
extern const uint8_t gm_sd1uj6gf5v8qh[8];
extern const uint8_t gm_sdjk9c005ec9t[13];
extern const uint8_t gm_sdle4vcf3dhti[3];
extern const uint8_t gm_sdo8qcbf74mp0[7];
extern const uint8_t gm_sdriahok6illd[12];
extern const uint8_t gm_sdrkb67lqt61h[88];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se3smlbkt8d02[37];
extern const uint8_t gm_se8us8vie9a8a[37];
extern const uint8_t gm_sed3pofb5bu9b[26];
extern const uint8_t gm_sekdm5lqg7176[43];
extern const uint8_t gm_sem2htac3j4np[66];
extern const uint8_t gm_sep8ve6ec8tv7[24];
extern const uint8_t gm_sepo1h9bofnod[30];
extern const uint8_t gm_seqji52qmi3ea[4];
extern const uint8_t gm_sf4qubhm4h2hm[32];
extern const uint8_t gm_sf90mcj0opih1[8];
extern const uint8_t gm_sfa00o5gkcj29[5];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sg0tsttb54h9c[85];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5o5gf2u71g7[9];
extern const uint8_t gm_sgcporqt2gqqg[16];
extern const uint8_t gm_sgd9tn7p5ihgk[10];
extern const uint8_t gm_sgkfsokc46mg5[34];
extern const uint8_t gm_sh08f8dq9lenu[41];
extern const uint8_t gm_sh0huebkcvk29[20];
extern const uint8_t gm_sh50kc23mi3vj[36];
extern const uint8_t gm_sh8houjk34evm[15];
extern const uint8_t gm_sh8l62ma11hq5[70];
extern const uint8_t gm_shb5dhf7pftn9[17];
extern const uint8_t gm_shda201svb9eg[119];
extern const uint8_t gm_shes0dtqt9sqq[28];
extern const uint8_t gm_shhlfsve4gvoq[72];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shj1t0cb4970k[27];
extern const uint8_t gm_si2ih874n1gou[10];
extern const uint8_t gm_si4l87dl4tpkq[8];
extern const uint8_t gm_si6c601mt9p4v[220];
extern const uint8_t gm_sihb07k8fk2nc[35];
extern const uint8_t gm_siipob9mkm9hq[24];
extern const uint8_t gm_sijp8lh2g2ld5[34];
extern const uint8_t gm_simovkrp5fi0r[16];
extern const uint8_t gm_sj0ur1o7a5584[16];
extern const uint8_t gm_sj2if4k4r1jer[8];
extern const uint8_t gm_sj5fh226119gq[14];
extern const uint8_t gm_sj90kfpommb4k[44];
extern const uint8_t gm_sj9krj49k3580[10];
extern const uint8_t gm_sjf1r2hmheh87[29];
extern const uint8_t gm_sjfrmur8rvp24[34];
extern const uint8_t gm_sjgrq0dv175qo[30];
extern const uint8_t gm_sjhqrnq2lvjcm[35];
extern const uint8_t gm_sjkbtepn8bqeb[24];
extern const uint8_t gm_sjlci8b0qvbpc[3];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjnv3p8t49clu[9];
extern const uint8_t gm_sjqmjo9ak50j8[5];
extern const uint8_t gm_sk01103sqa2lh[36];
extern const uint8_t gm_sk698gp5chukg[10];
extern const uint8_t gm_sknsei1oiabnd[13];
extern const uint8_t gm_skpsvuvlfj0ep[68];
extern const uint8_t gm_skrdl5pdcve8g[43];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_sljdair45oqft[31];
extern const uint8_t gm_slni8vfv3lequ[22];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_sm0gqinn3n1e9[15];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smovt8m5cpa13[7];
extern const uint8_t gm_smraeij5q6ubc[11];
extern const uint8_t gm_sn790bmojc3gl[33];
extern const uint8_t gm_sngckqdbegtlc[26];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snmrruc88a739[10];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_so3ukp67pds22[190];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sobb0dbnbb0kc[56];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_somon4mdq0gpj[24];
extern const uint8_t gm_sosa5l0k6d34c[15];
extern const uint8_t gm_sp20qfi70dn0k[33];
extern const uint8_t gm_sp46pm3sna4bl[86];
extern const uint8_t gm_spak5smc8j24b[14];
extern const uint8_t gm_spg4d1df6jnji[7];
extern const uint8_t gm_spkcu8lv7g8jc[5];
extern const uint8_t gm_spni6j7splqto[8];
extern const uint8_t gm_sq6h1gpkivf38[41];
extern const uint8_t gm_sq95m8isfa7vd[2];
extern const uint8_t gm_sq9f8k00ocil6[35];
extern const uint8_t gm_sq9lne9r5j3u2[8];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqin9fcebg38h[7];
extern const uint8_t gm_sqro42saen22n[108];
extern const uint8_t gm_sr3et5tffndla[28];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sri1d6adh4kkk[11];
extern const uint8_t gm_srlklqc70q6ju[3];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_srvl2rg1hpk9a[16];
extern const uint8_t gm_ss3kdmf5g7qmv[82];
extern const uint8_t gm_ss7moisvq3l8h[6];
extern const uint8_t gm_ssg5cub3gqpc7[13];
extern const uint8_t gm_ssi95ri2h4d02[28];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssk4a01soguqj[7];
extern const uint8_t gm_st28rv7m1lueb[9];
extern const uint8_t gm_staavgn0someg[56];
extern const uint8_t gm_stcdo02knddjn[7];
extern const uint8_t gm_std91rqsr8j0b[7];
extern const uint8_t gm_stk9qd6rl9jdj[21];
extern const uint8_t gm_sto79bcv9e91i[4];
extern const uint8_t gm_stotuneqhj37e[36];
extern const uint8_t gm_su8ulebf2ijla[20];
extern const uint8_t gm_sucfks6hb8bf7[44];
extern const uint8_t gm_sucs0qmdapqe8[41];
extern const uint8_t gm_suh21tg0nqmfn[10];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const uint8_t gm_sukj4damah3dd[85];
extern const uint8_t gm_supobfk7sjp99[48];
extern const uint8_t gm_sv12lv80frbvd[29];
extern const uint8_t gm_sv1f0l9ks3lvu[25];
extern const uint8_t gm_sv44h36stbh8s[7];
extern const uint8_t gm_sv61f25m0b9gs[4];
extern const uint8_t gm_svfte0jrv51gc[30];
extern const uint8_t gm_svmt0iljnadus[4];
extern const gm_type gm_ty1ak5vhpqj4;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyqa6gfa8omk;
extern const gm_type gm_tyqqajgod2bb;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_057m2j5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_057m2j5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bovj9ap *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bovj9ap *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bovj9ap *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05go7ic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_05go7ic F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b4oiuht *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b4oiuht *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b4oiuht *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05jebu4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_05jebu4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bc9gr0b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bc9gr0b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bc9gr0b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0af9jdh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0af9jdh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bm6js5o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bm6js5o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bm6js5o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0cu6em8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0cu6em8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b6qg4rq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b6qg4rq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b6qg4rq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_103pjna(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_103pjna F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r57fu0b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r57fu0b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r57fu0b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_15lco53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_15lco53 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_blrf705 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_blrf705 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_blrf705 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1et6tkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1et6tkr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rtv6jsp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rtv6jsp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rtv6jsp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lb8oku(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1lb8oku F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r1s7qdu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r1s7qdu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r1s7qdu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1mlu301(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1mlu301 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_booci4l *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_booci4l *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_booci4l *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1n0hdi5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1n0hdi5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r40dmo0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r40dmo0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r40dmo0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1s44o6q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1s44o6q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r8pfaos *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r8pfaos *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r8pfaos *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_23j123c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_23j123c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bia01cc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bia01cc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bia01cc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2k5hcs1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2k5hcs1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_be3fee1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_be3fee1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_be3fee1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_320ocof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_320ocof F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r4smojn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r4smojn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r4smojn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sfh3cs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3sfh3cs F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_buut5mk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_buut5mk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_buut5mk *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_48ghii1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_48ghii1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b3e91bl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b3e91bl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b3e91bl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4c88gjc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4c88gjc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r8e1h45 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r8e1h45 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r8e1h45 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4dfj0mb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4dfj0mb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rlu0tid *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rlu0tid *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rlu0tid *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4jf0q0u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4jf0q0u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r3el627 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r3el627 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r3el627 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4q7fa3j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4q7fa3j F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bi036vc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bi036vc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bi036vc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_507jnu8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_507jnu8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bsjq7um *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bsjq7um *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bsjq7um *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_65m9k9g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_65m9k9g F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r31rqga *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r31rqga *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r31rqga *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_68un0e5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_68un0e5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bl6khrb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bl6khrb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bl6khrb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7bcuctm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7bcuctm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ra2b58j *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ra2b58j *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ra2b58j *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7c6rrmg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7c6rrmg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bunqlnl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bunqlnl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bunqlnl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7dm3ok7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7dm3ok7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rd4d2vn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rd4d2vn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rd4d2vn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7en1q5j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7en1q5j F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ra91g4n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ra91g4n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ra91g4n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7o0ino2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7o0ino2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bmv30a9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bmv30a9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bmv30a9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_881vrts(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_881vrts F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bm4rhr0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bm4rhr0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bm4rhr0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8fdq0j2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8fdq0j2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rmu2e6q *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rmu2e6q *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rmu2e6q *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8qlbihr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8qlbihr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rse2efa *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rse2efa *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rse2efa *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8r57c5b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8r57c5b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r6pm0oj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r6pm0oj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r6pm0oj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_90cv517(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_90cv517 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ru16275 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ru16275 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ru16275 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_97k8lr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_97k8lr5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rqvn133 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rqvn133 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rqvn133 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9h0rhbh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9h0rhbh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rvm8ibl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rvm8ibl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rvm8ibl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9o505uv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9o505uv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_reauhlh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_reauhlh *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_reauhlh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aa36nmv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aa36nmv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rd9dflp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rd9dflp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rd9dflp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_atni201(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_atni201 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r4p6poi *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r4p6poi *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r4p6poi *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfqp4iu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bfqp4iu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bkdctpk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bkdctpk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bkdctpk *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bgvaums(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bgvaums F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rp4le08 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rp4le08 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rp4le08 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bsjjs4q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bsjjs4q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bofepup *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bofepup *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bofepup *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmpeusk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cmpeusk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rslrdtj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rslrdtj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rslrdtj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djm7a7b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_djm7a7b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b46cbct *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b46cbct *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b46cbct *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_elnenje(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_elnenje F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b946o7j *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b946o7j *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b946o7j *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eq52hho(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eq52hho F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ro2n9te *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ro2n9te *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ro2n9te *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eq52igv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eq52igv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rp57m7o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rp57m7o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rp57m7o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fa6dgsl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fa6dgsl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bl27tn4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bl27tn4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bl27tn4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fe8dlnp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fe8dlnp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r9ba7g9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r9ba7g9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r9ba7g9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fmt0nd3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fmt0nd3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rb39nel *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rb39nel *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rb39nel *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gjsb0o3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gjsb0o3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bv13gdl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bv13gdl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bv13gdl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gmjqp49(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gmjqp49 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r0u4lpe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r0u4lpe *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r0u4lpe *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gsrnq92(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gsrnq92 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rlkig7n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rlkig7n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rlkig7n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gtdu4sv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gtdu4sv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bpl33sh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bpl33sh *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bpl33sh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i1mdkd1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i1mdkd1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rjqt2eo *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rjqt2eo *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rjqt2eo *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i1s1tes(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i1s1tes F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_biupho2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_biupho2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_biupho2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i6f520e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i6f520e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_blcbfcc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_blcbfcc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_blcbfcc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ia3m98c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ia3m98c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bt4apif *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bt4apif *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bt4apif *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jh3ht86(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jh3ht86 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ree2v5u *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ree2v5u *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ree2v5u *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjf260s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jjf260s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bbsjt91 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bbsjt91 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bbsjt91 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jv1pdfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jv1pdfe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rpqj2mb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rpqj2mb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rpqj2mb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l1ol1gh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l1ol1gh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ru8569b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ru8569b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ru8569b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lc3l553(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lc3l553 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rh16oh6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rh16oh6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rh16oh6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lcl6k04(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lcl6k04 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bvitrsu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bvitrsu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bvitrsu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo08ias(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mo08ias F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bul917c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bul917c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bul917c *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mqpg3i3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mqpg3i3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b6aggpq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b6aggpq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b6aggpq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n3ad29p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_n3ad29p F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rgk51pp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rgk51pp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rgk51pp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_naiohrt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_naiohrt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b05vb48 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b05vb48 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b05vb48 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nmdqfj1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nmdqfj1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_beqekat *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_beqekat *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_beqekat *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nqqt1q3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nqqt1q3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_b2nifvt *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_b2nifvt *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_b2nifvt *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6rmrv9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o6rmrv9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r6aoskm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r6aoskm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r6aoskm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o78l9p5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o78l9p5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r8c6rs0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r8c6rs0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r8c6rs0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oqlb297(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_oqlb297 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rvu96av *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rvu96av *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rvu96av *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbpu4nu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pbpu4nu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bvot0a7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bvot0a7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bvot0a7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pvvaqhh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pvvaqhh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bs5ht1e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bs5ht1e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bs5ht1e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q0ab64e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q0ab64e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ra7pont *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ra7pont *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ra7pont *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qi7m0ev(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qi7m0ev F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rpma0gd *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rpma0gd *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rpma0gd *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sro7ivu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sro7ivu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bjrmbam *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bjrmbam *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bjrmbam *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tbto4cb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tbto4cb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rb3ud4d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rb3ud4d *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rb3ud4d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_toihk94(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_toihk94 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rkrg0f2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rkrg0f2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rkrg0f2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uqn7075(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_uqn7075 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_bnsf58k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_bnsf58k *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_bnsf58k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ut99gho(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ut99gho F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rrko99s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rrko99s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rrko99s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vcmvfau(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vcmvfau F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_r9dvh2e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_r9dvh2e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_r9dvh2e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhghpja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vhghpja F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rbr6trv *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rbr6trv *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rbr6trv *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vnbkcp2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vnbkcp2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_rn1vtpg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_rn1vtpg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_rn1vtpg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
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
__attribute__((aligned(4))) void gm_f_digits_br9h428(void *gm_out, gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digits_br9h428 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* neg */
    uint32_t gm_l3; /* base */
    gm_t9805qtonbsag gm_l4; /* tb */
    uint32_t gm_l5; /* acc */
    uint8_t gm_l6; /* after_digit */
    uint64_t gm_l7; /* i */
    uint8_t gm_l8; /* c */
    uint8_t gm_l9; /* d */
    uint32_t gm_l10; /* dv */
    uint32_t gm_l11; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 598u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        gm_t9805qtonbsag t3;
        uint32_t t4 = (uint32_t)t2;
        gm_t9805qtonbsag t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[4] = 1;
        gm_t9805qtonbsag t6;
        memset(&t6, 0, sizeof t6);
        t3 = ((unsigned __int128)(t2) <= (((unsigned __int128)0ULL << 64) | 4294967295ULL)) ? t5 : t6;
        gm_l4 = t3;
    }
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
        gm_l6 = ((uint8_t)0);
    }
    {
        uint64_t t7 = gm_l1;
        gm_l7 = t7;
    }
    {
        for (;;) {
            uint64_t t9 = gm_l7;
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t11 = (t9 < t10);
            if (!t11) goto lb8;
            {
                uint64_t t14 = gm_l7;
                uint64_t t15 = (uintptr_t)t14;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_src3d0v42kavhv7, 336, 14);
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
                gm_l8 = t17;
            }
            {
                uint8_t t19 = gm_l8;
                uint8_t t20 = ((t19 == ((uint8_t)95ULL)));
                if (!t20) goto ie18;
                {
                    uint8_t t23 = gm_l6;
                    uint8_t t24 = (uint8_t)!t23;
                    if (!t24) goto ie22;
                    {
                        uint64_t t26 = gm_l7;
                        gm_t4ce0jd3tdro4 t27;
                        GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t26);
                        gm_ttiiv5vhiuvvr t28;
                        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t28 + 8), t27);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t28);
                        goto gm_fdone;
                    }
                    goto ix22;
ie22:;
ix22:;
                }
                {
                    gm_l6 = ((uint8_t)0);
                }
                {
                    uint64_t t29 = gm_l7;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 340, 13);
                    uint64_t t32 = r30;
                    gm_l7 = t32;
                }
                {
                    goto lc8;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                uint8_t t33 = gm_l8;
                gm_tqs5p4n2tq19v t34 = gm_f_digit_value_2b3md79(t33);
                uint8_t t35;
                if (((((uint8_t *)&t34))[1] != 0)) {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
                    t35 = t36;
                } else {
                    uint64_t t37 = gm_l7;
                    gm_t4ce0jd3tdro4 t38;
                    GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t37);
                    gm_ttiiv5vhiuvvr t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t39 + 8), t38);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                    GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t39);
                    goto gm_fdone;
                }
                gm_l9 = t35;
            }
            {
                uint8_t t41 = gm_l9;
                uint32_t t42 = (uint32_t)t41;
                uint32_t t43 = gm_l3;
                uint8_t t44 = (t42 >= t43);
                if (!t44) goto ie40;
                {
                    uint64_t t46 = gm_l7;
                    gm_t4ce0jd3tdro4 t47;
                    GM_ST(uint64_t, ((uint8_t *)&t47 + 0), t46);
                    gm_ttiiv5vhiuvvr t48;
                    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t48 + 8), t47);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                    GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t48);
                    goto gm_fdone;
                }
                goto ix40;
ie40:;
ix40:;
            }
            {
                uint8_t t49 = gm_l9;
                gm_t9805qtonbsag t50;
                uint32_t t51 = (uint32_t)t49;
                gm_t9805qtonbsag t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t52), t51);
                ((uint8_t *)&t52)[4] = 1;
                gm_t9805qtonbsag t53;
                memset(&t53, 0, sizeof t53);
                t50 = ((unsigned __int128)(t49) <= (((unsigned __int128)0ULL << 64) | 4294967295ULL)) ? t52 : t53;
                uint32_t t54;
                if (((((uint8_t *)&t50))[4] != 0)) {
                    uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&t50) + 0));
                    t54 = t55;
                } else {
                    gm_ttiiv5vhiuvvr t56;
                    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)2ULL));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                    GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t56);
                    goto gm_fdone;
                }
                gm_l10 = t54;
            }
            {
                uint32_t t58 = gm_l5;
                uint8_t t59 = (!(t58 == ((uint32_t)0ULL)));
                if (!t59) goto ie57;
                {
                    gm_t9805qtonbsag t61 = gm_l4;
                    uint32_t t62;
                    if (((((uint8_t *)&t61))[4] != 0)) {
                        uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&t61) + 0));
                        t62 = t63;
                    } else {
                        gm_ttiiv5vhiuvvr t64;
                        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t64 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t64);
                        goto gm_fdone;
                    }
                    gm_l11 = t62;
                }
                {
                    uint32_t t65 = gm_l5;
                    uint32_t t66 = gm_l11;
                    uint32_t r67; bool o68 = __builtin_mul_overflow(t65, t66, &r67);
                    gm_t9805qtonbsag t69;
                    if (o68) {
                    gm_t9805qtonbsag t70;
                    memset(&t70, 0, sizeof t70);
                        t69 = t70;
                    } else {
                    uint32_t t71 = r67;
                    gm_t9805qtonbsag t72;
                    GM_ST(uint8_t, (uint8_t *)&t72 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t72 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t72), t71);
                    ((uint8_t *)&t72)[4] = 1;
                        t69 = t72;
                    }
                    uint32_t t73;
                    if (((((uint8_t *)&t69))[4] != 0)) {
                        uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&t69) + 0));
                        t73 = t74;
                    } else {
                        gm_ttiiv5vhiuvvr t75;
                        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t75);
                        goto gm_fdone;
                    }
                    gm_l5 = t73;
                }
                goto ix57;
ie57:;
ix57:;
            }
            {
                uint8_t t77 = gm_l2;
                if (!t77) goto ie76;
                {
                    uint32_t t79 = gm_l5;
                    uint32_t t80 = gm_l10;
                    uint32_t r81; bool o82 = __builtin_sub_overflow(t79, t80, &r81);
                    gm_t9805qtonbsag t83;
                    if (o82) {
                    gm_t9805qtonbsag t84;
                    memset(&t84, 0, sizeof t84);
                        t83 = t84;
                    } else {
                    uint32_t t85 = r81;
                    gm_t9805qtonbsag t86;
                    GM_ST(uint8_t, (uint8_t *)&t86 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t86 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t86), t85);
                    ((uint8_t *)&t86)[4] = 1;
                        t83 = t86;
                    }
                    uint32_t t87;
                    if (((((uint8_t *)&t83))[4] != 0)) {
                        uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&t83) + 0));
                        t87 = t88;
                    } else {
                        gm_ttiiv5vhiuvvr t89;
                        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t89 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t89);
                        goto gm_fdone;
                    }
                    gm_l5 = t87;
                }
                goto ix76;
ie76:;
                {
                    uint32_t t91 = gm_l5;
                    uint32_t t92 = gm_l10;
                    uint32_t r93; bool o94 = __builtin_add_overflow(t91, t92, &r93);
                    gm_t9805qtonbsag t95;
                    if (o94) {
                    gm_t9805qtonbsag t96;
                    memset(&t96, 0, sizeof t96);
                        t95 = t96;
                    } else {
                    uint32_t t97 = r93;
                    gm_t9805qtonbsag t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t98), t97);
                    ((uint8_t *)&t98)[4] = 1;
                        t95 = t98;
                    }
                    uint32_t t99;
                    if (((((uint8_t *)&t95))[4] != 0)) {
                        uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)&t95) + 0));
                        t99 = t100;
                    } else {
                        gm_ttiiv5vhiuvvr t101;
                        GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t101 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t101);
                        goto gm_fdone;
                    }
                    gm_l5 = t99;
                }
ix76:;
            }
            {
                gm_l6 = ((uint8_t)1);
            }
            {
                uint64_t t102 = gm_l7;
                uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                if (o104) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 357, 9);
                uint64_t t105 = r103;
                gm_l7 = t105;
            }
lc8:;
        }
lb8:;
    }
    {
        uint8_t t107 = gm_l6;
        uint8_t t108 = (uint8_t)!t107;
        if (!t108) goto ie106;
        {
            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_t4ce0jd3tdro4 t111;
            GM_ST(uint64_t, ((uint8_t *)&t111 + 0), t110);
            gm_ttiiv5vhiuvvr t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t112 + 8), t111);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t112);
            goto gm_fdone;
        }
        goto ix106;
ie106:;
ix106:;
    }
    {
        uint32_t t113 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out))[0] = 0;
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tssfa0col9q01 *)gm_out) + 8, t113);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tssfa0col9q01 gm_f_digits_br9h428_c(gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3) { gm_tssfa0col9q01 gm_r; gm_f_digits_br9h428(&gm_r, a0, a1, a2, a3); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_057m2j5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05go7ic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05jebu4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0af9jdh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0cu6em8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_103pjna(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_15lco53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1et6tkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lb8oku(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1mlu301(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1n0hdi5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1s44o6q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_23j123c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2k5hcs1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_320ocof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sfh3cs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_48ghii1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4c88gjc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4dfj0mb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4jf0q0u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4q7fa3j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_507jnu8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_65m9k9g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_68un0e5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7bcuctm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7c6rrmg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7dm3ok7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7en1q5j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7o0ino2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_881vrts(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8fdq0j2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8qlbihr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8r57c5b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_90cv517(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_97k8lr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9h0rhbh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9o505uv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aa36nmv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_atni201(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfqp4iu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bgvaums(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bsjjs4q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmpeusk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djm7a7b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_elnenje(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eq52hho(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eq52igv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fa6dgsl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fe8dlnp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fmt0nd3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gjsb0o3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gmjqp49(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gsrnq92(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gtdu4sv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i1mdkd1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i1s1tes(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i6f520e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ia3m98c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jh3ht86(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjf260s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jv1pdfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l1ol1gh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lc3l553(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lcl6k04(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mo08ias(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mqpg3i3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n3ad29p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_naiohrt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nmdqfj1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nqqt1q3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6rmrv9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o78l9p5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oqlb297(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbpu4nu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pvvaqhh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q0ab64e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qi7m0ev(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sro7ivu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tbto4cb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_toihk94(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uqn7075(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ut99gho(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vcmvfau(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhghpja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vnbkcp2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_b05vb48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b05vb48 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su8ulebf2ijla));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_naiohrt, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sc19lloml9nb7));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(16));
            gm_f_fn_naiohrt(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_naiohrt(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_naiohrt(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_b2nifvt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_b2nifvt F;
    gm_tgbgti82mf879 gm_l0; /* args */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 301u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6qe25ct4l9uk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tgbgti82mf879 t13 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_nqqt1q3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s2rbapbbig1pe));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(18));
            gm_f_fn_nqqt1q3(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_nqqt1q3(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s0m0qaokk51g0));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
            gm_f_fn_nqqt1q3(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s3347hvco05di));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(5));
            gm_f_fn_nqqt1q3(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_ty6groq37ram, ((uint8_t *)&t17), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            gm_f_fn_nqqt1q3(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_b3e91bl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b3e91bl F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9rhhj2j5hs2v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_48ghii1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_48ghii1(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_48ghii1(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sp20qfi70dn0k));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(32));
            gm_f_fn_48ghii1(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b46cbct F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sucfks6hb8bf7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_djm7a7b, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sh08f8dq9lenu));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(40));
            gm_f_fn_djm7a7b(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_djm7a7b(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_djm7a7b(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_b4oiuht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_b4oiuht F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9659cpb4brmg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t13 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_05go7ic, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sv1f0l9ks3lvu));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(24));
            gm_f_fn_05go7ic(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_05go7ic(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_spni6j7splqto));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
            gm_f_fn_05go7ic(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_b6aggpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_b6aggpq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sepo1h9bofnod));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_mqpg3i3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_suh21tg0nqmfn));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(9));
            gm_f_fn_mqpg3i3(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_mqpg3i3(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s6f3urtqqu41d));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(15));
            gm_f_fn_mqpg3i3(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_mqpg3i3(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_mqpg3i3(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_b6qg4rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b6qg4rq F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa9pve8r9407o));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_0cu6em8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s27jprdv4pn6e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(9));
            gm_f_fn_0cu6em8(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_0cu6em8(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sep8ve6ec8tv7));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(23));
            gm_f_fn_0cu6em8(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_b946o7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b946o7j F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shes0dtqt9sqq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_elnenje, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_elnenje(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sngckqdbegtlc));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(25));
            gm_f_fn_elnenje(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bbsjt91 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lj31vo6lvqe));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_jjf260s, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sj2if4k4r1jer));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            gm_f_fn_jjf260s(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_jjf260s(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_ssjhh9bdn23jf));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(16));
            gm_f_fn_jjf260s(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_bc9gr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bc9gr0b F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shda201svb9eg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(118));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_05jebu4, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sj2if4k4r1jer));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            gm_f_fn_05jebu4(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_05jebu4(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s9rujqsrrjcho));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(109));
            gm_f_fn_05jebu4(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_be3fee1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sri1d6adh4kkk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2k5hcs1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sg5o5gf2u71g7));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
            gm_f_fn_2k5hcs1(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_2k5hcs1(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_beqekat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_beqekat F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_staavgn0someg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_nmdqfj1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s7pnhdrn5iusl));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(52));
            gm_f_fn_nmdqfj1(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_nmdqfj1(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_nmdqfj1(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_bi036vc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bi036vc F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfa00o5gkcj29));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4q7fa3j, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_4q7fa3j(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_srlklqc70q6ju));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_4q7fa3j(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_bia01cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_bia01cc F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    gm_tidu2er0d7h95 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stcdo02knddjn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tidu2er0d7h95 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_23j123c, (void *)&F.h, cx});
            double t15 = GM_LD(double, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tyqqajgod2bb, ((uint8_t *)&t15), &(gm_spec){32, 0, 0, 0, 0, 0, 0u, 2})) {
                goto gm_lad;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s03pmf867dcsr));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_23j123c(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_biupho2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_biupho2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svfte0jrv51gc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_i1s1tes, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sbm4560lv1lt5));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(24));
            gm_f_fn_i1s1tes(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_i1s1tes(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_i1s1tes(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_i1s1tes(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_bjrmbam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bjrmbam F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbvqgqcoh1qhn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_sro7ivu, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sdriahok6illd));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(11));
            gm_f_fn_sro7ivu(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_sro7ivu(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s7c8ep4trp7bk));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
            gm_f_fn_sro7ivu(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_sro7ivu(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sq9f8k00ocil6));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(34));
            gm_f_fn_sro7ivu(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bkdctpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bkdctpk F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5e527dkpbusp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_bfqp4iu, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sb54ui2amic32));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_bfqp4iu(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_bfqp4iu(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sb54ui2amic32));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_bfqp4iu(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_bl27tn4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bl27tn4 F;
    gm_tommf4j9upomc gm_l0; /* args */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 249u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sckeklr60tlq3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tommf4j9upomc t13 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fa6dgsl, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_ssi95ri2h4d02));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(27));
            gm_f_fn_fa6dgsl(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_fa6dgsl(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_srs1vrn2uaams));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_fa6dgsl(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty467d5cm1d5, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_fa6dgsl(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bl6khrb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bl6khrb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shb5dhf7pftn9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_68un0e5, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sh8houjk34evm));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            gm_f_fn_68un0e5(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_blcbfcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_blcbfcc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqro42saen22n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(107));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_i6f520e, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1e7k26kdaoj3));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(30));
            gm_f_fn_i6f520e(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_i6f520e(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s23t712hkqlmp));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_f_fn_i6f520e(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_i6f520e(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sh8l62ma11hq5));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(69));
            gm_f_fn_i6f520e(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_blrf705(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_blrf705 F;
    GM_STACK_CHECK(&F, 253u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8do3iqjvck3j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tq3m5qgvkpje8 t13 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_15lco53, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_15lco53(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_15lco53(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s9r71kmnlqpv8));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(16));
            gm_f_fn_15lco53(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sj0ur1o7a5584));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(15));
            gm_f_fn_15lco53(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_15lco53(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sdo8qcbf74mp0));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(6));
            gm_f_fn_15lco53(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bm4rhr0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2haaj13v41fd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_881vrts, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_slni8vfv3lequ));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(21));
            gm_f_fn_881vrts(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_881vrts(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_881vrts(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_bm6js5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bm6js5o F;
    GM_STACK_CHECK(&F, 253u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spak5smc8j24b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_0af9jdh, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_0af9jdh(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_somhu4vltiil1));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_f_fn_0af9jdh(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_0af9jdh(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_shi7nidbc64qf));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
            gm_f_fn_0af9jdh(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_0af9jdh(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_f_fn_0af9jdh(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t24 = F.l2;
        if (!t24) goto ie23;
        {
            void * t26 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t26);
        }
        {
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t28);
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
inline __attribute__((aligned(4))) void gm_f_format_bmv30a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bmv30a9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj5fh226119gq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7o0ino2, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_7o0ino2(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7o0ino2(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sjnv3p8t49clu));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(8));
            gm_f_fn_7o0ino2(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_7o0ino2(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bnsf58k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn790bmojc3gl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_uqn7075, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_uqn7075(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_uqn7075(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_shj1t0cb4970k));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(26));
            gm_f_fn_uqn7075(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_uqn7075(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_uqn7075(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bofepup(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bofepup F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sto79bcv9e91i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_bsjjs4q, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_bsjjs4q(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_bsjjs4q(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_booci4l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_booci4l F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1fshvu7vb9i4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1mlu301, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sbfc2b8kquvq8));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
            gm_f_fn_1mlu301(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tyb2nno7n67r, ((uint8_t *)&t15), &(gm_spec){32, 0, 0, 0, 1, 1, 16u, -1})) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_bovj9ap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_bovj9ap F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 221u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3a91i4ku76eo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(80));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_057m2j5, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sa8k1i3b5phk8));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(23));
            gm_f_fn_057m2j5(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_stk9qd6rl9jdj));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(20));
            gm_f_fn_057m2j5(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t16), &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1})) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sljdair45oqft));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(30));
            gm_f_fn_057m2j5(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bpl33sh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bpl33sh F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4ccl84nrk27k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gtdu4sv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sdjk9c005ec9t));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(12));
            gm_f_fn_gtdu4sv(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_gtdu4sv(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s7nnecs6i9eki));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(40));
            gm_f_fn_gtdu4sv(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_bs5ht1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_bs5ht1e F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjqmjo9ak50j8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_pvvaqhh, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sdle4vcf3dhti));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_pvvaqhh(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_bsjq7um(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bsjq7um F;
    gm_t0bf4lga76ktk gm_l0; /* args */
    gm_t0bf4lga76ktk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 249u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp46pm3sna4bl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(85));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0bf4lga76ktk t13 = GM_LD(gm_t0bf4lga76ktk, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_507jnu8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            int32_t t16 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_507jnu8(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_507jnu8(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sem2htac3j4np));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(65));
            gm_f_fn_507jnu8(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyqa6gfa8omk, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sgcporqt2gqqg));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(15));
            gm_f_fn_507jnu8(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bt4apif(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bt4apif F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjkbtepn8bqeb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ia3m98c, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_scon24vlgg660));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
            gm_f_fn_ia3m98c(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ia3m98c(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s7hkl5mqa15gm));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_f_fn_ia3m98c(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_ia3m98c(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_ia3m98c(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bul917c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bul917c F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3j69epdb3egk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_mo08ias, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_somon4mdq0gpj));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(23));
            gm_f_fn_mo08ias(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_mo08ias(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bunqlnl F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sobb0dbnbb0kc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7c6rrmg, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_7c6rrmg(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7c6rrmg(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s5r4hcdoqsklk));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(52));
            gm_f_fn_7c6rrmg(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_buut5mk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_buut5mk F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_supobfk7sjp99));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t13 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_3sfh3cs, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_3sfh3cs(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sekdm5lqg7176));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(42));
            gm_f_fn_3sfh3cs(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_3sfh3cs(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bv13gdl F;
    gm_tgbgti82mf879 gm_l0; /* args */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tgbgti82mf879 t13 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gjsb0o3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_gjsb0o3(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_gjsb0o3(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_gjsb0o3(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_tyb2nno7n67r, ((uint8_t *)&t17), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bvitrsu F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2hut6059od59));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_lcl6k04, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sgkfsokc46mg5));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(33));
            gm_f_fn_lcl6k04(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_lcl6k04(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sv44h36stbh8s));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
            gm_f_fn_lcl6k04(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_bvot0a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bvot0a7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2aq94adogrjl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_pbpu4nu, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s6g8mt9e5cjjr));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(10));
            gm_f_fn_pbpu4nu(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_pbpu4nu(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s8tadn8f0l2a8));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(20));
            gm_f_fn_pbpu4nu(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_pbpu4nu(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_r0u4lpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r0u4lpe F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 253u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf90mcj0opih1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gmjqp49, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_gmjqp49(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_snukl6jqep6jf));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
            gm_f_fn_gmjqp49(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_gmjqp49(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r1s7qdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r1s7qdu F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf4qubhm4h2hm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1lb8oku, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s99scb3gc7vkm));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(28));
            gm_f_fn_1lb8oku(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_1lb8oku(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_1lb8oku(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r31rqga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r31rqga F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_simovkrp5fi0r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_65m9k9g, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snh0sl2cdagdn));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(12));
            gm_f_fn_65m9k9g(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_65m9k9g(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_65m9k9g(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r3el627(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r3el627 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4emnk8r9sirq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4jf0q0u, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s0m3gh47554ab));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(5));
            gm_f_fn_4jf0q0u(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_4jf0q0u(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_4jf0q0u(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_4jf0q0u(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_r40dmo0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r40dmo0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 253u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1s57v200fem0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1n0hdi5, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_1n0hdi5(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_1n0hdi5(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r4p6poi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sed3pofb5bu9b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_atni201, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sauf6ibu7mnba));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
            gm_f_fn_atni201(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_atni201(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3e9133b3pasn));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_atni201(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_atni201(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s542rn9shphg8));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(15));
            gm_f_fn_atni201(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_r4smojn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r4smojn F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5r4lidhg0cfv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_320ocof, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s12khqa42i680));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(28));
            gm_f_fn_320ocof(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_320ocof(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sjfrmur8rvp24));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(33));
            gm_f_fn_320ocof(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r57fu0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r57fu0b F;
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2pmug7pm7rdg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_103pjna, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_103pjna(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_103pjna(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_se08n5d1gngu7));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
            gm_f_fn_103pjna(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_103pjna(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s966nqk7bt67q));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(18));
            gm_f_fn_103pjna(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_103pjna(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            gm_f_fn_103pjna(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_r6aoskm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r6aoskm F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj90kfpommb4k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_o6rmrv9, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sucs0qmdapqe8));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(40));
            gm_f_fn_o6rmrv9(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_o6rmrv9(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_o6rmrv9(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r6pm0oj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r6pm0oj F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5dhdddu73608));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8r57c5b, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sbo8mn3eri83c));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(22));
            gm_f_fn_8r57c5b(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_r8c6rs0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r8c6rs0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s62t7qmf3ajh5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(68));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_o78l9p5, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sj9krj49k3580));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(9));
            gm_f_fn_o78l9p5(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_o78l9p5(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s5ndb61nne1fg));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(57));
            gm_f_fn_o78l9p5(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r8e1h45(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r8e1h45 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2esennel4fsa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4c88gjc, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sihb07k8fk2nc));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(34));
            gm_f_fn_4c88gjc(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_4c88gjc(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_4c88gjc(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_r8pfaos(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r8pfaos F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s000up2l16pib));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1s44o6q, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqin9fcebg38h));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
            gm_f_fn_1s44o6q(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_1s44o6q(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_r9ba7g9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r9ba7g9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm0gqinn3n1e9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fe8dlnp, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_ssg5cub3gqpc7));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(12));
            gm_f_fn_fe8dlnp(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_fe8dlnp(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_r9dvh2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_r9dvh2e F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spkcu8lv7g8jc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vcmvfau, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s9grefqkso86a));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_vcmvfau(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_ra2b58j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ra2b58j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjhqrnq2lvjcm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7bcuctm, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr3et5tffndla));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(27));
            gm_f_fn_7bcuctm(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7bcuctm(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sqdfr4k3vea5p));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_f_fn_7bcuctm(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_7bcuctm(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0) {
    struct gm_fr_format_ra7pont F;
    gm_tsn2omssg6cpf gm_l0; /* args */
    gm_tsn2omssg6cpf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tsn2omssg6cpf t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_q0ab64e, (void *)&F.h, cx});
            unsigned __int128 t15 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty1ak5vhpqj4, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_ra91g4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ra91g4n F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shhlfsve4gvoq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7en1q5j, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_7en1q5j(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7en1q5j(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s7st0fkehmujt));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(68));
            gm_f_fn_7en1q5j(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rb39nel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rb39nel F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sblao71d2av8h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fmt0nd3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_safiul2d9a6fl));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(13));
            gm_f_fn_fmt0nd3(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_fmt0nd3(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_std91rqsr8j0b));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
            gm_f_fn_fmt0nd3(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rb3ud4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rb3ud4d F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg0tsttb54h9c));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_tbto4cb, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_tbto4cb(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_tbto4cb(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_ss3kdmf5g7qmv));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(81));
            gm_f_fn_tbto4cb(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rbr6trv F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se3smlbkt8d02));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vhghpja, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4vgbnvmvj0st));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(33));
            gm_f_fn_vhghpja(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_vhghpja(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_vhghpja(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rd4d2vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rd4d2vn F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2o9ogopuec6c));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(107));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7dm3ok7, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_srvl2rg1hpk9a));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(15));
            gm_f_fn_7dm3ok7(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7dm3ok7(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s0n03nrf296lf));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(32));
            gm_f_fn_7dm3ok7(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_7dm3ok7(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s4rugtmn8jarn));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(56));
            gm_f_fn_7dm3ok7(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rd9dflp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skpsvuvlfj0ep));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(67));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_aa36nmv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1a10cqdk6nil));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(18));
            gm_f_fn_aa36nmv(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_aa36nmv(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_se8us8vie9a8a));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(36));
            gm_f_fn_aa36nmv(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_si2ih874n1gou));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(9));
            gm_f_fn_aa36nmv(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_reauhlh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_reauhlh F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5pq4t98troao));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_9o505uv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3jflb7gdk32k));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(31));
            gm_f_fn_9o505uv(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_9o505uv(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_ree2v5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ree2v5u F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk698gp5chukg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_jh3ht86, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_scavc1uf2hs8l));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
            gm_f_fn_jh3ht86(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_jh3ht86(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rgk51pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rgk51pp F;
    GM_STACK_CHECK(&F, 301u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk01103sqa2lh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)56ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tmr5oep1578tb t13 = GM_LD(gm_tmr5oep1578tb, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_n3ad29p, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_closure t19 = t14;
            gm_node *ccx20 = t19.cx;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_smovt8m5cpa13));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(6));
            gm_f_fn_n3ad29p(t19.env, ccx20, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_n3ad29p(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sosa5l0k6d34c));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(14));
            gm_f_fn_n3ad29p(t19.env, ccx20, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_n3ad29p(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s0m0qaokk51g0));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
            gm_f_fn_n3ad29p(t19.env, ccx20, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tyb2nno7n67r, ((uint8_t *)&t17), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(2));
            gm_f_fn_n3ad29p(t19.env, ccx20, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0) {
                gm_f_fn_n3ad29p(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t18)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s4a980fahtblo));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(2));
            gm_f_fn_n3ad29p(t19.env, ccx20, NULL, t25);
            if (gm_unw.kind) {
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t30);
            goto gm_fdone;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t31);
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
inline __attribute__((aligned(4))) void gm_f_format_rh16oh6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rh16oh6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv12lv80frbvd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_lc3l553, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_spg4d1df6jnji));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
            gm_f_fn_lc3l553(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_lc3l553(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s0ajhhgcemkg1));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(17));
            gm_f_fn_lc3l553(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_lc3l553(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_lc3l553(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_rjqt2eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rjqt2eo F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc149ns1jm5on));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_i1mdkd1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sv61f25m0b9gs));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
            gm_f_fn_i1mdkd1(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_i1mdkd1(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_i1mdkd1(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_i1mdkd1(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_rkrg0f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rkrg0f2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9nk06pmq8hl1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_toihk94, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s9uf4t24m7c3t));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(30));
            gm_f_fn_toihk94(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_toihk94(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_toihk94(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rlkig7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rlkig7n F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sijp8lh2g2ld5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gsrnq92, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sjgrq0dv175qo));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(29));
            gm_f_fn_gsrnq92(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_gsrnq92(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sjnkguo0atpm5));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
            gm_f_fn_gsrnq92(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rlu0tid(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rlu0tid F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sknsei1oiabnd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4dfj0mb, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sbo7sr73hvg7d));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(9));
            gm_f_fn_4dfj0mb(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_4dfj0mb(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_4dfj0mb(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rmu2e6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rmu2e6q F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb3o3ij0n2htb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8fdq0j2, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sjlci8b0qvbpc));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_8fdq0j2(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_8fdq0j2(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_rn1vtpg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rn1vtpg F;
    GM_STACK_CHECK(&F, 253u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sukj4damah3dd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t7neromnu8t62 t13 = GM_LD(gm_t7neromnu8t62, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vnbkcp2, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_smraeij5q6ubc));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(10));
            gm_f_fn_vnbkcp2(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_ss7moisvq3l8h));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(5));
            gm_f_fn_vnbkcp2(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_vnbkcp2(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s3ajneg0jdfi2));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(14));
            gm_f_fn_vnbkcp2(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_vnbkcp2(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s6p6u3ogotq4f));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(49));
            gm_f_fn_vnbkcp2(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_ro2n9te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ro2n9te F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2kuocg9pm4qc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_eq52hho, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s03c24qg7i06q));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(12));
            gm_f_fn_eq52hho(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_eq52hho(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sqdfr4k3vea5p));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_f_fn_eq52hho(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_eq52hho(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s4l3p0f0c6mmc));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(43));
            gm_f_fn_eq52hho(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_rp4le08(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rp4le08 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6m48vdr7h6mr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_bgvaums, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_st28rv7m1lueb));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(8));
            gm_f_fn_bgvaums(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_bgvaums(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sq9lne9r5j3u2));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
            gm_f_fn_bgvaums(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_bgvaums(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s8l5u1ln9m2k0));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(57));
            gm_f_fn_bgvaums(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_rp57m7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rp57m7o F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdrkb67lqt61h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(87));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_eq52igv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sh0huebkcvk29));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(19));
            gm_f_fn_eq52igv(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_eq52igv(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_scstobchrdgiu));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(66));
            gm_f_fn_eq52igv(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rpma0gd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rpma0gd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7g0chs5u30do));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_qi7m0ev, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s71712nhgrg2u));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
            gm_f_fn_qi7m0ev(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_qi7m0ev(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_slgmqgkioa4u6));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_qi7m0ev(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_qi7m0ev(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_rpqj2mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rpqj2mb F;
    GM_STACK_CHECK(&F, 253u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9da4sg04ql4e));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_jv1pdfe, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_jv1pdfe(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s9vfcoh7jj30b));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(16));
            gm_f_fn_jv1pdfe(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_jv1pdfe(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_stotuneqhj37e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(35));
            gm_f_fn_jv1pdfe(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_jv1pdfe(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sjnkguo0atpm5));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(2));
            gm_f_fn_jv1pdfe(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t24 = F.l2;
        if (!t24) goto ie23;
        {
            void * t26 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t26);
        }
        {
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t28);
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
inline __attribute__((aligned(4))) void gm_f_format_rqvn133(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rqvn133 F;
    GM_STACK_CHECK(&F, 269u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq6h1gpkivf38));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_97k8lr5, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s4oil3epcv041));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_97k8lr5(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_97k8lr5(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sjnv3p8t49clu));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(8));
            gm_f_fn_97k8lr5(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_97k8lr5(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_siipob9mkm9hq));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(23));
            gm_f_fn_97k8lr5(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_97k8lr5(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            gm_f_fn_97k8lr5(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rrko99s F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si6c601mt9p4v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(219));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ut99gho, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_so3ukp67pds22));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(189));
            gm_f_fn_ut99gho(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ut99gho(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sjf1r2hmheh87));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(28));
            gm_f_fn_ut99gho(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_rse2efa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rse2efa F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snmrruc88a739));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8qlbihr, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sd1uj6gf5v8qh));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            gm_f_fn_8qlbihr(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_8qlbihr(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_rslrdtj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_rslrdtj F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s389kh4hkfd99));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_cmpeusk, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_seqji52qmi3ea));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_f_fn_cmpeusk(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_rtv6jsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_rtv6jsp F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svmt0iljnadus));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1et6tkr, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sq95m8isfa7vd));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_1et6tkr(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_ru16275(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ru16275 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6kp8voc0etjv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_90cv517, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sltjg0sbhbo73));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(12));
            gm_f_fn_90cv517(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_90cv517(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_skrdl5pdcve8g));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(42));
            gm_f_fn_90cv517(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_90cv517(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_90cv517(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_ru8569b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ru8569b F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgd9tn7p5ihgk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_l1ol1gh, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_scnb9tl13g4lt));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            gm_f_fn_l1ol1gh(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_l1ol1gh(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_rvm8ibl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rvm8ibl F;
    GM_STACK_CHECK(&F, 445u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh50kc23mi3vj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)112ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_th2d9c7aju8hp t13 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_9h0rhbh, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 96));
            gm_closure t22 = t14;
            gm_node *ccx23 = t22.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_smooaouqmm8dj));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_si4l87dl4tpkq));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(7));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_ssk4a01soguqj));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(6));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t18), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(2));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t19) + 8) != 0) {
                gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t19)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) != 0) {
                gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t20)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t21) + 8) != 0) {
                gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t21)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(1));
            gm_f_fn_9h0rhbh(t22.env, ccx23, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t32 = F.l2;
        if (!t32) goto ie31;
        {
            void * t34 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t34);
        }
        {
            gm_t9qkh0rsooc8d t35;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t35);
            goto gm_fdone;
        }
        goto ix31;
ie31:;
ix31:;
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
inline __attribute__((aligned(4))) void gm_f_format_rvu96av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rvu96av F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0iioal6odpu7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_oqlb297, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s9dh9d8gs6a16));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
            gm_f_fn_oqlb297(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_oqlb297(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
