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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { void * a[3]; } gm_thmgeksj5pdth;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 a[27]; } gm_tlg1vmfbpbvtq;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; gm_tvdg94bk7aa22 f2; gm_tvdg94bk7aa22 f3; } gm_t0v9ekh8mobe3;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tiilmfd16v3fc v1; } f1; } gm_t716e5hdn64ba;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { uint64_t f0; gm_tv0k2vg3054a6 f1; } gm_tdvhflnc2f5o8;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
typedef struct { gm_t5v0c8ia6a5mo f0; uint8_t f1; uint8_t pe[7]; } gm_tj8m6gmlgjilo;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlg1vmfbpbvtq) == 432 && _Alignof(gm_tlg1vmfbpbvtq) == 8 && offsetof(gm_tlg1vmfbpbvtq, a) == 0, "layout of `[27]string`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t0v9ekh8mobe3) == 48 && _Alignof(gm_t0v9ekh8mobe3) == 4 && offsetof(gm_t0v9ekh8mobe3, f0) == 0 && offsetof(gm_t0v9ekh8mobe3, f1) == 12 && offsetof(gm_t0v9ekh8mobe3, f2) == 24 && offsetof(gm_t0v9ekh8mobe3, f3) == 36, "layout of `(Builtin, Builtin, Builtin, Builtin)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t716e5hdn64ba) == 32 && _Alignof(gm_t716e5hdn64ba) == 8 && offsetof(gm_t716e5hdn64ba, f0) == 0 && offsetof(gm_t716e5hdn64ba, f1) == 8, "layout of `EffectNamed`");
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tdvhflnc2f5o8) == 56 && _Alignof(gm_tdvhflnc2f5o8) == 8 && offsetof(gm_tdvhflnc2f5o8, f0) == 0 && offsetof(gm_tdvhflnc2f5o8, f1) == 8, "layout of `(i: usize, x: OpInfo)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_tj8m6gmlgjilo) == 64 && _Alignof(gm_tj8m6gmlgjilo) == 8 && offsetof(gm_tj8m6gmlgjilo, f0) == 0 && offsetof(gm_tj8m6gmlgjilo, f1) == 56, "layout of `?ParamInfo`");
typedef struct { gm_t716e5hdn64ba f0; uint8_t f1; uint8_t pe[7]; } gm_tlbrdhah49ci5;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tlbrdhah49ci5) == 40 && _Alignof(gm_tlbrdhah49ci5) == 8 && offsetof(gm_tlbrdhah49ci5, f0) == 0 && offsetof(gm_tlbrdhah49ci5, f1) == 32, "layout of `?EffectNamed`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adjust_receiver_2j7250l {
    gm_frame h;
    void * ret;
};
struct gm_fr_arity_vmamjfj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bits_cast_0kaubje {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_a7cjd7d {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* vals */
    void * ret;
};
struct gm_fr_builtin_method_jist12a {
    gm_frame h;
    gm_t53at80pfdus3 l10; /* k */
    void * ret;
};
struct gm_fr_check_from_tag_g3el2co {
    gm_frame h;
    gm_tckdobunaulee l7; /* v */
    void * ret;
};
struct gm_fr_collection_method_1a7d15p {
    gm_frame h;
    gm_t4gut2c9fj1kt l28; /* d */
    void * ret;
};
struct gm_fr_core_fn_uts2vhe {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_effect_named_b8afdbp {
    gm_frame h;
    gm_tlbrdhah49ci5 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_emit_with_continuation_n_lv5ofqt {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d0 */
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
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
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_finish_call_kjknkfg {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    void * ret;
};
struct gm_fr_fits_receiver_1qqamgd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_cast_mismatch_e9s21md {
    gm_frame h;
    void * l3; /* tys */
    gm_closure l4; /* pointer */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_u4oqjin {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_method_call_hebo20o {
    gm_frame h;
    gm_t0mglksrkfa5e l18; /* f */
    gm_t4gut2c9fj1kt l25; /* d */
    void * ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_module_method_cb3lmfk {
    gm_frame h;
    gm_tqqhoivahirr0 l14; /* sig */
    gm_tj8m6gmlgjilo l15; /* first */
    gm_t5v0c8ia6a5mo l18; /* p */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_t5v0c8ia6a5mo l21; /* fp */
    gm_trqfvmjtt4sij l24; /* vals */
    void * ret;
};
struct gm_fr_numeric_conversion_1ovq7at {
    gm_frame h;
    void * ret;
};
struct gm_fr_numeric_method_jo5uq0l {
    gm_frame h;
    void * ret;
};
struct gm_fr_numeric_pair_54urfaa {
    gm_frame h;
    void * ret;
};
struct gm_fr_op_method_82pb0m6 {
    gm_frame h;
    gm_tammvh1iespi3 l13; /* info */
    gm_tv0k2vg3054a6 l17; /* o */
    gm_trqfvmjtt4sij l22; /* vals */
    void * ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_with_qfvlipt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_perform_named_h7gr9vi {
    gm_frame h;
    gm_tammvh1iespi3 l9; /* info */
    gm_tv0k2vg3054a6 l13; /* o */
    void * ret;
};
struct gm_fr_pointer_cast_6siqq5h {
    gm_frame h;
    void * l5; /* tys */
    gm_closure l7; /* kind */
    gm_t53at80pfdus3 l13; /* fa */
    gm_t53at80pfdus3 l15; /* fb */
    gm_tddtjfggdat0d l16; /* a */
    gm_tddtjfggdat0d l17; /* b */
    void * ret;
};
struct gm_fr_pointer_gv6lvmk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_1qdglea {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_same_repr_830dsa9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
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
struct gm_fr_slice_fn_np9f5vf {
    gm_frame h;
    void * ret;
};
struct gm_fr_takes_receiver_uhu9min {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_type_arg_grgffgp {
    gm_frame h;
    gm_tc3oht4ksth9d l4; /* e */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsigned_of_n48qlse {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_arg_i5l23i1 {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* e */
    void * ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adjust_receiver_2j7250l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, const void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_auto_deref_vdb1hdn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_bits_cast_0kaubje(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_builtin_method_jist12a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_check_from_tag_g3el2co(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_collection_method_1a7d15p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_with_continuation_n_lv5ofqt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_fits_receiver_1qqamgd(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_fn_cast_mismatch_e9s21md(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_format_0cb2lu1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4frhu1b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_560qv19(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6pukbnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7b9kq2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7f83ds0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8fu7rb8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ahnpa17(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_cpioo77(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dir32ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ercsatq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fu8jpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hpnf26s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ilcl0ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jfku65e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lsckir6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mpl2gek(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nf4pnfb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ohgvj26(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p66k75v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p9hcrrd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pe22rd6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qe2mgnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qe94i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rn1vtpg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uesfu03(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_up1o5ed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vtr1b5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kind_u4oqjin(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_module_method_cb3lmfk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_numeric_conversion_1ovq7at(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_numeric_method_jo5uq0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4, uint8_t a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) void * gm_f_numeric_pair_54urfaa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_op_method_82pb0m6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, uint64_t a4, const void * a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_param_with_qfvlipt(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_as_9ncunsc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_pointer_cast_6siqq5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_pointer_gv6lvmk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_1qdglea(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_same_repr_830dsa9(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_slice_fn_np9f5vf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_from_vrsguuh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_free_9554eha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_soa_make_5imjrl1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_takes_receiver_uhu9min(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unsigned_of_n48qlse(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_kdoa98a1tm0[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s1mud7cvvuuon[4];
extern const uint8_t gm_s2gpk5q7mg4o8[5];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s398novfl4jrd[5];
extern const uint8_t gm_s3e818t2qbjbh[5];
extern const uint8_t gm_s3hqs40g2kokp[9];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s42aeamnkrchf[31];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s541nrvershlm[7];
extern const uint8_t gm_s55kqbt4f7gau[4];
extern const uint8_t gm_s5iv4icrhn1vp[19];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s6a2c11qu9scr[13];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s7f68g16j7a3o[6];
extern const uint8_t gm_s7moa4u0q6e5d[8];
extern const uint8_t gm_s7rl9plavbrfo[17];
extern const uint8_t gm_s7u0p3hthk80r[9];
extern const uint8_t gm_s7uql3407tind[10];
extern const uint8_t gm_s8m12els4res5[26];
extern const uint8_t gm_s9ahj85e3uamt[12];
extern const uint8_t gm_sasg9nroe4gk1[4];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_sd2iiq8cuaks3[5];
extern const uint8_t gm_selatfetsf85i[10];
extern const uint8_t gm_sfr85ju00e9hk[138];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sg7of7eks8a5u[7];
extern const uint8_t gm_sgb0oed9c3voo[5];
extern const uint8_t gm_sgemqs85b1iol[9];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_shb5mqv6etf1t[5];
extern const uint8_t gm_shne2m0cj5kgi[6];
extern const uint8_t gm_sjb60mo5hejk2[6];
extern const uint8_t gm_sk6ueggk5ji6v[10];
extern const uint8_t gm_skgcaidqnnhjr[11];
extern const uint8_t gm_skj7cpsc7j6g9[5];
extern const uint8_t gm_sm139pecc5n3p[6];
extern const uint8_t gm_sm1kt2tr7m23b[4];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_sn6gnsehoh5ru[7];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_snlqf7m58jjdu[5];
extern const uint8_t gm_soefp7bosmvt1[12];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcngug8co99ok1[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srk226j8lb4rq[9];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_ss226ibsv86g3[15];
extern const uint8_t gm_ss3f61htk015v[7];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssc48vnvhmmnh[9];
extern const uint8_t gm_st4r3sao0kja5[6];
extern const uint8_t gm_st88sqv2u9dqh[8];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eq929lennfd3(a, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)21) return gm_eqehhdtpeti1(a + 4, b + 4);
    if (ta == (uint8_t)68) return gm_eqov4e3mbi0f(a + 4, b + 4);
    if (ta == (uint8_t)72) return gm_eqtje7cegoml(a + 4, b + 4);
    if (ta == (uint8_t)79) return gm_eq0lfe1s2pin(a + 4, b + 4);
    if (ta == (uint8_t)80) return gm_eq467d5cm1d5(a + 4, b + 4);
    if (ta == (uint8_t)81) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_math_vector_parts_49c7865((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_tb7u19loo3j87 t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (!t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_vector_parts_crrfopm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_tb7u19loo3j87 t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 133u);
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
        gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
__attribute__((aligned(4))) void * gm_f_adjust_receiver_2j7250l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, const void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_adjust_receiver_2j7250l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_t88rcucg1b7uk gm_l3; /* param */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    uint8_t gm_l9; /* m */
    uint8_t gm_l10; /* c1 */
    gm_t88rcucg1b7uk gm_l11; /* pointee */
    uint8_t gm_l12; /* c2 */
    gm_t88rcucg1b7uk gm_l13; /* p2 */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    GM_STACK_CHECK(&F, 1633u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t6 = gm_l6;
        gm_t88rcucg1b7uk t7 = gm_l3;
        gm_t88rcucg1b7uk t8 = gm_l2;
        uint8_t t9 = gm_f_fits_receiver_1qqamgd(t6, GM_CONV(gm_csguankko4lmhd, t7), GM_CONV(gm_csguankko4lmhd, t8));
        if (!t9) goto ie5;
        {
            void * t11 = gm_l1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t14 = gm_l6;
        gm_t88rcucg1b7uk t15 = gm_l3;
        gm_t53at80pfdus3 t16;
        gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
        gm_t53at80pfdus3 t17 = t16;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t18 != ((uint8_t)8ULL)) goto ie13;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)&t17) + 8) + 0));
        gm_l7 = t19;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t17) + 8) + 4));
        gm_l8 = t20;
        void * t21 = gm_l6;
        gm_t88rcucg1b7uk t22 = gm_l8;
        gm_t88rcucg1b7uk t23 = gm_l2;
        uint8_t t24 = gm_f_fits_receiver_1qqamgd(t21, GM_CONV(gm_csguankko4lmhd, t22), GM_CONV(gm_csguankko4lmhd, t23));
        if (!t24) goto ie13;
        {
            void * t27 = gm_l0;
            void * t28 = gm_l1;
            gm_tqs5p4n2tq19v t29 = gm_f_place_mutability_0tlm4g8(t27, t28);
            gm_tqs5p4n2tq19v t30 = t29;
            if (!((((uint8_t *)&t30))[1] != 0)) goto ie26;
            uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t30));
            gm_l9 = t31;
            {
                uint8_t t34 = gm_l9;
                uint8_t t35 = (uint8_t)!t34;
                if (!t35) goto ie33;
                uint8_t t36 = gm_l7;
                uint8_t t37 = (uint8_t)!t36;
                if (!t37) goto ie33;
                {
                    void * t39 = gm_l0;
                    void * t40 = gm_l1;
                    gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
                    gm_tnpifv20irnph t43;
                    GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
                    gm_tabb710d9mq2e t44;
                    gm_f_format_ilcl0ue((void *)&t44, (void *)NULL, cx, NULL, t43);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t45 = t44;
                    if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcngug8co99ok1, 488, 54);
                    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                    gm_b16_8 t47 = t46;
                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                    gm_b0_1 t49 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t39, t41, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                void * t50 = gm_l6;
                uint8_t t51 = gm_l7;
                gm_t88rcucg1b7uk t52 = gm_l2;
                gm_t88rcucg1b7uk t53 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t50, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t54 = gm_l1;
                gm_teti411eujvf4 t55;
                memset(&t55, 0, sizeof t55);
                GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)24ULL));
                GM_ST(void *, ((uint8_t *)&t55 + 8), t54);
                gm_tl874t2atnlh3 t56 = gm_l5;
                void * t57 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t53, &t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t58 = 0;
                F.ret = t57;
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            uint8_t t60 = gm_l7;
            if (!t60) goto ie59;
            {
                void * t62 = gm_l6;
                gm_t88rcucg1b7uk t63 = gm_l2;
                gm_t88rcucg1b7uk t64 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t62, ((uint8_t)1), t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t65 = gm_l1;
                gm_teti411eujvf4 t66;
                memset(&t66, 0, sizeof t66);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)25ULL));
                GM_ST(void *, ((uint8_t *)&t66 + 8), t65);
                gm_tl874t2atnlh3 t67 = gm_l5;
                void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t64, &t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t69 = 0;
                F.ret = t68;
                goto gm_fret;
            }
            goto ix59;
ie59:;
ix59:;
        }
        {
            void * t70 = gm_l0;
            void * t71 = gm_l1;
            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
            gm_tnpifv20irnph t74;
            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
            gm_tabb710d9mq2e t75;
            gm_f_format_lsckir6((void *)&t75, (void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t76 = t75;
            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_srcngug8co99ok1, 492, 29);
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
            gm_b16_8 t78 = t77;
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_b0_1 t80 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t70, t72, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t81 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t83 = gm_l6;
        gm_t88rcucg1b7uk t84 = gm_l3;
        gm_t53at80pfdus3 t85;
        gm_f_kind_jcnt4hm((void *)&t85, t83, GM_CONV(gm_csguankko4lmhd, t84));
        gm_t53at80pfdus3 t86 = t85;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
        if (t87 != ((uint8_t)8ULL)) goto ie82;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)&t86) + 8) + 0));
        gm_l10 = t88;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t86) + 8) + 4));
        gm_l11 = t89;
        void * t90 = gm_l6;
        gm_t88rcucg1b7uk t91 = gm_l2;
        gm_t53at80pfdus3 t92;
        gm_f_kind_jcnt4hm((void *)&t92, t90, GM_CONV(gm_csguankko4lmhd, t91));
        gm_t53at80pfdus3 t93 = t92;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t94 != ((uint8_t)8ULL)) goto ie82;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)&t93) + 8) + 0));
        gm_l12 = t95;
        gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t93) + 8) + 4));
        gm_l13 = t96;
        void * t97 = gm_l6;
        gm_t88rcucg1b7uk t98 = gm_l11;
        gm_t88rcucg1b7uk t99 = gm_l13;
        uint8_t t100 = gm_f_fits_receiver_1qqamgd(t97, GM_CONV(gm_csguankko4lmhd, t98), GM_CONV(gm_csguankko4lmhd, t99));
        if (!t100) goto ie82;
        {
            uint8_t t103 = gm_l10;
            if (!t103) goto ie102;
            {
                void * t105 = gm_l0;
                void * t106 = gm_l1;
                void * t107 = gm_l6;
                gm_t88rcucg1b7uk t108 = gm_l13;
                gm_t88rcucg1b7uk t109 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t107, ((uint8_t)1), t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t110 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t105, t106, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t111 = 0;
                F.ret = t110;
                goto gm_fret;
            }
            goto ix102;
ie102:;
ix102:;
        }
        {
            uint8_t t113 = gm_l12;
            uint8_t t114 = (uint8_t)!t113;
            if (!t114) goto ie112;
            {
                void * t116 = gm_l1;
                uint8_t t117 = 0;
                F.ret = t116;
                goto gm_fret;
            }
            goto ix112;
ie112:;
ix112:;
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        void * t119 = gm_l6;
        gm_t88rcucg1b7uk t120 = gm_l2;
        gm_t53at80pfdus3 t121;
        gm_f_kind_jcnt4hm((void *)&t121, t119, GM_CONV(gm_csguankko4lmhd, t120));
        gm_t53at80pfdus3 t122 = t121;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&t122) + 0));
        if (t123 != ((uint8_t)8ULL)) goto ie118;
        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t122) + 8) + 4));
        gm_l14 = t124;
        void * t125 = gm_l6;
        gm_t88rcucg1b7uk t126 = gm_l3;
        gm_t88rcucg1b7uk t127 = gm_l14;
        uint8_t t128 = gm_f_fits_receiver_1qqamgd(t125, GM_CONV(gm_csguankko4lmhd, t126), GM_CONV(gm_csguankko4lmhd, t127));
        if (!t128) goto ie118;
        {
            gm_t88rcucg1b7uk t130 = gm_l14;
            void * t131 = gm_l1;
            gm_teti411eujvf4 t132;
            memset(&t132, 0, sizeof t132);
            GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t132 + 8), t131);
            gm_tl874t2atnlh3 t133 = gm_l5;
            void * t134 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t130, &t132, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t135 = 0;
            F.ret = t134;
            goto gm_fret;
        }
        goto ix118;
ie118:;
ix118:;
    }
    {
        void * t136 = gm_l0;
        gm_tl874t2atnlh3 t137 = gm_l5;
        gm_t6j6smj7qoctu t138;
        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t138 + 0, t139);
        void * t140 = gm_l0;
        gm_t88rcucg1b7uk t141 = gm_l2;
        gm_b16_8 t142 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t140, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t138 + 16, t142);
        gm_tabb710d9mq2e t143;
        gm_f_format_nf4pnfb((void *)&t143, (void *)NULL, cx, NULL, &t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t144 = t143;
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 8, gm_srcngug8co99ok1, 500, 20);
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t144) + 8));
        gm_b16_8 t146 = t145;
        gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
        gm_b0_1 t148 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t136, t137, t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t149 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_arity_vmamjfj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = gm_l2;
        uint8_t t5 = (!(t3 == t4));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_tl874t2atnlh3 t8 = gm_l4;
            gm_tq3m5qgvkpje8 t9;
            gm_b16_8 t10 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t10);
            uint64_t t11 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t9 + 16, t11);
            gm_b16_8 t12;
            uint64_t t14 = gm_l2;
            uint8_t t15 = ((t14 == ((uint64_t)1ULL)));
            if (!t15) goto ie13;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
            gm_b16_8 t18 = t17;
            t12 = t18;
            goto ix13;
ie13:;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s03pmf867dcsr));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_b16_8 t21 = t20;
            t12 = t21;
ix13:;
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 24, t12);
            gm_tabb710d9mq2e t22;
            gm_f_format_ercsatq((void *)&t22, (void *)NULL, cx, NULL, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcngug8co99ok1, 88, 24);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_b0_1 t27 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t7, t8, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t28 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t29 = 0;
        F.ret = ((uint8_t)1);
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
__attribute__((aligned(4))) void * gm_f_bits_cast_0kaubje(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_bits_cast_0kaubje F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* src */
    uint64_t gm_l6; /* a */
    uint64_t gm_l7; /* b */
    gm_tn3r4rvlh0rsv gm_l8; /* <temporary array> */
    GM_STACK_CHECK(&F, 902u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l2;
        uint8_t t15 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie12;
        {
            void * t18 = gm_l0;
            gm_tl874t2atnlh3 t19 = gm_l3;
            void * t20 = gm_l0;
            gm_t88rcucg1b7uk t21 = gm_l2;
            gm_b16_8 t22 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
            gm_tabb710d9mq2e t24;
            gm_f_format_ohgvj26((void *)&t24, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcngug8co99ok1, 452, 24);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_b0_1 t29 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t19, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix12;
ie12:;
        void * t31 = gm_l4;
        gm_t88rcucg1b7uk t32 = gm_l5;
        uint8_t t33 = gm_f_has_infer_3o82a0b(t31, GM_CONV(gm_csguankko4lmhd, t32));
        uint8_t t34 = (uint8_t)!t33;
        if (!t34) goto ie30;
        void * t35 = gm_l4;
        gm_t88rcucg1b7uk t36 = gm_l5;
        uint8_t t37 = gm_f_has_param_u9t0efs(t35, GM_CONV(gm_csguankko4lmhd, t36));
        uint8_t t38 = (uint8_t)!t37;
        if (!t38) goto ie30;
        void * t39 = gm_l4;
        gm_t88rcucg1b7uk t40 = gm_l2;
        uint8_t t41 = gm_f_has_param_u9t0efs(t39, GM_CONV(gm_csguankko4lmhd, t40));
        uint8_t t42 = (uint8_t)!t41;
        if (!t42) goto ie30;
        {
            void * t44 = gm_l0;
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
            gm_t88rcucg1b7uk t46 = gm_l5;
            gm_tbccvikg6b96e t47;
            gm_f_layout_a5ovnrm((void *)&t47, (void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t48 = t47;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
            gm_l6 = t49;
        }
        {
            void * t50 = gm_l0;
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
            gm_t88rcucg1b7uk t52 = gm_l2;
            gm_tbccvikg6b96e t53;
            gm_f_layout_a5ovnrm((void *)&t53, (void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t54 = t53;
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&t54) + 0));
            gm_l7 = t55;
        }
        {
            uint64_t t57 = gm_l6;
            uint64_t t58 = gm_l7;
            uint8_t t59 = (!(t57 == t58));
            if (!t59) goto ie56;
            {
                void * t61 = gm_l0;
                gm_tl874t2atnlh3 t62 = gm_l3;
                gm_thddbqb6snabm t63;
                void * t64 = gm_l0;
                gm_t88rcucg1b7uk t65 = gm_l5;
                gm_b16_8 t66 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t66);
                uint64_t t67 = gm_l6;
                GM_ST(uint64_t, (uint8_t *)&t63 + 16, t67);
                void * t68 = gm_l0;
                gm_t88rcucg1b7uk t69 = gm_l2;
                gm_b16_8 t70 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t63 + 24, t70);
                uint64_t t71 = gm_l7;
                GM_ST(uint64_t, (uint8_t *)&t63 + 40, t71);
                gm_tabb710d9mq2e t72;
                gm_f_format_hpnf26s((void *)&t72, (void *)NULL, cx, NULL, &t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t73 = t72;
                if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcngug8co99ok1, 457, 28);
                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                gm_b16_8 t75 = t74;
                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                gm_b0_1 t77 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t61, t62, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix56;
ie56:;
ix56:;
        }
        goto ix30;
ie30:;
ix30:;
ix12:;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t78 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t79 = gm_l2;
        {
            void * t81 = gm_l1;
            gm_tn3r4rvlh0rsv t82;
            GM_ST(void *, ((uint8_t *)&t82 + 0), t81);
            gm_l8 = t82;
        }
        void * t83 = ((uint8_t *)&gm_l8);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 460, 30);
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(((uint8_t *)t83 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t85 = t84;
        gm_tl874t2atnlh3 t86 = gm_l3;
        void * t87 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t78, t79, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t88 = 0;
        F.ret = t87;
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
inline __attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_builtin_a7cjd7d F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l7; /* a */
    GM_STACK_CHECK(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_vp1djv1_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 8));
                    void * t14 = t13;
                    void * t17 = GM_LD(void *, ((uint8_t *)&t14));
                    gm_l7 = t17;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l4);
                        void * t20 = gm_l7;
                        gm_tftb1odc739t5 t21 = gm_f_push_d943vgp_2(t19, &(*(gm_t69m56meqefs8 *)((uint8_t *)t20)));
                        gm_tftb1odc739t5 t22 = t21;
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcngug8co99ok1, 96, 21);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 96, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l6;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l6 = t28;
                }
            }
lb4:;
        }
    }
    {
        gm_t88rcucg1b7uk t29 = gm_l1;
        gm_tt8o5kebou62r t30;
        memset((uint8_t *)&t30 + 12, 0, 4);
        gm_tvdg94bk7aa22 t31 = gm_l0;
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t30 + 0, t31);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t32);
        gm_teti411eujvf4 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t33 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t33 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t33 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t33 + 8), t30);
        gm_tl874t2atnlh3 t34 = gm_l3;
        void * t35 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t29, &t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = 0;
        F.ret = t35;
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
__attribute__((aligned(4))) void * gm_f_builtin_method_jist12a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_builtin_method_jist12a F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_t08olq97rscn3 gm_l3; /* name */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_b16_8 gm_l7; /* n */
    uint8_t gm_l8; /* is_int */
    uint8_t gm_l9; /* is_float */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    uint8_t gm_l12; /* is_const */
    gm_t88rcucg1b7uk gm_l13; /* pointee */
    gm_t88rcucg1b7uk gm_l14; /* outer */
    gm_t88rcucg1b7uk gm_l15; /* target */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_tn3r4rvlh0rsv gm_l17; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_tep2drsl9408b gm_l19; /* uv */
    gm_tn3r4rvlh0rsv gm_l20; /* <temporary array> */
    GM_STACK_CHECK(&F, 2034u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
        gm_l7 = t5;
    }
    {
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        void * t8 = gm_l6;
        gm_t88rcucg1b7uk t9 = gm_l2;
        uint8_t t10 = gm_f_is_integer_8d4f9ek(t8, GM_CONV(gm_csguankko4lmhd, t9));
        if (!t10) {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 96);
            void * t13 = gm_l6;
            gm_t88rcucg1b7uk t14 = gm_l2;
            gm_taari1s0m51a7 t15 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t17;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[1] = 1;
            gm_taari1s0m51a7 t18 = t15;
            gm_taari1s0m51a7 t19 = t17;
            uint8_t t20 = (gm_eq5307j5k9ms(((uint8_t *)&t18), ((uint8_t *)&t19)));
            t7 = t20;
        } else t7 = 1;
        if (!t7) {
            void * t21 = gm_l0;
            gm_t88rcucg1b7uk t22 = gm_l2;
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)3ULL));
            uint8_t t24 = gm_f_param_with_qfvlipt(t21, GM_CONV(gm_csguankko4lmhd, t22), t23);
            t6 = t24;
        } else t6 = 1;
        gm_l8 = t6;
    }
    {
        uint8_t t25 = 0;
        uint8_t t26 = 0;
        void * t27 = gm_l6;
        gm_t88rcucg1b7uk t28 = gm_l2;
        gm_t53at80pfdus3 t29;
        gm_f_kind_jcnt4hm((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
        gm_t53at80pfdus3 t30 = t29;
        uint8_t t31;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t34 != ((uint8_t)3ULL)) goto mn33;
        t31 = ((uint8_t)1);
        goto me32;
mn33:;
        t31 = ((uint8_t)0);
        goto me32;
mn35:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 106, 17);
me32:;
        if (!t31) {
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 96);
            void * t38 = gm_l6;
            gm_t88rcucg1b7uk t39 = gm_l2;
            gm_taari1s0m51a7 t40 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t37, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t41;
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t42;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[1] = 1;
            gm_taari1s0m51a7 t43 = t40;
            gm_taari1s0m51a7 t44 = t42;
            uint8_t t45 = (gm_eq5307j5k9ms(((uint8_t *)&t43), ((uint8_t *)&t44)));
            t26 = t45;
        } else t26 = 1;
        if (!t26) {
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l2;
            gm_t9qkh0rsooc8d t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)5ULL));
            uint8_t t49 = gm_f_param_with_qfvlipt(t46, GM_CONV(gm_csguankko4lmhd, t47), t48);
            t25 = t49;
        } else t25 = 1;
        gm_l9 = t25;
    }
    {
        uint8_t t51 = 0;
        uint8_t t52 = gm_l8;
        if (!t52) {
            uint8_t t53 = gm_l9;
            t51 = t53;
        } else t51 = 1;
        if (!t51) goto ie50;
        {
            void * t55 = gm_l0;
            void * t56 = gm_l1;
            gm_t88rcucg1b7uk t57 = gm_l2;
            gm_b16_8 t58 = gm_l7;
            uint8_t t59 = gm_l8;
            uint8_t t60 = gm_l9;
            gm_b16_8 t61 = gm_l4;
            gm_tl874t2atnlh3 t62 = gm_l5;
            void * t63 = gm_f_numeric_method_jo5uq0l((void *)NULL, cx, NULL, t55, t56, t57, t58, t59, t60, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t64 = 0;
            F.ret = t63;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        void * t65 = gm_l6;
        gm_t88rcucg1b7uk t66 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l10, t65, GM_CONV(gm_csguankko4lmhd, t66));
    }
    {
        uint8_t t68 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_s3e818t2qbjbh, 4) == 0));
        if (!t68) goto ie67;
        uint8_t t69;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t77) { case ((uint8_t)0ULL): goto ma75; case ((uint8_t)1ULL): goto ma75; case ((uint8_t)2ULL): goto ma75; case ((uint8_t)3ULL): goto ma75; case ((uint8_t)4ULL): goto ma75; case ((uint8_t)5ULL): goto ma75; case ((uint8_t)6ULL): goto ma75; case ((uint8_t)7ULL): goto ma73; case ((uint8_t)8ULL): goto ma71; case ((uint8_t)9ULL): goto ma72; case ((uint8_t)10ULL): goto ma75; case ((uint8_t)11ULL): goto ma75; case ((uint8_t)12ULL): goto ma75; case ((uint8_t)13ULL): goto ma75; case ((uint8_t)14ULL): goto ma75; case ((uint8_t)15ULL): goto ma75; case ((uint8_t)16ULL): goto ma74; case ((uint8_t)17ULL): goto ma75; case ((uint8_t)18ULL): goto ma75; case ((uint8_t)19ULL): goto ma75; case ((uint8_t)20ULL): goto ma75; case ((uint8_t)21ULL): goto ma75; case ((uint8_t)22ULL): goto ma75; case ((uint8_t)23ULL): goto ma75; case ((uint8_t)24ULL): goto ma75; case ((uint8_t)25ULL): goto ma75; case ((uint8_t)26ULL): goto ma75; default: goto ma75; }
ma71:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t79 != ((uint8_t)8ULL)) goto mn78;
        t69 = ((uint8_t)1);
        goto me70;
mn78:;
ma72:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t81 != ((uint8_t)9ULL)) goto mn80;
        t69 = ((uint8_t)1);
        goto me70;
mn80:;
ma73:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t83 != ((uint8_t)7ULL)) goto mn82;
        t69 = ((uint8_t)1);
        goto me70;
mn82:;
ma74:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t85 != ((uint8_t)16ULL)) goto mn84;
        t69 = ((uint8_t)1);
        goto me70;
mn84:;
ma75:;
        t69 = ((uint8_t)0);
        goto me70;
mn86:;
ma76:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 109, 24);
me70:;
        if (!t69) goto ie67;
        {
            {
                void * t89 = gm_l0;
                gm_b16_8 t90 = gm_l4;
                gm_b16_8 t91 = gm_l7;
                gm_tl874t2atnlh3 t92 = gm_l5;
                uint8_t t93 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t89, t90, ((uint64_t)0ULL), t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)9ULL));
            void * t95 = gm_l6;
            gm_t88rcucg1b7uk t96 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t98 = gm_l1;
                gm_tn3r4rvlh0rsv t99;
                GM_ST(void *, ((uint8_t *)&t99 + 0), t98);
                gm_l11 = t99;
            }
            void * t100 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 111, 47);
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(((uint8_t *)t100 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t102 = t101;
            gm_tl874t2atnlh3 t103 = gm_l5;
            void * t104 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t94, t96, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t105 = 0;
            F.ret = t104;
            goto gm_fret;
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
        uint8_t t107 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_snlqf7m58jjdu, 4) == 0));
        if (!t107) goto ie106;
        uint8_t t108;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t117) { case ((uint8_t)0ULL): goto ma115; case ((uint8_t)1ULL): goto ma115; case ((uint8_t)2ULL): goto ma115; case ((uint8_t)3ULL): goto ma115; case ((uint8_t)4ULL): goto ma115; case ((uint8_t)5ULL): goto ma115; case ((uint8_t)6ULL): goto ma115; case ((uint8_t)7ULL): goto ma112; case ((uint8_t)8ULL): goto ma110; case ((uint8_t)9ULL): goto ma111; case ((uint8_t)10ULL): goto ma115; case ((uint8_t)11ULL): goto ma115; case ((uint8_t)12ULL): goto ma115; case ((uint8_t)13ULL): goto ma114; case ((uint8_t)14ULL): goto ma115; case ((uint8_t)15ULL): goto ma115; case ((uint8_t)16ULL): goto ma113; case ((uint8_t)17ULL): goto ma115; case ((uint8_t)18ULL): goto ma115; case ((uint8_t)19ULL): goto ma115; case ((uint8_t)20ULL): goto ma115; case ((uint8_t)21ULL): goto ma115; case ((uint8_t)22ULL): goto ma115; case ((uint8_t)23ULL): goto ma115; case ((uint8_t)24ULL): goto ma115; case ((uint8_t)25ULL): goto ma115; case ((uint8_t)26ULL): goto ma115; default: goto ma115; }
ma110:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t119 != ((uint8_t)8ULL)) goto mn118;
        t108 = ((uint8_t)1);
        goto me109;
mn118:;
ma111:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t121 != ((uint8_t)9ULL)) goto mn120;
        t108 = ((uint8_t)1);
        goto me109;
mn120:;
ma112:;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t123 != ((uint8_t)7ULL)) goto mn122;
        t108 = ((uint8_t)1);
        goto me109;
mn122:;
ma113:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t125 != ((uint8_t)16ULL)) goto mn124;
        t108 = ((uint8_t)1);
        goto me109;
mn124:;
ma114:;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t127 != ((uint8_t)13ULL)) goto mn126;
        t108 = ((uint8_t)1);
        goto me109;
mn126:;
ma115:;
        t108 = ((uint8_t)0);
        goto me109;
mn128:;
ma116:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 113, 24);
me109:;
        if (!t108) goto ie106;
        {
            void * t130 = gm_l0;
            void * t131 = gm_l1;
            gm_t88rcucg1b7uk t132 = gm_l2;
            gm_b16_8 t133 = gm_l4;
            gm_tl874t2atnlh3 t134 = gm_l5;
            void * t135 = gm_f_pointer_cast_6siqq5h((void *)NULL, cx, NULL, t130, t131, t132, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t136 = 0;
            F.ret = t135;
            goto gm_fret;
        }
        goto ix106;
ie106:;
ix106:;
    }
    {
        uint8_t t138 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 18) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_s5iv4icrhn1vp, 18) == 0));
        if (!t138) goto ie137;
        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t139 != ((uint8_t)8ULL)) goto ie137;
        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)&F.l10) + 8) + 0));
        gm_l12 = t140;
        gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l10) + 8) + 4));
        gm_l13 = t141;
        {
            void * t143 = gm_l0;
            gm_b16_8 t144 = gm_l4;
            gm_tl874t2atnlh3 t145 = gm_l5;
            gm_tkn42u7na3t81 t146 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t143, t144, ((uint64_t)0ULL), t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t147 = t146;
            gm_t88rcucg1b7uk t148;
            if (((((uint8_t *)&t147))[4] != 0)) {
                gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t147) + 0));
                t148 = t149;
            } else {
                uint8_t t150 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l14 = t148;
        }
        {
            void * t151 = gm_l6;
            uint8_t t152 = gm_l12;
            gm_t88rcucg1b7uk t153 = gm_l14;
            gm_t88rcucg1b7uk t154 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t151, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t154;
        }
        {
            void * t156 = gm_l0;
            gm_t88rcucg1b7uk t157 = gm_l15;
            void * t158 = gm_l6;
            uint8_t t159 = gm_l12;
            gm_t88rcucg1b7uk t160 = gm_l13;
            gm_t88rcucg1b7uk t161 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t158, t159, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t162 = gm_f_embeds_2jpqm45(t156, GM_CONV(gm_csguankko4lmhd, t157), GM_CONV(gm_csguankko4lmhd, t161));
            uint8_t t163 = (uint8_t)!t162;
            if (!t163) goto ie155;
            {
                void * t165 = gm_l0;
                gm_tl874t2atnlh3 t166 = gm_l5;
                gm_t6j6smj7qoctu t167;
                void * t168 = gm_l0;
                gm_t88rcucg1b7uk t169 = gm_l14;
                gm_b16_8 t170 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t170);
                void * t171 = gm_l0;
                gm_t88rcucg1b7uk t172 = gm_l13;
                gm_b16_8 t173 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t171, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t167 + 16, t173);
                gm_tabb710d9mq2e t174;
                gm_f_format_fu8jpss((void *)&t174, (void *)NULL, cx, NULL, &t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t175 = t174;
                if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 8, gm_srcngug8co99ok1, 119, 28);
                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t175) + 8));
                gm_b16_8 t177 = t176;
                gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                gm_b0_1 t179 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t165, t166, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t180 = gm_l0;
                void * t181 = GM_LD(void *, (((uint8_t *)t180) + 0));
                gm_tl874t2atnlh3 t182 = gm_l5;
                void * t183 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t181, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t184 = 0;
                F.ret = t183;
                goto gm_fret;
            }
            goto ix155;
ie155:;
ix155:;
        }
        {
            gm_t88rcucg1b7uk t185 = gm_l15;
            gm_tmn7n0n9raou5 t186;
            GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)6ULL));
            void * t187 = gm_l1;
            gm_t8jsc4n9vp2ib t188;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t188 + 0), t186);
            GM_ST(void *, ((uint8_t *)&t188 + 8), t187);
            gm_teti411eujvf4 t189;
            memset(&t189, 0, sizeof t189);
            GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t189 + 8), t188);
            gm_tl874t2atnlh3 t190 = gm_l5;
            void * t191 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t185, &t189, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t192 = 0;
            F.ret = t191;
            goto gm_fret;
        }
        goto ix137;
ie137:;
ix137:;
    }
    {
        uint8_t t194 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_ssc48vnvhmmnh, 8) == 0));
        if (!t194) goto ie193;
        uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t195 != ((uint8_t)9ULL)) goto ie193;
        gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l10) + 8) + 4));
        gm_l16 = t196;
        gm_t88rcucg1b7uk t197 = gm_l16;
        void * t198 = gm_l6;
        gm_t88rcucg1b7uk t199 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t200 = t197;
        gm_t88rcucg1b7uk t201 = t199;
        uint8_t t202 = (gm_eq9m6mvtmgde(((uint8_t *)&t200), ((uint8_t *)&t201)));
        if (!t202) goto ie193;
        {
            {
                void * t205 = gm_l0;
                gm_b16_8 t206 = gm_l4;
                gm_b16_8 t207 = gm_l7;
                gm_tl874t2atnlh3 t208 = gm_l5;
                uint8_t t209 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t205, t206, ((uint64_t)0ULL), t207, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t210;
            GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t210 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)60ULL));
            void * t211 = gm_l6;
            gm_t88rcucg1b7uk t212 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t214 = gm_l1;
                gm_tn3r4rvlh0rsv t215;
                GM_ST(void *, ((uint8_t *)&t215 + 0), t214);
                gm_l17 = t215;
            }
            void * t216 = ((uint8_t *)&gm_l17);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 126, 50);
            gm_b16_8 t217;
            GM_ST(void *, (uint8_t *)&t217, (void *)(((uint8_t *)t216 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t218 = t217;
            gm_tl874t2atnlh3 t219 = gm_l5;
            void * t220 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t210, t212, t218, t219);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t221 = 0;
            F.ret = t220;
            goto gm_fret;
        }
        goto ix193;
ie193:;
ix193:;
    }
    {
        uint8_t t223 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_s6vvtrmtnt3j1, 4) == 0));
        if (!t223) goto ie222;
        uint8_t t224 = 0;
        uint8_t t225 = 0;
        void * t226 = gm_l0;
        gm_t88rcucg1b7uk t227 = gm_l2;
        uint8_t t228 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t226, t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t228) {
            uint8_t t229 = ((GM_LD(uint8_t, ((uint8_t *)&F.l10) + 0) == ((uint8_t)4ULL)));
            t225 = t229;
        } else t225 = 1;
        if (!t225) {
            void * t230 = gm_l0;
            void * t231 = GM_LD(void *, (((uint8_t *)t230) + 0));
            gm_t88rcucg1b7uk t232 = gm_l2;
            gm_tkn42u7na3t81 t233 = gm_f_bits_struct_ty_l1kjtap(t231, GM_CONV(gm_csguankko4lmhd, t232));
            gm_tkn42u7na3t81 t234 = t233;
            uint8_t t235;
            if (((((uint8_t *)&t234))[4] != 0)) goto mn237;
            t235 = ((uint8_t)0);
            goto me236;
mn237:;
            t235 = ((uint8_t)1);
            goto me236;
mn238:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 128, 64);
me236:;
            t224 = t235;
        } else t224 = 1;
        if (!t224) goto ie222;
        {
            void * t241 = gm_l0;
            gm_b16_8 t242 = gm_l4;
            gm_b16_8 t243 = gm_l7;
            gm_tl874t2atnlh3 t244 = gm_l5;
            uint8_t t245 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t241, t242, ((uint64_t)1ULL), t243, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t246 = (uint8_t)!t245;
            if (!t246) goto ie240;
            {
                void * t248 = gm_l0;
                void * t249 = GM_LD(void *, (((uint8_t *)t248) + 0));
                gm_tl874t2atnlh3 t250 = gm_l5;
                void * t251 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t249, t250);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t252 = 0;
                F.ret = t251;
                goto gm_fret;
            }
            goto ix240;
ie240:;
ix240:;
        }
        {
            void * t253 = gm_l0;
            gm_b16_8 t254 = gm_l4;
            gm_tl874t2atnlh3 t255 = gm_l5;
            gm_tkn42u7na3t81 t256 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t253, t254, ((uint64_t)0ULL), t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t257 = t256;
            gm_t88rcucg1b7uk t258;
            if (((((uint8_t *)&t257))[4] != 0)) {
                gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t257) + 0));
                t258 = t259;
            } else {
                uint8_t t260 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l18 = t258;
        }
        {
            void * t261 = gm_l0;
            void * t262 = gm_l1;
            gm_t88rcucg1b7uk t263 = gm_l18;
            gm_tl874t2atnlh3 t264 = gm_l5;
            void * t265 = gm_f_bits_cast_0kaubje((void *)NULL, cx, NULL, t261, t262, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t266 = 0;
            F.ret = t265;
            goto gm_fret;
        }
        goto ix222;
ie222:;
ix222:;
    }
    {
        uint8_t t268 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_s420tm9ua7ggg, 3) == 0));
        if (!t268) goto ie267;
        uint8_t t269;
        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t272 != ((uint8_t)17ULL)) goto mn271;
        t269 = ((uint8_t)1);
        goto me270;
mn271:;
        t269 = ((uint8_t)0);
        goto me270;
mn273:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 133, 23);
me270:;
        if (!t269) goto ie267;
        {
            void * t276 = gm_l0;
            void * t277 = GM_LD(void *, (((uint8_t *)t276) + 0));
            gm_t88rcucg1b7uk t278 = gm_l2;
            gm_tdlpv0q69lov1 t279;
            gm_f_union_variants_a7khaqp((void *)&t279, t277, GM_CONV(gm_csguankko4lmhd, t278));
            gm_tdlpv0q69lov1 t280 = t279;
            if (!((((uint8_t *)&t280))[24] != 0)) goto ie275;
            gm_tep2drsl9408b t281 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&t280));
            gm_l19 = t281;
            {
                {
                    void * t284 = gm_l0;
                    gm_b16_8 t285 = gm_l4;
                    gm_b16_8 t286 = gm_l7;
                    gm_tl874t2atnlh3 t287 = gm_l5;
                    uint8_t t288 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t284, t285, ((uint64_t)0ULL), t286, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            {
            }
            {
                gm_tvdg94bk7aa22 t289;
                GM_ST(uint8_t, (uint8_t *)&t289 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t289 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t289 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t289 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)14ULL));
                gm_t88rcucg1b7uk t290 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 16));
                {
                    void * t292 = gm_l1;
                    gm_tn3r4rvlh0rsv t293;
                    GM_ST(void *, ((uint8_t *)&t293 + 0), t292);
                    gm_l20 = t293;
                }
                void * t294 = ((uint8_t *)&gm_l20);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 136, 42);
                gm_b16_8 t295;
                GM_ST(void *, (uint8_t *)&t295, (void *)(((uint8_t *)t294 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t296 = t295;
                gm_tl874t2atnlh3 t297 = gm_l5;
                void * t298 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t289, t290, t296, t297);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t299 = 0;
                F.ret = t298;
                goto gm_fret;
            }
            goto ix275;
ie275:;
ix275:;
        }
        goto ix267;
ie267:;
ix267:;
    }
    {
        void * t300 = gm_l0;
        void * t301 = gm_l1;
        gm_t88rcucg1b7uk t302 = gm_l2;
        gm_b16_8 t303 = gm_l7;
        gm_b16_8 t304 = gm_l4;
        gm_tl874t2atnlh3 t305 = gm_l5;
        void * t306 = gm_f_collection_method_1a7d15p((void *)NULL, cx, NULL, t300, t301, t302, t303, t304, t305);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t307 = 0;
        F.ret = t306;
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
__attribute__((aligned(4))) void * gm_f_check_from_tag_g3el2co(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_from_tag_g3el2co F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* u */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tep2drsl9408b gm_l4; /* uv */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l8; /* x */
    gm_tn3r4rvlh0rsv gm_l9; /* <temporary array> */
    GM_STACK_CHECK(&F, 1015u);
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
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tdlpv0q69lov1 t5;
        gm_f_union_variants_a7khaqp((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tdlpv0q69lov1 t6 = t5;
        gm_tep2drsl9408b t7;
        if (((((uint8_t *)&t6))[24] != 0)) {
            gm_tep2drsl9408b t8 = GM_LD(gm_tep2drsl9408b, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = gm_l3;
                void * t12 = gm_l0;
                gm_t88rcucg1b7uk t13 = gm_l1;
                gm_b16_8 t14 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t15;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
                gm_tabb710d9mq2e t16;
                gm_f_format_qe94i5r((void *)&t16, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcngug8co99ok1, 466, 24);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
                gm_b16_8 t19 = t18;
                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                gm_b0_1 t21 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t10, t11, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            gm_tl874t2atnlh3 t24 = gm_l3;
            void * t25 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        gm_l4 = t7;
    }
    {
        {
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_l5 = t28;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t30 = gm_l6;
                uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t32 = (t30 < t31);
                if (!t32) goto lb29;
                {
                    uint64_t t35 = gm_l6;
                    uint64_t t36 = (uintptr_t)t35;
                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tckdobunaulee t40 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t36 * 72));
                    F.l7 = t40;
                    {
                        uint8_t t43;
                        if ((((((uint8_t *)&F.l7) + 16))[4] != 0)) goto mn45;
                        t43 = ((uint8_t)0);
                        goto me44;
mn45:;
                        t43 = ((uint8_t)1);
                        goto me44;
mn46:;
                        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 470, 12);
me44:;
                        if (!t43) goto ie42;
                        {
                            void * t48 = gm_l0;
                            gm_tl874t2atnlh3 t49 = gm_l3;
                            void * t50 = gm_l0;
                            gm_t88rcucg1b7uk t51 = gm_l1;
                            gm_b16_8 t52 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            gm_tnpifv20irnph t53;
                            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                            gm_tabb710d9mq2e t54;
                            gm_f_format_up1o5ed((void *)&t54, (void *)NULL, cx, NULL, t53);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            gm_tabb710d9mq2e t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srcngug8co99ok1, 471, 28);
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                            gm_b16_8 t57 = t56;
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                            gm_b0_1 t59 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t48, t49, t58);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                        }
                        {
                            goto lb29;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 469, 5);
me38:;
                }
lc29:;
                {
                    uint64_t t61 = gm_l6;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l6 = t64;
                }
            }
gm_ll33:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb29; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc29; }
            }
            goto gm_lad;
lb29:;
        }
    }
    {
        void * t66 = gm_l0;
        gm_b16_8 t67 = gm_l2;
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s66fk044n1n0k));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(8));
        gm_tl874t2atnlh3 t69 = gm_l3;
        uint8_t t70 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t66, t67, ((uint64_t)1ULL), t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = (uint8_t)!t70;
        if (!t71) goto ie65;
        {
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            gm_tl874t2atnlh3 t75 = gm_l3;
            void * t76 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto ix65;
ie65:;
ix65:;
    }
    {
        void * t78 = gm_l0;
        gm_b16_8 t79 = gm_l2;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
        gm_tl874t2atnlh3 t81 = gm_l3;
        void * t82 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t78, t79, ((uint64_t)0ULL), t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t82;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)15ULL));
        void * t84 = gm_l0;
        void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
        void * t86 = (void *)(((uint8_t *)t85) + 8);
        gm_t88rcucg1b7uk t87 = gm_l1;
        gm_t88rcucg1b7uk t88 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t86, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            void * t90 = gm_l8;
            gm_tn3r4rvlh0rsv t91;
            GM_ST(void *, ((uint8_t *)&t91 + 0), t90);
            gm_l9 = t91;
        }
        void * t92 = ((uint8_t *)&gm_l9);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 477, 52);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t92 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t94 = t93;
        gm_tl874t2atnlh3 t95 = gm_l3;
        void * t96 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t83, t88, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t97 = 0;
        F.ret = t96;
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
__attribute__((aligned(4))) void * gm_f_collection_method_1a7d15p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_collection_method_1a7d15p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* arr */
    gm_t88rcucg1b7uk gm_l8; /* p */
    void * gm_l9; /* v */
    gm_tnnjeerolos0a gm_l10; /* kind */
    void * gm_l11; /* base */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint8_t gm_l13; /* is_const */
    uint8_t gm_l14; /* c */
    gm_t88rcucg1b7uk gm_l15; /* e */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* e */
    gm_tqs5p4n2tq19v gm_l18; /* m */
    uint8_t gm_l19; /* c */
    gm_t88rcucg1b7uk gm_l20; /* pointee */
    gm_t88rcucg1b7uk gm_l21; /* e */
    gm_t88rcucg1b7uk gm_l22; /* e */
    void * gm_l23; /* i */
    gm_t5fru0lp11n7t gm_l24; /* <temporary array> */
    gm_t5fru0lp11n7t gm_l25; /* <temporary array> */
    gm_tvdg94bk7aa22 gm_l26; /* op */
    gm_tn3r4rvlh0rsv gm_l27; /* <temporary array> */
    gm_b16_8 gm_l29; /* text */
    GM_STACK_CHECK(&F, 4083u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_l7 = t5;
    }
    {
        void * t7 = gm_l6;
        gm_t88rcucg1b7uk t8 = gm_l2;
        gm_t53at80pfdus3 t9;
        gm_f_kind_jcnt4hm((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_t53at80pfdus3 t10 = t9;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t11 != ((uint8_t)8ULL)) goto ie6;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t10) + 8) + 4));
        gm_l8 = t12;
        {
            gm_t88rcucg1b7uk t14 = gm_l8;
            gm_l7 = t14;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint8_t t16 = 0;
        void * t17 = gm_l6;
        gm_t88rcucg1b7uk t18 = gm_l7;
        uint8_t t19 = gm_f_is_vector_gkq6b88(t17, GM_CONV(gm_csguankko4lmhd, t18));
        if (!t19) {
            void * t20 = gm_l6;
            gm_t88rcucg1b7uk t21 = gm_l7;
            uint8_t t22 = gm_f_is_math_vector_09mtuao(t20, GM_CONV(gm_csguankko4lmhd, t21));
            t16 = t22;
        } else t16 = 1;
        if (!t16) goto ie15;
        {
            void * t24;
            gm_t88rcucg1b7uk t26 = gm_l7;
            gm_t88rcucg1b7uk t27 = gm_l2;
            gm_t88rcucg1b7uk t28 = t26;
            gm_t88rcucg1b7uk t29 = t27;
            uint8_t t30 = (gm_eq9m6mvtmgde(((uint8_t *)&t28), ((uint8_t *)&t29)));
            if (!t30) goto ie25;
            void * t32 = gm_l1;
            void * t33 = t32;
            t24 = t33;
            goto ix25;
ie25:;
            gm_t88rcucg1b7uk t35 = gm_l7;
            void * t36 = gm_l1;
            gm_teti411eujvf4 t37;
            memset(&t37, 0, sizeof t37);
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t37 + 8), t36);
            gm_tl874t2atnlh3 t38 = gm_l5;
            void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t40 = t39;
            t24 = t40;
ix25:;
            gm_l9 = t24;
        }
        {
            void * t42 = gm_l6;
            gm_t88rcucg1b7uk t43 = gm_l7;
            uint8_t t44 = gm_f_is_vector_gkq6b88(t42, GM_CONV(gm_csguankko4lmhd, t43));
            if (!t44) goto ie41;
            {
                void * t46 = gm_l0;
                void * t47 = gm_l9;
                gm_t88rcucg1b7uk t48 = gm_l7;
                gm_b16_8 t49 = gm_l3;
                gm_b16_8 t50 = gm_l4;
                gm_tl874t2atnlh3 t51 = gm_l5;
                void * t52 = gm_f_vector_method_hgn4sb1((void *)NULL, cx, NULL, t46, t47, t48, t49, t50, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            void * t54 = gm_l0;
            void * t55 = gm_l9;
            gm_t88rcucg1b7uk t56 = gm_l7;
            gm_b16_8 t57 = gm_l3;
            gm_b16_8 t58 = gm_l4;
            gm_tl874t2atnlh3 t59 = gm_l5;
            void * t60 = gm_f_math_vector_method_ko2814s((void *)NULL, cx, NULL, t54, t55, t56, t57, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t63 = gm_l6;
        gm_t88rcucg1b7uk t64 = gm_l7;
        gm_t53at80pfdus3 t65;
        gm_f_kind_jcnt4hm((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
        gm_t53at80pfdus3 t66 = t65;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
        if (t67 != ((uint8_t)11ULL)) goto ie62;
        gm_tnnjeerolos0a t68 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t66) + 8) + 12));
        gm_l10 = t68;
        uint8_t t69;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
        if (t72 != ((uint8_t)1ULL)) goto mn71;
        t69 = ((uint8_t)1);
        goto me70;
mn71:;
        t69 = ((uint8_t)0);
        goto me70;
mn73:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 345, 48);
me70:;
        if (!t69) goto ie62;
        {
            void * t75 = gm_l0;
            gm_tl874t2atnlh3 t76 = gm_l5;
            gm_t6j6smj7qoctu t77;
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l7;
            gm_b16_8 t80 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 0, t80);
            gm_b16_8 t81 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 16, t81);
            gm_tabb710d9mq2e t82;
            gm_f_format_p66k75v((void *)&t82, (void *)NULL, cx, NULL, &t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t83 = t82;
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcngug8co99ok1, 346, 24);
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
            gm_b16_8 t85 = t84;
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
            gm_b0_1 t87 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t75, t76, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t88 = gm_l0;
            void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
            gm_tl874t2atnlh3 t90 = gm_l5;
            void * t91 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        void * t93 = gm_l1;
        gm_l11 = t93;
    }
    {
        gm_t88rcucg1b7uk t94 = gm_l2;
        gm_l12 = t94;
    }
    {
        gm_l13 = ((uint8_t)0);
    }
    {
        void * t95 = gm_l6;
        gm_t88rcucg1b7uk t96 = gm_l2;
        gm_t53at80pfdus3 t97;
        gm_f_kind_jcnt4hm((void *)&t97, t95, GM_CONV(gm_csguankko4lmhd, t96));
        gm_t53at80pfdus3 t98 = t97;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t98) + 0));
        switch (t105) { case ((uint8_t)0ULL): goto ma103; case ((uint8_t)1ULL): goto ma103; case ((uint8_t)2ULL): goto ma103; case ((uint8_t)3ULL): goto ma103; case ((uint8_t)4ULL): goto ma103; case ((uint8_t)5ULL): goto ma103; case ((uint8_t)6ULL): goto ma103; case ((uint8_t)7ULL): goto ma103; case ((uint8_t)8ULL): goto ma102; case ((uint8_t)9ULL): goto ma103; case ((uint8_t)10ULL): goto ma100; case ((uint8_t)11ULL): goto ma101; case ((uint8_t)12ULL): goto ma103; case ((uint8_t)13ULL): goto ma103; case ((uint8_t)14ULL): goto ma103; case ((uint8_t)15ULL): goto ma103; case ((uint8_t)16ULL): goto ma103; case ((uint8_t)17ULL): goto ma103; case ((uint8_t)18ULL): goto ma103; case ((uint8_t)19ULL): goto ma103; case ((uint8_t)20ULL): goto ma101; case ((uint8_t)21ULL): goto ma103; case ((uint8_t)22ULL): goto ma103; case ((uint8_t)23ULL): goto ma103; case ((uint8_t)24ULL): goto ma103; case ((uint8_t)25ULL): goto ma103; case ((uint8_t)26ULL): goto ma103; default: goto ma103; }
ma100:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t98) + 0));
        if (t107 != ((uint8_t)10ULL)) goto mn106;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)&t98) + 8) + 0));
        gm_l14 = t108;
        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t98) + 8) + 4));
        gm_l15 = t109;
        {
            gm_t88rcucg1b7uk t111 = gm_l15;
            gm_l12 = t111;
        }
        {
            uint8_t t112 = gm_l14;
            gm_l13 = t112;
        }
        goto me99;
mn106:;
ma101:;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t98) + 0));
        if (t116 != ((uint8_t)11ULL)) goto pa115;
        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t98) + 8) + 8));
        gm_l16 = t117;
        goto po114;
pa115:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t98) + 0));
        if (t119 != ((uint8_t)20ULL)) goto pa118;
        gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t98) + 8) + 4));
        gm_l16 = t120;
        goto po114;
pa118:;
        goto mn113;
po114:;
        {
            void * t122 = gm_l0;
            void * t123 = gm_l1;
            gm_tqs5p4n2tq19v t124 = gm_f_place_mutability_0tlm4g8(t122, t123);
            gm_l18 = t124;
        }
        {
            uint8_t t126;
            if (((((uint8_t *)&gm_l18))[1] != 0)) goto mn128;
            t126 = ((uint8_t)1);
            goto me127;
mn128:;
            t126 = ((uint8_t)0);
            goto me127;
mn129:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 359, 16);
me127:;
            if (!t126) goto ie125;
            gm_b16_8 t130 = gm_l3;
            gm_t0uuhaljsf410 t131;
            gm_b16_8 t132;
            GM_ST(void *, (uint8_t *)&t132, (void *)(gm_st88sqv2u9dqh));
            GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t131 + 0, t132);
            gm_b16_8 t133;
            GM_ST(void *, (uint8_t *)&t133, (void *)(gm_spevct0q3cjgb));
            GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t131 + 16, t133);
            gm_b16_8 t134;
            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s2gpk5q7mg4o8));
            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t131 + 32, t134);
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sgvvkov5kvprt));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&t131 + 48, t135);
            gm_t0uuhaljsf410 t136 = t131;
            uint8_t t137 = 0;
            gm_b16_8 t138 = t130;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t136) + 0, ((uint8_t *)&t138))) t137 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t136) + 16, ((uint8_t *)&t138))) t137 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t136) + 32, ((uint8_t *)&t138))) t137 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t136) + 48, ((uint8_t *)&t138))) t137 = 1;
            uint8_t t139 = (uint8_t)(t137);
            if (!t139) goto ie125;
            {
                void * t141 = gm_l0;
                gm_tl874t2atnlh3 t142 = gm_l5;
                gm_b16_8 t143 = gm_l3;
                gm_tnpifv20irnph t144;
                GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t143);
                gm_tabb710d9mq2e t145;
                gm_f_format_6pukbnn((void *)&t145, (void *)NULL, cx, NULL, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t146 = t145;
                if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_srcngug8co99ok1, 360, 32);
                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                gm_b16_8 t148 = t147;
                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                gm_b0_1 t150 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t141, t142, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix125;
ie125:;
ix125:;
        }
        {
            gm_t88rcucg1b7uk t151 = gm_l16;
            gm_l12 = t151;
        }
        {
            gm_tqs5p4n2tq19v t152 = gm_l18;
            gm_tqs5p4n2tq19v t153;
            GM_ST(uint8_t, ((uint8_t *)&t153), ((uint8_t)1));
            ((uint8_t *)&t153)[1] = 1;
            gm_tqs5p4n2tq19v t154 = t152;
            gm_tqs5p4n2tq19v t155 = t153;
            uint8_t t156 = (!gm_eqebiqthsuot(((uint8_t *)&t154), ((uint8_t *)&t155)));
            gm_l13 = t156;
        }
        goto me99;
mn113:;
ma102:;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t98) + 0));
        if (t158 != ((uint8_t)8ULL)) goto mn157;
        uint8_t t159 = GM_LD(uint8_t, ((((uint8_t *)&t98) + 8) + 0));
        gm_l19 = t159;
        gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t98) + 8) + 4));
        gm_l20 = t160;
        void * t161 = gm_l6;
        gm_t88rcucg1b7uk t162 = gm_l20;
        gm_t53at80pfdus3 t163;
        gm_f_kind_jcnt4hm((void *)&t163, t161, GM_CONV(gm_csguankko4lmhd, t162));
        gm_t53at80pfdus3 t164 = t163;
        uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&t164) + 0));
        if (t169 != ((uint8_t)11ULL)) goto pa168;
        gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t164) + 8) + 8));
        gm_l21 = t170;
        goto po167;
pa168:;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t164) + 0));
        if (t172 != ((uint8_t)20ULL)) goto pa171;
        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t164) + 8) + 4));
        gm_l21 = t173;
        goto po167;
pa171:;
        goto mn166;
po167:;
        {
            gm_t88rcucg1b7uk t175 = gm_l20;
            void * t176 = gm_l1;
            gm_teti411eujvf4 t177;
            memset(&t177, 0, sizeof t177);
            GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t177 + 8), t176);
            gm_tl874t2atnlh3 t178 = gm_l5;
            void * t179 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t175, &t177, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t179;
        }
        {
            gm_t88rcucg1b7uk t180 = gm_l21;
            gm_l12 = t180;
        }
        {
            uint8_t t181 = gm_l19;
            gm_l13 = t181;
        }
        goto me165;
mn166:;
        uint8_t t183 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me165;
mn182:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 365, 29);
me165:;
        goto me99;
mn157:;
ma103:;
        uint8_t t185 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me99;
mn184:;
ma104:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 352, 5);
me99:;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbb1ri5er9djq, 3) == 0)) goto pa189;
        goto po188;
pa189:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_st88sqv2u9dqh, 7) == 0)) goto pa190;
        goto po188;
pa190:;
        goto mn187;
po188:;
        {
            void * t193 = gm_l0;
            gm_b16_8 t194 = gm_l4;
            gm_b16_8 t195 = gm_l3;
            gm_tl874t2atnlh3 t196 = gm_l5;
            uint8_t t197 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t193, t194, ((uint64_t)1ULL), t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t198 = (uint8_t)!t197;
            if (!t198) goto ie192;
            {
                void * t200 = gm_l0;
                void * t201 = GM_LD(void *, (((uint8_t *)t200) + 0));
                gm_tl874t2atnlh3 t202 = gm_l5;
                void * t203 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t201, t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t204 = 0;
                F.ret = t203;
                goto gm_fret;
            }
            goto ix192;
ie192:;
ix192:;
        }
        {
            void * t205 = gm_l0;
            gm_b16_8 t206 = gm_l4;
            void * t207 = gm_l6;
            gm_t88rcucg1b7uk t208 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t209 = gm_l5;
            void * t210 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t205, t206, ((uint64_t)0ULL), t208, t209);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t210;
        }
        {
            uint8_t t212 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbb1ri5er9djq, 3) == 0));
            if (!t212) goto ie211;
            {
            }
            {
                gm_tvdg94bk7aa22 t214;
                GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t214 + 0), ((uint8_t)36ULL));
                void * t215 = gm_l6;
                gm_t88rcucg1b7uk t216 = gm_l12;
                gm_t88rcucg1b7uk t217 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t215, t216);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                {
                    void * t219 = gm_l11;
                    void * t220 = gm_l23;
                    gm_t5fru0lp11n7t t221;
                    GM_ST(void *, ((uint8_t *)&t221 + 0), t219);
                    GM_ST(void *, ((uint8_t *)&t221 + 8), t220);
                    gm_l24 = t221;
                }
                void * t222 = ((uint8_t *)&gm_l24);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcngug8co99ok1, 379, 70);
                gm_b16_8 t223;
                GM_ST(void *, (uint8_t *)&t223, (void *)(((uint8_t *)t222 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t224 = t223;
                gm_tl874t2atnlh3 t225 = gm_l5;
                void * t226 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t214, t217, t224, t225);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t227 = 0;
                F.ret = t226;
                goto gm_fret;
            }
            goto ix211;
ie211:;
ix211:;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t228;
            GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t228 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)37ULL));
            void * t229 = gm_l6;
            void * t230 = gm_l6;
            uint8_t t231 = gm_l13;
            gm_t88rcucg1b7uk t232 = gm_l12;
            gm_t88rcucg1b7uk t233 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t230, t231, t232);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t234 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t229, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t236 = gm_l11;
                void * t237 = gm_l23;
                gm_t5fru0lp11n7t t238;
                GM_ST(void *, ((uint8_t *)&t238 + 0), t236);
                GM_ST(void *, ((uint8_t *)&t238 + 8), t237);
                gm_l25 = t238;
            }
            void * t239 = ((uint8_t *)&gm_l25);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcngug8co99ok1, 380, 76);
            gm_b16_8 t240;
            GM_ST(void *, (uint8_t *)&t240, (void *)(((uint8_t *)t239 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t241 = t240;
            gm_tl874t2atnlh3 t242 = gm_l5;
            void * t243 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t228, t234, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t244 = 0;
            F.ret = t243;
            goto gm_fret;
        }
        goto me186;
mn187:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_snivf25e5fme8, 5) == 0)) goto pa247;
        goto po246;
pa247:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s4pmpk07ugvq6, 4) == 0)) goto pa248;
        goto po246;
pa248:;
        goto mn245;
po246:;
        {
            {
                void * t251 = gm_l0;
                gm_b16_8 t252 = gm_l4;
                gm_b16_8 t253 = gm_l3;
                gm_tl874t2atnlh3 t254 = gm_l5;
                uint8_t t255 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t251, t252, ((uint64_t)0ULL), t253, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_tvdg94bk7aa22 t256;
            uint8_t t258 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_snivf25e5fme8, 5) == 0));
            if (!t258) goto ie257;
            gm_tvdg94bk7aa22 t260;
            GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t260 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t260 + 0), ((uint8_t)38ULL));
            gm_tvdg94bk7aa22 t261 = t260;
            t256 = t261;
            goto ix257;
ie257:;
            gm_tvdg94bk7aa22 t263;
            GM_ST(uint8_t, (uint8_t *)&t263 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t263 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t263 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t263 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)39ULL));
            gm_tvdg94bk7aa22 t264 = t263;
            t256 = t264;
ix257:;
            gm_l26 = t256;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t265 = gm_l26;
            void * t266 = gm_l6;
            gm_t88rcucg1b7uk t267 = gm_l12;
            gm_t88rcucg1b7uk t268 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t266, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t270 = gm_l11;
                gm_tn3r4rvlh0rsv t271;
                GM_ST(void *, ((uint8_t *)&t271 + 0), t270);
                gm_l27 = t271;
            }
            void * t272 = ((uint8_t *)&gm_l27);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 385, 52);
            gm_b16_8 t273;
            GM_ST(void *, (uint8_t *)&t273, (void *)(((uint8_t *)t272 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t274 = t273;
            gm_tl874t2atnlh3 t275 = gm_l5;
            void * t276 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t265, t268, t274, t275);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t277 = 0;
            F.ret = t276;
            goto gm_fret;
        }
        goto me186;
mn245:;
        goto me186;
mn278:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 375, 5);
me186:;
    }
    {
        gm_b16_8 t281 = gm_l3;
        gm_tlg1vmfbpbvtq t282 = GM_LD(gm_tlg1vmfbpbvtq, gm_cukabrstocl);
        gm_tlg1vmfbpbvtq t283 = t282;
        uint8_t t284 = 0;
        gm_b16_8 t285 = t281;
        for (uintptr_t i = 0; i < (uintptr_t)(27); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t283) + i * 16, ((uint8_t *)&t285))) { t284 = 1; break; }
        uint8_t t286 = (uint8_t)(t284);
        if (!t286) goto ie280;
        {
            void * t288 = gm_l0;
            void * t289 = gm_l11;
            gm_b16_8 t290 = gm_l3;
            gm_b16_8 t291 = gm_l4;
            gm_tl874t2atnlh3 t292 = gm_l5;
            void * t293 = gm_f_slice_fn_np9f5vf((void *)NULL, cx, NULL, t288, t289, t290, t291, t292);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t294 = 0;
            F.ret = t293;
            goto gm_fret;
        }
        goto ix280;
ie280:;
ix280:;
    }
    {
        uint8_t t296 = 0;
        uint8_t t297 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm2k5tp7dtg07, 3) == 0));
        if (!t297) {
            uint8_t t298 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s55kqbt4f7gau, 3) == 0));
            t296 = t298;
        } else t296 = 1;
        if (!t296) goto ie295;
        {
            gm_tl874t2atnlh3 t300 = gm_l5;
            gm_t6j6smj7qoctu t301;
            gm_b16_8 t302 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t301 + 0, t302);
            gm_b16_8 t303 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t301 + 16, t303);
            gm_tabb710d9mq2e t304;
            gm_f_format_pe22rd6((void *)&t304, (void *)NULL, cx, NULL, &t301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t305 = t304;
            if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 8, gm_srcngug8co99ok1, 391, 31);
            gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&t305) + 8));
            gm_b16_8 t307 = t306;
            gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&t307));
            gm_f_error_s6hn871((void *)&F.l28, (void *)NULL, cx, NULL, t300, t308);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t309 = gm_l0;
            void * t310 = GM_LD(void *, (((uint8_t *)t309) + 0));
            void * t311 = GM_LD(void *, (((uint8_t *)t310) + 0));
            void * t312 = GM_LD(void *, (((uint8_t *)t311) + 0));
            uint32_t t313 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
            void * t314 = gm_f_file_o15fqku(t312, t313);
            uint64_t t315 = GM_LD(uintptr_t, (((uint8_t *)t314) + 16) + 8);
            void * t316 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t314) + 16)));
            uint32_t t317 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
            uint64_t t318 = (uint64_t)t317;
            uint64_t t319 = (uintptr_t)t318;
            uint32_t t320 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t321 = (uint64_t)t320;
            uint64_t t322 = (uintptr_t)t321;
            if (t319 > t322 || t322 > (uintptr_t)t315) gm_trap_slice(t319, t322, t315, gm_srcngug8co99ok1, 392, 17);
            gm_b16_8 t323;
            GM_ST(void *, (uint8_t *)&t323, (void *)(((uint8_t *)t316 + t319 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(t322 - t319));
            gm_l29 = t323;
        }
        {
            uint64_t t325 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t326 = ((t325 == ((uint64_t)0ULL)));
            if (!t326) goto ie324;
            gm_b16_8 t327 = gm_l29;
            gm_b16_8 t328;
            GM_ST(void *, (uint8_t *)&t328, (void *)(gm_sbvutvpa8pe3d));
            GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(2));
            uint8_t t329 = gm_f_ends_with_biriu5l(t327, t328);
            if (!t329) goto ie324;
            {
                void * t331 = (void *)((uint8_t *)&F.l28);
                gm_b16_8 t332;
                GM_ST(void *, (uint8_t *)&t332, (void *)(gm_soefp7bosmvt1));
                GM_ST(uintptr_t, (uint8_t *)&t332 + 8, (uintptr_t)(11));
                uint32_t t333 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                uint32_t t334 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                uint32_t r335; bool o336 = __builtin_sub_overflow(t334, ((uint32_t)2ULL), &r335);
                if (o336) gm_trap("integer overflow in `-`", gm_srcngug8co99ok1, 394, 71);
                uint32_t t337 = r335;
                uint32_t t338 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                gm_tl874t2atnlh3 t339;
                GM_ST(uint32_t, ((uint8_t *)&t339 + 0), t333);
                GM_ST(uint32_t, ((uint8_t *)&t339 + 4), t337);
                GM_ST(uint32_t, ((uint8_t *)&t339 + 8), t338);
                gm_b16_8 t340;
                GM_ST(void *, (uint8_t *)&t340, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(0));
                gm_b0_1 t341 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t331, t332, t339, t340);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix324;
ie324:;
ix324:;
        }
        {
            void * t342 = gm_l0;
            void * t343 = GM_LD(void *, (((uint8_t *)t342) + 0));
            gm_b0_1 t344 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t343, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l28)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix295;
ie295:;
        {
            void * t346 = gm_l0;
            gm_tl874t2atnlh3 t347 = gm_l5;
            gm_b16_8 t348 = gm_l3;
            gm_tnpifv20irnph t349;
            GM_ST(gm_b16_8, ((uint8_t *)&t349 + 0), t348);
            gm_tabb710d9mq2e t350;
            gm_f_format_dir32ml((void *)&t350, (void *)NULL, cx, NULL, t349);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t351 = t350;
            if (((((uint8_t *)&t351))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t351) + 8, gm_srcngug8co99ok1, 398, 24);
            gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&t351) + 8));
            gm_b16_8 t353 = t352;
            gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
            gm_b0_1 t355 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t346, t347, t354);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix295:;
    }
    {
        void * t356 = gm_l0;
        void * t357 = GM_LD(void *, (((uint8_t *)t356) + 0));
        gm_tl874t2atnlh3 t358 = gm_l5;
        void * t359 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t357, t358);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t360 = 0;
        F.ret = t359;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_core_fn_uts2vhe F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* mname */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* m */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 1059u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_agi9f7f((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcngug8co99ok1, 437, 14);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l3 = t6;
    }
    {
        {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 336) + 0));
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l5;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 8));
                    uint64_t t22 = t21;
                    uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)&t22));
                    gm_l6 = t25;
                    {
                        uint64_t t28 = gm_l6;
                        uint64_t t29 = (uintptr_t)t28;
                        void * t30 = gm_l0;
                        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                        uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 200) + 0) + 8);
                        if (t29 >= (uintptr_t)(t33)) gm_trap_index(t29, t33, gm_srcngug8co99ok1, 439, 12);
                        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 200) + 0)) + t29 * 184) + 0));
                        gm_b16_8 t35 = gm_l3;
                        gm_b16_8 t36 = t35;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                        gm_b16_8 t38 = t34;
                        gm_b16_8 t39 = t37;
                        uint8_t t40 = (!gm_eq0iqiubfm7e(((uint8_t *)&t38), ((uint8_t *)&t39)));
                        if (!t40) goto ie27;
                        {
                            goto lc12;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        void * t42 = gm_l0;
                        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
                        uint64_t t45 = gm_l6;
                        gm_b16_8 t46 = gm_l2;
                        gm_tf5srh4mp4b3i t47 = gm_f_lookup_59juqoo(t44, t45, t46);
                        gm_tf5srh4mp4b3i t48 = t47;
                        uint64_t t49;
                        if (((((uint8_t *)&t48))[8] != 0)) {
                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
                            t49 = t50;
                        } else {
                            gm_t9805qtonbsag t51;
                            memset(&t51, 0, sizeof t51);
                            uint8_t t52 = 0;
                            F.ret = t51;
                            goto gm_fret;
                        }
                        gm_l7 = t49;
                    }
                    {
                        void * t54 = gm_l0;
                        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
                        uint64_t t56 = gm_l7;
                        gm_t6qooom8qa2do t57;
                        gm_f_resolve_decl_pus042v((void *)&t57, (void *)NULL, cx, NULL, t55, t56);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        gm_t6qooom8qa2do t58 = t57;
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                        if (t59 != ((uint8_t)5ULL)) goto ie53;
                        uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&t58) + 8));
                        gm_l8 = t60;
                        {
                            uint32_t t62 = gm_l8;
                            gm_t9805qtonbsag t63;
                            GM_ST(uint8_t, (uint8_t *)&t63 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t63 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t63), t62);
                            ((uint8_t *)&t63)[4] = 1;
                            uint8_t t64 = 0;
                            F.ret = t63;
                            goto gm_fret;
                        }
                        goto ix53;
ie53:;
                        {
                            gm_t9805qtonbsag t66;
                            memset(&t66, 0, sizeof t66);
                            uint8_t t67 = 0;
                            F.ret = t66;
                            goto gm_fret;
                        }
ix53:;
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 438, 5);
me23:;
                }
lc12:;
                {
                    uint64_t t69 = gm_l5;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l5 = t72;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        gm_t9805qtonbsag t73;
        memset(&t73, 0, sizeof t73);
        uint8_t t74 = 0;
        F.ret = t73;
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
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_named_b8afdbp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* d */
    void * gm_l3; /* callee */
    gm_tiilmfd16v3fc gm_l4; /* r */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint32_t gm_l9; /* eff */
    gm_tiilmfd16v3fc gm_l10; /* r */
    GM_STACK_CHECK(&F, 2343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma4:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)19ULL)) goto mn10;
        void * t12 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l3 = t12;
        {
            {
                void * t15 = gm_l0;
                void * t16 = gm_l3;
                gm_tlbrdhah49ci5 t17;
                gm_f_effect_named_b8afdbp((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tlbrdhah49ci5 t18 = t17;
                gm_t716e5hdn64ba t19;
                if (((((uint8_t *)&t18))[32] != 0)) {
                    gm_t716e5hdn64ba t20 = GM_LD(gm_t716e5hdn64ba, (((uint8_t *)&t18) + 0));
                    t19 = t20;
                } else {
                    gm_tlbrdhah49ci5 t21;
                    memset(&t21, 0, sizeof t21);
                    uint8_t t22 = 0;
                    F.ret = t21;
                    goto gm_fret;
                }
            }
        }
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
            void * t27 = gm_l1;
            gm_tqu3m2l3n8mnc t28;
            gm_f_resolve_effect_ref_bggirs2((void *)&t28, (void *)NULL, cx, NULL, t24, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqu3m2l3n8mnc t29 = t28;
            gm_tiilmfd16v3fc t30;
            if (((((uint8_t *)&t29))[24] != 0)) {
                gm_tiilmfd16v3fc t31 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t29) + 0));
                t30 = t31;
            } else {
                gm_tlbrdhah49ci5 t32;
                memset(&t32, 0, sizeof t32);
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            gm_l4 = t30;
        }
        {
            gm_tiilmfd16v3fc t34 = gm_l4;
            gm_t716e5hdn64ba t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t35 + 8), t34);
            gm_tlbrdhah49ci5 t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 36, 0);
            GM_ST(gm_t716e5hdn64ba, ((uint8_t *)&t36), t35);
            ((uint8_t *)&t36)[32] = 1;
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto me3;
mn10:;
ma5:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)6ULL)) goto mn38;
        gm_t08olq97rscn3 t40 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t40;
        {
            void * t43 = gm_l0;
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tqvu78rpi6q1h t45;
            gm_f_lookup_local_6f38t7p((void *)&t45, t43, t44);
            gm_tqvu78rpi6q1h t46 = t45;
            uint8_t t47;
            if (((((uint8_t *)&t46))[208] != 0)) goto mn49;
            t47 = ((uint8_t)0);
            goto me48;
mn49:;
            t47 = ((uint8_t)1);
            goto me48;
mn50:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 624, 16);
me48:;
            if (!t47) goto ie42;
            {
                gm_tlbrdhah49ci5 t52;
                memset(&t52, 0, sizeof t52);
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 8));
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tf5srh4mp4b3i t59 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t55, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t60 = t59;
            uint64_t t61;
            if (((((uint8_t *)&t60))[8] != 0)) {
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&t60) + 0));
                t61 = t62;
            } else {
                gm_tlbrdhah49ci5 t63;
                memset(&t63, 0, sizeof t63);
                uint8_t t64 = 0;
                F.ret = t63;
                goto gm_fret;
            }
            gm_l2 = t61;
        }
        goto me3;
mn38:;
ma6:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t66 != ((uint8_t)22ULL)) goto mn65;
        void * t67 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t67;
        gm_t08olq97rscn3 t68 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t68;
        {
            void * t70 = gm_l0;
            void * t71 = gm_l6;
            gm_tf5srh4mp4b3i t72 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t73 = t72;
            uint64_t t74;
            if (((((uint8_t *)&t73))[8] != 0)) {
                uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&t73) + 0));
                t74 = t75;
            } else {
                gm_tlbrdhah49ci5 t76;
                memset(&t76, 0, sizeof t76);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            gm_l8 = t74;
        }
        {
            void * t78 = gm_l0;
            void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
            uint64_t t80 = gm_l8;
            void * t81 = gm_l0;
            uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 8));
            gm_tf5srh4mp4b3i t83 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t79, t80, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t84 = t83;
            uint64_t t85;
            if (((((uint8_t *)&t84))[8] != 0)) {
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&t84) + 0));
                t85 = t86;
            } else {
                gm_tlbrdhah49ci5 t87;
                memset(&t87, 0, sizeof t87);
                uint8_t t88 = 0;
                F.ret = t87;
                goto gm_fret;
            }
            gm_l2 = t85;
        }
        goto me3;
mn65:;
ma7:;
        gm_tlbrdhah49ci5 t90;
        memset(&t90, 0, sizeof t90);
        uint8_t t91 = 0;
        F.ret = t90;
        goto gm_fret;
        goto me3;
mn89:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 617, 5);
me3:;
    }
    {
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        uint64_t t94 = gm_l2;
        gm_t6qooom8qa2do t95;
        gm_f_resolve_decl_pus042v((void *)&t95, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t96 = t95;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
        switch (t102) { case ((uint8_t)0ULL): goto ma100; case ((uint8_t)1ULL): goto ma100; case ((uint8_t)2ULL): goto ma98; case ((uint8_t)3ULL): goto ma99; case ((uint8_t)4ULL): goto ma100; case ((uint8_t)5ULL): goto ma100; case ((uint8_t)6ULL): goto ma100; case ((uint8_t)7ULL): goto ma100; case ((uint8_t)8ULL): goto ma100; case ((uint8_t)9ULL): goto ma100; case ((uint8_t)10ULL): goto ma100; case ((uint8_t)11ULL): goto ma100; default: goto ma100; }
ma98:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
        if (t104 != ((uint8_t)2ULL)) goto mn103;
        uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&t96) + 8));
        gm_l9 = t105;
        uint32_t t106 = gm_l9;
        gm_t716e5hdn64ba t107;
        GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t107 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t107 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t107 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t107 + 8), t106);
        gm_tlbrdhah49ci5 t108;
        GM_ST(uint8_t, (uint8_t *)&t108 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t108 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t108 + 36, 0);
        GM_ST(gm_t716e5hdn64ba, ((uint8_t *)&t108), t107);
        ((uint8_t *)&t108)[32] = 1;
        uint8_t t109 = 0;
        F.ret = t108;
        goto gm_fret;
        goto me97;
mn103:;
ma99:;
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
        if (t111 != ((uint8_t)3ULL)) goto mn110;
        gm_tiilmfd16v3fc t112 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t96) + 8));
        gm_l10 = t112;
        gm_tiilmfd16v3fc t113 = gm_l10;
        gm_t716e5hdn64ba t114;
        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t114 + 8), t113);
        gm_tlbrdhah49ci5 t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 36, 0);
        GM_ST(gm_t716e5hdn64ba, ((uint8_t *)&t115), t114);
        ((uint8_t *)&t115)[32] = 1;
        uint8_t t116 = 0;
        F.ret = t115;
        goto gm_fret;
        goto me97;
mn110:;
ma100:;
        gm_tlbrdhah49ci5 t118;
        memset(&t118, 0, sizeof t118);
        uint8_t t119 = 0;
        F.ret = t118;
        goto gm_fret;
        goto me97;
mn117:;
ma101:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 633, 5);
me97:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_with_continuation_n_lv5ofqt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_emit_with_continuation_n_lv5ofqt F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* base */
    gm_tl874t2atnlh3 gm_l3; /* name */
    void * gm_l5; /* file */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t4gut2c9fj1kt t2 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)a1));
        F.l4 = t2;
    }
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        void * t8 = gm_f_file_o15fqku(t6, t7);
        gm_l5 = t8;
    }
    {
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        uint8_t t12 = ((t10 == t11));
        if (!t12) goto ie9;
        void * t13 = gm_l5;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
        gm_tcc1rq4n1avb2 t15 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t13, t14));
        gm_tcc1rq4n1avb2 t16 = t15;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&t16) + 0));
        void * t18 = gm_l5;
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        gm_tcc1rq4n1avb2 t20 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t18, t19));
        gm_tcc1rq4n1avb2 t21 = t20;
        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&t21) + 0));
        uint8_t t23 = (t17 < t22);
        if (!t23) goto ie9;
        {
            void * t25 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sfr85ju00e9hk));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(137));
            gm_f_add_note_46v502s(t25, t26);
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        gm_b0_1 t29 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t28, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
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
inline __attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3) {
    struct gm_fr_finish_call_kjknkfg F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* pre */
    void * gm_l2; /* call */
    uint8_t gm_l3; /* receiver */
    gm_b16_8 gm_l5; /* args */
    GM_STACK_CHECK(&F, 435u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l2;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = gm_l2;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t10 != ((uint8_t)34ULL)) goto ie8;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((((uint8_t *)t9) + 8) + 8) + 40));
        gm_l5 = t11;
        uint8_t t12 = gm_l3;
        if (!t12) goto ie8;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint8_t t14 = (t13 > ((uint64_t)0ULL));
        if (!t14) goto ie8;
        {
            uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcngug8co99ok1, 603, 9);
            uint8_t *t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t16 * 88);
            void * t19 = gm_l0;
            uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcngug8co99ok1, 603, 36);
            gm_tq7s98ihonkju t22 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_t69m56meqefs8 *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t20 * 88)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdoa98a1tm0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcngug8co99ok1, 603, 32);
            void * t24 = GM_LD(void *, (((uint8_t *)&t23) + 8));
            void * t25 = (void *)((uint8_t *)&F.l4);
            void * t26 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t19, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t27 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t26));
            GM_ST(gm_t69m56meqefs8, t18, t27);
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t29 = gm_l1;
        gm_b16_8 t30 = t29;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_tftb1odc739t5 t32 = gm_f_extend_nkql463(t28, t31);
        gm_tftb1odc739t5 t33 = t32;
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcngug8co99ok1, 605, 5);
        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
    }
    {
        void * t35 = gm_l2;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t35) + 0));
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        void * t38 = gm_l2;
        void * t39 = gm_l2;
        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
        void * t41 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t36, t37, t38, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) uint8_t gm_f_fits_receiver_1qqamgd(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits_receiver_1qqamgd F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* a */
    uint32_t gm_l3; /* x */
    uint32_t gm_l4; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_t53at80pfdus3 t13;
        gm_f_kind_jcnt4hm((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
        gm_t53at80pfdus3 t14 = t13;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t15 != ((uint8_t)17ULL)) goto ie10;
        uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l3 = t16;
        {
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l2;
            gm_t53at80pfdus3 t21;
            gm_f_kind_jcnt4hm((void *)&t21, t19, GM_CONV(gm_csguankko4lmhd, t20));
            gm_t53at80pfdus3 t22 = t21;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t23 != ((uint8_t)17ULL)) goto ie18;
            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)&t22) + 8) + 0));
            gm_l4 = t24;
            {
                void * t26 = gm_l0;
                uint32_t t27 = gm_l3;
                uint32_t t28 = gm_f_template_9o92cvv(t26, t27);
                void * t29 = gm_l0;
                uint32_t t30 = gm_l4;
                uint32_t t31 = gm_f_template_9o92cvv(t29, t30);
                uint8_t t32 = ((t28 == t31));
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix18;
ie18:;
            {
                uint8_t t35 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix18:;
        }
        goto ix10;
ie10:;
        {
            uint8_t t37 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix10:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_fn_cast_mismatch_e9s21md(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_fn_cast_mismatch_e9s21md F;
    void * gm_l0; /* cx */
    void * gm_l1; /* from */
    void * gm_l2; /* to */
    gm_closure gm_l5; /* same_repr */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_t88rcucg1b7uk gm_l10; /* b */
    GM_STACK_CHECK(&F, 1190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        F.l3 = t4;
    }
    {
        gm_closure t5 = ((gm_closure){(void *)gm_f_pointer_gv6lvmk, (void *)&F.h, cx});
        F.l4 = t5;
    }
    {
        gm_closure t6 = ((gm_closure){(void *)gm_f_same_repr_830dsa9, (void *)&F.h, cx});
        gm_l5 = t6;
    }
    {
        void * t8 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t8) + 40));
        void * t10 = gm_l2;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 40));
        uint8_t t12 = (!(t9 == t11));
        if (!t12) goto ie7;
        {
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s42aeamnkrchf));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(30));
            gm_th2lqbo77pvsc t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[16] = 1;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t18 = gm_l1;
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
        void * t20 = gm_l2;
        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8);
        uint8_t t22 = (!(t19 == t21));
        if (!t22) goto ie17;
        {
            void * t24 = gm_l1;
            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 0) + 8);
            void * t26 = gm_l2;
            uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
            gm_t2lqu8jsj511d t28;
            GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t25);
            GM_ST(uint64_t, ((uint8_t *)&t28 + 8), t27);
            gm_tabb710d9mq2e t29;
            gm_f_format_ahnpa17((void *)&t29, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t30 = t29;
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcngug8co99ok1, 321, 54);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_b16_8 t32 = t31;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_th2lqbo77pvsc t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[16] = 1;
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            void * t37 = gm_l1;
            uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 0) + 8);
            gm_l6 = t38;
        }
        {
            for (;;) {
                uint64_t t40 = gm_l7;
                uint64_t t41 = gm_l6;
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l7;
                    uint64_t t46 = t45;
                    uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&t46));
                    gm_l8 = t49;
                    {
                        uint64_t t51 = gm_l8;
                        uint64_t t52 = (uintptr_t)t51;
                        void * t53 = gm_l1;
                        uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)t53) + 0) + 8);
                        if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srcngug8co99ok1, 323, 14);
                        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t53) + 0)) + t52 * 4));
                        gm_l9 = t55;
                    }
                    {
                        uint64_t t56 = gm_l8;
                        uint64_t t57 = (uintptr_t)t56;
                        void * t58 = gm_l2;
                        uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)t58) + 0) + 8);
                        if (t57 >= (uintptr_t)(t59)) gm_trap_index(t57, t59, gm_srcngug8co99ok1, 324, 14);
                        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t58) + 0)) + t57 * 4));
                        gm_l10 = t60;
                    }
                    {
                        gm_closure t62 = gm_l5;
                        gm_t88rcucg1b7uk t63 = gm_l9;
                        gm_t88rcucg1b7uk t64 = gm_l10;
                        gm_closure t65 = t62;
                        gm_node *ccx66 = t65.cx;
                        void * t67 = t65.code;
                        uint8_t t68;
                        if (GM_PTR_TAGGED(t67)) {
                            t68 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t67))(GM_CONV(gm_csguankko4lmhd, t63), GM_CONV(gm_csguankko4lmhd, t64));
                        } else {
                            t68 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t67)(t65.env, ccx66, NULL, t63, t64);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        uint8_t t69 = (uint8_t)!t68;
                        if (!t69) goto ie61;
                        {
                            gm_t7neromnu8t62 t71;
                            uint64_t t72 = gm_l8;
                            uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                            if (o74) gm_trap("integer overflow in `+`", gm_srcngug8co99ok1, 326, 119);
                            uint64_t t75 = r73;
                            GM_ST(uint64_t, (uint8_t *)&t71 + 0, t75);
                            void * t76 = gm_l0;
                            gm_t88rcucg1b7uk t77 = gm_l9;
                            gm_b16_8 t78 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t76, t77);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t71 + 8, t78);
                            void * t79 = gm_l0;
                            gm_t88rcucg1b7uk t80 = gm_l10;
                            gm_b16_8 t81 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t79, t80);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t71 + 24, t81);
                            gm_tabb710d9mq2e t82;
                            gm_f_format_rn1vtpg((void *)&t82, (void *)NULL, cx, NULL, &t71);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_tabb710d9mq2e t83 = t82;
                            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcngug8co99ok1, 326, 20);
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
                            gm_b16_8 t85 = t84;
                            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                            gm_th2lqbo77pvsc t87;
                            GM_ST(uint8_t, (uint8_t *)&t87 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t87 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t87 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t87), t86);
                            ((uint8_t *)&t87)[16] = 1;
                            uint8_t t88 = 0;
                            F.ret = t87;
                            goto gm_fret;
                        }
                        goto ix61;
ie61:;
ix61:;
                    }
                    goto me47;
mn48:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 322, 5);
me47:;
                }
lc39:;
                {
                    uint64_t t90 = gm_l7;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l7 = t93;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        gm_closure t95 = gm_l5;
        void * t96 = gm_l1;
        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + 16));
        void * t98 = gm_l2;
        gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t98) + 16));
        gm_closure t100 = t95;
        gm_node *ccx101 = t100.cx;
        void * t102 = t100.code;
        uint8_t t103;
        if (GM_PTR_TAGGED(t102)) {
            t103 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t102))(GM_CONV(gm_csguankko4lmhd, t97), GM_CONV(gm_csguankko4lmhd, t99));
        } else {
            t103 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t102)(t100.env, ccx101, NULL, t97, t99);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t104 = (uint8_t)!t103;
        if (!t104) goto ie94;
        {
            gm_t6j6smj7qoctu t106;
            void * t107 = gm_l0;
            void * t108 = gm_l1;
            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t108) + 16));
            gm_b16_8 t110 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t107, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t110);
            void * t111 = gm_l0;
            void * t112 = gm_l2;
            gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t112) + 16));
            gm_b16_8 t114 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t111, t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t114);
            gm_tabb710d9mq2e t115;
            gm_f_format_jfku65e((void *)&t115, (void *)NULL, cx, NULL, &t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t116 = t115;
            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srcngug8co99ok1, 330, 16);
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
            gm_b16_8 t118 = t117;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            gm_th2lqbo77pvsc t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t120), t119);
            ((uint8_t *)&t120)[16] = 1;
            uint8_t t121 = 0;
            F.ret = t120;
            goto gm_fret;
        }
        goto ix94;
ie94:;
ix94:;
    }
    {
        gm_th2lqbo77pvsc t122;
        memset(&t122, 0, sizeof t122);
        uint8_t t123 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_kind_u4oqjin(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_kind_u4oqjin F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l5));
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t15 != ((uint8_t)8ULL)) goto pa14;
    goto po13;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t17 != ((uint8_t)9ULL)) goto pa16;
    goto po13;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t19 != ((uint8_t)7ULL)) goto pa18;
    goto po13;
pa18:;
    goto mn12;
po13:;
    t5 = ((uint8_t)1ULL);
    goto me6;
mn12:;
ma8:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t21 != ((uint8_t)16ULL)) goto mn20;
    t5 = ((uint8_t)2ULL);
    goto me6;
mn20:;
ma9:;
    t5 = ((uint8_t)0ULL);
    goto me6;
mn22:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 275, 32);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_method_call_hebo20o F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_tjpqvfd72o62u gm_l7; /* info */
    void * gm_l8; /* recv */
    gm_t88rcucg1b7uk gm_l9; /* rty */
    void * gm_l10; /* soa_recv */
    gm_tjpqvfd72o62u gm_l11; /* info */
    gm_t88rcucg1b7uk gm_l12; /* derefed_ty */
    gm_t88rcucg1b7uk gm_l13; /* p */
    gm_b16_8 gm_l14; /* fields */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l19; /* field */
    void * gm_l20; /* e */
    gm_t08olq97rscn3 gm_l21; /* mname */
    uint8_t gm_l22; /* internal */
    gm_tbngattl1fo51 gm_l23; /* r */
    void * gm_l24; /* e */
    GM_STACK_CHECK(&F, 2822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        gm_tkn42u7na3t81 t8 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t9 = t8;
        if (!((((uint8_t *)&t9))[4] != 0)) goto ie5;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t9));
        gm_l6 = t10;
        {
            uint8_t t13 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 8) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_s66fk044n1n0k, 8) == 0));
            if (!t13) goto ie12;
            {
                void * t15 = gm_l0;
                gm_t88rcucg1b7uk t16 = gm_l6;
                gm_b16_8 t17 = gm_l3;
                gm_tl874t2atnlh3 t18 = gm_l4;
                void * t19 = gm_f_check_from_tag_g3el2co((void *)NULL, cx, NULL, t15, t16, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            uint8_t t22 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sqfrv3g5v9m2n, 4) == 0));
            if (!t22) goto ie21;
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            gm_t88rcucg1b7uk t26 = gm_l6;
            uint8_t t27 = gm_f_is_vector_gkq6b88(t25, GM_CONV(gm_csguankko4lmhd, t26));
            if (!t27) goto ie21;
            {
                void * t29 = gm_l0;
                gm_t88rcucg1b7uk t30 = gm_l6;
                gm_b16_8 t31 = gm_l3;
                gm_tl874t2atnlh3 t32 = gm_l4;
                void * t33 = gm_f_vector_load_ssdvomk((void *)NULL, cx, NULL, t29, t30, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
            gm_t88rcucg1b7uk t38 = gm_l6;
            gm_t8cc1mtbrhlpt t39;
            gm_f_soa_info_pq4q43l((void *)&t39, t37, GM_CONV(gm_csguankko4lmhd, t38));
            gm_t8cc1mtbrhlpt t40 = t39;
            if (!((((uint8_t *)&t40))[32] != 0)) goto ie35;
            gm_tjpqvfd72o62u t41 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)&t40));
            gm_l7 = t41;
            {
                uint8_t t44 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sgb0oed9c3voo, 4) == 0));
                if (!t44) goto ie43;
                {
                    void * t46 = gm_l0;
                    gm_t88rcucg1b7uk t47 = gm_l6;
                    gm_b16_8 t48 = gm_l3;
                    gm_tl874t2atnlh3 t49 = gm_l4;
                    void * t50 = gm_f_soa_make_5imjrl1((void *)NULL, cx, NULL, t46, t47, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l7)), t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t51 = 0;
                    F.ret = t50;
                    goto gm_fret;
                }
                goto ix43;
ie43:;
ix43:;
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            void * t52 = gm_l0;
            gm_t88rcucg1b7uk t53 = gm_l6;
            gm_b16_8 t54 = gm_l3;
            gm_th2lqbo77pvsc t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t55), t54);
            ((uint8_t *)&t55)[16] = 1;
            gm_tl874t2atnlh3 t56 = gm_l4;
            void * t57 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t52, t53, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), &t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t59 = gm_l0;
        void * t60 = gm_l1;
        gm_tkn42u7na3t81 t61;
        memset(&t61, 0, sizeof t61);
        void * t62 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t59, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t62;
    }
    {
        void * t63 = gm_l0;
        void * t64 = (void *)(((uint8_t *)t63) + 96);
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
        void * t67 = gm_l8;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t67) + 0));
        gm_t88rcucg1b7uk t69 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t64, t66, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t69;
    }
    {
        void * t71 = gm_l5;
        gm_t88rcucg1b7uk t72 = gm_l9;
        uint8_t t73 = gm_f_is_error_8k27gd1(t71, GM_CONV(gm_csguankko4lmhd, t72));
        if (!t73) goto ie70;
        {
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
            gm_tl874t2atnlh3 t77 = gm_l4;
            void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        void * t80 = gm_l0;
        void * t81 = gm_l8;
        void * t82 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t82;
    }
    {
        void * t84 = gm_l0;
        void * t85 = gm_l10;
        gm_t8cc1mtbrhlpt t86;
        gm_f_soa_of_jufdcj2((void *)&t86, (void *)NULL, cx, NULL, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8cc1mtbrhlpt t87 = t86;
        if (!((((uint8_t *)&t87))[32] != 0)) goto ie83;
        gm_tjpqvfd72o62u t88 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)&t87));
        gm_l11 = t88;
        {
            uint8_t t91 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_srv4cua6q51nq, 4) == 0));
            if (!t91) goto ie90;
            {
                void * t93 = gm_l0;
                void * t94 = gm_l10;
                gm_b16_8 t95 = gm_l3;
                gm_tl874t2atnlh3 t96 = gm_l4;
                void * t97 = gm_f_soa_free_9554eha((void *)NULL, cx, NULL, t93, t94, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l11)), t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
            }
            goto ix90;
ie90:;
ix90:;
        }
        {
            uint8_t t100 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sgvvkov5kvprt, 9) == 0));
            if (!t100) goto ie99;
            {
                void * t102 = gm_l0;
                void * t103 = gm_l10;
                gm_b16_8 t104 = gm_l3;
                gm_tl874t2atnlh3 t105 = gm_l4;
                void * t106 = gm_f_soa_copy_from_vrsguuh((void *)NULL, cx, NULL, t102, t103, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l11)), t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t107 = 0;
                F.ret = t106;
                goto gm_fret;
            }
            goto ix99;
ie99:;
ix99:;
        }
        {
            void * t108 = gm_l0;
            gm_tl874t2atnlh3 t109 = gm_l4;
            gm_t6j6smj7qoctu t110;
            void * t111 = gm_l0;
            void * t112 = gm_l0;
            void * t113 = (void *)(((uint8_t *)t112) + 96);
            void * t114 = gm_l0;
            void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
            void * t116 = gm_l10;
            gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t116) + 0));
            gm_t88rcucg1b7uk t118 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t113, t115, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t119 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t111, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t119);
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t120);
            gm_tabb710d9mq2e t121;
            gm_f_format_mpl2gek((void *)&t121, (void *)NULL, cx, NULL, &t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t122 = t121;
            if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 8, gm_srcngug8co99ok1, 24, 24);
            gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
            gm_b16_8 t124 = t123;
            gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
            gm_b0_1 t126 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t108, t109, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t127 = gm_l0;
            void * t128 = GM_LD(void *, (((uint8_t *)t127) + 0));
            gm_tl874t2atnlh3 t129 = gm_l4;
            void * t130 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t131 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        gm_t88rcucg1b7uk t132 = gm_l9;
        gm_l12 = t132;
    }
    {
        void * t134 = gm_l5;
        void * t135 = gm_l0;
        void * t136 = (void *)(((uint8_t *)t135) + 96);
        void * t137 = gm_l5;
        void * t138 = gm_l8;
        gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t138) + 0));
        gm_t88rcucg1b7uk t140 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t136, t137, t139);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t141;
        gm_f_kind_jcnt4hm((void *)&t141, t134, GM_CONV(gm_csguankko4lmhd, t140));
        gm_t53at80pfdus3 t142 = t141;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t142) + 0));
        if (t143 != ((uint8_t)8ULL)) goto ie133;
        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t142) + 8) + 4));
        gm_l13 = t144;
        {
            gm_t88rcucg1b7uk t146 = gm_l13;
            gm_l12 = t146;
        }
        goto ix133;
ie133:;
ix133:;
    }
    {
        void * t148 = gm_l0;
        void * t149 = GM_LD(void *, (((uint8_t *)t148) + 0));
        gm_t88rcucg1b7uk t150 = gm_l12;
        gm_th2lqbo77pvsc t151;
        gm_f_struct_fields_chqu3it((void *)&t151, t149, GM_CONV(gm_csguankko4lmhd, t150));
        gm_th2lqbo77pvsc t152 = t151;
        if (!((((uint8_t *)&t152))[16] != 0)) goto ie147;
        gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
        gm_l14 = t153;
        {
            {
                gm_b16_8 t156 = gm_l14;
                gm_b16_8 t157 = t156;
                gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                gm_l15 = t158;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t160 = gm_l16;
                    uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t162 = (t160 < t161);
                    if (!t162) goto lb159;
                    {
                        gm_t8ta482bl6be0 t165;
                        uint64_t t166 = gm_l16;
                        GM_ST(uint64_t, (uint8_t *)&t165 + 0, t166);
                        uint64_t t167 = gm_l16;
                        uint64_t t168 = (uintptr_t)t167;
                        uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        memcpy((uint8_t *)&t165 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t168 * 40), 40);
                        gm_t8ta482bl6be0 t170 = t165;
                        uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&t170) + 0));
                        gm_l17 = t173;
                        gm_t0mglksrkfa5e t174 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t170) + 8));
                        F.l18 = t174;
                        {
                            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                            gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                            gm_b16_8 t179 = t177;
                            gm_b16_8 t180 = t178;
                            uint8_t t181 = (gm_eq0iqiubfm7e(((uint8_t *)&t179), ((uint8_t *)&t180)));
                            if (!t181) goto ie176;
                            {
                                gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16));
                                void * t184 = gm_l0;
                                void * t185 = gm_l8;
                                void * t186 = gm_f_auto_deref_vdb1hdn((void *)NULL, cx, NULL, t184, t185);
                                if (gm_unw.kind) {
                                    goto gm_ll163;
                                }
                                uint64_t t187 = gm_l17;
                                uint32_t t188 = (uint32_t)t187;
                                gm_tbl8il92l97ut t189;
                                GM_ST(uint32_t, (uint8_t *)&t189 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t189 + 0), t186);
                                GM_ST(uint32_t, ((uint8_t *)&t189 + 8), t188);
                                gm_teti411eujvf4 t190;
                                memset(&t190, 0, sizeof t190);
                                GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t190 + 8), t189);
                                gm_tl874t2atnlh3 t191 = gm_l4;
                                void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t183, &t190, t191);
                                if (gm_unw.kind) {
                                    goto gm_ll163;
                                }
                                gm_l19 = t192;
                            }
                            {
                                void * t193 = gm_l0;
                                void * t194 = gm_l19;
                                gm_b16_8 t195 = gm_l3;
                                gm_tl874t2atnlh3 t196 = gm_l4;
                                void * t197 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t193, t194, t195, t196);
                                if (gm_unw.kind) {
                                    goto gm_ll163;
                                }
                                uint8_t t198 = 0;
                                F.ret = t197;
                                goto gm_fret;
                            }
                            goto ix176;
ie176:;
ix176:;
                        }
                        goto me171;
mn172:;
                        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 31, 9);
me171:;
                    }
lc159:;
                    {
                        uint64_t t200 = gm_l16;
                        uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                        uint64_t t203 = r201;
                        gm_l16 = t203;
                    }
                }
gm_ll163:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb159; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc159; }
                }
                goto gm_lad;
lb159:;
            }
        }
        goto ix147;
ie147:;
ix147:;
    }
    {
        void * t205 = gm_l0;
        void * t206 = gm_l8;
        gm_t88rcucg1b7uk t207 = gm_l9;
        gm_b16_8 t208 = gm_l3;
        gm_tl874t2atnlh3 t209 = gm_l4;
        void * t210 = gm_f_builtin_method_jist12a((void *)NULL, cx, NULL, t205, t206, t207, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t208, t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t211 = t210;
        if (!(GM_LD(void *, ((uint8_t *)&t211)) != 0)) goto ie204;
        void * t212 = GM_LD(void *, ((uint8_t *)&t211));
        gm_l20 = t212;
        {
            void * t214 = gm_l20;
            uint8_t t215 = 0;
            F.ret = t214;
            goto gm_fret;
        }
        goto ix204;
ie204:;
ix204:;
    }
    {
        gm_t08olq97rscn3 t216 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a2));
        gm_l21 = t216;
    }
    {
        gm_l22 = ((uint8_t)0);
    }
    {
        void * t218 = gm_l0;
        void * t219 = GM_LD(void *, (((uint8_t *)t218) + 0));
        gm_t88rcucg1b7uk t220 = gm_l9;
        gm_thetuhtouve7f t221 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t219, t220);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thetuhtouve7f t222 = t221;
        if (!((((uint8_t *)&t222))[8] != 0)) goto ie217;
        gm_tbngattl1fo51 t223 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&t222));
        gm_l23 = t223;
        {
            uint8_t t226 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
            if (!t226) goto ie225;
            {
                memset((uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)) + 28, 0, 4);
                gm_b16_8 t228;
                uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                if (!t230) goto ie229;
                gm_b16_8 t232;
                GM_ST(void *, (uint8_t *)&t232, (void *)(gm_ss226ibsv86g3));
                GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(14));
                gm_b16_8 t233 = t232;
                t228 = t233;
                goto ix229;
ie229:;
                gm_b16_8 t235;
                GM_ST(void *, (uint8_t *)&t235, (void *)(gm_selatfetsf85i));
                GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(9));
                gm_b16_8 t236 = t235;
                t228 = t236;
ix229:;
                gm_tl874t2atnlh3 t237 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)) + 0, t228);
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)) + 16, t237);
            }
            {
                gm_l22 = ((uint8_t)1);
            }
            goto ix225;
ie225:;
ix225:;
        }
        goto ix217;
ie217:;
ix217:;
    }
    {
        void * t239 = gm_l0;
        void * t240 = gm_l8;
        uint8_t t241 = gm_l22;
        gm_b16_8 t242 = gm_l3;
        gm_tl874t2atnlh3 t243 = gm_l4;
        void * t244 = gm_f_module_method_cb3lmfk((void *)NULL, cx, NULL, t239, t240, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l21)), t241, t242, t243);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t245 = t244;
        if (!(GM_LD(void *, ((uint8_t *)&t245)) != 0)) goto ie238;
        void * t246 = GM_LD(void *, ((uint8_t *)&t245));
        gm_l24 = t246;
        {
            void * t248 = gm_l24;
            uint8_t t249 = 0;
            F.ret = t248;
            goto gm_fret;
        }
        goto ix238;
ie238:;
ix238:;
    }
    {
        gm_tl874t2atnlh3 t250 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_t6j6smj7qoctu t251;
        void * t252 = gm_l0;
        void * t253 = gm_l0;
        void * t254 = (void *)(((uint8_t *)t253) + 96);
        void * t255 = gm_l0;
        void * t256 = GM_LD(void *, (((uint8_t *)t255) + 0));
        gm_t88rcucg1b7uk t257 = gm_l9;
        gm_t88rcucg1b7uk t258 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t254, t256, t257);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t259 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t252, t258);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t251 + 0, t259);
        gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t251 + 16, t260);
        gm_tabb710d9mq2e t261;
        gm_f_format_uesfu03((void *)&t261, (void *)NULL, cx, NULL, &t251);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t262 = t261;
        if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 8, gm_srcngug8co99ok1, 51, 32);
        gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&t262) + 8));
        gm_b16_8 t264 = t263;
        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
        gm_f_error_s6hn871((void *)&F.l25, (void *)NULL, cx, NULL, t250, t265);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t266 = gm_l0;
        void * t267 = gm_l1;
        gm_tl874t2atnlh3 t268 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t267) + 72));
        gm_tl874t2atnlh3 t269 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_b0_1 t270 = gm_f_emit_with_continuation_n_lv5ofqt((void *)NULL, cx, NULL, t266, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l25)), t268, t269);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t271 = gm_l0;
        void * t272 = GM_LD(void *, (((uint8_t *)t271) + 0));
        gm_tl874t2atnlh3 t273 = gm_l4;
        void * t274 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t272, t273);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t275 = 0;
        F.ret = t274;
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
__attribute__((aligned(4))) void * gm_f_module_method_cb3lmfk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_module_method_cb3lmfk F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint8_t gm_l3; /* internal */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* rty */
    gm_t88rcucg1b7uk gm_l8; /* s */
    gm_t88rcucg1b7uk gm_l9; /* p */
    uint32_t gm_l10; /* id */
    uint64_t gm_l11; /* module */
    uint64_t gm_l12; /* d */
    uint32_t gm_l13; /* f */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    void * gm_l20; /* e */
    void * gm_l22; /* r */
    gm_t6j6smj7qoctu gm_l23; /* bound */
    void * gm_l25; /* call */
    GM_STACK_CHECK(&F, 3387u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t11;
    }
    {
        gm_t88rcucg1b7uk t12 = gm_l7;
        gm_l8 = t12;
    }
    {
        void * t14 = gm_l6;
        gm_t88rcucg1b7uk t15 = gm_l7;
        gm_t53at80pfdus3 t16;
        gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
        gm_t53at80pfdus3 t17 = t16;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t18 != ((uint8_t)8ULL)) goto ie13;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t17) + 8) + 4));
        gm_l9 = t19;
        {
            gm_t88rcucg1b7uk t21 = gm_l9;
            gm_l8 = t21;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t23 = gm_l6;
        gm_t88rcucg1b7uk t24 = gm_l8;
        gm_t53at80pfdus3 t25;
        gm_f_kind_jcnt4hm((void *)&t25, t23, GM_CONV(gm_csguankko4lmhd, t24));
        gm_t53at80pfdus3 t26 = t25;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t27 != ((uint8_t)17ULL)) goto ie22;
        uint32_t t28 = GM_LD(uint32_t, ((((uint8_t *)&t26) + 8) + 0));
        gm_l10 = t28;
        {
            uint32_t t30 = gm_l10;
            uint64_t t31 = (uint64_t)t30;
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
            uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 1088) + 0) + 8);
            if (t32 >= (uintptr_t)(t35)) gm_trap_index(t32, t35, gm_srcngug8co99ok1, 512, 15);
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t34) + 1088) + 0)) + t32 * 184) + 16));
            gm_l11 = t36;
        }
        {
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            uint64_t t40 = gm_l11;
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tf5srh4mp4b3i t42 = gm_f_lookup_59juqoo(t39, t40, t41);
            gm_tf5srh4mp4b3i t43 = t42;
            uint64_t t44;
            if (((((uint8_t *)&t43))[8] != 0)) {
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
                t44 = t45;
            } else {
                void * t46 = gm_l0;
                void * t47 = gm_l1;
                gm_t88rcucg1b7uk t48 = gm_l7;
                gm_t88rcucg1b7uk t49 = gm_l8;
                uint64_t t50 = gm_l11;
                gm_b16_8 t51 = gm_l4;
                gm_tl874t2atnlh3 t52 = gm_l5;
                void * t53 = gm_f_op_method_82pb0m6((void *)NULL, cx, NULL, t46, t47, t48, t49, t50, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t54 = 0;
                F.ret = t53;
                goto gm_fret;
            }
            gm_l12 = t44;
        }
        {
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
            uint64_t t58 = gm_l12;
            gm_t6qooom8qa2do t59;
            gm_f_resolve_decl_pus042v((void *)&t59, (void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t60 = t59;
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
            if (t61 != ((uint8_t)5ULL)) goto ie55;
            uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&t60) + 8));
            gm_l13 = t62;
            {
                uint8_t t65 = gm_l3;
                uint8_t t66 = (uint8_t)!t65;
                if (!t66) goto ie64;
                void * t67 = gm_l0;
                void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                uint64_t t69 = gm_l12;
                void * t70 = gm_l0;
                uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 8));
                uint8_t t72 = gm_f_decl_visible_bd168ti(t68, t69, t71);
                uint8_t t73 = (uint8_t)!t72;
                if (!t73) goto ie64;
                {
                    {
                        void * t76 = gm_l0;
                        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                        uint64_t t78 = gm_l11;
                        void * t79 = gm_l0;
                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 8));
                        gm_tf5srh4mp4b3i t81 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t77, t78, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t80);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                {
                    void * t82 = gm_l0;
                    void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
                    gm_tl874t2atnlh3 t84 = gm_l5;
                    void * t85 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t83, t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t86 = 0;
                    F.ret = t85;
                    goto gm_fret;
                }
                goto ix64;
ie64:;
ix64:;
            }
            {
                void * t87 = gm_l0;
                void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
                uint64_t t89 = gm_l12;
                void * t90 = gm_l0;
                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t90) + 8));
                gm_b0_1 t92 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t88, t89, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint32_t t93 = gm_l13;
                uint64_t t94 = (uint64_t)t93;
                uint64_t t95 = (uintptr_t)t94;
                void * t96 = gm_l0;
                void * t97 = GM_LD(void *, (((uint8_t *)t96) + 0));
                uint64_t t98 = GM_LD(uintptr_t, ((((uint8_t *)t97) + 344) + 0) + 8);
                if (t95 >= (uintptr_t)(t98)) gm_trap_index(t95, t98, gm_srcngug8co99ok1, 520, 12);
                gm_tqqhoivahirr0 t99 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t97) + 344) + 0)) + t95 * 136));
                F.l14 = t99;
            }
            {
                gm_tj8m6gmlgjilo t100;
                memset(&t100, 0, sizeof t100);
                F.l15 = t100;
            }
            {
                {
                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 40));
                    gm_l16 = t102;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t104 = gm_l17;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t106 = (t104 < t105);
                        if (!t106) goto lb103;
                        {
                            uint64_t t109 = gm_l17;
                            uint64_t t110 = (uintptr_t)t109;
                            uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            gm_t5v0c8ia6a5mo t114 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t110 * 56));
                            F.l18 = t114;
                            {
                                uint8_t t117;
                                if ((((((uint8_t *)&F.l18) + 40))[4] != 0)) goto mn119;
                                t117 = ((uint8_t)1);
                                goto me118;
mn119:;
                                t117 = ((uint8_t)0);
                                goto me118;
mn120:;
                                gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 525, 12);
me118:;
                                if (!t117) goto ie116;
                                {
                                    gm_t5v0c8ia6a5mo t122 = F.l18;
                                    gm_tj8m6gmlgjilo t123;
                                    GM_ST(uint8_t, (uint8_t *)&t123 + 57, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t123 + 58, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t123 + 60, 0);
                                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t123), t122);
                                    ((uint8_t *)&t123)[56] = 1;
                                    F.l15 = t123;
                                }
                                {
                                    goto lb103;
                                }
                                goto ix116;
ie116:;
ix116:;
                            }
                            goto me112;
mn113:;
                            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 524, 5);
me112:;
                        }
lc103:;
                        {
                            uint64_t t125 = gm_l17;
                            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                            uint64_t t128 = r126;
                            gm_l17 = t128;
                        }
                    }
lb103:;
                }
            }
            {
                if (!((((uint8_t *)&F.l15))[56] != 0)) goto ie129;
                gm_t5v0c8ia6a5mo t130 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)&F.l15));
                F.l19 = t130;
                {
                    void * t133 = gm_l6;
                    gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                    gm_t88rcucg1b7uk t135 = gm_l8;
                    uint8_t t136 = gm_f_takes_receiver_uhu9min(t133, GM_CONV(gm_csguankko4lmhd, t134), GM_CONV(gm_csguankko4lmhd, t135));
                    uint8_t t137 = (uint8_t)!t136;
                    if (!t137) goto ie132;
                    {
                        void * t140 = gm_l0;
                        void * t141 = gm_l1;
                        gm_t88rcucg1b7uk t142 = gm_l7;
                        gm_t88rcucg1b7uk t143 = gm_l8;
                        uint64_t t144 = gm_l11;
                        gm_b16_8 t145 = gm_l4;
                        gm_tl874t2atnlh3 t146 = gm_l5;
                        void * t147 = gm_f_op_method_82pb0m6((void *)NULL, cx, NULL, t140, t141, t142, t143, t144, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t145, t146);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t148 = t147;
                        if (!(GM_LD(void *, ((uint8_t *)&t148)) != 0)) goto ie139;
                        void * t149 = GM_LD(void *, ((uint8_t *)&t148));
                        gm_l20 = t149;
                        {
                            void * t151 = gm_l20;
                            uint8_t t152 = 0;
                            F.ret = t151;
                            goto gm_fret;
                        }
                        goto ix139;
ie139:;
ix139:;
                    }
                    goto ix132;
ie132:;
ix132:;
                }
                goto ix129;
ie129:;
ix129:;
            }
            {
                gm_tj8m6gmlgjilo t153 = F.l15;
                gm_tj8m6gmlgjilo t154 = t153;
                gm_t5v0c8ia6a5mo t155;
                if (((((uint8_t *)&t154))[56] != 0)) {
                    gm_t5v0c8ia6a5mo t156 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t154) + 0));
                    t155 = t156;
                } else {
                    {
                        void * t158 = gm_l0;
                        gm_tl874t2atnlh3 t159 = gm_l5;
                        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                        gm_tnpifv20irnph t161;
                        GM_ST(gm_b16_8, ((uint8_t *)&t161 + 0), t160);
                        gm_tabb710d9mq2e t162;
                        gm_f_format_4frhu1b((void *)&t162, (void *)NULL, cx, NULL, t161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t163 = t162;
                        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 8, gm_srcngug8co99ok1, 536, 24);
                        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&t163) + 8));
                        gm_b16_8 t165 = t164;
                        gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                        gm_b0_1 t167 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t158, t159, t166);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t168 = gm_l0;
                    void * t169 = GM_LD(void *, (((uint8_t *)t168) + 0));
                    gm_tl874t2atnlh3 t170 = gm_l5;
                    void * t171 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t169, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t172 = 0;
                    F.ret = t171;
                    goto gm_fret;
                }
                F.l21 = t155;
            }
            {
                void * t173 = gm_l0;
                void * t174 = gm_l1;
                gm_t88rcucg1b7uk t175 = gm_l7;
                gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                gm_tl874t2atnlh3 t177 = gm_l5;
                void * t178 = gm_f_adjust_receiver_2j7250l((void *)NULL, cx, NULL, t173, t174, t175, t176, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t179 = t178;
                void * t180;
                if ((GM_LD(void *, ((uint8_t *)&t179)) != 0)) {
                    void * t181 = GM_LD(void *, (((uint8_t *)&t179) + 0));
                    t180 = t181;
                } else {
                    void * t182 = gm_l0;
                    void * t183 = GM_LD(void *, (((uint8_t *)t182) + 0));
                    gm_tl874t2atnlh3 t184 = gm_l5;
                    void * t185 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t183, t184);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t186 = 0;
                    F.ret = t185;
                    goto gm_fret;
                }
                gm_l22 = t180;
            }
            {
                uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 80) + 8);
                uint8_t t189 = (t188 > ((uint64_t)0ULL));
                if (!t189) goto ie187;
                {
                    void * t191 = gm_l0;
                    uint32_t t192 = gm_l13;
                    void * t193 = gm_l22;
                    gm_b16_8 t194 = gm_l4;
                    gm_tl874t2atnlh3 t195 = gm_l5;
                    void * t196 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t191, t192, t193, t194, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t197 = 0;
                    F.ret = t196;
                    goto gm_fret;
                }
                goto ix187;
ie187:;
ix187:;
            }
            {
                void * t198 = gm_l0;
                uint64_t t199 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 40) + 8);
                void * t200 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 40)));
                uint64_t t201 = (uintptr_t)((uint64_t)1ULL);
                if (t201 > (uintptr_t)t199 || (uintptr_t)t199 > (uintptr_t)t199) gm_trap_slice(t201, (uintptr_t)t199, t199, gm_srcngug8co99ok1, 541, 27);
                gm_b16_8 t202;
                GM_ST(void *, (uint8_t *)&t202, (void *)(((uint8_t *)t200 + t201 * 56)));
                GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)((uintptr_t)t199 - t201));
                gm_b16_8 t203 = t202;
                gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 32));
                gm_b16_8 t206 = gm_l4;
                gm_tl874t2atnlh3 t207 = gm_l5;
                gm_b16_8 t208;
                GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sg7of7eks8a5u));
                GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(6));
                gm_ttu09lij36f8c t209;
                gm_f_bind_args_dnih8ih((void *)&t209, (void *)NULL, cx, NULL, t198, t204, t205, t206, t207, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttu09lij36f8c t210 = t209;
                gm_t6j6smj7qoctu t211;
                if (((((uint8_t *)&t210))[32] != 0)) {
                    gm_t6j6smj7qoctu t212 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t210) + 0));
                    t211 = t212;
                } else {
                    void * t213 = gm_l0;
                    void * t214 = GM_LD(void *, (((uint8_t *)t213) + 0));
                    gm_tl874t2atnlh3 t215 = gm_l5;
                    void * t216 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t214, t215);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t217 = 0;
                    F.ret = t216;
                    goto gm_fret;
                }
                gm_l23 = t211;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l24, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t218 = (void *)((uint8_t *)&F.l24);
                void * t219 = gm_l22;
                gm_tftb1odc739t5 t220 = gm_f_push_d943vgp_2(t218, &(*(gm_t69m56meqefs8 *)((uint8_t *)t219)));
                gm_tftb1odc739t5 t221 = t220;
                if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_srcngug8co99ok1, 543, 5);
                gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
            }
            {
                void * t223 = (void *)((uint8_t *)&F.l24);
                gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 0));
                gm_b16_8 t225 = t224;
                gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                gm_tftb1odc739t5 t227 = gm_f_extend_gig5o6g(t223, t226);
                gm_tftb1odc739t5 t228 = t227;
                if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_srcngug8co99ok1, 544, 5);
                gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
            }
            {
                void * t230 = gm_l0;
                void * t231 = (void *)(((uint8_t *)&F.l14) + 64);
                gm_tl874t2atnlh3 t232 = gm_l5;
                gm_b0_1 t233 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t230, t231, t232);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 56));
                gm_to76jli3bqddq t235;
                uint32_t t236 = gm_l13;
                gm_tsth9qu1dv0g3 t237;
                memset(&t237, 0, sizeof t237);
                GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t237 + 8), t236);
                GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t235 + 0, t237);
                gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t235 + 40, t238);
                gm_teti411eujvf4 t239;
                GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)34ULL));
                GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t239 + 8), t235);
                gm_tl874t2atnlh3 t240 = gm_l5;
                void * t241 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t234, &t239, t240);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t241;
            }
            {
                void * t242 = gm_l0;
                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 16));
                void * t244 = gm_l25;
                void * t245 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t242, t243, t244, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t246 = 0;
                F.ret = t245;
                goto gm_fret;
            }
            goto ix55;
ie55:;
            {
                void * t248 = gm_l0;
                void * t249 = gm_l1;
                gm_t88rcucg1b7uk t250 = gm_l7;
                gm_t88rcucg1b7uk t251 = gm_l8;
                uint64_t t252 = gm_l11;
                gm_b16_8 t253 = gm_l4;
                gm_tl874t2atnlh3 t254 = gm_l5;
                void * t255 = gm_f_op_method_82pb0m6((void *)NULL, cx, NULL, t248, t249, t250, t251, t252, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t253, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t256 = 0;
                F.ret = t255;
                goto gm_fret;
            }
ix55:;
        }
        goto ix22;
ie22:;
        {
            uint8_t t258 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
ix22:;
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
__attribute__((aligned(4))) void * gm_f_numeric_conversion_1ovq7at(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_numeric_conversion_1ovq7at F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_b16_8 gm_l2; /* n */
    uint8_t gm_l3; /* is_float */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_tvdg94bk7aa22 gm_l8; /* op */
    uint8_t gm_l9; /* t_int */
    uint8_t gm_l10; /* t_float */
    uint8_t gm_l11; /* ok */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_tn3r4rvlh0rsv gm_l13; /* <temporary array> */
    GM_STACK_CHECK(&F, 1250u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t6 = gm_l0;
        gm_b16_8 t7 = gm_l4;
        gm_b16_8 t8 = gm_l2;
        gm_tl874t2atnlh3 t9 = gm_l5;
        uint8_t t10 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t6, t7, ((uint64_t)1ULL), t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = (uint8_t)!t10;
        if (!t11) goto ie5;
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            gm_tl874t2atnlh3 t15 = gm_l5;
            void * t16 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t18 = gm_l0;
        gm_b16_8 t19 = gm_l4;
        gm_tl874t2atnlh3 t20 = gm_l5;
        gm_tkn42u7na3t81 t21 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t18, t19, ((uint64_t)0ULL), t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t22 = t21;
        gm_t88rcucg1b7uk t23;
        if (((((uint8_t *)&t22))[4] != 0)) {
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t22) + 0));
            t23 = t24;
        } else {
            uint8_t t25 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l7 = t23;
    }
    {
        gm_tvdg94bk7aa22 t26;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s7f68g16j7a3o, 5) == 0)) goto mn28;
        gm_tvdg94bk7aa22 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)1ULL));
        t26 = t29;
        goto me27;
mn28:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sjb60mo5hejk2, 5) == 0)) goto mn30;
        gm_tvdg94bk7aa22 t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
        t26 = t31;
        goto me27;
mn30:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_shne2m0cj5kgi, 5) == 0)) goto mn32;
        gm_tvdg94bk7aa22 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)3ULL));
        t26 = t33;
        goto me27;
mn32:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_shb5mqv6etf1t, 4) == 0)) goto mn34;
        gm_tvdg94bk7aa22 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)4ULL));
        t26 = t35;
        goto me27;
mn34:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s02419ud122hi, 7) == 0)) goto mn36;
        gm_tvdg94bk7aa22 t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)5ULL));
        t26 = t37;
        goto me27;
mn36:;
        gm_tvdg94bk7aa22 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)6ULL));
        t26 = t39;
        goto me27;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 224, 23);
me27:;
        gm_l8 = t26;
    }
    {
        uint8_t t40 = 0;
        void * t41 = gm_l6;
        gm_t88rcucg1b7uk t42 = gm_l7;
        uint8_t t43 = gm_f_is_integer_8d4f9ek(t41, GM_CONV(gm_csguankko4lmhd, t42));
        if (!t43) {
            void * t44 = gm_l0;
            gm_t88rcucg1b7uk t45 = gm_l7;
            gm_t9qkh0rsooc8d t46;
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)3ULL));
            uint8_t t47 = gm_f_param_with_qfvlipt(t44, GM_CONV(gm_csguankko4lmhd, t45), t46);
            t40 = t47;
        } else t40 = 1;
        gm_l9 = t40;
    }
    {
        uint8_t t48 = 0;
        void * t49 = gm_l6;
        gm_t88rcucg1b7uk t50 = gm_l7;
        gm_t53at80pfdus3 t51;
        gm_f_kind_jcnt4hm((void *)&t51, t49, GM_CONV(gm_csguankko4lmhd, t50));
        gm_t53at80pfdus3 t52 = t51;
        uint8_t t53;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
        if (t56 != ((uint8_t)3ULL)) goto mn55;
        t53 = ((uint8_t)1);
        goto me54;
mn55:;
        t53 = ((uint8_t)0);
        goto me54;
mn57:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 233, 16);
me54:;
        if (!t53) {
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = gm_l7;
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)5ULL));
            uint8_t t61 = gm_f_param_with_qfvlipt(t58, GM_CONV(gm_csguankko4lmhd, t59), t60);
            t48 = t61;
        } else t48 = 1;
        gm_l10 = t48;
    }
    {
        uint8_t t62;
        gm_tvdg94bk7aa22 t64 = gm_l8;
        gm_t0v9ekh8mobe3 t65;
        gm_tvdg94bk7aa22 t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t65 + 0, t66);
        gm_tvdg94bk7aa22 t67;
        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t67 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t65 + 12, t67);
        gm_tvdg94bk7aa22 t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)6ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t65 + 24, t68);
        gm_tvdg94bk7aa22 t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t65 + 36, t69);
        gm_t0v9ekh8mobe3 t70 = t65;
        uint8_t t71 = 0;
        gm_tvdg94bk7aa22 t72 = t64;
        if (gm_eqg63o53ar52(((uint8_t *)&t70) + 0, ((uint8_t *)&t72))) t71 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t70) + 12, ((uint8_t *)&t72))) t71 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t70) + 24, ((uint8_t *)&t72))) t71 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t70) + 36, ((uint8_t *)&t72))) t71 = 1;
        uint8_t t73 = (uint8_t)(t71);
        if (!t73) goto ie63;
        uint8_t t75 = gm_l9;
        uint8_t t76 = t75;
        t62 = t76;
        goto ix63;
ie63:;
        uint8_t t78 = 0;
        uint8_t t79 = gm_l9;
        if (t79) {
            uint8_t t80 = gm_l3;
            t78 = t80;
        } else t78 = 0;
        uint8_t t81 = t78;
        t62 = t81;
ix63:;
        gm_l11 = t62;
    }
    {
        uint8_t t83 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)2ULL)));
        if (!t83) goto ie82;
        uint8_t t84 = gm_l10;
        if (!t84) goto ie82;
        uint8_t t85 = gm_l9;
        uint8_t t86 = (uint8_t)!t85;
        if (!t86) goto ie82;
        {
            void * t88 = gm_l0;
            gm_tl874t2atnlh3 t89 = gm_l5;
            gm_t6j6smj7qoctu t90;
            void * t91 = gm_l0;
            gm_t88rcucg1b7uk t92 = gm_l7;
            gm_b16_8 t93 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t93);
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l7;
            gm_b16_8 t96 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 16, t96);
            gm_tabb710d9mq2e t97;
            gm_f_format_cpioo77((void *)&t97, (void *)NULL, cx, NULL, &t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t98 = t97;
            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_srcngug8co99ok1, 236, 24);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
            gm_b16_8 t100 = t99;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_b0_1 t102 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t88, t89, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix82;
ie82:;
        uint8_t t104 = gm_l11;
        uint8_t t105 = (uint8_t)!t104;
        if (!t105) goto ie103;
        {
            void * t107 = gm_l0;
            gm_tl874t2atnlh3 t108 = gm_l5;
            gm_t6j6smj7qoctu t109;
            gm_b16_8 t110 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 0, t110);
            void * t111 = gm_l0;
            gm_t88rcucg1b7uk t112 = gm_l7;
            gm_b16_8 t113 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 16, t113);
            gm_tabb710d9mq2e t114;
            gm_f_format_8fu7rb8((void *)&t114, (void *)NULL, cx, NULL, &t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t115 = t114;
            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 8, gm_srcngug8co99ok1, 238, 24);
            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
            gm_b16_8 t117 = t116;
            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
            gm_b0_1 t119 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t107, t108, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix103;
ie103:;
ix103:;
ix82:;
    }
    {
        gm_t88rcucg1b7uk t120;
        uint8_t t122 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)5ULL)));
        if (!t122) goto ie121;
        void * t124 = gm_l6;
        gm_t88rcucg1b7uk t125 = gm_l7;
        gm_t88rcucg1b7uk t126 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t124, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t127 = t126;
        t120 = t127;
        goto ix121;
ie121:;
        gm_t88rcucg1b7uk t129 = gm_l7;
        gm_t88rcucg1b7uk t130 = t129;
        t120 = t130;
ix121:;
        gm_l12 = t120;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t131 = gm_l8;
        gm_t88rcucg1b7uk t132 = gm_l12;
        {
            void * t134 = gm_l1;
            gm_tn3r4rvlh0rsv t135;
            GM_ST(void *, ((uint8_t *)&t135 + 0), t134);
            gm_l13 = t135;
        }
        void * t136 = ((uint8_t *)&gm_l13);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 241, 28);
        gm_b16_8 t137;
        GM_ST(void *, (uint8_t *)&t137, (void *)(((uint8_t *)t136 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t138 = t137;
        gm_tl874t2atnlh3 t139 = gm_l5;
        void * t140 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t131, t132, t138, t139);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t141 = 0;
        F.ret = t140;
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
__attribute__((aligned(4))) void * gm_f_numeric_method_jo5uq0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4, uint8_t a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_numeric_method_jo5uq0l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_b16_8 gm_l3; /* n */
    uint8_t gm_l4; /* is_int */
    uint8_t gm_l5; /* is_float */
    gm_b16_8 gm_l6; /* args */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_tn3r4rvlh0rsv gm_l9; /* one */
    gm_t88rcucg1b7uk gm_l10; /* t */
    void * gm_l11; /* zero */
    gm_t5fru0lp11n7t gm_l12; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l13; /* t */
    void * gm_l14; /* lo */
    void * gm_l15; /* hi */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    gm_thmgeksj5pdth gm_l17; /* <temporary array> */
    void * gm_l18; /* e */
    gm_t9qkh0rsooc8d gm_l19; /* flavor */
    gm_t88rcucg1b7uk gm_l20; /* ty */
    gm_t5fru0lp11n7t gm_l21; /* <temporary array> */
    gm_tvdg94bk7aa22 gm_l22; /* op */
    gm_t88rcucg1b7uk gm_l23; /* t */
    GM_STACK_CHECK(&F, 3405u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l8 = t4;
    }
    {
        void * t5 = gm_l1;
        gm_tn3r4rvlh0rsv t6;
        GM_ST(void *, ((uint8_t *)&t6 + 0), t5);
        gm_l9 = t6;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s7f68g16j7a3o, 5) == 0)) goto pa10;
        goto po9;
pa10:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sjb60mo5hejk2, 5) == 0)) goto pa11;
        goto po9;
pa11:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_shne2m0cj5kgi, 5) == 0)) goto pa12;
        goto po9;
pa12:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_shb5mqv6etf1t, 4) == 0)) goto pa13;
        goto po9;
pa13:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s02419ud122hi, 7) == 0)) goto pa14;
        goto po9;
pa14:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbanjlvk3n14i, 3) == 0)) goto pa15;
        goto po9;
pa15:;
        goto mn8;
po9:;
        void * t16 = gm_l0;
        void * t17 = gm_l1;
        gm_b16_8 t18 = gm_l3;
        uint8_t t19 = gm_l5;
        gm_b16_8 t20 = gm_l6;
        gm_tl874t2atnlh3 t21 = gm_l7;
        void * t22 = gm_f_numeric_conversion_1ovq7at((void *)NULL, cx, NULL, t16, t17, t18, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me7;
mn8:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sfrjcsmisriv1, 2) == 0)) goto mn24;
        {
            void * t27 = gm_l0;
            gm_b16_8 t28 = gm_l6;
            gm_b16_8 t29 = gm_l3;
            gm_tl874t2atnlh3 t30 = gm_l7;
            uint8_t t31 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t27, t28, ((uint64_t)1ULL), t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t32 = (uint8_t)!t31;
            if (!t32) goto ie26;
            {
                void * t34 = gm_l0;
                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                gm_tl874t2atnlh3 t36 = gm_l7;
                void * t37 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t38 = 0;
                F.ret = t37;
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            void * t39 = gm_l0;
            gm_b16_8 t40 = gm_l6;
            gm_tl874t2atnlh3 t41 = gm_l7;
            gm_tkn42u7na3t81 t42 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t39, t40, ((uint64_t)0ULL), t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t43 = t42;
            gm_t88rcucg1b7uk t44;
            if (((((uint8_t *)&t43))[4] != 0)) {
                gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t43) + 0));
                t44 = t45;
            } else {
                uint8_t t46 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l10 = t44;
        }
        {
            void * t48 = gm_l8;
            gm_t88rcucg1b7uk t49 = gm_l10;
            gm_t53at80pfdus3 t50;
            gm_f_kind_jcnt4hm((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
            gm_t53at80pfdus3 t51 = t50;
            uint8_t t52;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t51) + 0));
            if (t55 != ((uint8_t)3ULL)) goto mn54;
            t52 = ((uint8_t)0);
            goto me53;
mn54:;
            t52 = ((uint8_t)1);
            goto me53;
mn56:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 152, 16);
me53:;
            if (!t52) goto ie47;
            void * t57 = gm_l0;
            gm_t88rcucg1b7uk t58 = gm_l10;
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)5ULL));
            uint8_t t60 = gm_f_param_with_qfvlipt(t57, GM_CONV(gm_csguankko4lmhd, t58), t59);
            uint8_t t61 = (uint8_t)!t60;
            if (!t61) goto ie47;
            {
                void * t63 = gm_l0;
                gm_tl874t2atnlh3 t64 = gm_l7;
                void * t65 = gm_l0;
                gm_t88rcucg1b7uk t66 = gm_l10;
                gm_b16_8 t67 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t68;
                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
                gm_tabb710d9mq2e t69;
                gm_f_format_qe2mgnq((void *)&t69, (void *)NULL, cx, NULL, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t70 = t69;
                if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srcngug8co99ok1, 153, 32);
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
                gm_b16_8 t72 = t71;
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t64, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            gm_tvdg94bk7aa22 t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t76 = gm_l10;
            void * t77 = ((uint8_t *)&gm_l9);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 155, 39);
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t77 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t79 = t78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_tl874t2atnlh3 t81 = gm_l7;
            void * t82 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t75, t76, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto me7;
mn24:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6a2c11qu9scr, 12) == 0)) goto mn84;
        uint8_t t86 = gm_l4;
        if (!t86) goto ie85;
        {
            {
                void * t89 = gm_l0;
                gm_b16_8 t90 = gm_l6;
                gm_b16_8 t91 = gm_l3;
                gm_tl874t2atnlh3 t92 = gm_l7;
                uint8_t t93 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t89, t90, ((uint64_t)0ULL), t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t94 = gm_l0;
            void * t95 = (void *)(((uint8_t *)t94) + 320);
            gm_t91vm1g0k2rpm t96;
            memset((uint8_t *)&t96 + 34, 0, 6);
            void * t97 = gm_l1;
            gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t96 + 0, t98);
            gm_tl874t2atnlh3 t99 = gm_l7;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t96 + 4, t99);
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s6a2c11qu9scr));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(12));
            GM_ST(gm_b16_8, (uint8_t *)&t96 + 16, t100);
            GM_ST(uint8_t, (uint8_t *)&t96 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t96 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t101;
            GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t101 + 8), t96);
            gm_tftb1odc739t5 t102 = gm_f_push_7koe9lk(t95, &t101);
            gm_tftb1odc739t5 t103 = t102;
            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srcngug8co99ok1, 159, 13);
            gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
        }
        {
            void * t105 = gm_l1;
            gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t105) + 0));
            gm_tbci5ospk5nnn t107;
            gm_f_zero_s25l990((void *)&t107, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t108 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t108 + 8), t107);
            gm_tl874t2atnlh3 t109 = gm_l7;
            void * t110 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t106, &t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t110;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)20ULL));
            void * t112 = gm_l8;
            void * t113 = gm_l0;
            void * t114 = (void *)(((uint8_t *)t113) + 96);
            void * t115 = gm_l0;
            void * t116 = GM_LD(void *, (((uint8_t *)t115) + 0));
            void * t117 = gm_l1;
            gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t117) + 0));
            gm_t88rcucg1b7uk t119 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t114, t116, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t120 = gm_f_unsigned_of_n48qlse((void *)NULL, cx, NULL, t112, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t122 = gm_l1;
                void * t123 = gm_l11;
                gm_t5fru0lp11n7t t124;
                GM_ST(void *, ((uint8_t *)&t124 + 0), t122);
                GM_ST(void *, ((uint8_t *)&t124 + 8), t123);
                gm_l12 = t124;
            }
            void * t125 = ((uint8_t *)&gm_l12);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcngug8co99ok1, 161, 89);
            gm_b16_8 t126;
            GM_ST(void *, (uint8_t *)&t126, (void *)(((uint8_t *)t125 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t127 = t126;
            gm_tl874t2atnlh3 t128 = gm_l7;
            void * t129 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t111, t120, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t130 = 0;
            F.ret = t129;
            goto gm_fret;
        }
        goto ix85;
ie85:;
ix85:;
        goto me7;
mn84:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6vvtrmtnt3j1, 4) == 0)) goto mn131;
        {
            void * t134 = gm_l0;
            gm_b16_8 t135 = gm_l6;
            gm_b16_8 t136 = gm_l3;
            gm_tl874t2atnlh3 t137 = gm_l7;
            uint8_t t138 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t134, t135, ((uint64_t)1ULL), t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t139 = (uint8_t)!t138;
            if (!t139) goto ie133;
            {
                void * t141 = gm_l0;
                void * t142 = GM_LD(void *, (((uint8_t *)t141) + 0));
                gm_tl874t2atnlh3 t143 = gm_l7;
                void * t144 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t142, t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t145 = 0;
                F.ret = t144;
                goto gm_fret;
            }
            goto ix133;
ie133:;
ix133:;
        }
        {
            void * t146 = gm_l0;
            gm_b16_8 t147 = gm_l6;
            gm_tl874t2atnlh3 t148 = gm_l7;
            gm_tkn42u7na3t81 t149 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t146, t147, ((uint64_t)0ULL), t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t150 = t149;
            gm_t88rcucg1b7uk t151;
            if (((((uint8_t *)&t150))[4] != 0)) {
                gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t150) + 0));
                t151 = t152;
            } else {
                uint8_t t153 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l13 = t151;
        }
        {
            void * t154 = gm_l0;
            void * t155 = gm_l1;
            gm_t88rcucg1b7uk t156 = gm_l13;
            gm_tl874t2atnlh3 t157 = gm_l7;
            void * t158 = gm_f_bits_cast_0kaubje((void *)NULL, cx, NULL, t154, t155, t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t159 = 0;
            F.ret = t158;
            goto gm_fret;
        }
        goto me7;
mn131:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0)) goto pa162;
        goto po161;
pa162:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s2mtoab140jp6, 3) == 0)) goto pa163;
        goto po161;
pa163:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s7u0p3hthk80r, 8) == 0)) goto pa164;
        goto po161;
pa164:;
        goto mn160;
po161:;
        void * t165 = gm_l0;
        void * t166 = gm_l1;
        gm_b16_8 t167 = gm_l3;
        gm_b16_8 t168 = gm_l6;
        gm_tl874t2atnlh3 t169 = gm_l7;
        void * t170 = gm_f_numeric_pair_54urfaa((void *)NULL, cx, NULL, t165, t166, t167, t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t171 = 0;
        F.ret = t170;
        goto gm_fret;
        goto me7;
mn160:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm139pecc5n3p, 5) == 0)) goto mn172;
        {
            void * t175 = gm_l0;
            gm_b16_8 t176 = gm_l6;
            gm_b16_8 t177 = gm_l3;
            gm_tl874t2atnlh3 t178 = gm_l7;
            uint8_t t179 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t175, t176, ((uint64_t)2ULL), t177, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t180 = (uint8_t)!t179;
            if (!t180) goto ie174;
            {
                void * t182 = gm_l0;
                void * t183 = GM_LD(void *, (((uint8_t *)t182) + 0));
                gm_tl874t2atnlh3 t184 = gm_l7;
                void * t185 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t183, t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t186 = 0;
                F.ret = t185;
                goto gm_fret;
            }
            goto ix174;
ie174:;
ix174:;
        }
        {
            void * t187 = gm_l0;
            gm_b16_8 t188 = gm_l6;
            void * t189 = gm_l1;
            gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t189) + 0));
            gm_tl874t2atnlh3 t191 = gm_l7;
            void * t192 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t187, t188, ((uint64_t)0ULL), t190, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t192;
        }
        {
            void * t193 = gm_l0;
            gm_b16_8 t194 = gm_l6;
            void * t195 = gm_l1;
            gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t195) + 0));
            gm_tl874t2atnlh3 t197 = gm_l7;
            void * t198 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t193, t194, ((uint64_t)1ULL), t196, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t198;
        }
        {
            void * t199 = gm_l0;
            void * t200 = gm_l1;
            gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t200) + 0));
            void * t202 = gm_l14;
            gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t202) + 0));
            gm_tl874t2atnlh3 t204 = gm_l7;
            gm_b16_8 t205;
            GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s8m12els4res5));
            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(25));
            gm_t88rcucg1b7uk t206 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t199, t201, t203, t204, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t206;
        }
        {
            {
                void * t208 = gm_l0;
                gm_t88rcucg1b7uk t209 = gm_l16;
                void * t210 = gm_l15;
                gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t210) + 0));
                gm_tl874t2atnlh3 t212 = gm_l7;
                gm_b16_8 t213;
                GM_ST(void *, (uint8_t *)&t213, (void *)(gm_s8m12els4res5));
                GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(25));
                gm_t88rcucg1b7uk t214 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t208, t209, t211, t212, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t215;
            GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t215 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)18ULL));
            gm_t88rcucg1b7uk t216 = gm_l16;
            {
                void * t218 = gm_l1;
                void * t219 = gm_l14;
                void * t220 = gm_l15;
                GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l17)) + 0, t218);
                GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l17)) + 8, t219);
                GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l17)) + 16, t220);
            }
            void * t221 = ((uint8_t *)&gm_l17);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcngug8co99ok1, 175, 40);
            gm_b16_8 t222;
            GM_ST(void *, (uint8_t *)&t222, (void *)(((uint8_t *)t221 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_b16_8 t223 = t222;
            gm_tl874t2atnlh3 t224 = gm_l7;
            void * t225 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t215, t216, t223, t224);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t226 = 0;
            F.ret = t225;
            goto gm_fret;
        }
        goto me7;
mn172:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbl3pbs9195ho, 3) == 0)) goto mn227;
        {
            {
                void * t230 = gm_l0;
                gm_b16_8 t231 = gm_l6;
                gm_b16_8 t232 = gm_l3;
                gm_tl874t2atnlh3 t233 = gm_l7;
                uint8_t t234 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t230, t231, ((uint64_t)0ULL), t232, t233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t235 = gm_l0;
            void * t236 = (void *)(((uint8_t *)t235) + 320);
            gm_t91vm1g0k2rpm t237;
            memset((uint8_t *)&t237 + 34, 0, 6);
            void * t238 = gm_l1;
            gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t238) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t237 + 0, t239);
            gm_tl874t2atnlh3 t240 = gm_l7;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t237 + 4, t240);
            gm_b16_8 t241;
            GM_ST(void *, (uint8_t *)&t241, (void *)(gm_sbl3pbs9195ho));
            GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t237 + 16, t241);
            GM_ST(uint8_t, (uint8_t *)&t237 + 32, ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&t237 + 33, ((uint8_t)1));
            gm_tpm220j5qhjke t242;
            GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t242 + 8), t237);
            gm_tftb1odc739t5 t243 = gm_f_push_7koe9lk(t236, &t242);
            gm_tftb1odc739t5 t244 = t243;
            if (((((uint8_t *)&t244))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t244) + 1, gm_srcngug8co99ok1, 179, 13);
            gm_b0_1 t245 = GM_LD(gm_b0_1, (((uint8_t *)&t244) + 1));
        }
        {
            gm_tvdg94bk7aa22 t246;
            GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t246 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)19ULL));
            void * t247 = gm_l1;
            gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t247) + 0));
            void * t249 = ((uint8_t *)&gm_l9);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 180, 43);
            gm_b16_8 t250;
            GM_ST(void *, (uint8_t *)&t250, (void *)(((uint8_t *)t249 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t251 = t250;
            gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
            gm_tl874t2atnlh3 t253 = gm_l7;
            void * t254 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t246, t248, t252, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t255 = 0;
            F.ret = t254;
            goto gm_fret;
        }
        goto me7;
mn227:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_svpofgjtij7vj, 3) == 0)) goto pa258;
        goto po257;
pa258:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgemqs85b1iol, 8) == 0)) goto pa259;
        goto po257;
pa259:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s7moa4u0q6e5d, 7) == 0)) goto pa260;
        goto po257;
pa260:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 11) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s9ahj85e3uamt, 11) == 0)) goto pa261;
        goto po257;
pa261:;
        goto mn256;
po257:;
        uint8_t t263 = gm_l4;
        if (!t263) goto ie262;
        {
            void * t266 = gm_l0;
            gm_b16_8 t267 = gm_l6;
            gm_b16_8 t268 = gm_l3;
            gm_tl874t2atnlh3 t269 = gm_l7;
            uint8_t t270 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t266, t267, ((uint64_t)1ULL), t268, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t271 = (uint8_t)!t270;
            if (!t271) goto ie265;
            {
                void * t273 = gm_l0;
                void * t274 = GM_LD(void *, (((uint8_t *)t273) + 0));
                gm_tl874t2atnlh3 t275 = gm_l7;
                void * t276 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t274, t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t277 = 0;
                F.ret = t276;
                goto gm_fret;
            }
            goto ix265;
ie265:;
ix265:;
        }
        {
            void * t278 = gm_l0;
            gm_b16_8 t279 = gm_l6;
            void * t280 = gm_l8;
            gm_t88rcucg1b7uk t281 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t280, ((uint8_t)0), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t282 = gm_l7;
            void * t283 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t278, t279, ((uint64_t)0ULL), t281, t282);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t283;
        }
        {
            gm_t9qkh0rsooc8d t284;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_svpofgjtij7vj, 3) == 0)) goto mn286;
            gm_t9qkh0rsooc8d t287;
            GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)0ULL));
            t284 = t287;
            goto me285;
mn286:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgemqs85b1iol, 8) == 0)) goto mn288;
            gm_t9qkh0rsooc8d t289;
            GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)1ULL));
            t284 = t289;
            goto me285;
mn288:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s7moa4u0q6e5d, 7) == 0)) goto mn290;
            gm_t9qkh0rsooc8d t291;
            GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)2ULL));
            t284 = t291;
            goto me285;
mn290:;
            gm_t9qkh0rsooc8d t293;
            GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)3ULL));
            t284 = t293;
            goto me285;
mn292:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 185, 34);
me285:;
            gm_l19 = t284;
        }
        {
            gm_t88rcucg1b7uk t294;
            uint8_t t296 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l19) + 0) == ((uint8_t)3ULL)));
            if (!t296) goto ie295;
            void * t298 = gm_l8;
            void * t299 = gm_l1;
            gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t299) + 0));
            gm_t88rcucg1b7uk t301 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t298, t300);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t302 = t301;
            t294 = t302;
            goto ix295;
ie295:;
            void * t304 = gm_l1;
            gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 0));
            gm_t88rcucg1b7uk t306 = t305;
            t294 = t306;
ix295:;
            gm_l20 = t294;
        }
        {
        }
        {
            gm_t9qkh0rsooc8d t307 = gm_l19;
            gm_tvdg94bk7aa22 t308;
            GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t308 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t308 + 6, 0);
            GM_ST(uint32_t, (uint8_t *)&t308 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t308 + 4), t307);
            gm_t88rcucg1b7uk t309 = gm_l20;
            {
                void * t311 = gm_l1;
                void * t312 = gm_l18;
                gm_t5fru0lp11n7t t313;
                GM_ST(void *, ((uint8_t *)&t313 + 0), t311);
                GM_ST(void *, ((uint8_t *)&t313 + 8), t312);
                gm_l21 = t313;
            }
            void * t314 = ((uint8_t *)&gm_l21);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcngug8co99ok1, 192, 46);
            gm_b16_8 t315;
            GM_ST(void *, (uint8_t *)&t315, (void *)(((uint8_t *)t314 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t316 = t315;
            gm_tl874t2atnlh3 t317 = gm_l7;
            void * t318 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t308, t309, t316, t317);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t319 = 0;
            F.ret = t318;
            goto gm_fret;
        }
        goto ix262;
ie262:;
ix262:;
        goto me7;
mn256:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_ss3f61htk015v, 6) == 0)) goto pa322;
        goto po321;
pa322:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s541nrvershlm, 6) == 0)) goto pa323;
        goto po321;
pa323:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s7uql3407tind, 9) == 0)) goto pa324;
        goto po321;
pa324:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s3hqs40g2kokp, 8) == 0)) goto pa325;
        goto po321;
pa325:;
        goto mn320;
po321:;
        uint8_t t327 = gm_l5;
        if (!t327) goto ie326;
        {
            {
                void * t330 = gm_l0;
                gm_b16_8 t331 = gm_l6;
                gm_b16_8 t332 = gm_l3;
                gm_tl874t2atnlh3 t333 = gm_l7;
                uint8_t t334 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t330, t331, ((uint64_t)0ULL), t332, t333);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_tvdg94bk7aa22 t335;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_ss3f61htk015v, 6) == 0)) goto mn337;
            gm_tvdg94bk7aa22 t338;
            GM_ST(uint8_t, (uint8_t *)&t338 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t338 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t338 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t338 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t338 + 0), ((uint8_t)22ULL));
            t335 = t338;
            goto me336;
mn337:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s541nrvershlm, 6) == 0)) goto mn339;
            gm_tvdg94bk7aa22 t340;
            GM_ST(uint8_t, (uint8_t *)&t340 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t340 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t340 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t340 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)23ULL));
            t335 = t340;
            goto me336;
mn339:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s7uql3407tind, 9) == 0)) goto mn341;
            gm_tvdg94bk7aa22 t342;
            GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t342 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t342 + 0), ((uint8_t)24ULL));
            t335 = t342;
            goto me336;
mn341:;
            gm_tvdg94bk7aa22 t344;
            GM_ST(uint8_t, (uint8_t *)&t344 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t344 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t344 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t344 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t344 + 0), ((uint8_t)25ULL));
            t335 = t344;
            goto me336;
mn343:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 196, 31);
me336:;
            gm_l22 = t335;
        }
        {
            gm_tvdg94bk7aa22 t345 = gm_l22;
            void * t346 = gm_l8;
            gm_t88rcucg1b7uk t347 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t346);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t348 = ((uint8_t *)&gm_l9);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 202, 47);
            gm_b16_8 t349;
            GM_ST(void *, (uint8_t *)&t349, (void *)(((uint8_t *)t348 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t350 = t349;
            gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
            gm_tl874t2atnlh3 t352 = gm_l7;
            void * t353 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t345, t347, t351, t352);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t354 = 0;
            F.ret = t353;
            goto gm_fret;
        }
        goto ix326;
ie326:;
ix326:;
        goto me7;
mn320:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s398novfl4jrd, 4) == 0)) goto mn355;
        uint8_t t357 = gm_l5;
        if (!t357) goto ie356;
        {
            {
                void * t360 = gm_l0;
                gm_b16_8 t361 = gm_l6;
                gm_b16_8 t362 = gm_l3;
                gm_tl874t2atnlh3 t363 = gm_l7;
                uint8_t t364 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t360, t361, ((uint64_t)0ULL), t362, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_tvdg94bk7aa22 t365;
            GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t365 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)26ULL));
            void * t366 = gm_l1;
            gm_t88rcucg1b7uk t367 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t366) + 0));
            void * t368 = ((uint8_t *)&gm_l9);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 206, 44);
            gm_b16_8 t369;
            GM_ST(void *, (uint8_t *)&t369, (void *)(((uint8_t *)t368 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t369 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t370 = t369;
            gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)&t370));
            gm_tl874t2atnlh3 t372 = gm_l7;
            void * t373 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t365, t367, t371, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t374 = 0;
            F.ret = t373;
            goto gm_fret;
        }
        goto ix356;
ie356:;
ix356:;
        goto me7;
mn355:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sn6gnsehoh5ru, 6) == 0)) goto mn375;
        gm_t88rcucg1b7uk t377 = gm_l2;
        void * t378 = gm_l8;
        gm_t88rcucg1b7uk t379 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t378);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t380 = t377;
        gm_t88rcucg1b7uk t381 = t379;
        uint8_t t382 = (gm_eq9m6mvtmgde(((uint8_t *)&t380), ((uint8_t *)&t381)));
        if (!t382) goto ie376;
        {
            void * t384 = gm_l0;
            gm_b16_8 t385 = gm_l6;
            gm_tl874t2atnlh3 t386 = gm_l7;
            gm_tkn42u7na3t81 t387 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t384, t385, ((uint64_t)0ULL), t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t388 = t387;
            gm_t88rcucg1b7uk t389;
            if (((((uint8_t *)&t388))[4] != 0)) {
                gm_t88rcucg1b7uk t390 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t388) + 0));
                t389 = t390;
            } else {
                uint8_t t391 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l23 = t389;
        }
        {
            gm_tvdg94bk7aa22 t392;
            GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t392 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)13ULL));
            void * t393 = gm_l8;
            gm_t88rcucg1b7uk t394 = gm_l23;
            gm_t88rcucg1b7uk t395 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t393, t394);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t396 = ((uint8_t *)&gm_l9);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 210, 53);
            gm_b16_8 t397;
            GM_ST(void *, (uint8_t *)&t397, (void *)(((uint8_t *)t396 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t397 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t398 = t397;
            gm_b16_8 t399 = GM_LD(gm_b16_8, ((uint8_t *)&t398));
            gm_tl874t2atnlh3 t400 = gm_l7;
            void * t401 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t392, t395, t399, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t402 = 0;
            F.ret = t401;
            goto gm_fret;
        }
        goto ix376;
ie376:;
ix376:;
        goto me7;
mn375:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sasg9nroe4gk1, 3) == 0)) goto pa405;
        goto po404;
pa405:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm1kt2tr7m23b, 3) == 0)) goto pa406;
        goto po404;
pa406:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s1mud7cvvuuon, 3) == 0)) goto pa407;
        goto po404;
pa407:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_srk226j8lb4rq, 8) == 0)) goto pa408;
        goto po404;
pa408:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_st4r3sao0kja5, 5) == 0)) goto pa409;
        goto po404;
pa409:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_skgcaidqnnhjr, 10) == 0)) goto pa410;
        goto po404;
pa410:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_skj7cpsc7j6g9, 4) == 0)) goto pa411;
        goto po404;
pa411:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sd2iiq8cuaks3, 4) == 0)) goto pa412;
        goto po404;
pa412:;
        goto mn403;
po404:;
        void * t413 = gm_l0;
        gm_tl874t2atnlh3 t414 = gm_l7;
        gm_b16_8 t415 = gm_l3;
        gm_tnpifv20irnph t416;
        GM_ST(gm_b16_8, ((uint8_t *)&t416 + 0), t415);
        gm_tabb710d9mq2e t417;
        gm_f_format_7b9kq2t((void *)&t417, (void *)NULL, cx, NULL, t416);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t418 = t417;
        if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 8, gm_srcngug8co99ok1, 212, 111);
        gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)&t418) + 8));
        gm_b16_8 t420 = t419;
        gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)&t420));
        void * t422 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t413, t414, t421);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t423 = 0;
        F.ret = t422;
        goto gm_fret;
        goto me7;
mn403:;
        goto me7;
mn424:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 146, 5);
me7:;
    }
    {
        uint8_t t426 = 0;
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
__attribute__((aligned(4))) void * gm_f_numeric_pair_54urfaa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_numeric_pair_54urfaa F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* y */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    gm_tvdg94bk7aa22 gm_l7; /* op */
    gm_t88rcucg1b7uk gm_l8; /* rty */
    gm_t5fru0lp11n7t gm_l9; /* <temporary array> */
    GM_STACK_CHECK(&F, 800u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_l3;
        gm_b16_8 t5 = gm_l2;
        gm_tl874t2atnlh3 t6 = gm_l4;
        uint8_t t7 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t3, t4, ((uint64_t)1ULL), t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            gm_tl874t2atnlh3 t12 = gm_l4;
            void * t13 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        gm_b16_8 t16 = gm_l3;
        void * t17 = gm_l1;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t17) + 0));
        gm_tl874t2atnlh3 t19 = gm_l4;
        void * t20 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t15, t16, ((uint64_t)0ULL), t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + 0));
        void * t24 = gm_l5;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t24) + 0));
        gm_tl874t2atnlh3 t26 = gm_l4;
        gm_b16_8 t27 = gm_l2;
        gm_tnpifv20irnph t28;
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
        gm_tabb710d9mq2e t29;
        gm_f_format_0cb2lu1((void *)&t29, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcngug8co99ok1, 248, 50);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_t88rcucg1b7uk t34 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t21, t23, t25, t26, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t34;
    }
    {
        gm_tvdg94bk7aa22 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)16ULL));
        gm_l7 = t35;
    }
    {
        gm_t88rcucg1b7uk t36 = gm_l6;
        gm_l8 = t36;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sm3o0uerjumpm, 3) == 0)) goto mn38;
        goto me37;
mn38:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s2mtoab140jp6, 3) == 0)) goto mn40;
        {
            gm_tvdg94bk7aa22 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)17ULL));
            gm_l7 = t42;
        }
        goto me37;
mn40:;
        {
            void * t45 = gm_l0;
            void * t46 = (void *)(((uint8_t *)t45) + 320);
            gm_t91vm1g0k2rpm t47;
            memset((uint8_t *)&t47 + 34, 0, 6);
            gm_t88rcucg1b7uk t48 = gm_l6;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t47 + 0, t48);
            gm_tl874t2atnlh3 t49 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t47 + 4, t49);
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s7u0p3hthk80r));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&t47 + 16, t50);
            GM_ST(uint8_t, (uint8_t *)&t47 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t47 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t51 + 8), t47);
            gm_tftb1odc739t5 t52 = gm_f_push_7koe9lk(t46, &t51);
            gm_tftb1odc739t5 t53 = t52;
            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcngug8co99ok1, 255, 13);
            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
        }
        {
            gm_tvdg94bk7aa22 t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)20ULL));
            gm_l7 = t55;
        }
        {
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
            void * t58 = (void *)(((uint8_t *)t57) + 8);
            void * t59 = gm_l0;
            void * t60 = (void *)(((uint8_t *)t59) + 96);
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
            gm_t88rcucg1b7uk t63 = gm_l6;
            gm_t88rcucg1b7uk t64 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t60, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t65 = gm_f_unsigned_of_n48qlse((void *)NULL, cx, NULL, t58, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t65;
        }
        goto me37;
mn43:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 251, 5);
me37:;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t66 = gm_l7;
        gm_t88rcucg1b7uk t67 = gm_l8;
        {
            void * t69 = gm_l1;
            void * t70 = gm_l5;
            gm_t5fru0lp11n7t t71;
            GM_ST(void *, ((uint8_t *)&t71 + 0), t69);
            GM_ST(void *, ((uint8_t *)&t71 + 8), t70);
            gm_l9 = t71;
        }
        void * t72 = ((uint8_t *)&gm_l9);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcngug8co99ok1, 260, 29);
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t72 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t74 = t73;
        gm_tl874t2atnlh3 t75 = gm_l4;
        void * t76 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t66, t67, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_op_method_82pb0m6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, uint64_t a4, const void * a5, gm_b16_8 a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_op_method_82pb0m6 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_t88rcucg1b7uk gm_l3; /* s */
    uint64_t gm_l4; /* module */
    gm_t08olq97rscn3 gm_l5; /* name */
    gm_b16_8 gm_l6; /* args */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t3877pk7lv748 gm_l11; /* n */
    uint32_t gm_l12; /* eff */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* op_i */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_t88rcucg1b7uk gm_l19; /* x */
    void * gm_l20; /* r */
    gm_t6j6smj7qoctu gm_l21; /* bound */
    gm_tiilmfd16v3fc gm_l23; /* eref */
    void * gm_l24; /* call */
    GM_STACK_CHECK(&F, 2586u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l6 = a6;
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l8 = t4;
    }
    {
        {
            uint64_t t6 = gm_l4;
            uint64_t t7 = (uintptr_t)t6;
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 200) + 0) + 8);
            if (t7 >= (uintptr_t)(t11)) gm_trap_index(t7, t11, gm_srcngug8co99ok1, 571, 14);
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t10) + 200) + 0)) + t7 * 184) + 16) + 0));
            gm_l9 = t12;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l10;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l10;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_t3877pk7lv748 t24 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t20 * 24));
                    gm_l11 = t24;
                    {
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                        gm_t6qooom8qa2do t30;
                        gm_f_resolve_decl_pus042v((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        gm_t6qooom8qa2do t31 = t30;
                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                        if (t32 != ((uint8_t)2ULL)) goto ie26;
                        uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&t31) + 8));
                        gm_l12 = t33;
                        {
                            uint32_t t35 = gm_l12;
                            uint64_t t36 = (uint64_t)t35;
                            uint64_t t37 = (uintptr_t)t36;
                            void * t38 = gm_l0;
                            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
                            uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 1048) + 0) + 8);
                            if (t37 >= (uintptr_t)(t40)) gm_trap_index(t37, t40, gm_srcngug8co99ok1, 573, 17);
                            gm_tammvh1iespi3 t41 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t39) + 1048) + 0)) + t37 * 56));
                            F.l13 = t41;
                        }
                        {
                            uint8_t t43 = 0;
                            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 16));
                            uint64_t t45 = gm_l4;
                            uint8_t t46 = (!(t44 == t45));
                            if (!t46) {
                                uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 24) + 8);
                                uint8_t t48 = (t47 > ((uint64_t)0ULL));
                                t43 = t48;
                            } else t43 = 1;
                            if (!t43) goto ie42;
                            {
                                goto lc13;
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        {
                            void * t51 = gm_l0;
                            void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
                            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                            void * t54 = gm_l0;
                            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
                            uint8_t t56 = gm_f_decl_visible_bd168ti(t52, t53, t55);
                            uint8_t t57 = (uint8_t)!t56;
                            if (!t57) goto ie50;
                            {
                                goto lc13;
                            }
                            goto ix50;
ie50:;
ix50:;
                        }
                        {
                            {
                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 40));
                                gm_b16_8 t61 = t60;
                                gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                                gm_l14 = t62;
                            }
                            {
                                gm_l15 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t64 = gm_l15;
                                    uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    uint8_t t66 = (t64 < t65);
                                    if (!t66) goto lb63;
                                    {
                                        gm_tdvhflnc2f5o8 t69;
                                        uint64_t t70 = gm_l15;
                                        GM_ST(uint64_t, (uint8_t *)&t69 + 0, t70);
                                        uint64_t t71 = gm_l15;
                                        uint64_t t72 = (uintptr_t)t71;
                                        uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                        memcpy((uint8_t *)&t69 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t72 * 48), 48);
                                        gm_tdvhflnc2f5o8 t74 = t69;
                                        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&t74) + 0));
                                        gm_l16 = t77;
                                        gm_tv0k2vg3054a6 t78 = GM_LD(gm_tv0k2vg3054a6, (((uint8_t *)&t74) + 8));
                                        F.l17 = t78;
                                        {
                                            uint8_t t81 = 0;
                                            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)a5) + 0));
                                            gm_b16_8 t84 = t82;
                                            gm_b16_8 t85 = t83;
                                            uint8_t t86 = (!gm_eq0iqiubfm7e(((uint8_t *)&t84), ((uint8_t *)&t85)));
                                            if (!t86) {
                                                uint64_t t87 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                                uint8_t t88 = ((t87 == ((uint64_t)0ULL)));
                                                t81 = t88;
                                            } else t81 = 1;
                                            if (!t81) goto ie80;
                                            {
                                                goto lc63;
                                            }
                                            goto ix80;
ie80:;
ix80:;
                                        }
                                        {
                                            uint64_t t90 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                            if (t90 >= (uintptr_t)(t91)) gm_trap_index(t90, t91, gm_srcngug8co99ok1, 578, 18);
                                            gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 24)) + t90 * 56) + 16));
                                            gm_l18 = t92;
                                        }
                                        {
                                            void * t94 = gm_l8;
                                            gm_t88rcucg1b7uk t95 = gm_l18;
                                            gm_t53at80pfdus3 t96;
                                            gm_f_kind_jcnt4hm((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
                                            gm_t53at80pfdus3 t97 = t96;
                                            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t97) + 0));
                                            if (t98 != ((uint8_t)8ULL)) goto ie93;
                                            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t97) + 8) + 4));
                                            gm_l19 = t99;
                                            {
                                                gm_t88rcucg1b7uk t101 = gm_l19;
                                                gm_l18 = t101;
                                            }
                                            goto ix93;
ie93:;
ix93:;
                                        }
                                        {
                                            gm_t88rcucg1b7uk t103 = gm_l18;
                                            gm_t88rcucg1b7uk t104 = gm_l3;
                                            gm_t88rcucg1b7uk t105 = t103;
                                            gm_t88rcucg1b7uk t106 = t104;
                                            uint8_t t107 = (!gm_eq9m6mvtmgde(((uint8_t *)&t105), ((uint8_t *)&t106)));
                                            if (!t107) goto ie102;
                                            {
                                                goto lc63;
                                            }
                                            goto ix102;
ie102:;
ix102:;
                                        }
                                        {
                                            void * t109 = gm_l0;
                                            void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
                                            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                                            void * t112 = gm_l0;
                                            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t112) + 8));
                                            gm_b0_1 t114 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t110, t111, t113);
                                            if (gm_unw.kind) {
                                                goto gm_ll67;
                                            }
                                        }
                                        {
                                            void * t115 = gm_l0;
                                            void * t116 = gm_l1;
                                            gm_t88rcucg1b7uk t117 = gm_l2;
                                            uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t119 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                            if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcngug8co99ok1, 582, 48);
                                            gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 24)) + t118 * 56) + 16));
                                            gm_tl874t2atnlh3 t121 = gm_l7;
                                            void * t122 = gm_f_adjust_receiver_2j7250l((void *)NULL, cx, NULL, t115, t116, t117, t120, &(*(gm_t08olq97rscn3 *)((uint8_t *)a5)), t121);
                                            if (gm_unw.kind) {
                                                goto gm_ll67;
                                            }
                                            void * t123 = t122;
                                            void * t124;
                                            if ((GM_LD(void *, ((uint8_t *)&t123)) != 0)) {
                                                void * t125 = GM_LD(void *, (((uint8_t *)&t123) + 0));
                                                t124 = t125;
                                            } else {
                                                void * t126 = gm_l0;
                                                void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
                                                gm_tl874t2atnlh3 t128 = gm_l7;
                                                void * t129 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t127, t128);
                                                if (gm_unw.kind) {
                                                    goto gm_ll67;
                                                }
                                                uint8_t t130 = 0;
                                                F.ret = t129;
                                                goto gm_fret;
                                            }
                                            gm_l20 = t124;
                                        }
                                        {
                                            void * t131 = gm_l0;
                                            uint64_t t132 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                            void * t133 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 24)));
                                            uint64_t t134 = (uintptr_t)((uint64_t)1ULL);
                                            if (t134 > (uintptr_t)t132 || (uintptr_t)t132 > (uintptr_t)t132) gm_trap_slice(t134, (uintptr_t)t132, t132, gm_srcngug8co99ok1, 583, 35);
                                            gm_b16_8 t135;
                                            GM_ST(void *, (uint8_t *)&t135, (void *)(((uint8_t *)t133 + t134 * 56)));
                                            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)((uintptr_t)t132 - t134));
                                            gm_b16_8 t136 = t135;
                                            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                                            uint64_t t138 = gm_l4;
                                            gm_b16_8 t139 = gm_l6;
                                            gm_tl874t2atnlh3 t140 = gm_l7;
                                            gm_b16_8 t141;
                                            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_sk6ueggk5ji6v));
                                            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(9));
                                            gm_ttu09lij36f8c t142;
                                            gm_f_bind_args_dnih8ih((void *)&t142, (void *)NULL, cx, NULL, t131, t137, t138, t139, t140, t141);
                                            if (gm_unw.kind) {
                                                goto gm_ll67;
                                            }
                                            gm_ttu09lij36f8c t143 = t142;
                                            gm_t6j6smj7qoctu t144;
                                            if (((((uint8_t *)&t143))[32] != 0)) {
                                                gm_t6j6smj7qoctu t145 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t143) + 0));
                                                t144 = t145;
                                            } else {
                                                void * t146 = gm_l0;
                                                void * t147 = GM_LD(void *, (((uint8_t *)t146) + 0));
                                                gm_tl874t2atnlh3 t148 = gm_l7;
                                                void * t149 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t147, t148);
                                                if (gm_unw.kind) {
                                                    goto gm_ll67;
                                                }
                                                uint8_t t150 = 0;
                                                F.ret = t149;
                                                goto gm_fret;
                                            }
                                            gm_l21 = t144;
                                        }
                                        {
                                            gm_f_list_vp1djv1_2((void *)&F.l22, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_ll67;
                                            }
                                        }
                                        {
                                            void * t151 = (void *)((uint8_t *)&F.l22);
                                            void * t152 = gm_l20;
                                            gm_tftb1odc739t5 t153 = gm_f_push_d943vgp_2(t151, &(*(gm_t69m56meqefs8 *)((uint8_t *)t152)));
                                            gm_tftb1odc739t5 t154 = t153;
                                            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srcngug8co99ok1, 585, 13);
                                            gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
                                        }
                                        {
                                            void * t156 = (void *)((uint8_t *)&F.l22);
                                            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                            gm_b16_8 t158 = t157;
                                            gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                                            gm_tftb1odc739t5 t160 = gm_f_extend_gig5o6g(t156, t159);
                                            gm_tftb1odc739t5 t161 = t160;
                                            if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcngug8co99ok1, 586, 13);
                                            gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                                        }
                                        {
                                            memset((uint8_t *)&gm_l23 + 4, 0, 4);
                                            uint32_t t163 = gm_l12;
                                            GM_ST(uint32_t, (uint8_t *)&gm_l23 + 0, t163);
                                            gm_b16_8 t164 = gm_f_empty_slice_fh7svmj();
                                            gm_b16_8 t165 = t164;
                                            gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                                            GM_ST(gm_b16_8, (uint8_t *)&gm_l23 + 8, t166);
                                        }
                                        {
                                            void * t167 = gm_l0;
                                            void * t168 = (void *)(((uint8_t *)t167) + 192);
                                            gm_tj44oivvogvtl t169;
                                            memset((uint8_t *)&t169 + 36, 0, 4);
                                            memcpy((uint8_t *)&t169 + 0, ((uint8_t *)&gm_l23), 24);
                                            gm_tl874t2atnlh3 t170 = gm_l7;
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t169 + 24, t170);
                                            gm_tftb1odc739t5 t171 = gm_f_push_3onuaru(t168, &t169);
                                            gm_tftb1odc739t5 t172 = t171;
                                            if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srcngug8co99ok1, 588, 13);
                                            gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                                        }
                                        {
                                            uint8_t t175 = ((GM_LD(uint8_t, (((uint8_t *)&F.l17) + 16) + 0) == ((uint8_t)0ULL)));
                                            if (!t175) goto ie174;
                                            {
                                                void * t177 = gm_l0;
                                                void * t178 = (void *)(((uint8_t *)t177) + 536);
                                                gm_tl874t2atnlh3 t179 = gm_l7;
                                                gm_tftb1odc739t5 t180 = gm_f_push_0mk7udh(t178, GM_CONV(gm_cseahvg8v7p0vs, t179));
                                                gm_tftb1odc739t5 t181 = t180;
                                                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcngug8co99ok1, 589, 32);
                                                gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
                                            }
                                            goto ix174;
ie174:;
ix174:;
                                        }
                                        {
                                            gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 40));
                                            gm_to76jli3bqddq t184;
                                            gm_t419mo45g7j0a t185;
                                            memset((uint8_t *)&t185 + 28, 0, 4);
                                            memcpy((uint8_t *)&t185 + 0, ((uint8_t *)&gm_l23), 24);
                                            uint64_t t186 = gm_l16;
                                            uint32_t t187 = (uint32_t)t186;
                                            GM_ST(uint32_t, (uint8_t *)&t185 + 24, t187);
                                            gm_tsth9qu1dv0g3 t188;
                                            GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t188 + 8), t185);
                                            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t184 + 0, t188);
                                            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t184 + 40, t189);
                                            gm_teti411eujvf4 t190;
                                            GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)34ULL));
                                            GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t190 + 8), t184);
                                            gm_tl874t2atnlh3 t191 = gm_l7;
                                            void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t183, &t190, t191);
                                            if (gm_unw.kind) {
                                                goto gm_ll67;
                                            }
                                            gm_l24 = t192;
                                        }
                                        {
                                            void * t193 = gm_l0;
                                            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                            void * t195 = gm_l24;
                                            void * t196 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t193, t194, t195, ((uint8_t)1));
                                            if (gm_unw.kind) {
                                                goto gm_ll67;
                                            }
                                            uint8_t t197 = 0;
                                            F.ret = t196;
                                            goto gm_fret;
                                        }
                                        goto me75;
mn76:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 576, 9);
me75:;
                                    }
lc63:;
                                    {
                                        uint64_t t199 = gm_l15;
                                        uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
                                        uint64_t t202 = r200;
                                        gm_l15 = t202;
                                    }
                                }
gm_ll67:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb63; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc63; }
                                }
                                goto gm_ll17;
lb63:;
                            }
                        }
                        goto ix26;
ie26:;
                        {
                            goto lc13;
                        }
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 571, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t205 = gm_l10;
                    uint64_t r206; bool o207 = __builtin_add_overflow(t205, ((uint64_t)1ULL), &r206);
                    uint64_t t208 = r206;
                    gm_l10 = t208;
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
        uint8_t t209 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_param_with_qfvlipt(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_with_qfvlipt F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    uint32_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 8));
        gm_l3 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            uint32_t t14 = gm_l3;
            gm_t9qkh0rsooc8d t15 = gm_l2;
            uint8_t t16 = gm_f_param_has_2r09oc7(t13, t14, t15);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t19 = 0;
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
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_perform_named_h7gr9vi F;
    void * gm_l0; /* cx */
    gm_t716e5hdn64ba gm_l1; /* named */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* eff */
    gm_tqu3m2l3n8mnc gm_l6; /* inst */
    uint32_t gm_l7; /* e */
    gm_tiilmfd16v3fc gm_l8; /* r */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_tiilmfd16v3fc gm_l14; /* r */
    GM_STACK_CHECK(&F, 1191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
        gm_tqu3m2l3n8mnc t2;
        memset(&t2, 0, sizeof t2);
        gm_l6 = t2;
    }
    {
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
        gm_l7 = t6;
        {
            uint32_t t8 = gm_l7;
            gm_l5 = t8;
        }
        goto me3;
mn4:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        gm_tiilmfd16v3fc t11 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)a1) + 8));
        gm_l8 = t11;
        {
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
            gm_l5 = t13;
        }
        {
            gm_tiilmfd16v3fc t14 = gm_l8;
            gm_tqu3m2l3n8mnc t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[24] = 1;
            gm_l6 = t15;
        }
        goto me3;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 644, 5);
me3:;
    }
    {
        uint32_t t16 = gm_l5;
        uint64_t t17 = (uint64_t)t16;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 1048) + 0) + 8);
        if (t18 >= (uintptr_t)(t21)) gm_trap_index(t18, t21, gm_srcngug8co99ok1, 651, 13);
        gm_tammvh1iespi3 t22 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 1048) + 0)) + t18 * 56));
        F.l9 = t22;
    }
    {
        {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 40));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_l10 = t26;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t28 = gm_l11;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    gm_tdvhflnc2f5o8 t33;
                    uint64_t t34 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t33 + 0, t34);
                    uint64_t t35 = gm_l11;
                    uint64_t t36 = (uintptr_t)t35;
                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t33 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t36 * 48), 48);
                    gm_tdvhflnc2f5o8 t38 = t33;
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t38) + 0));
                    gm_l12 = t41;
                    gm_tv0k2vg3054a6 t42 = GM_LD(gm_tv0k2vg3054a6, (((uint8_t *)&t38) + 8));
                    F.l13 = t42;
                    {
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                        gm_b16_8 t47 = t45;
                        gm_b16_8 t48 = t46;
                        uint8_t t49 = (!gm_eq0iqiubfm7e(((uint8_t *)&t47), ((uint8_t *)&t48)));
                        if (!t49) goto ie44;
                        {
                            goto lc27;
                        }
                        goto ix44;
ie44:;
ix44:;
                    }
                    {
                        if (!((((uint8_t *)&gm_l6))[24] != 0)) goto ie51;
                        gm_tiilmfd16v3fc t52 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l6));
                        gm_l14 = t52;
                        {
                            void * t54 = gm_l0;
                            gm_tiilmfd16v3fc t55 = gm_l14;
                            uint64_t t56 = gm_l12;
                            uint32_t t57 = (uint32_t)t56;
                            gm_b16_8 t58 = gm_l3;
                            gm_tl874t2atnlh3 t59 = gm_l4;
                            void * t60 = gm_f_perform_op_as_9ncunsc((void *)NULL, cx, NULL, t54, &t55, t57, t58, t59);
                            if (gm_unw.kind) {
                                goto gm_ll31;
                            }
                            uint8_t t61 = 0;
                            F.ret = t60;
                            goto gm_fret;
                        }
                        goto ix51;
ie51:;
ix51:;
                    }
                    {
                        void * t62 = gm_l0;
                        uint32_t t63 = gm_l5;
                        uint64_t t64 = gm_l12;
                        uint32_t t65 = (uint32_t)t64;
                        gm_b16_8 t66 = gm_l3;
                        gm_tl874t2atnlh3 t67 = gm_l4;
                        void * t68 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t62, t63, t65, t66, t67);
                        if (gm_unw.kind) {
                            goto gm_ll31;
                        }
                        uint8_t t69 = 0;
                        F.ret = t68;
                        goto gm_fret;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 652, 5);
me39:;
                }
lc27:;
                {
                    uint64_t t71 = gm_l11;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l11 = t74;
                }
            }
gm_ll31:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
            }
            goto gm_lad;
lb27:;
        }
    }
    {
        void * t75 = gm_l0;
        gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_t6j6smj7qoctu t77;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t77 + 0, t78);
        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t77 + 16, t79);
        gm_tabb710d9mq2e t80;
        gm_f_format_p9hcrrd((void *)&t80, (void *)NULL, cx, NULL, &t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t81 = t80;
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srcngug8co99ok1, 657, 25);
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
        gm_b16_8 t83 = t82;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_b0_1 t85 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t75, t76, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        gm_tl874t2atnlh3 t88 = gm_l4;
        void * t89 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t87, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t90 = 0;
        F.ret = t89;
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
__attribute__((aligned(4))) void * gm_f_pointer_cast_6siqq5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_pointer_cast_6siqq5h F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* rty */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_closure gm_l8; /* same */
    uint8_t gm_l9; /* ok */
    gm_t88rcucg1b7uk gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* b */
    gm_t88rcucg1b7uk gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l14; /* i */
    gm_b16_8 gm_l18; /* why */
    gm_tn3r4rvlh0rsv gm_l19; /* <temporary array> */
    GM_STACK_CHECK(&F, 2441u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        F.l5 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_b16_8 t6 = gm_l3;
        gm_tl874t2atnlh3 t7 = gm_l4;
        gm_tkn42u7na3t81 t8 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t5, t6, ((uint64_t)0ULL), t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t9 = t8;
        gm_t88rcucg1b7uk t10;
        if (((((uint8_t *)&t9))[4] != 0)) {
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            uint8_t t12 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t10;
    }
    {
        gm_closure t13 = ((gm_closure){(void *)gm_f_kind_u4oqjin, (void *)&F.h, cx});
        F.l7 = t13;
    }
    {
        gm_closure t14 = ((gm_closure){(void *)gm_f_same_1qdglea, (void *)&F.h, cx});
        gm_l8 = t14;
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        void * t16 = F.l5;
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_t53at80pfdus3 t18;
        gm_f_kind_jcnt4hm((void *)&t18, t16, GM_CONV(gm_csguankko4lmhd, t17));
        gm_t53at80pfdus3 t19 = t18;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t20 != ((uint8_t)13ULL)) goto ie15;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t19) + 8));
        gm_l10 = t21;
        {
            void * t24 = F.l5;
            gm_t88rcucg1b7uk t25 = gm_l6;
            gm_t53at80pfdus3 t26;
            gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
            gm_t53at80pfdus3 t27 = t26;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            if (t28 != ((uint8_t)13ULL)) goto ie23;
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t27) + 8));
            gm_l11 = t29;
            {
                gm_closure t31 = gm_l8;
                gm_t88rcucg1b7uk t32 = gm_l10;
                gm_t88rcucg1b7uk t33 = gm_l11;
                gm_closure t34 = t31;
                gm_node *ccx35 = t34.cx;
                void * t36 = t34.code;
                uint8_t t37;
                if (GM_PTR_TAGGED(t36)) {
                    t37 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t36))(GM_CONV(gm_csguankko4lmhd, t32), GM_CONV(gm_csguankko4lmhd, t33));
                } else {
                    t37 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t36)(t34.env, ccx35, NULL, t32, t33);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t37;
            }
            goto ix23;
ie23:;
ix23:;
        }
        goto ix15;
ie15:;
        void * t39 = F.l5;
        gm_t88rcucg1b7uk t40 = gm_l6;
        gm_t53at80pfdus3 t41;
        gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        gm_t53at80pfdus3 t42 = t41;
        uint8_t t43;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
        if (t46 != ((uint8_t)13ULL)) goto mn45;
        t43 = ((uint8_t)0);
        goto me44;
mn45:;
        t43 = ((uint8_t)1);
        goto me44;
mn47:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 284, 15);
me44:;
        if (!t43) goto ie38;
        {
            gm_closure t49 = gm_l8;
            gm_t88rcucg1b7uk t50 = gm_l2;
            gm_t88rcucg1b7uk t51 = gm_l6;
            gm_closure t52 = t49;
            gm_node *ccx53 = t52.cx;
            void * t54 = t52.code;
            uint8_t t55;
            if (GM_PTR_TAGGED(t54)) {
                t55 = ((uint8_t (*)(gm_csguankko4lmhd, gm_csguankko4lmhd))GM_PTR_UNTAG(t54))(GM_CONV(gm_csguankko4lmhd, t50), GM_CONV(gm_csguankko4lmhd, t51));
            } else {
                t55 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_t88rcucg1b7uk))t54)(t52.env, ccx53, NULL, t50, t51);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t55;
        }
        goto ix38;
ie38:;
ix38:;
ix15:;
    }
    {
        uint8_t t57 = gm_l9;
        uint8_t t58 = (uint8_t)!t57;
        if (!t58) goto ie56;
        {
            void * t60 = gm_l0;
            gm_tl874t2atnlh3 t61 = gm_l4;
            gm_t6j6smj7qoctu t62;
            void * t63 = gm_l0;
            gm_t88rcucg1b7uk t64 = gm_l2;
            gm_b16_8 t65 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t65);
            void * t66 = gm_l0;
            gm_t88rcucg1b7uk t67 = gm_l6;
            gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t68);
            gm_tabb710d9mq2e t69;
            gm_f_format_7f83ds0((void *)&t69, (void *)NULL, cx, NULL, &t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t70 = t69;
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srcngug8co99ok1, 288, 24);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
            gm_b16_8 t72 = t71;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t60, t61, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
            gm_tl874t2atnlh3 t77 = gm_l4;
            void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        gm_t53at80pfdus3 t80;
        void * t82 = F.l5;
        gm_t88rcucg1b7uk t83 = gm_l2;
        gm_t53at80pfdus3 t84;
        gm_f_kind_jcnt4hm((void *)&t84, t82, GM_CONV(gm_csguankko4lmhd, t83));
        gm_t53at80pfdus3 t85 = t84;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t85) + 0));
        if (t86 != ((uint8_t)13ULL)) goto ie81;
        gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t85) + 8));
        gm_l12 = t87;
        void * t89 = F.l5;
        gm_t88rcucg1b7uk t90 = gm_l12;
        gm_t53at80pfdus3 t91;
        gm_f_kind_jcnt4hm((void *)&t91, t89, GM_CONV(gm_csguankko4lmhd, t90));
        gm_t53at80pfdus3 t92 = t91;
        t80 = t92;
        goto ix81;
ie81:;
        void * t94 = F.l5;
        gm_t88rcucg1b7uk t95 = gm_l2;
        gm_t53at80pfdus3 t96;
        gm_f_kind_jcnt4hm((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
        gm_t53at80pfdus3 t97 = t96;
        t80 = t97;
ix81:;
        F.l13 = t80;
    }
    {
        gm_t53at80pfdus3 t98;
        void * t100 = F.l5;
        gm_t88rcucg1b7uk t101 = gm_l6;
        gm_t53at80pfdus3 t102;
        gm_f_kind_jcnt4hm((void *)&t102, t100, GM_CONV(gm_csguankko4lmhd, t101));
        gm_t53at80pfdus3 t103 = t102;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&t103) + 0));
        if (t104 != ((uint8_t)13ULL)) goto ie99;
        gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t103) + 8));
        gm_l14 = t105;
        void * t107 = F.l5;
        gm_t88rcucg1b7uk t108 = gm_l14;
        gm_t53at80pfdus3 t109;
        gm_f_kind_jcnt4hm((void *)&t109, t107, GM_CONV(gm_csguankko4lmhd, t108));
        gm_t53at80pfdus3 t110 = t109;
        t98 = t110;
        goto ix99;
ie99:;
        void * t112 = F.l5;
        gm_t88rcucg1b7uk t113 = gm_l6;
        gm_t53at80pfdus3 t114;
        gm_f_kind_jcnt4hm((void *)&t114, t112, GM_CONV(gm_csguankko4lmhd, t113));
        gm_t53at80pfdus3 t115 = t114;
        t98 = t115;
ix99:;
        F.l15 = t98;
    }
    {
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
        if (t117 != ((uint8_t)16ULL)) goto ie116;
        gm_tddtjfggdat0d t118 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l13) + 8));
        F.l16 = t118;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
        if (t119 != ((uint8_t)16ULL)) goto ie116;
        gm_tddtjfggdat0d t120 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l15) + 8));
        F.l17 = t120;
        {
            void * t123 = gm_l0;
            void * t124 = (void *)((uint8_t *)&F.l16);
            void * t125 = (void *)((uint8_t *)&F.l17);
            gm_th2lqbo77pvsc t126;
            gm_f_fn_cast_mismatch_e9s21md((void *)&t126, (void *)NULL, cx, NULL, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t127 = t126;
            if (!((((uint8_t *)&t127))[16] != 0)) goto ie122;
            gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
            gm_l18 = t128;
            {
                void * t130 = gm_l0;
                gm_tl874t2atnlh3 t131 = gm_l4;
                gm_tfeab279c16rr t132;
                void * t133 = gm_l0;
                gm_t88rcucg1b7uk t134 = gm_l2;
                gm_b16_8 t135 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t133, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t132 + 0, t135);
                void * t136 = gm_l0;
                gm_t88rcucg1b7uk t137 = gm_l6;
                gm_b16_8 t138 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t136, t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t132 + 16, t138);
                gm_b16_8 t139 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t132 + 32, t139);
                gm_tabb710d9mq2e t140;
                gm_f_format_560qv19((void *)&t140, (void *)NULL, cx, NULL, &t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t141 = t140;
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_srcngug8co99ok1, 296, 28);
                gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
                gm_b16_8 t143 = t142;
                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                gm_b0_1 t145 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t130, t131, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t146 = gm_l0;
                void * t147 = GM_LD(void *, (((uint8_t *)t146) + 0));
                gm_tl874t2atnlh3 t148 = gm_l4;
                void * t149 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t147, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t150 = 0;
                F.ret = t149;
                goto gm_fret;
            }
            goto ix122;
ie122:;
ix122:;
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t151;
        GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t151 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)8ULL));
        gm_t88rcucg1b7uk t152 = gm_l6;
        {
            void * t154 = gm_l1;
            gm_tn3r4rvlh0rsv t155;
            GM_ST(void *, ((uint8_t *)&t155 + 0), t154);
            gm_l19 = t155;
        }
        void * t156 = ((uint8_t *)&gm_l19);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcngug8co99ok1, 300, 30);
        gm_b16_8 t157;
        GM_ST(void *, (uint8_t *)&t157, (void *)(((uint8_t *)t156 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t158 = t157;
        gm_tl874t2atnlh3 t159 = gm_l4;
        void * t160 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t151, t152, t158, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t161 = 0;
        F.ret = t160;
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
inline __attribute__((aligned(4))) uint8_t gm_f_pointer_gv6lvmk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_pointer_gv6lvmk F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* i */
    gm_t88rcucg1b7uk gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t88rcucg1b7uk t2;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t5 = gm_l0;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)13ULL)) goto ie3;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l1 = t9;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = t11;
        t2 = t12;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = t14;
        t2 = t15;
ix3:;
        gm_l2 = t2;
    }
    {
        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_t53at80pfdus3 t18;
        gm_f_kind_jcnt4hm((void *)&t18, t16, GM_CONV(gm_csguankko4lmhd, t17));
        gm_t53at80pfdus3 t19 = t18;
        uint8_t t20;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma23; case ((uint8_t)17ULL): goto ma24; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma24; case ((uint8_t)23ULL): goto ma24; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma24; case ((uint8_t)26ULL): goto ma24; default: goto ma24; }
ma22:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t30 != ((uint8_t)8ULL)) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t32 != ((uint8_t)9ULL)) goto pa31;
        goto po28;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t34 != ((uint8_t)7ULL)) goto pa33;
        goto po28;
pa33:;
        goto mn27;
po28:;
        t20 = ((uint8_t)1ULL);
        goto me21;
mn27:;
ma23:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t36 != ((uint8_t)16ULL)) goto mn35;
        t20 = ((uint8_t)2ULL);
        goto me21;
mn35:;
ma24:;
        t20 = ((uint8_t)0ULL);
        goto me21;
mn37:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 310, 16);
me21:;
        uint8_t t38 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_1qdglea(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_same_1qdglea F;
    gm_t88rcucg1b7uk gm_l0; /* a */
    gm_t88rcucg1b7uk gm_l1; /* b */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    gm_closure t2 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l7));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    uint8_t t7;
    if (GM_PTR_TAGGED(t6)) {
        t7 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
    } else {
        t7 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    if (t8) {
        gm_closure t9 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l7));
        gm_t88rcucg1b7uk t10 = gm_l0;
        gm_closure t11 = t9;
        gm_node *ccx12 = t11.cx;
        void * t13 = t11.code;
        uint8_t t14;
        if (GM_PTR_TAGGED(t13)) {
            t14 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t13))(GM_CONV(gm_csguankko4lmhd, t10));
        } else {
            t14 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t13)(t11.env, ccx12, NULL, t10);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t15 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pointer_cast_6siqq5h *)((&F.h)->up))->l7));
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_closure t17 = t15;
        gm_node *ccx18 = t17.cx;
        void * t19 = t17.code;
        uint8_t t20;
        if (GM_PTR_TAGGED(t19)) {
            t20 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t19))(GM_CONV(gm_csguankko4lmhd, t16));
        } else {
            t20 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t19)(t17.env, ccx18, NULL, t16);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = ((t14 == t20));
        t1 = t21;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_repr_830dsa9(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_same_repr_830dsa9 F;
    gm_t88rcucg1b7uk gm_l0; /* a */
    gm_t88rcucg1b7uk gm_l1; /* b */
    GM_STACK_CHECK(&F, 469u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_t88rcucg1b7uk t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t10 = 0;
        uint8_t t11 = 0;
        gm_closure t12 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l4));
        gm_t88rcucg1b7uk t13 = gm_l0;
        gm_closure t14 = t12;
        gm_node *ccx15 = t14.cx;
        void * t16 = t14.code;
        uint8_t t17;
        if (GM_PTR_TAGGED(t16)) {
            t17 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t16))(GM_CONV(gm_csguankko4lmhd, t13));
        } else {
            t17 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t16)(t14.env, ccx15, NULL, t13);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (!(t17 == ((uint8_t)0ULL)));
        if (t18) {
            gm_closure t19 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l4));
            gm_t88rcucg1b7uk t20 = gm_l0;
            gm_closure t21 = t19;
            gm_node *ccx22 = t21.cx;
            void * t23 = t21.code;
            uint8_t t24;
            if (GM_PTR_TAGGED(t23)) {
                t24 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t23))(GM_CONV(gm_csguankko4lmhd, t20));
            } else {
                t24 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t23)(t21.env, ccx22, NULL, t20);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_closure t25 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l4));
            gm_t88rcucg1b7uk t26 = gm_l1;
            gm_closure t27 = t25;
            gm_node *ccx28 = t27.cx;
            void * t29 = t27.code;
            uint8_t t30;
            if (GM_PTR_TAGGED(t29)) {
                t30 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t29))(GM_CONV(gm_csguankko4lmhd, t26));
            } else {
                t30 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t29)(t27.env, ccx28, NULL, t26);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = ((t24 == t30));
            t11 = t31;
        } else t11 = 0;
        if (t11) {
            void * t32 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t33 = gm_l0;
            gm_t53at80pfdus3 t34;
            gm_f_kind_jcnt4hm((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
            gm_t53at80pfdus3 t35 = t34;
            uint8_t t36;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t39 != ((uint8_t)13ULL)) goto mn38;
            t36 = ((uint8_t)1);
            goto me37;
mn38:;
            t36 = ((uint8_t)0);
            goto me37;
mn40:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 318, 70);
me37:;
            void * t41 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fn_cast_mismatch_e9s21md *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t42 = gm_l1;
            gm_t53at80pfdus3 t43;
            gm_f_kind_jcnt4hm((void *)&t43, t41, GM_CONV(gm_csguankko4lmhd, t42));
            gm_t53at80pfdus3 t44 = t43;
            uint8_t t45;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
            if (t48 != ((uint8_t)13ULL)) goto mn47;
            t45 = ((uint8_t)1);
            goto me46;
mn47:;
            t45 = ((uint8_t)0);
            goto me46;
mn49:;
            gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 318, 103);
me46:;
            uint8_t t50 = ((t36 == t45));
            t10 = t50;
        } else t10 = 0;
        uint8_t t51 = 0;
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
__attribute__((aligned(4))) void * gm_f_slice_fn_np9f5vf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_slice_fn_np9f5vf F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* f */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* bt */
    void * gm_l8; /* recv */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    uint8_t gm_l11; /* m */
    void * gm_l12; /* p */
    GM_STACK_CHECK(&F, 996u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss496b65r9omj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = gm_l2;
        gm_t9805qtonbsag t5 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t6 = t5;
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = gm_l4;
                gm_b16_8 t12 = gm_l2;
                gm_tnpifv20irnph t13;
                GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
                gm_tabb710d9mq2e t14;
                gm_f_format_vtr1b5e((void *)&t14, (void *)NULL, cx, NULL, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t15 = t14;
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcngug8co99ok1, 415, 24);
                gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
                gm_b16_8 t17 = t16;
                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
                gm_b0_1 t19 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t10, t11, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_tl874t2atnlh3 t22 = gm_l4;
            void * t23 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        gm_l5 = t7;
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        gm_l6 = t27;
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 96);
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        void * t32 = gm_l1;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
        gm_t88rcucg1b7uk t34 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t34;
    }
    {
        void * t35 = gm_l1;
        gm_l8 = t35;
    }
    {
        void * t36 = gm_l6;
        gm_t88rcucg1b7uk t37 = gm_l7;
        gm_t53at80pfdus3 t38;
        gm_f_kind_jcnt4hm((void *)&t38, t36, GM_CONV(gm_csguankko4lmhd, t37));
        gm_t53at80pfdus3 t39 = t38;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
        if (t44 != ((uint8_t)11ULL)) goto pa43;
        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t39) + 8) + 8));
        gm_l9 = t45;
        goto po42;
pa43:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
        if (t47 != ((uint8_t)20ULL)) goto pa46;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t39) + 8) + 4));
        gm_l9 = t48;
        goto po42;
pa46:;
        goto mn41;
po42:;
        {
            void * t50 = gm_l0;
            void * t51 = gm_l1;
            gm_tqs5p4n2tq19v t52 = gm_f_place_mutability_0tlm4g8(t50, t51);
            gm_tqs5p4n2tq19v t53 = t52;
            uint8_t t54;
            if (((((uint8_t *)&t53))[1] != 0)) {
                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
                t54 = t55;
            } else {
                {
                    void * t57 = gm_l0;
                    gm_tl874t2atnlh3 t58 = gm_l4;
                    gm_b16_8 t59 = gm_l2;
                    gm_tnpifv20irnph t60;
                    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
                    gm_tabb710d9mq2e t61;
                    gm_f_format_6pukbnn((void *)&t61, (void *)NULL, cx, NULL, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t62 = t61;
                    if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srcngug8co99ok1, 424, 32);
                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
                    gm_b16_8 t64 = t63;
                    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                    gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t57, t58, t65);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t67 = gm_l0;
                void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                gm_tl874t2atnlh3 t69 = gm_l4;
                void * t70 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
            gm_l11 = t54;
        }
        {
            void * t72 = gm_l6;
            uint8_t t73 = gm_l11;
            uint8_t t74 = (uint8_t)!t73;
            gm_t88rcucg1b7uk t75 = gm_l7;
            gm_t88rcucg1b7uk t76 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t72, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t77 = gm_l1;
            gm_teti411eujvf4 t78;
            memset(&t78, 0, sizeof t78);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t78 + 8), t77);
            gm_tl874t2atnlh3 t79 = gm_l4;
            void * t80 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t76, &t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t80;
        }
        {
            void * t81 = gm_l0;
            void * t82 = gm_l12;
            void * t83 = gm_l6;
            uint8_t t84 = gm_l11;
            uint8_t t85 = (uint8_t)!t84;
            gm_t88rcucg1b7uk t86 = gm_l9;
            gm_t88rcucg1b7uk t87 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t83, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t88 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t81, t82, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t88;
        }
        goto me40;
mn41:;
        goto me40;
mn89:;
        gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 421, 5);
me40:;
    }
    {
        void * t91 = gm_l0;
        uint32_t t92 = gm_l5;
        void * t93 = gm_l8;
        gm_b16_8 t94 = gm_l3;
        gm_tl874t2atnlh3 t95 = gm_l4;
        void * t96 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t91, t92, t93, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t97 = 0;
        F.ret = t96;
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
inline __attribute__((aligned(4))) uint8_t gm_f_takes_receiver_uhu9min(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_takes_receiver_uhu9min F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* param */
    gm_t88rcucg1b7uk gm_l2; /* s */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = gm_l3;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)8ULL)) goto ie3;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l4 = t9;
        {
            gm_t88rcucg1b7uk t11 = gm_l4;
            gm_l3 = t11;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l3;
        gm_t88rcucg1b7uk t14 = gm_l2;
        uint8_t t15 = gm_f_fits_receiver_1qqamgd(t12, GM_CONV(gm_csguankko4lmhd, t13), GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_type_arg_grgffgp F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* i */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = (t3 < t4);
        if (!t5) goto ie2;
        uint64_t t6 = gm_l2;
        uint64_t t7 = (uintptr_t)t6;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcngug8co99ok1, 70, 43);
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t7 * 144) + 0) + 0));
        if (t9 != ((uint8_t)0ULL)) goto ie2;
        gm_tc3oht4ksth9d t10 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t7 * 144) + 0) + 8));
        F.l4 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)((uint8_t *)&F.l4);
            gm_t88rcucg1b7uk t14 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t14;
        }
        {
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_t88rcucg1b7uk t19 = gm_l5;
            uint8_t t20 = gm_f_is_error_8k27gd1(t18, GM_CONV(gm_csguankko4lmhd, t19));
            if (!t20) goto ie15;
            {
                gm_tkn42u7na3t81 t22;
                memset(&t22, 0, sizeof t22);
                uint8_t t23 = 0;
                F.ret = t22;
                goto gm_fret;
            }
            goto ix15;
ie15:;
ix15:;
        }
        {
            gm_t88rcucg1b7uk t24 = gm_l5;
            gm_tkn42u7na3t81 t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[4] = 1;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t27 = gm_l0;
        gm_tl874t2atnlh3 t28 = gm_l3;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_spskd7qirpoe3));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(24));
        gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t27, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unsigned_of_n48qlse(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_unsigned_of_n48qlse F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    gm_t88rcucg1b7uk t5;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    uint16_t t14 = GM_LD(uint16_t, ((((uint8_t *)&t4) + 8) + 2));
    gm_l2 = t14;
    void * t15 = gm_l0;
    uint16_t t16 = gm_l2;
    gm_t88rcucg1b7uk t17 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t15, ((uint8_t)0), t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t5 = t17;
    goto me6;
mn12:;
ma8:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t19 != ((uint8_t)2ULL)) goto mn18;
    void * t20 = gm_l0;
    gm_t88rcucg1b7uk t21 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t5 = t21;
    goto me6;
mn18:;
ma9:;
    gm_t88rcucg1b7uk t23 = gm_l1;
    t5 = t23;
    goto me6;
mn22:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srcngug8co99ok1, 264, 66);
me6:;
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
inline __attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_value_arg_i5l23i1 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* i */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = (t3 < t4);
        if (!t5) goto ie2;
        uint64_t t6 = gm_l2;
        uint64_t t7 = (uintptr_t)t6;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcngug8co99ok1, 81, 43);
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t7 * 144) + 0) + 0));
        if (t9 != ((uint8_t)0ULL)) goto ie2;
        gm_tc3oht4ksth9d t10 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t7 * 144) + 0) + 8));
        F.l5 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = gm_l0;
            void * t14 = (void *)((uint8_t *)&F.l5);
            gm_t88rcucg1b7uk t15 = gm_l3;
            gm_tkn42u7na3t81 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[4] = 1;
            void * t17 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t13, t14, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t18 = gm_l3;
            void * t19 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t12, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        gm_tl874t2atnlh3 t22 = gm_l4;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s7rl9plavbrfo));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(16));
        gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        gm_tl874t2atnlh3 t27 = gm_l4;
        void * t28 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = 0;
        F.ret = t28;
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
