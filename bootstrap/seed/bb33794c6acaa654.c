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
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t22b1ebhvur5a;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_b16_8 f2; } gm_t8qqrlhb610a2;
typedef struct { gm_tk9n3vlila9qg f0; uint8_t f1; uint8_t pe[7]; } gm_ta2tdjm589013;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_t0uuhaljsf410 f0; uint8_t f1; uint8_t pe[7]; } gm_ti6pdj2sunpe4;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_th2lqbo77pvsc f2; } gm_tmf08dt0mom0q;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_tk9n3vlila9qg f1; } gm_toa5kh9gm9738;
typedef struct { gm_tu1089hhijadf f0; uint8_t f1; uint8_t pe[7]; } gm_tom1ad5bcqgi1;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t0uuhaljsf410 v1; } f1; } gm_tq6bq450l0idg;
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_th2lqbo77pvsc f6; gm_b16_8 f7; gm_b16_8 f8; } gm_ts1nru0t7g7aj;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tu1089hhijadf v1; } f1; } gm_tvtl3n8qta358;
typedef struct { gm_tin97tt4ud0s1 f0; gm_tin97tt4ud0s1 f1; } gm_t08dlk5fmn1l9;
_Static_assert(sizeof(gm_t22b1ebhvur5a) == 40 && _Alignof(gm_t22b1ebhvur5a) == 8 && offsetof(gm_t22b1ebhvur5a, f0) == 0 && offsetof(gm_t22b1ebhvur5a, f1) == 8, "layout of `string!(ident: string, rest: string)`");
typedef struct { gm_b16_8 f0; gm_tmf08dt0mom0q f1; } gm_t3rj5o15e922s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
_Static_assert(sizeof(gm_t8qqrlhb610a2) == 56 && _Alignof(gm_t8qqrlhb610a2) == 8 && offsetof(gm_t8qqrlhb610a2, f0) == 0 && offsetof(gm_t8qqrlhb610a2, f1) == 16 && offsetof(gm_t8qqrlhb610a2, f2) == 40, "layout of `Choice`");
_Static_assert(sizeof(gm_ta2tdjm589013) == 48 && _Alignof(gm_ta2tdjm589013) == 8 && offsetof(gm_ta2tdjm589013, f0) == 0 && offsetof(gm_ta2tdjm589013, f1) == 40, "layout of `?StackItem`");
typedef struct { gm_t6ok5tvcjc200 f0; gm_b16_8 f1; } gm_tbqru6ibafrpr;
typedef struct { gm_tmf08dt0mom0q f0; uint8_t f1; uint8_t pe[7]; } gm_tcmp1j2cnqi8i;
typedef struct { gm_t8qqrlhb610a2 f0; uint8_t f1; uint8_t pe[7]; } gm_tgi1k2malmpgi;
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_ti6pdj2sunpe4) == 72 && _Alignof(gm_ti6pdj2sunpe4) == 8 && offsetof(gm_ti6pdj2sunpe4, f0) == 0 && offsetof(gm_ti6pdj2sunpe4, f1) == 64, "layout of `?TemplateArgs`");
typedef struct { gm_ts1nru0t7g7aj f0; uint8_t f1; uint8_t pe[7]; } gm_tie69pa8pplgc;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tmf08dt0mom0q) == 104 && _Alignof(gm_tmf08dt0mom0q) == 8 && offsetof(gm_tmf08dt0mom0q, f0) == 0 && offsetof(gm_tmf08dt0mom0q, f1) == 40 && offsetof(gm_tmf08dt0mom0q, f2) == 80, "layout of `Refs`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t8qqrlhb610a2 v1; } f1; } gm_tmfqp43enl3pa;
typedef struct { gm_t6ok5tvcjc200 f0; gm_trqfvmjtt4sij f1; } gm_to54va2pes5v2;
_Static_assert(sizeof(gm_toa5kh9gm9738) == 48 && _Alignof(gm_toa5kh9gm9738) == 8 && offsetof(gm_toa5kh9gm9738, f0) == 0 && offsetof(gm_toa5kh9gm9738, f1) == 8, "layout of `(i: usize, x: LintRule)`");
_Static_assert(sizeof(gm_tom1ad5bcqgi1) == 64 && _Alignof(gm_tom1ad5bcqgi1) == 8 && offsetof(gm_tom1ad5bcqgi1, f0) == 0 && offsetof(gm_tom1ad5bcqgi1, f1) == 56, "layout of `?Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tq6bq450l0idg) == 72 && _Alignof(gm_tq6bq450l0idg) == 8 && offsetof(gm_tq6bq450l0idg, f0) == 0 && offsetof(gm_tq6bq450l0idg, f1) == 8, "layout of `string!ArchiveInfo`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ts1nru0t7g7aj) == 168 && _Alignof(gm_ts1nru0t7g7aj) == 8 && offsetof(gm_ts1nru0t7g7aj, f0) == 0 && offsetof(gm_ts1nru0t7g7aj, f1) == 16 && offsetof(gm_ts1nru0t7g7aj, f2) == 32 && offsetof(gm_ts1nru0t7g7aj, f3) == 56 && offsetof(gm_ts1nru0t7g7aj, f4) == 80 && offsetof(gm_ts1nru0t7g7aj, f5) == 96 && offsetof(gm_ts1nru0t7g7aj, f6) == 112 && offsetof(gm_ts1nru0t7g7aj, f7) == 136 && offsetof(gm_ts1nru0t7g7aj, f8) == 152, "layout of `LockedPackage`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
typedef struct { gm_t6ok5tvcjc200 f0; gm_t8qqrlhb610a2 f1; } gm_tv2l6796tk7ol;
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tmf08dt0mom0q v1; } f1; } gm_tv61mo81rn7eb;
_Static_assert(sizeof(gm_tvtl3n8qta358) == 64 && _Alignof(gm_tvtl3n8qta358) == 8 && offsetof(gm_tvtl3n8qta358, f0) == 0 && offsetof(gm_tvtl3n8qta358, f1) == 8, "layout of `string!Version`");
_Static_assert(sizeof(gm_t08dlk5fmn1l9) == 80 && _Alignof(gm_t08dlk5fmn1l9) == 8 && offsetof(gm_t08dlk5fmn1l9, f0) == 0 && offsetof(gm_t08dlk5fmn1l9, f1) == 40, "layout of `(res: Resolution, lock: Lock)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_toq2f2q4opoa3 f3; } gm_t2f6hp2vik01f;
_Static_assert(sizeof(gm_t3rj5o15e922s) == 120 && _Alignof(gm_t3rj5o15e922s) == 8 && offsetof(gm_t3rj5o15e922s, f0) == 0 && offsetof(gm_t3rj5o15e922s, f1) == 16, "layout of `UrlRefs`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_tbqru6ibafrpr) == 56 && _Alignof(gm_tbqru6ibafrpr) == 8 && offsetof(gm_tbqru6ibafrpr, f0) == 0 && offsetof(gm_tbqru6ibafrpr, f1) == 40, "layout of `Owner`");
_Static_assert(sizeof(gm_tcmp1j2cnqi8i) == 112 && _Alignof(gm_tcmp1j2cnqi8i) == 8 && offsetof(gm_tcmp1j2cnqi8i, f0) == 0 && offsetof(gm_tcmp1j2cnqi8i, f1) == 104, "layout of `?Refs`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
_Static_assert(sizeof(gm_tgi1k2malmpgi) == 64 && _Alignof(gm_tgi1k2malmpgi) == 8 && offsetof(gm_tgi1k2malmpgi, f0) == 0 && offsetof(gm_tgi1k2malmpgi, f1) == 56, "layout of `?Choice`");
_Static_assert(sizeof(gm_tie69pa8pplgc) == 176 && _Alignof(gm_tie69pa8pplgc) == 8 && offsetof(gm_tie69pa8pplgc, f0) == 0 && offsetof(gm_tie69pa8pplgc, f1) == 168, "layout of `?LockedPackage`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tikairc198460;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
typedef struct { gm_b16_8 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[6]; gm_tl572d7579hns f2; gm_tnpifv20irnph f3; gm_tv37d3dpt2opu f4; gm_tin97tt4ud0s1 f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_trqfvmjtt4sij f9; gm_tin97tt4ud0s1 f10; gm_trqfvmjtt4sij f11; gm_tin97tt4ud0s1 f12; gm_tin97tt4ud0s1 f13; gm_tin97tt4ud0s1 f14; gm_tin97tt4ud0s1 f15; uint8_t f16; uint8_t p17[7]; gm_tin97tt4ud0s1 f17; gm_trqfvmjtt4sij f18; } gm_tlhfcpum9vunj;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tmfqp43enl3pa) == 64 && _Alignof(gm_tmfqp43enl3pa) == 8 && offsetof(gm_tmfqp43enl3pa, f0) == 0 && offsetof(gm_tmfqp43enl3pa, f1) == 8, "layout of `string!Choice`");
_Static_assert(sizeof(gm_to54va2pes5v2) == 80 && _Alignof(gm_to54va2pes5v2) == 8 && offsetof(gm_to54va2pes5v2, f0) == 0 && offsetof(gm_to54va2pes5v2, f1) == 40, "layout of `(key: Key, value: List(Key))`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
typedef struct { gm_tbqru6ibafrpr f0; uint8_t f1; uint8_t pe[7]; } gm_tqa5rsrlv432m;
typedef struct { gm_t08dlk5fmn1l9 f0; uint8_t f1; uint8_t pe[7]; } gm_tuu655st500b0;
_Static_assert(sizeof(gm_tv2l6796tk7ol) == 96 && _Alignof(gm_tv2l6796tk7ol) == 8 && offsetof(gm_tv2l6796tk7ol, f0) == 0 && offsetof(gm_tv2l6796tk7ol, f1) == 40, "layout of `(key: Key, value: Choice)`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_tv61mo81rn7eb) == 112 && _Alignof(gm_tv61mo81rn7eb) == 8 && offsetof(gm_tv61mo81rn7eb, f0) == 0 && offsetof(gm_tv61mo81rn7eb, f1) == 8, "layout of `string!Refs`");
_Static_assert(sizeof(gm_t2f6hp2vik01f) == 96 && _Alignof(gm_t2f6hp2vik01f) == 8 && offsetof(gm_t2f6hp2vik01f, f0) == 0 && offsetof(gm_t2f6hp2vik01f, f1) == 16 && offsetof(gm_t2f6hp2vik01f, f2) == 32 && offsetof(gm_t2f6hp2vik01f, f3) == 48, "layout of `Req`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tuu655st500b0 v1; } f1; } gm_t8a9rhgj2oa25;
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tikairc198460) == 328 && _Alignof(gm_tikairc198460) == 8 && offsetof(gm_tikairc198460, f0) == 0 && offsetof(gm_tikairc198460, f1) == 8, "layout of `string!Manifest`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
_Static_assert(sizeof(gm_tlhfcpum9vunj) == 640 && _Alignof(gm_tlhfcpum9vunj) == 8 && offsetof(gm_tlhfcpum9vunj, f0) == 0 && offsetof(gm_tlhfcpum9vunj, f1) == 16 && offsetof(gm_tlhfcpum9vunj, f2) == 24 && offsetof(gm_tlhfcpum9vunj, f3) == 48 && offsetof(gm_tlhfcpum9vunj, f4) == 64 && offsetof(gm_tlhfcpum9vunj, f5) == 112 && offsetof(gm_tlhfcpum9vunj, f6) == 152 && offsetof(gm_tlhfcpum9vunj, f7) == 192 && offsetof(gm_tlhfcpum9vunj, f8) == 232 && offsetof(gm_tlhfcpum9vunj, f9) == 272 && offsetof(gm_tlhfcpum9vunj, f10) == 312 && offsetof(gm_tlhfcpum9vunj, f11) == 352 && offsetof(gm_tlhfcpum9vunj, f12) == 392 && offsetof(gm_tlhfcpum9vunj, f13) == 432 && offsetof(gm_tlhfcpum9vunj, f14) == 472 && offsetof(gm_tlhfcpum9vunj, f15) == 512 && offsetof(gm_tlhfcpum9vunj, f16) == 552 && offsetof(gm_tlhfcpum9vunj, f17) == 560 && offsetof(gm_tlhfcpum9vunj, f18) == 600, "layout of `Resolver`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
_Static_assert(sizeof(gm_tqa5rsrlv432m) == 64 && _Alignof(gm_tqa5rsrlv432m) == 8 && offsetof(gm_tqa5rsrlv432m, f0) == 0 && offsetof(gm_tqa5rsrlv432m, f1) == 56, "layout of `?Owner`");
_Static_assert(sizeof(gm_tuu655st500b0) == 88 && _Alignof(gm_tuu655st500b0) == 8 && offsetof(gm_tuu655st500b0, f0) == 0 && offsetof(gm_tuu655st500b0, f1) == 80, "layout of `?(res: Resolution, lock: Lock)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_t8a9rhgj2oa25) == 96 && _Alignof(gm_t8a9rhgj2oa25) == 8 && offsetof(gm_t8a9rhgj2oa25, f0) == 0 && offsetof(gm_t8a9rhgj2oa25, f1) == 8, "layout of `string!?(res: Resolution, lock: Lock)`");
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_add_req_klu87ju {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t2f6hp2vik01f l2; /* req */
    gm_trqfvmjtt4sij l3; /* reqs */
    gm_t2f6hp2vik01f l7; /* q */
    gm_b0_1 ret;
};
struct gm_fr_after_req_ti0a6un {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t2f6hp2vik01f l2; /* req */
    gm_t8qqrlhb610a2 l3; /* c */
    gm_b0_1 ret;
};
struct gm_fr_archive_info_gq6dkee {
    gm_frame h;
    gm_t0uuhaljsf410 l4; /* a */
    gm_ti6pdj2sunpe4 ret;
};
struct gm_fr_build_lock_hmfdkj8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* packages */
    gm_trqfvmjtt4sij l2; /* keys */
    gm_tv2l6796tk7ol l5; /* c */
    gm_t6ok5tvcjc200 l8; /* key */
    gm_t8qqrlhb610a2 l9; /* choice */
    gm_ts1nru0t7g7aj l12; /* l */
    gm_trqfvmjtt4sij l13; /* used */
    gm_t6ok5tvcjc200 l16; /* k */
    gm_t6ok5tvcjc200 l19; /* key */
    gm_t0uuhaljsf410 l21; /* info */
    gm_t62efos6ssciq ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_refresh_names_9ih3ok9 {
    gm_frame h;
    gm_tv2l6796tk7ol l9; /* c */
    gm_to54va2pes5v2 l15; /* c */
    gm_t2f6hp2vik01f l18; /* q */
    gm_tl572d7579hns ret;
};
struct gm_fr_choose_9ee155u {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t8qqrlhb610a2 l2; /* c */
    gm_tie69pa8pplgc l4; /* locked */
    gm_ts1nru0t7g7aj l6; /* l */
    gm_t2f6hp2vik01f l9; /* q */
    gm_t8qqrlhb610a2 l10; /* choice */
    gm_ts1nru0t7g7aj l11; /* l */
    gm_tmf08dt0mom0q l14; /* refs */
    gm_tmfqp43enl3pa ret;
};
struct gm_fr_clear_m4blb4l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_pirklev {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_qd87n65 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_rm1b3rc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_s0loit2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_db_tssgvk0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_dependency_names_6m0jc80 {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_trqfvmjtt4sij l2; /* names */
    gm_trqfvmjtt4sij l3; /* keys */
    gm_t6ok5tvcjc200 l6; /* k */
    gm_trqfvmjtt4sij l7; /* out */
    gm_b16_8 ret;
};
struct gm_fr_describe_mp0u2mq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* parts */
    gm_t2f6hp2vik01f l4; /* r */
    gm_trqfvmjtt4sij l5; /* out */
    gm_b16_8 ret;
};
struct gm_fr_discard_bdpju80 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_downloads_vdenqu5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effective_vikkdqh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* others */
    gm_t2f6hp2vik01f l4; /* r */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_14fhq9i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_attt3ii {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cpdaeui {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gsm9gv5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hr6df4i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kvf8gur {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lcqki20 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mnej5m4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n8lsnom {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oihj9aq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p1sl7gg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pkfv52g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s0g91u7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ensure_archive_70kh53q {
    gm_frame h;
    gm_tt008mgut8glb l2; /* u */
    gm_t0uuhaljsf410 l3; /* info */
    gm_tie69pa8pplgc l6; /* locked */
    gm_tin97tt4ud0s1 l7; /* old */
    gm_ts1nru0t7g7aj l10; /* p */
    gm_ts1nru0t7g7aj l12; /* l */
    gm_ts1nru0t7g7aj l19; /* l */
    gm_t0uuhaljsf410 l23; /* info */
    gm_t0uuhaljsf410 l32; /* info */
    gm_t0uuhaljsf410 l33; /* info */
    gm_tq6bq450l0idg ret;
};
struct gm_fr_ensure_checkout_hvmsso1 {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t8qqrlhb610a2 l2; /* choice */
    gm_t2f6hp2vik01f l5; /* first */
    gm_ts1nru0t7g7aj l13; /* l */
    gm_ts1nru0t7g7aj l14; /* l */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_entry_name_qnlemsm {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_trqfvmjtt4sij l3; /* deps */
    gm_trqfvmjtt4sij l4; /* reqs */
    gm_t2f6hp2vik01f l7; /* q */
    gm_b16_8 ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_find_ref_h89e731 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_default_p4tis8s {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* s */
    uint8_t ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_0k0up67 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_refreshed_ifi7dst {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_trqfvmjtt4sij l3; /* reqs */
    gm_t2f6hp2vik01f l9; /* q */
    gm_ts1nru0t7g7aj l10; /* l */
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_key_equal_n71njnf {
    gm_frame h;
    gm_t6ok5tvcjc200 l0; /* a */
    gm_t6ok5tvcjc200 l1; /* b */
    uint8_t ret;
};
struct gm_fr_key_less_9obq0rq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_map_136uk7q {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_key_map_e8nmg3h {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_key_map_ihrh7ij {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_key_map_kjnah27 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_list_0pjrgaq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1q70459 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_254rpp1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_2hljflu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6vurufm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_agmtpnt {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dc4p80b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jsujn2k {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nccb2fo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_o444f8n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pfs7ocg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qeqmcmu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vsi9vun {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_locked_entry_4693mpc {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_tin97tt4ud0s1 l2; /* old */
    gm_tie69pa8pplgc ret;
};
struct gm_fr_make_read_only_a29eok8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_make_writable_6uascrq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mirror_8u3u6oe {
    gm_frame h;
    gm_t6ok5tvcjc200 l1; /* key */
    gm_t2f6hp2vik01f l5; /* first */
    gm_tmf08dt0mom0q l7; /* refs */
    gm_tl572d7579hns ret;
};
struct gm_fr_pick_ronijl9 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* pins */
    gm_t2f6hp2vik01f l8; /* r */
    gm_t2f6hp2vik01f l12; /* p */
    gm_trqfvmjtt4sij l13; /* versions */
    gm_t2f6hp2vik01f l16; /* r */
    gm_tin97tt4ud0s1 l17; /* v */
    gm_t2f6hp2vik01f l19; /* pin */
    gm_tu1089hhijadf l22; /* v */
    gm_tin97tt4ud0s1 l25; /* r */
    gm_tom1ad5bcqgi1 l30; /* best_v */
    gm_tu1089hhijadf l36; /* v */
    gm_tin97tt4ud0s1 l40; /* r */
    gm_tu1089hhijadf l41; /* b */
    gm_tu1089hhijadf l42; /* v */
    gm_tmfqp43enl3pa ret;
};
struct gm_fr_pop_rnfkbb9 {
    gm_frame h;
    gm_tk9n3vlila9qg l2; /* x */
    gm_ta2tdjm589013 ret;
};
struct gm_fr_read_manifest_u9uajvn {
    gm_frame h;
    gm_tikairc198460 ret;
};
struct gm_fr_record_dep_84vd3la {
    gm_frame h;
    gm_tqa5rsrlv432m l1; /* owner */
    gm_t6ok5tvcjc200 l2; /* key */
    gm_tbqru6ibafrpr l3; /* o */
    gm_trqfvmjtt4sij l5; /* keys */
    gm_b0_1 ret;
};
struct gm_fr_recorded_version_lb5b4vt {
    gm_frame h;
    gm_t2f6hp2vik01f l7; /* r */
    gm_tu1089hhijadf l9; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_refs_of_jfglrbh {
    gm_frame h;
    gm_t3rj5o15e922s l4; /* it */
    gm_tcmp1j2cnqi8i ret;
};
struct gm_fr_remove_tree_rp98g1s {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_req_same_h2pl2bq {
    gm_frame h;
    gm_t2f6hp2vik01f l0; /* a */
    gm_t2f6hp2vik01f l1; /* b */
    uint8_t ret;
};
struct gm_fr_resolver_ms661bf {
    gm_frame h;
    gm_tv37d3dpt2opu l3; /* old */
    gm_tlhfcpum9vunj ret;
};
struct gm_fr_run_resolver_tf2ua9j {
    gm_frame h;
    gm_tm43kqe3e8b4a l2; /* manifest */
    gm_tin97tt4ud0s1 l4; /* lock */
    gm_t8a9rhgj2oa25 ret;
};
struct gm_fr_satisfies_mlodg56 {
    gm_frame h;
    gm_toq2f2q4opoa3 l2; /* spec */
    gm_tin97tt4ud0s1 l6; /* req */
    gm_tu1089hhijadf l8; /* v */
    uint8_t ret;
};
struct gm_fr_sha_mismatch_lq7bp8e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_short_rev_uin3i2s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_1kptd3j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_subdir_of_gjqk3qb {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_summarize_archive_cicsplu {
    gm_frame h;
    gm_tq6bq450l0idg ret;
};
struct gm_fr_tag_version_aloeltn {
    gm_frame h;
    gm_tom1ad5bcqgi1 ret;
};
struct gm_fr_walk_59naf6k {
    gm_frame h;
    gm_tqa5rsrlv432m l4; /* owner */
    gm_tk9n3vlila9qg l8; /* s */
    gm_tk9n3vlila9qg l12; /* t */
    gm_trqfvmjtt4sij l13; /* names */
    gm_tk9n3vlila9qg l16; /* t */
    gm_trqfvmjtt4sij l17; /* deps */
    gm_t7tiv6vif86qg l20; /* dep */
    gm_tbqru6ibafrpr l24; /* o */
    gm_tm43kqe3e8b4a l27; /* m */
    gm_te5jk64pjl28k l28; /* g */
    gm_t6ok5tvcjc200 l29; /* key */
    gm_t8qqrlhb610a2 l30; /* choice */
    gm_tm43kqe3e8b4a l33; /* m */
    gm_tt008mgut8glb l35; /* u */
    gm_t0uuhaljsf410 l36; /* info */
    gm_t6ok5tvcjc200 l37; /* key */
    gm_tm43kqe3e8b4a l40; /* m */
    gm_t6ok5tvcjc200 l44; /* k */
    gm_tl572d7579hns ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_req_klu87ju(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_after_req_ti0a6un(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_archive_dir_srvvp3g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_archive_info_gq6dkee(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ti6pdj2sunpe4 gm_f_archive_info_gq6dkee_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_build_lock_hmfdkj8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_check_refresh_names_9ih3ok9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_checkout_dir_mvijcuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_checkout_j42th18(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_choose_9ee155u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_clear_m4blb4l(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_pirklev(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_qd87n65(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_rm1b3rc(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_s0loit2(void * a0);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dependency_names_6m0jc80(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_mp0u2mq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_download_eek737p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_drop_nns2kfo(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_effective_vikkdqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_attt3ii(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cpdaeui(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gsm9gv5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvf8gur(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lcqki20(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mnej5m4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n8lsnom(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oihj9aq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p1sl7gg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s0g91u7(void);
__attribute__((aligned(4))) void gm_f_ensure_archive_70kh53q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_ensure_checkout_hvmsso1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_entry_name_qnlemsm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_fetch_mirror_q06gvv0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_find_locked_83oh60j(void *gm_out, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_find_ref_h89e731(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_find_ref_h89e731_c(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_0c1hioc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2orhd15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3pk07bs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7sll6e8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_95e0vi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9di2rrs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ajecpf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_asjuqmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bpl33sh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cvqjb45(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dvcii4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eagbsnc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fj9bgml(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fnh5q7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g1540jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ia6bs6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jth3mgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l821hfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lbar0n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_m5mroph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mv68p71(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n8h20c1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nsoplg3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o83d7mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ro2n9te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rpqj2mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s20j3pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_u75ve7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vakr6l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_get_7q4fb48(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_get_9fn35sd(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_get_a0eoncd(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_get_vc34oj2(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_introsort_h9phkod(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s_c(gm_toq2f2q4opoa3 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_refreshed_ifi7dst(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf_c(gm_t6ok5tvcjc200 a0, gm_t6ok5tvcjc200 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_less_9obq0rq(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_key_map_136uk7q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_key_map_e8nmg3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_key_map_ihrh7ij(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_key_map_kjnah27(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1q70459(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_254rpp1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_2hljflu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6vurufm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_agmtpnt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dc4p80b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jsujn2k(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nccb2fo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_o444f8n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qeqmcmu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_refs_j3is1o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_vsi9vun(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_locked_entry_4693mpc(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tie69pa8pplgc gm_f_locked_entry_4693mpc_c(void * a0, gm_t6ok5tvcjc200 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_mark_used_5dtkpil(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_mirror_8u3u6oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_option_less_72r3i7m(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_pick_ronijl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_rnfkbb9(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1htqqaf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7mbittu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d61dqrj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_fp6nnvl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ljifgtt(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_mt615h0(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r6o112l(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vgb8k63(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_1jc6f83(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_4eaoh8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_jng5s3d(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_ui22st8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_read_manifest_u9uajvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_text_g0qbj1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_dep_84vd3la(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_recorded_version_lb5b4vt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_refs_of_jfglrbh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tcmp1j2cnqi8i gm_f_refs_of_jfglrbh_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_repo_dir_name_6927pg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_req_matches_dflcrek(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq_c(gm_t2f6hp2vik01f a0, gm_t2f6hp2vik01f a1);
__attribute__((aligned(4))) void gm_f_resolver_ms661bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_rev_parse_3l092b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_run_resolver_tf2ua9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_mlodg56(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sha_mismatch_lq7bp8e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_short_rev_uin3i2s(gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_slot_5e9u9id(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_slot_9ns6k3q(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_1kptd3j(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_store_for_package_347124r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_subdir_of_gjqk3qb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_summarize_archive_cicsplu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4);
__attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_tree_checksum_k6o2imm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_unpack_eb2aboi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_walk_59naf6k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3, const void * a4);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_clp0sh2cgr2[16] __attribute__((aligned(8)));
extern const uint8_t gm_s2j8t1jqamft8[5];
extern const uint8_t gm_s5ujsjbtr77bt[14];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sbpcdsg6ifd91[5];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_ses9qtp6kdd22[8];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgib96mf5meqv[11];
extern const uint8_t gm_sn2t7jm5mf8pm[5];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_spc0i9srvnmq5[6];
extern const uint8_t gm_sr0hr1ep2j2vv[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcpsceljs9unl1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssdi9g220if8h[2];
extern const uint8_t gm_ssv5povjldd7c[3];
extern const uint8_t gm_su2m3cb0nts7p[3];
extern const uint8_t gm_svulttobk4q7t[4];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
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
inline __attribute__((aligned(4))) void gm_f_clear_m4blb4l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_m4blb4l F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 80)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_pirklev(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_pirklev F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 96)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_qd87n65(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_qd87n65 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_rm1b3rc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_rm1b3rc F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_s0loit2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_s0loit2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_db_tssgvk0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* url */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su2m3cb0nts7p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_l1;
    gm_b16_8 t6 = gm_f_repo_dir_name_6927pg7((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t4, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_discard_bdpju80 F;
    gm_t6j6smj7qoctu gm_l0; /* d */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
            gm_ttln0d0hd70en t4 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_downloads_vdenqu5 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa2h9vonan0ma));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_14fhq9i F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_attt3ii(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_attt3ii F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cpdaeui(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cpdaeui F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gsm9gv5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gsm9gv5 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hr6df4i F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvf8gur(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kvf8gur F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lcqki20(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lcqki20 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mnej5m4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mnej5m4 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n8lsnom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n8lsnom F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 168)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oihj9aq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oihj9aq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p1sl7gg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p1sl7gg F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pkfv52g F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s0g91u7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s0g91u7 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    gm_b16_8 t5 = t2;
    gm_b16_8 t6 = t4;
    uint8_t t7 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_p4tis8s F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = ((GM_LD(uint8_t, ((uint8_t *)a0) + 0) == ((uint8_t)0ULL)));
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_0k0up67 F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
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
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
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
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_equal_n71njnf F;
    GM_STACK_CHECK_SHALLOW(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
    gm_b16_8 t4 = t2;
    gm_b16_8 t5 = t3;
    uint8_t t6 = (gm_eq0iqiubfm7e(((uint8_t *)&t4), ((uint8_t *)&t5)));
    if (t6) {
        gm_th2lqbo77pvsc t7 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 16));
        gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 16));
        gm_th2lqbo77pvsc t9 = t7;
        gm_th2lqbo77pvsc t10 = t8;
        uint8_t t11 = (gm_eq0ogca82g1f(((uint8_t *)&t9), ((uint8_t *)&t10)));
        t1 = t11;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_key_less_9obq0rq(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_less_9obq0rq F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 199u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
        gm_b16_8 t7 = t4;
        gm_b16_8 t8 = t6;
        uint8_t t9 = (!gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 0));
            gm_b16_8 t15 = t12;
            gm_b16_8 t16 = t14;
            uint8_t t17 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        uint8_t t21 = gm_f_option_less_72r3i7m(&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t19) + 16)), &(*(gm_th2lqbo77pvsc *)(((uint8_t *)t20) + 16)));
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_key_map_136uk7q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_136uk7q F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_254rpp1((void *)&t1, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_key_map_e8nmg3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_e8nmg3h F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_o444f8n((void *)&t1, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_key_map_ihrh7ij(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_ihrh7ij F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_2hljflu((void *)&t1, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_key_map_kjnah27(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_key_map_kjnah27 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_6vurufm((void *)&t1, (void *)NULL, cx, NULL);
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
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0pjrgaq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_pkfv52g();
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
inline __attribute__((aligned(4))) void gm_f_list_1q70459(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1q70459 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hr6df4i();
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
inline __attribute__((aligned(4))) void gm_f_list_254rpp1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_254rpp1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_gsm9gv5();
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
inline __attribute__((aligned(4))) void gm_f_list_2hljflu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2hljflu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s0g91u7();
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
inline __attribute__((aligned(4))) void gm_f_list_6vurufm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6vurufm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oihj9aq();
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
inline __attribute__((aligned(4))) void gm_f_list_agmtpnt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_agmtpnt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_mnej5m4();
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
inline __attribute__((aligned(4))) void gm_f_list_dc4p80b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dc4p80b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_14fhq9i();
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
inline __attribute__((aligned(4))) void gm_f_list_jsujn2k(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jsujn2k F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lcqki20();
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
inline __attribute__((aligned(4))) void gm_f_list_nccb2fo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nccb2fo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_attt3ii();
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
inline __attribute__((aligned(4))) void gm_f_list_o444f8n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_o444f8n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_cpdaeui();
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
inline __attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pfs7ocg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n8lsnom();
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
inline __attribute__((aligned(4))) void gm_f_list_qeqmcmu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qeqmcmu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kvf8gur();
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
inline __attribute__((aligned(4))) void gm_f_list_vsi9vun(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vsi9vun F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p1sl7gg();
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
inline __attribute__((aligned(4))) void gm_f_locked_entry_4693mpc(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_locked_entry_4693mpc F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 393u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tv37d3dpt2opu t3 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)t2) + 64));
        gm_tv37d3dpt2opu t4 = t3;
        gm_tin97tt4ud0s1 t5;
        if (((((uint8_t *)&t4))[40] != 0)) {
            gm_tin97tt4ud0s1 t6 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tie69pa8pplgc t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        F.l2 = t5;
    }
    {
        void * t9 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_f_find_locked_83oh60j((void *)&(*(gm_tie69pa8pplgc *)gm_out), t9, t10, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)a1) + 16)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_read_only_a29eok8 F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svulttobk4q7t));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
        gm_b16_8 t3 = gm_l0;
        gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_writable_6uascrq F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spc0i9srvnmq5));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
        gm_b16_8 t3 = gm_l0;
        gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_rnfkbb9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_rnfkbb9 F;
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
            gm_ta2tdjm589013 t8;
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
        gm_tk9n3vlila9qg t17 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_tk9n3vlila9qg t29 = F.l2;
        gm_ta2tdjm589013 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 44, 0);
        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[40] = 1;
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
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_tree_rp98g1s F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b0_1 t3 = gm_f_make_writable_6uascrq((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_l0;
        gm_ttln0d0hd70en t5 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t6 = t5;
        uint8_t t7 = ((((uint8_t *)&t6))[0] != 0);
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
inline __attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_req_same_h2pl2bq F;
    GM_STACK_CHECK_SHALLOW(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = t4;
    gm_b16_8 t7 = t5;
    uint8_t t8 = (gm_eq0iqiubfm7e(((uint8_t *)&t6), ((uint8_t *)&t7)));
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 16));
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 16));
        gm_b16_8 t11 = t9;
        gm_b16_8 t12 = t10;
        uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
        t3 = t13;
    } else t3 = 0;
    if (t3) {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 32));
        gm_b16_8 t16 = t14;
        gm_b16_8 t17 = t15;
        uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
        t2 = t18;
    } else t2 = 0;
    if (t2) {
        uint8_t t19 = gm_f_spec_equal_foa1umn(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a0) + 48)), &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a1) + 48)));
        t1 = t19;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_short_rev_uin3i2s(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_short_rev_uin3i2s F;
    gm_b16_8 gm_l0; /* rev */
    GM_STACK_CHECK_SHALLOW(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t6;
    uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    uint8_t t9 = (t8 < ((uint64_t)12ULL));
    if (!t9) goto ie7;
    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    uint64_t t12 = t11;
    t6 = t12;
    goto ix7;
ie7:;
    uint64_t t14 = ((uint64_t)12ULL);
    t6 = t14;
ix7:;
    uint64_t t15 = (uintptr_t)t6;
    if (t5 > t15 || t15 > (uintptr_t)t3) gm_trap_slice(t5, t15, t3, gm_srcpsceljs9unl1, 516, 43);
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15 - t5));
    F.ret = t16;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_1kptd3j(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_1kptd3j F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_h9phkod(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
        gm_f_introsort_r6p6ckl(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 115u);
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
    gm_th2lqbo77pvsc t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_req_klu87ju(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_req_klu87ju F;
    void * gm_l0; /* r */
    void * gm_l4; /* reqs */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 112);
        void * t4 = gm_f_slot_9ns6k3q(t3, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        void * t5 = t4;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            {
                gm_f_list_vsi9vun((void *)&F.l3, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t9 = (void *)((uint8_t *)&F.l3);
                gm_tftb1odc739t5 t10 = gm_f_push_vgb8k63(t9, &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
                gm_tftb1odc739t5 t11 = t10;
                if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcpsceljs9unl1, 407, 9);
                gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
            }
            {
                void * t13 = gm_l0;
                void * t14 = (void *)(((uint8_t *)t13) + 112);
                gm_b0_1 t15 = gm_f_put_ui22st8((void *)NULL, cx, NULL, t14, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t16 = gm_l0;
                gm_b0_1 t17 = gm_f_after_req_ti0a6un((void *)NULL, cx, NULL, t16, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t18;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        {
            void * t21 = gm_l4;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
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
                    gm_t2f6hp2vik01f t34 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t30 * 96));
                    F.l7 = t34;
                    {
                        uint8_t t37 = gm_f_req_same_h2pl2bq(&(*(gm_t2f6hp2vik01f *)((uint8_t *)&F.l7)), &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
                        if (!t37) goto ie36;
                        {
                            gm_b0_1 t39;
                            uint8_t t40 = 0;
                            F.ret = t39;
                            goto gm_fret;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    goto me32;
mn33:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 412, 5);
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
        void * t46 = gm_l4;
        gm_tftb1odc739t5 t47 = gm_f_push_vgb8k63(t46, &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
        gm_tftb1odc739t5 t48 = t47;
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcpsceljs9unl1, 415, 5);
        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
    }
    {
        void * t50 = gm_l0;
        gm_b0_1 t51 = gm_f_after_req_ti0a6un((void *)NULL, cx, NULL, t50, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_t2f6hp2vik01f *)((uint8_t *)a2)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_after_req_ti0a6un(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_after_req_ti0a6un F;
    void * gm_l0; /* r */
    GM_STACK_CHECK(&F, 325u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 312);
        gm_tgi1k2malmpgi t4;
        gm_f_get_a0eoncd((void *)&t4, t3, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tgi1k2malmpgi t5 = t4;
        gm_t8qqrlhb610a2 t6;
        if (((((uint8_t *)&t5))[56] != 0)) {
            gm_t8qqrlhb610a2 t7 = GM_LD(gm_t8qqrlhb610a2, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        F.l3 = t6;
    }
    {
        uint8_t t11 = gm_f_is_default_p4tis8s(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a2) + 48)));
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie10;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t15 = gm_f_satisfies_mlodg56((void *)NULL, cx, NULL, t13, t14, &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)a2) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie10;
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 312);
            gm_f_drop_nns2kfo(t19, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        }
        {
            void * t20 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t20) + 552), ((uint8_t)1));
        }
        goto ix10;
ie10:;
ix10:;
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
inline __attribute__((aligned(4))) void gm_f_archive_info_gq6dkee(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_archive_info_gq6dkee F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 468u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 232) + 0));
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
                    gm_t0uuhaljsf410 t16 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 64));
                    F.l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            gm_t0uuhaljsf410 t25 = F.l4;
                            gm_ti6pdj2sunpe4 t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 65, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 66, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 68, 0);
                            GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[64] = 1;
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
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 610, 5);
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
        gm_ti6pdj2sunpe4 t33;
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
__attribute__((aligned(4))) gm_ti6pdj2sunpe4 gm_f_archive_info_gq6dkee_c(void * a0, gm_b16_8 a1) { gm_ti6pdj2sunpe4 gm_r; gm_f_archive_info_gq6dkee(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_build_lock_hmfdkj8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_build_lock_hmfdkj8 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* checksum */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l20; /* url */
    GM_STACK_CHECK(&F, 3446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_pfs7ocg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_qeqmcmu((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 312) + 0) + 0));
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
                    gm_tv2l6796tk7ol t16 = GM_LD(gm_tv2l6796tk7ol, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 96));
                    F.l5 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t19 = gm_f_push_d61dqrj(t18, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l5) + 0)));
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcpsceljs9unl1, 836, 39);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 836, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l4;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l4 = t26;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_closure t28 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_key_less_9obq0rq)), NULL, cx});
        gm_f_sort_by_1kptd3j(t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l6 = t30;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t32 = gm_l7;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l7;
                    uint64_t t38 = (uintptr_t)t37;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t6ok5tvcjc200 t42 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t38 * 40));
                    F.l8 = t42;
                    {
                        void * t44 = gm_l0;
                        void * t45 = (void *)(((uint8_t *)t44) + 312);
                        gm_tgi1k2malmpgi t46;
                        gm_f_get_a0eoncd((void *)&t46, t45, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                        gm_tgi1k2malmpgi t47 = t46;
                        if (!((((uint8_t *)&t47))[56] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 839, 19);
                        gm_t8qqrlhb610a2 t48 = GM_LD(gm_t8qqrlhb610a2, ((uint8_t *)&t47));
                        F.l9 = t48;
                    }
                    {
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(0));
                        gm_l10 = t49;
                    }
                    {
                        void * t51 = gm_l0;
                        void * t52 = (void *)(((uint8_t *)t51) + 432);
                        gm_th2lqbo77pvsc t53;
                        gm_f_get_vc34oj2((void *)&t53, t52, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                        gm_th2lqbo77pvsc t54 = t53;
                        if (!((((uint8_t *)&t54))[16] != 0)) goto ie50;
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                        gm_l11 = t55;
                        {
                            gm_b16_8 t57 = gm_l11;
                            gm_l10 = t57;
                        }
                        goto ix50;
ie50:;
                        void * t59 = gm_l0;
                        gm_tie69pa8pplgc t60;
                        gm_f_locked_entry_4693mpc((void *)&t60, t59, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                        gm_tie69pa8pplgc t61 = t60;
                        if (!((((uint8_t *)&t61))[168] != 0)) goto ie58;
                        gm_ts1nru0t7g7aj t62 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&t61));
                        F.l12 = t62;
                        {
                            gm_b16_8 t64;
                            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 96));
                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_b16_8 t68 = t66;
                            gm_b16_8 t69 = t67;
                            uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                            if (!t70) goto ie65;
                            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 136));
                            gm_b16_8 t73 = t72;
                            t64 = t73;
                            goto ix65;
ie65:;
                            void * t75 = gm_l0;
                            void * t76 = (void *)(((uint8_t *)t75) + 392);
                            gm_th2lqbo77pvsc t77;
                            gm_f_get_vc34oj2((void *)&t77, t76, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                            gm_th2lqbo77pvsc t78 = t77;
                            if (!((((uint8_t *)&t78))[16] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 844, 87);
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_t3ujhu6qph7de t80;
                            gm_f_tree_checksum_k6o2imm((void *)&t80, (void *)NULL, cx, NULL, t79);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            gm_t3ujhu6qph7de t81 = t80;
                            if (((((uint8_t *)&t81))[0] != 0)) {
                                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
                                gm_t62efos6ssciq t83;
                                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t83 + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&t83 + 32, 0);
                                GM_ST(uint64_t, (uint8_t *)&t83 + 40, 0);
                                ((uint8_t *)&t83)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t83 + 8), t82);
                                F.ret = t83;
                                goto gm_fret;
                            }
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
                            gm_b16_8 t85 = t84;
                            t64 = t85;
ix65:;
                            gm_l10 = t64;
                        }
                        goto ix58;
ie58:;
                        {
                            void * t87 = gm_l0;
                            void * t88 = (void *)(((uint8_t *)t87) + 392);
                            gm_th2lqbo77pvsc t89;
                            gm_f_get_vc34oj2((void *)&t89, t88, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                            gm_th2lqbo77pvsc t90 = t89;
                            if (!((((uint8_t *)&t90))[16] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 846, 42);
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                            gm_t3ujhu6qph7de t92;
                            gm_f_tree_checksum_k6o2imm((void *)&t92, (void *)NULL, cx, NULL, t91);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            gm_t3ujhu6qph7de t93 = t92;
                            if (((((uint8_t *)&t93))[0] != 0)) {
                                gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
                                gm_t62efos6ssciq t95;
                                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t95 + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&t95 + 32, 0);
                                GM_ST(uint64_t, (uint8_t *)&t95 + 40, 0);
                                ((uint8_t *)&t95)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t95 + 8), t94);
                                F.ret = t95;
                                goto gm_fret;
                            }
                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
                            gm_l10 = t96;
                        }
ix58:;
ix50:;
                    }
                    {
                        void * t97 = (void *)((uint8_t *)&F.l1);
                        gm_ts1nru0t7g7aj t98;
                        void * t99 = gm_l0;
                        gm_b16_8 t100 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t99, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll35;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t100);
                        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t101);
                        memcpy((uint8_t *)&t98 + 32, (((uint8_t *)&F.l8) + 16), 24);
                        memcpy((uint8_t *)&t98 + 56, (((uint8_t *)&F.l9) + 16), 24);
                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 40));
                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 80, t102);
                        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 96, t103);
                        gm_th2lqbo77pvsc t104;
                        memset(&t104, 0, sizeof t104);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t98 + 112, t104);
                        gm_b16_8 t105 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 136, t105);
                        void * t106 = gm_l0;
                        gm_b16_8 t107 = gm_f_dependency_names_6m0jc80((void *)NULL, cx, NULL, t106, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll35;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 152, t107);
                        gm_tftb1odc739t5 t108 = gm_f_push_1htqqaf(t97, &t98);
                        gm_tftb1odc739t5 t109 = t108;
                        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcpsceljs9unl1, 848, 9);
                        gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 838, 5);
me40:;
                }
lc31:;
                {
                    uint64_t t112 = gm_l7;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l7 = t115;
                }
            }
gm_ll35:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
            }
            goto gm_lad;
lb31:;
        }
    }
    {
        gm_f_list_qeqmcmu((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t117 = gm_l0;
            gm_b16_8 t118 = GM_LD(gm_b16_8, ((((uint8_t *)t117) + 352) + 0));
            gm_l14 = t118;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t120 = gm_l15;
                uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t122 = (t120 < t121);
                if (!t122) goto lb119;
                {
                    uint64_t t125 = gm_l15;
                    uint64_t t126 = (uintptr_t)t125;
                    uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_t6ok5tvcjc200 t130 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t126 * 40));
                    F.l16 = t130;
                    {
                        void * t132 = (void *)((uint8_t *)&F.l13);
                        gm_tftb1odc739t5 t133 = gm_f_push_d61dqrj(t132, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l16)));
                        gm_tftb1odc739t5 t134 = t133;
                        if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_srcpsceljs9unl1, 861, 38);
                        gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                    }
                    goto me128;
mn129:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 861, 5);
me128:;
                }
lc119:;
                {
                    uint64_t t137 = gm_l15;
                    uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                    uint64_t t140 = r138;
                    gm_l15 = t140;
                }
            }
lb119:;
        }
    }
    {
        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_closure t142 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_key_less_9obq0rq)), NULL, cx});
        gm_f_sort_by_1kptd3j(t141, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_l17 = t144;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t146 = gm_l18;
                uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t148 = (t146 < t147);
                if (!t148) goto lb145;
                {
                    uint64_t t151 = gm_l18;
                    uint64_t t152 = (uintptr_t)t151;
                    uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_t6ok5tvcjc200 t156 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t152 * 40));
                    F.l19 = t156;
                    {
                        gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                        gm_b16_8 t159;
                        GM_ST(void *, (uint8_t *)&t159, (void *)(gm_s2j8t1jqamft8));
                        GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(4));
                        gm_th2lqbo77pvsc t160;
                        gm_f_strip_prefix_b4949s8((void *)&t160, t158, t159);
                        gm_th2lqbo77pvsc t161 = t160;
                        gm_b16_8 t162;
                        if (((((uint8_t *)&t161))[16] != 0)) {
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 0));
                            t162 = t163;
                        } else {
                            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                            t162 = t164;
                        }
                        gm_l20 = t162;
                    }
                    {
                        void * t165 = gm_l0;
                        gm_b16_8 t166 = gm_l20;
                        gm_ti6pdj2sunpe4 t167;
                        gm_f_archive_info_gq6dkee((void *)&t167, t165, t166);
                        gm_ti6pdj2sunpe4 t168 = t167;
                        if (!((((uint8_t *)&t168))[64] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 865, 17);
                        gm_t0uuhaljsf410 t169 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&t168));
                        F.l21 = t169;
                    }
                    {
                        void * t170 = (void *)((uint8_t *)&F.l1);
                        gm_ts1nru0t7g7aj t171;
                        void * t172 = gm_l0;
                        gm_b16_8 t173 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t172, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l19)));
                        if (gm_unw.kind) {
                            goto gm_ll149;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t171 + 0, t173);
                        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t171 + 16, t174);
                        memcpy((uint8_t *)&t171 + 32, (((uint8_t *)&F.l19) + 16), 24);
                        gm_th2lqbo77pvsc t175;
                        memset(&t175, 0, sizeof t175);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t171 + 56, t175);
                        gm_b16_8 t176;
                        GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(0));
                        GM_ST(gm_b16_8, (uint8_t *)&t171 + 80, t176);
                        gm_b16_8 t177;
                        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(0));
                        GM_ST(gm_b16_8, (uint8_t *)&t171 + 96, t177);
                        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 16));
                        gm_tnpifv20irnph t179;
                        GM_ST(gm_b16_8, ((uint8_t *)&t179 + 0), t178);
                        gm_tabb710d9mq2e t180;
                        gm_f_format_9di2rrs((void *)&t180, (void *)NULL, cx, NULL, t179);
                        if (gm_unw.kind) {
                            goto gm_ll149;
                        }
                        gm_tabb710d9mq2e t181 = t180;
                        if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_srcpsceljs9unl1, 873, 22);
                        gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
                        gm_b16_8 t183 = t182;
                        gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
                        gm_th2lqbo77pvsc t185;
                        GM_ST(uint8_t, (uint8_t *)&t185 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t185 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t185 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t185), t184);
                        ((uint8_t *)&t185)[16] = 1;
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t171 + 112, t185);
                        gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 48));
                        GM_ST(gm_b16_8, (uint8_t *)&t171 + 136, t186);
                        void * t187 = gm_l0;
                        gm_b16_8 t188 = gm_f_dependency_names_6m0jc80((void *)NULL, cx, NULL, t187, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l19)));
                        if (gm_unw.kind) {
                            goto gm_ll149;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t171 + 152, t188);
                        gm_tftb1odc739t5 t189 = gm_f_push_1htqqaf(t170, &t171);
                        gm_tftb1odc739t5 t190 = t189;
                        if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_srcpsceljs9unl1, 866, 9);
                        gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
                    }
                    goto me154;
mn155:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 863, 5);
me154:;
                }
lc145:;
                {
                    uint64_t t193 = gm_l18;
                    uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                    uint64_t t196 = r194;
                    gm_l18 = t196;
                }
            }
gm_ll149:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb145; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc145; }
            }
            goto gm_lad;
lb145:;
        }
    }
    {
        gm_tin97tt4ud0s1 t197;
        memcpy((uint8_t *)&t197 + 0, ((uint8_t *)&F.l1), 40);
        gm_t62efos6ssciq t198;
        GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
        ((uint8_t *)&t198)[0] = 0;
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t198 + 8), t197);
        gm_t62efos6ssciq t199 = t198;
        uint8_t t200 = ((((uint8_t *)&t199))[0] != 0);
        F.ret = t198;
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
__attribute__((aligned(4))) void gm_f_check_refresh_names_9ih3ok9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_refresh_names_9ih3ok9 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* n */
    gm_b16_8 gm_l2; /* names */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* n */
    uint8_t gm_l6; /* known */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* u */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    GM_STACK_CHECK(&F, 1423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t2) + 24) + 0));
        if (t6 != ((uint8_t)2ULL)) goto mn5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 24) + 8));
        gm_l1 = t7;
        gm_b16_8 t8 = gm_l1;
        t3 = t8;
        goto me4;
mn5:;
        gm_b0_1 t10;
        gm_tl572d7579hns t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
        ((uint8_t *)&t11)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
        gm_tl572d7579hns t12 = t11;
        uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
        F.ret = t11;
        goto gm_fret;
        goto me4;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 372, 14);
me4:;
        gm_l2 = t3;
    }
    {
        {
            gm_b16_8 t15 = gm_l2;
            gm_l3 = t15;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l4;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t23 * 16));
                    gm_l5 = t27;
                    {
                        gm_l6 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t30 = gm_l0;
                            gm_b16_8 t31 = GM_LD(gm_b16_8, (((((uint8_t *)t30) + 312) + 0) + 0));
                            gm_l7 = t31;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t33 = gm_l8;
                                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t35 = (t33 < t34);
                                if (!t35) goto lb32;
                                {
                                    uint64_t t38 = gm_l8;
                                    uint64_t t39 = (uintptr_t)t38;
                                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_tv2l6796tk7ol t43 = GM_LD(gm_tv2l6796tk7ol, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t39 * 96));
                                    F.l9 = t43;
                                    {
                                        void * t46 = gm_l0;
                                        gm_b16_8 t47 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t46, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l9) + 0)));
                                        if (gm_unw.kind) {
                                            goto gm_ll36;
                                        }
                                        gm_b16_8 t48 = gm_l5;
                                        gm_b16_8 t49 = t47;
                                        gm_b16_8 t50 = t48;
                                        uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
                                        if (!t51) goto ie45;
                                        {
                                            gm_l6 = ((uint8_t)1);
                                        }
                                        goto ix45;
ie45:;
ix45:;
                                    }
                                    goto me41;
mn42:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 378, 9);
me41:;
                                }
lc32:;
                                {
                                    uint64_t t54 = gm_l8;
                                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                                    uint64_t t57 = r55;
                                    gm_l8 = t57;
                                }
                            }
gm_ll36:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
                            }
                            goto gm_ll20;
lb32:;
                        }
                    }
                    {
                        {
                            void * t59 = gm_l0;
                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((((uint8_t *)t59) + 272) + 0));
                            gm_l10 = t60;
                        }
                        {
                            gm_l11 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t62 = gm_l11;
                                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint8_t t64 = (t62 < t63);
                                if (!t64) goto lb61;
                                {
                                    uint64_t t67 = gm_l11;
                                    uint64_t t68 = (uintptr_t)t67;
                                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t68 * 16));
                                    gm_l12 = t72;
                                    {
                                        gm_b16_8 t75 = gm_l12;
                                        gm_b16_8 t76 = gm_l5;
                                        gm_b16_8 t77 = t75;
                                        gm_b16_8 t78 = t76;
                                        uint8_t t79 = (gm_eq0iqiubfm7e(((uint8_t *)&t77), ((uint8_t *)&t78)));
                                        if (!t79) goto ie74;
                                        {
                                            gm_l6 = ((uint8_t)1);
                                        }
                                        goto ix74;
ie74:;
ix74:;
                                    }
                                    goto me70;
mn71:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 381, 9);
me70:;
                                }
lc61:;
                                {
                                    uint64_t t82 = gm_l11;
                                    uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                                    uint64_t t85 = r83;
                                    gm_l11 = t85;
                                }
                            }
lb61:;
                        }
                    }
                    {
                        {
                            void * t87 = gm_l0;
                            gm_b16_8 t88 = GM_LD(gm_b16_8, (((((uint8_t *)t87) + 112) + 0) + 0));
                            gm_l13 = t88;
                        }
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t90 = gm_l14;
                                uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint8_t t92 = (t90 < t91);
                                if (!t92) goto lb89;
                                {
                                    uint64_t t95 = gm_l14;
                                    uint64_t t96 = (uintptr_t)t95;
                                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    gm_to54va2pes5v2 t100 = GM_LD(gm_to54va2pes5v2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t96 * 80));
                                    F.l15 = t100;
                                    {
                                        {
                                            gm_b16_8 t103 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l15) + 40) + 0));
                                            gm_l16 = t103;
                                        }
                                        {
                                            gm_l17 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t105 = gm_l17;
                                                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                                uint8_t t107 = (t105 < t106);
                                                if (!t107) goto lb104;
                                                {
                                                    uint64_t t110 = gm_l17;
                                                    uint64_t t111 = (uintptr_t)t110;
                                                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                                    gm_t2f6hp2vik01f t115 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t111 * 96));
                                                    F.l18 = t115;
                                                    {
                                                        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 16));
                                                        gm_b16_8 t119 = gm_l5;
                                                        gm_b16_8 t120 = t118;
                                                        gm_b16_8 t121 = t119;
                                                        uint8_t t122 = (gm_eq0iqiubfm7e(((uint8_t *)&t120), ((uint8_t *)&t121)));
                                                        if (!t122) goto ie117;
                                                        {
                                                            gm_l6 = ((uint8_t)1);
                                                        }
                                                        goto ix117;
ie117:;
ix117:;
                                                    }
                                                    goto me113;
mn114:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 385, 13);
me113:;
                                                }
lc104:;
                                                {
                                                    uint64_t t125 = gm_l17;
                                                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                                                    uint64_t t128 = r126;
                                                    gm_l17 = t128;
                                                }
                                            }
lb104:;
                                        }
                                    }
                                    goto me98;
mn99:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 384, 9);
me98:;
                                }
lc89:;
                                {
                                    uint64_t t130 = gm_l14;
                                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                                    uint64_t t133 = r131;
                                    gm_l14 = t133;
                                }
                            }
lb89:;
                        }
                    }
                    {
                        uint8_t t135 = gm_l6;
                        uint8_t t136 = (uint8_t)!t135;
                        if (!t136) goto ie134;
                        {
                            gm_b16_8 t138 = gm_l5;
                            gm_tnpifv20irnph t139;
                            GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t138);
                            gm_tabb710d9mq2e t140;
                            gm_f_format_u75ve7h((void *)&t140, (void *)NULL, cx, NULL, t139);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            gm_tabb710d9mq2e t141 = t140;
                            if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_srcpsceljs9unl1, 389, 32);
                            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
                            gm_b16_8 t143 = t142;
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                            gm_tl572d7579hns t145;
                            GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                            ((uint8_t *)&t145)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t145 + 8), t144);
                            gm_tl572d7579hns t146 = t145;
                            uint8_t t147 = ((((uint8_t *)&t146))[0] != 0);
                            F.ret = t145;
                            goto gm_fret;
                        }
                        goto ix134;
ie134:;
ix134:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 376, 5);
me25:;
                }
lc16:;
                {
                    uint64_t t149 = gm_l4;
                    uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                    uint64_t t152 = r150;
                    gm_l4 = t152;
                }
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
            }
            goto gm_lad;
lb16:;
        }
    }
    {
        gm_b0_1 t153;
        gm_tl572d7579hns t154;
        GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t154 + 16, 0);
        ((uint8_t *)&t154)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t154 + 8), t153);
        gm_tl572d7579hns t155 = t154;
        uint8_t t156 = ((((uint8_t *)&t155))[0] != 0);
        F.ret = t154;
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
__attribute__((aligned(4))) void gm_f_choose_9ee155u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_choose_9ee155u F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* reqs */
    uint8_t gm_l5; /* reuse */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l12; /* dep */
    gm_b16_8 gm_l13; /* url */
    GM_STACK_CHECK(&F, 3704u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 312);
        gm_tgi1k2malmpgi t5;
        gm_f_get_a0eoncd((void *)&t5, t4, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tgi1k2malmpgi t6 = t5;
        if (!((((uint8_t *)&t6))[56] != 0)) goto ie2;
        gm_t8qqrlhb610a2 t7 = GM_LD(gm_t8qqrlhb610a2, ((uint8_t *)&t6));
        F.l2 = t7;
        {
            gm_t8qqrlhb610a2 t9 = F.l2;
            gm_tmfqp43enl3pa t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t10 + 8), t9);
            gm_tmfqp43enl3pa t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 112);
        gm_tqetrgo0q24b5 t15;
        gm_f_get_9fn35sd((void *)&t15, t14, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tqetrgo0q24b5 t16 = t15;
        if (!((((uint8_t *)&t16))[40] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 473, 13);
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t16));
        gm_trqfvmjtt4sij t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 0));
        gm_l3 = t19;
    }
    {
        void * t20 = gm_l0;
        gm_f_locked_entry_4693mpc((void *)&F.l4, t20, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
    }
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        if (!((((uint8_t *)&F.l4))[168] != 0)) goto ie21;
        gm_ts1nru0t7g7aj t22 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&F.l4));
        F.l6 = t22;
        {
            void * t25 = gm_l0;
            uint8_t t26 = gm_f_is_refreshed_ifi7dst((void *)NULL, cx, NULL, t25, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) goto ie24;
            {
                gm_l5 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t30 = gm_l3;
                    gm_b16_8 t31 = t30;
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                    gm_b16_8 t33 = gm_f_effective_vikkdqh((void *)NULL, cx, NULL, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t33;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t35 = gm_l8;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t37 = (t35 < t36);
                        if (!t37) goto lb34;
                        {
                            uint64_t t40 = gm_l8;
                            uint64_t t41 = (uintptr_t)t40;
                            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_t2f6hp2vik01f t45 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t41 * 96));
                            F.l9 = t45;
                            {
                                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 80));
                                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 96));
                                uint8_t t50 = gm_f_satisfies_mlodg56((void *)NULL, cx, NULL, t48, t49, &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l9) + 48)));
                                if (gm_unw.kind) {
                                    goto gm_ll38;
                                }
                                uint8_t t51 = (uint8_t)!t50;
                                if (!t51) goto ie47;
                                {
                                    gm_l5 = ((uint8_t)0);
                                }
                                goto ix47;
ie47:;
ix47:;
                            }
                            goto me43;
mn44:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 479, 13);
me43:;
                        }
lc34:;
                        {
                            uint64_t t54 = gm_l8;
                            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                            uint64_t t57 = r55;
                            gm_l8 = t57;
                        }
                    }
gm_ll38:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
                    }
                    goto gm_lad;
lb34:;
                }
            }
            goto ix24;
ie24:;
ix24:;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 0, t58);
        gm_th2lqbo77pvsc t59;
        memset(&t59, 0, sizeof t59);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l10 + 16, t59);
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 40, t60);
    }
    {
        uint8_t t62 = gm_l5;
        if (!t62) goto ie61;
        {
            gm_tie69pa8pplgc t64 = F.l4;
            gm_tie69pa8pplgc t65 = t64;
            if (!((((uint8_t *)&t65))[168] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 486, 14);
            gm_ts1nru0t7g7aj t66 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&t65));
            F.l11 = t66;
        }
        {
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 96));
            gm_b16_8 t68 = gm_l3;
            gm_b16_8 t69 = t68;
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 80));
            gm_th2lqbo77pvsc t72;
            gm_f_recorded_version_lb5b4vt((void *)&t72, (void *)NULL, cx, NULL, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 80));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t8qqrlhb610a2 *)((uint8_t *)&F.l10)) + 0, t67);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t8qqrlhb610a2 *)((uint8_t *)&F.l10)) + 16, t72);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t8qqrlhb610a2 *)((uint8_t *)&F.l10)) + 40, t73);
        }
        goto ix61;
ie61:;
        {
            uint64_t t75 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcpsceljs9unl1, 491, 16);
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t75 * 96) + 16));
            gm_l12 = t77;
        }
        {
            uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcpsceljs9unl1, 492, 16);
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t78 * 96) + 32));
            gm_l13 = t80;
        }
        {
            void * t82 = gm_l0;
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t82) + 16) + 1));
            if (!t83) goto ie81;
            void * t84 = gm_l0;
            uint8_t t85;
            if ((((((uint8_t *)t84) + 64))[40] != 0)) goto mn87;
            t85 = ((uint8_t)1);
            goto me86;
mn87:;
            t85 = ((uint8_t)0);
            goto me86;
mn88:;
            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 493, 30);
me86:;
            if (!t85) goto ie81;
            {
                gm_b16_8 t90 = GM_LD(gm_b16_8, gm_clp0sh2cgr2);
                gm_tmfqp43enl3pa t91;
                memset(&t91, 0, sizeof t91);
                ((uint8_t *)&t91)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t91 + 8), t90);
                gm_tmfqp43enl3pa t92 = t91;
                uint8_t t93 = ((((uint8_t *)&t92))[0] != 0);
                F.ret = t91;
                goto gm_fret;
            }
            goto ix81;
ie81:;
ix81:;
        }
        {
            void * t95 = gm_l0;
            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t95) + 16) + 1));
            if (!t96) goto ie94;
            {
                gm_t6j6smj7qoctu t98;
                gm_b16_8 t99 = gm_l12;
                GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
                gm_b16_8 t100 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t100);
                gm_tabb710d9mq2e t101;
                gm_f_format_cvqjb45((void *)&t101, (void *)NULL, cx, NULL, &t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t102 = t101;
                if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srcpsceljs9unl1, 495, 21);
                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
                gm_b16_8 t104 = t103;
                gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                gm_tmfqp43enl3pa t106;
                memset(&t106, 0, sizeof t106);
                ((uint8_t *)&t106)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t106 + 8), t105);
                gm_tmfqp43enl3pa t107 = t106;
                uint8_t t108 = ((((uint8_t *)&t107))[0] != 0);
                F.ret = t106;
                goto gm_fret;
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            void * t110 = gm_l0;
            uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t110) + 16) + 0));
            if (!t111) goto ie109;
            {
                gm_t6j6smj7qoctu t113;
                gm_b16_8 t114 = gm_l12;
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
                gm_b16_8 t115 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t115);
                gm_tabb710d9mq2e t116;
                gm_f_format_ajecpf6((void *)&t116, (void *)NULL, cx, NULL, &t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t117 = t116;
                if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_srcpsceljs9unl1, 502, 21);
                gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                gm_b16_8 t119 = t118;
                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                gm_tmfqp43enl3pa t121;
                memset(&t121, 0, sizeof t121);
                ((uint8_t *)&t121)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t121 + 8), t120);
                gm_tmfqp43enl3pa t122 = t121;
                uint8_t t123 = ((((uint8_t *)&t122))[0] != 0);
                F.ret = t121;
                goto gm_fret;
            }
            goto ix109;
ie109:;
ix109:;
        }
        {
            void * t124 = gm_l0;
            gm_tl572d7579hns t125;
            gm_f_mirror_8u3u6oe((void *)&t125, (void *)NULL, cx, NULL, t124, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl572d7579hns t126 = t125;
            if (((((uint8_t *)&t126))[0] != 0)) {
                gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
                gm_tmfqp43enl3pa t128;
                memset(&t128, 0, sizeof t128);
                ((uint8_t *)&t128)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t128 + 8), t127);
                F.ret = t128;
                goto gm_fret;
            }
            gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 8));
        }
        {
            void * t130 = gm_l0;
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
            gm_tcmp1j2cnqi8i t132;
            gm_f_refs_of_jfglrbh((void *)&t132, t130, t131);
            gm_tcmp1j2cnqi8i t133 = t132;
            if (!((((uint8_t *)&t133))[104] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 509, 17);
            gm_tmf08dt0mom0q t134 = GM_LD(gm_tmf08dt0mom0q, ((uint8_t *)&t133));
            F.l14 = t134;
        }
        {
            gm_b16_8 t135 = gm_l13;
            gm_b16_8 t136 = gm_l3;
            gm_b16_8 t137 = t136;
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
            void * t139 = (void *)((uint8_t *)&F.l14);
            void * t140 = gm_l0;
            void * t141 = (void *)(((uint8_t *)t140) + 48);
            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
            gm_b16_8 t143 = gm_f_db_tssgvk0((void *)NULL, cx, NULL, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmfqp43enl3pa t144;
            gm_f_pick_ronijl9((void *)&t144, (void *)NULL, cx, NULL, t135, t138, t139, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmfqp43enl3pa t145 = t144;
            if (((((uint8_t *)&t145))[0] != 0)) {
                gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&t145) + 8));
                gm_tmfqp43enl3pa t147;
                memset(&t147, 0, sizeof t147);
                ((uint8_t *)&t147)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t147 + 8), t146);
                F.ret = t147;
                goto gm_fret;
            }
            gm_t8qqrlhb610a2 t148 = GM_LD(gm_t8qqrlhb610a2, (((uint8_t *)&t145) + 8));
            F.l10 = t148;
        }
ix61:;
    }
    {
        void * t149 = gm_l0;
        void * t150 = (void *)(((uint8_t *)t149) + 312);
        gm_b0_1 t151 = gm_f_put_4eaoh8f((void *)NULL, cx, NULL, t150, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), &(*(gm_t8qqrlhb610a2 *)((uint8_t *)&F.l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t8qqrlhb610a2 t152 = F.l10;
        gm_tmfqp43enl3pa t153;
        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
        ((uint8_t *)&t153)[0] = 0;
        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t153 + 8), t152);
        gm_tmfqp43enl3pa t154 = t153;
        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
        F.ret = t153;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_dependency_names_6m0jc80(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_dependency_names_6m0jc80 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* n */
    GM_STACK_CHECK(&F, 813u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 512);
        gm_tqetrgo0q24b5 t5;
        gm_f_get_7q4fb48((void *)&t5, t4, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tqetrgo0q24b5 t6 = t5;
        if (!((((uint8_t *)&t6))[40] != 0)) goto ie2;
        gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t6));
        F.l3 = t7;
        {
            {
                gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_l4 = t10;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t14 = (t12 < t13);
                    if (!t14) goto lb11;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_t6ok5tvcjc200 t22 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t18 * 40));
                        F.l6 = t22;
                        {
                            void * t24 = (void *)((uint8_t *)&F.l2);
                            void * t25 = gm_l0;
                            gm_b16_8 t26 = gm_f_entry_name_qnlemsm((void *)NULL, cx, NULL, t25, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l6)));
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            gm_tftb1odc739t5 t27 = gm_f_push_3ef746b(t24, t26);
                            gm_tftb1odc739t5 t28 = t27;
                            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcpsceljs9unl1, 816, 31);
                            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                        }
                        goto me20;
mn21:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 816, 9);
me20:;
                    }
lc11:;
                    {
                        uint64_t t31 = gm_l5;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        uint64_t t34 = r32;
                        gm_l5 = t34;
                    }
                }
gm_ll15:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
                }
                goto gm_lad;
lb11:;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_f_sort_skeree7(t35);
    }
    {
        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_l8 = t39;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t41 = gm_l9;
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    gm_tf1bjg81u0glv t46;
                    uint64_t t47 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t46 + 0, t47);
                    uint64_t t48 = gm_l9;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t49 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t46 + 8, t51);
                    gm_tf1bjg81u0glv t52 = t46;
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&t52) + 0));
                    gm_l10 = t55;
                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                    gm_l11 = t56;
                    {
                        uint64_t t59 = gm_l10;
                        uint8_t t60 = (t59 > ((uint64_t)0ULL));
                        if (!t60) goto ie58;
                        uint64_t t61 = gm_l10;
                        uint64_t r62; bool o63 = __builtin_sub_overflow(t61, ((uint64_t)1ULL), &r62);
                        if (o63) gm_trap("integer overflow in `-`", gm_srcpsceljs9unl1, 821, 34);
                        uint64_t t64 = r62;
                        uint64_t t65 = (uintptr_t)t64;
                        uint64_t t66 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                        if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_srcpsceljs9unl1, 821, 22);
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t65 * 16));
                        gm_b16_8 t68 = gm_l11;
                        gm_b16_8 t69 = t67;
                        gm_b16_8 t70 = t68;
                        uint8_t t71 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t70)));
                        if (!t71) goto ie58;
                        {
                            goto lc40;
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    {
                        void * t73 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t74 = gm_l11;
                        gm_tftb1odc739t5 t75 = gm_f_push_3ef746b(t73, t74);
                        gm_tftb1odc739t5 t76 = t75;
                        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcpsceljs9unl1, 822, 9);
                        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 820, 5);
me53:;
                }
lc40:;
                {
                    uint64_t t79 = gm_l9;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l9 = t82;
                }
            }
lb40:;
        }
    }
    {
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t84 = t83;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        uint8_t t86 = 0;
        F.ret = t85;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_mp0u2mq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_describe_mp0u2mq F;
    gm_b16_8 gm_l0; /* reqs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* p */
    GM_STACK_CHECK(&F, 973u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
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
                    gm_t2f6hp2vik01f t15 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 96));
                    F.l4 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l1);
                        gm_t6j6smj7qoctu t18;
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                        gm_b16_8 t20 = gm_f_spec_text_9k2gjft((void *)NULL, cx, NULL, &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l4) + 48)));
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
                        gm_tabb710d9mq2e t21;
                        gm_f_format_fj9bgml((void *)&t21, (void *)NULL, cx, NULL, &t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tabb710d9mq2e t22 = t21;
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcpsceljs9unl1, 124, 32);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
                        gm_b16_8 t24 = t23;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                        gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t17, t25);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcpsceljs9unl1, 124, 21);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 124, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t30 = gm_l3;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l3 = t33;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_f_sort_skeree7(t34);
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_b16_8 t37 = t36;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            gm_l6 = t38;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l7;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    gm_tf1bjg81u0glv t45;
                    uint64_t t46 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t45 + 0, t46);
                    uint64_t t47 = gm_l7;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t48 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t45 + 8, t50);
                    gm_tf1bjg81u0glv t51 = t45;
                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&t51) + 0));
                    gm_l8 = t54;
                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                    gm_l9 = t55;
                    {
                        uint64_t t58 = gm_l8;
                        uint8_t t59 = (t58 > ((uint64_t)0ULL));
                        if (!t59) goto ie57;
                        uint64_t t60 = gm_l8;
                        uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                        if (o62) gm_trap("integer overflow in `-`", gm_srcpsceljs9unl1, 128, 34);
                        uint64_t t63 = r61;
                        uint64_t t64 = (uintptr_t)t63;
                        uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcpsceljs9unl1, 128, 22);
                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t64 * 16));
                        gm_b16_8 t67 = gm_l9;
                        gm_b16_8 t68 = t66;
                        gm_b16_8 t69 = t67;
                        uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                        if (!t70) goto ie57;
                        {
                            goto lc39;
                        }
                        goto ix57;
ie57:;
ix57:;
                    }
                    {
                        void * t72 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t73 = gm_l9;
                        gm_tftb1odc739t5 t74 = gm_f_push_3ef746b(t72, t73);
                        gm_tftb1odc739t5 t75 = t74;
                        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srcpsceljs9unl1, 129, 9);
                        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                    }
                    goto me52;
mn53:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 127, 5);
me52:;
                }
lc39:;
                {
                    uint64_t t78 = gm_l7;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l7 = t81;
                }
            }
lb39:;
        }
    }
    {
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t83 = t82;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t86;
        gm_f_join_sfknljb((void *)&t86, (void *)NULL, cx, NULL, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t87 = t86;
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcpsceljs9unl1, 131, 12);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
        gm_b16_8 t89 = t88;
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
        uint8_t t91 = 0;
        F.ret = t90;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effective_vikkdqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_effective_vikkdqh F;
    gm_b16_8 gm_l0; /* reqs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vsi9vun((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
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
                    gm_t2f6hp2vik01f t15 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 96));
                    F.l4 = t15;
                    {
                        uint8_t t18 = gm_f_is_default_p4tis8s(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l4) + 48)));
                        uint8_t t19 = (uint8_t)!t18;
                        if (!t19) goto ie17;
                        {
                            void * t21 = (void *)((uint8_t *)&F.l1);
                            gm_tftb1odc739t5 t22 = gm_f_push_vgb8k63(t21, &(*(gm_t2f6hp2vik01f *)((uint8_t *)&F.l4)));
                            gm_tftb1odc739t5 t23 = t22;
                            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcpsceljs9unl1, 81, 37);
                            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 80, 5);
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
        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
        uint8_t t32 = ((t31 == ((uint64_t)0ULL)));
        if (!t32) goto ie30;
        {
            gm_b16_8 t34 = gm_l0;
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        uint8_t t39 = 0;
        F.ret = t38;
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
__attribute__((aligned(4))) void gm_f_ensure_archive_70kh53q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_ensure_archive_70kh53q F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* dep */
    gm_b16_8 gm_l4; /* h */
    gm_b16_8 gm_l5; /* lock_key */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_th2lqbo77pvsc gm_l11; /* locked_hash */
    gm_b16_8 gm_l13; /* a */
    gm_th2lqbo77pvsc gm_l14; /* expected_hash */
    gm_b16_8 gm_l15; /* origin */
    gm_b16_8 gm_l16; /* h */
    gm_b16_8 gm_l17; /* h */
    gm_th2lqbo77pvsc gm_l18; /* locked_sum */
    gm_b16_8 gm_l20; /* h */
    gm_b16_8 gm_l21; /* h */
    gm_b16_8 gm_l22; /* dest */
    gm_t6j6smj7qoctu gm_l24; /* dl */
    gm_b16_8 gm_l25; /* h */
    gm_b16_8 gm_l26; /* dest */
    gm_b16_8 gm_l27; /* e */
    gm_b16_8 gm_l28; /* e */
    gm_b16_8 gm_l29; /* sum */
    gm_b16_8 gm_l30; /* l */
    gm_b16_8 gm_l31; /* dir */
    GM_STACK_CHECK(&F, 8062u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_ti6pdj2sunpe4 t5;
        gm_f_archive_info_gq6dkee((void *)&t5, t3, t4);
        gm_ti6pdj2sunpe4 t6 = t5;
        if (!((((uint8_t *)&t6))[64] != 0)) goto ie2;
        gm_t0uuhaljsf410 t7 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)&t6));
        F.l3 = t7;
        {
            if (!(((((uint8_t *)a2) + 16))[16] != 0)) goto ie9;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            gm_l4 = t10;
            {
                gm_b16_8 t13 = gm_l4;
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
                gm_b16_8 t15 = t13;
                gm_b16_8 t16 = t14;
                uint8_t t17 = (!gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                if (!t17) goto ie12;
                {
                    gm_b16_8 t19 = gm_l1;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
                    gm_b16_8 t22 = gm_l4;
                    gm_b16_8 t23;
                    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_se24c7hpp401n));
                    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(10));
                    gm_b16_8 t24 = gm_f_sha_mismatch_lq7bp8e((void *)NULL, cx, NULL, t19, t20, t21, t22, t23);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq6bq450l0idg t25;
                    memset(&t25, 0, sizeof t25);
                    ((uint8_t *)&t25)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 8), t24);
                    gm_tq6bq450l0idg t26 = t25;
                    uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
                    F.ret = t25;
                    goto gm_fret;
                }
                goto ix12;
ie12:;
ix12:;
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            gm_t0uuhaljsf410 t28 = F.l3;
            gm_tq6bq450l0idg t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            ((uint8_t *)&t29)[0] = 0;
            GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t29 + 8), t28);
            gm_tq6bq450l0idg t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_s20j3pl((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcpsceljs9unl1, 625, 17);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
        gm_l5 = t36;
    }
    {
        gm_tie69pa8pplgc t37;
        memset(&t37, 0, sizeof t37);
        F.l6 = t37;
    }
    {
        void * t39 = gm_l0;
        if (!(((((uint8_t *)t39) + 64))[40] != 0)) goto ie38;
        gm_tin97tt4ud0s1 t40 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)t39) + 64));
        F.l7 = t40;
        {
            {
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                gm_l8 = t43;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t45 = gm_l9;
                    uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t47 = (t45 < t46);
                    if (!t47) goto lb44;
                    {
                        uint64_t t50 = gm_l9;
                        uint64_t t51 = (uintptr_t)t50;
                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_ts1nru0t7g7aj t55 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t51 * 168));
                        F.l10 = t55;
                        {
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 16));
                            gm_b16_8 t59 = gm_l5;
                            gm_b16_8 t60 = t59;
                            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                            gm_b16_8 t62 = t58;
                            gm_b16_8 t63 = t61;
                            uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                            if (!t64) goto ie57;
                            {
                                gm_ts1nru0t7g7aj t66 = F.l10;
                                gm_tie69pa8pplgc t67;
                                GM_ST(uint8_t, (uint8_t *)&t67 + 169, 0);
                                GM_ST(uint16_t, (uint8_t *)&t67 + 170, 0);
                                GM_ST(uint32_t, (uint8_t *)&t67 + 172, 0);
                                GM_ST(gm_ts1nru0t7g7aj, ((uint8_t *)&t67), t66);
                                ((uint8_t *)&t67)[168] = 1;
                                F.l6 = t67;
                            }
                            goto ix57;
ie57:;
ix57:;
                        }
                        goto me53;
mn54:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 628, 9);
me53:;
                    }
lc44:;
                    {
                        uint64_t t69 = gm_l9;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        uint64_t t72 = r70;
                        gm_l9 = t72;
                    }
                }
lb44:;
            }
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        gm_th2lqbo77pvsc t73;
        memset(&t73, 0, sizeof t73);
        gm_l11 = t73;
    }
    {
        if (!((((uint8_t *)&F.l6))[168] != 0)) goto ie74;
        gm_ts1nru0t7g7aj t75 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&F.l6));
        F.l12 = t75;
        {
            if (!(((((uint8_t *)&F.l12) + 112))[16] != 0)) goto ie77;
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 112));
            gm_l13 = t78;
            {
                gm_b16_8 t80 = gm_l13;
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_son0j0k0t10ro));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(7));
                gm_th2lqbo77pvsc t82;
                gm_f_strip_prefix_b4949s8((void *)&t82, t80, t81);
                gm_th2lqbo77pvsc t83 = t82;
                gm_b16_8 t84;
                if (((((uint8_t *)&t83))[16] != 0)) {
                    gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 0));
                    t84 = t85;
                } else {
                    gm_b16_8 t86 = gm_l13;
                    t84 = t86;
                }
                gm_th2lqbo77pvsc t87;
                GM_ST(uint8_t, (uint8_t *)&t87 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t87 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t87 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t87), t84);
                ((uint8_t *)&t87)[16] = 1;
                gm_l11 = t87;
            }
            goto ix77;
ie77:;
ix77:;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
        gm_th2lqbo77pvsc t88;
        memset(&t88, 0, sizeof t88);
        gm_l14 = t88;
    }
    {
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(0));
        gm_l15 = t89;
    }
    {
        if (!(((((uint8_t *)a2) + 16))[16] != 0)) goto ie90;
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
        gm_l16 = t91;
        {
            gm_b16_8 t93 = gm_l16;
            gm_th2lqbo77pvsc t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t94), t93);
            ((uint8_t *)&t94)[16] = 1;
            gm_l14 = t94;
        }
        {
            gm_b16_8 t95;
            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_se24c7hpp401n));
            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(10));
            gm_l15 = t95;
        }
        goto ix90;
ie90:;
        if (!((((uint8_t *)&gm_l11))[16] != 0)) goto ie96;
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l17 = t97;
        {
            gm_b16_8 t99 = gm_l17;
            gm_th2lqbo77pvsc t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t100), t99);
            ((uint8_t *)&t100)[16] = 1;
            gm_l14 = t100;
        }
        {
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sgib96mf5meqv));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(10));
            gm_l15 = t101;
        }
        goto ix96;
ie96:;
ix96:;
ix90:;
    }
    {
        gm_th2lqbo77pvsc t102;
        memset(&t102, 0, sizeof t102);
        gm_l18 = t102;
    }
    {
        if (!((((uint8_t *)&F.l6))[168] != 0)) goto ie103;
        gm_ts1nru0t7g7aj t104 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&F.l6));
        F.l19 = t104;
        {
            if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie106;
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
            gm_l20 = t107;
            {
                gm_th2lqbo77pvsc t110 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l19) + 112));
                gm_b16_8 t111 = gm_l20;
                gm_tnpifv20irnph t112;
                GM_ST(gm_b16_8, ((uint8_t *)&t112 + 0), t111);
                gm_tabb710d9mq2e t113;
                gm_f_format_9di2rrs((void *)&t113, (void *)NULL, cx, NULL, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t114 = t113;
                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srcpsceljs9unl1, 650, 29);
                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                gm_b16_8 t116 = t115;
                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                gm_th2lqbo77pvsc t118;
                GM_ST(uint8_t, (uint8_t *)&t118 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t118 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t118 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t118), t117);
                ((uint8_t *)&t118)[16] = 1;
                gm_th2lqbo77pvsc t119 = t110;
                gm_th2lqbo77pvsc t120 = t118;
                uint8_t t121 = (gm_eq0ogca82g1f(((uint8_t *)&t119), ((uint8_t *)&t120)));
                if (!t121) goto ie109;
                {
                    gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 136));
                    gm_th2lqbo77pvsc t124;
                    GM_ST(uint8_t, (uint8_t *)&t124 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t124 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t124 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t124), t123);
                    ((uint8_t *)&t124)[16] = 1;
                    gm_l18 = t124;
                }
                goto ix109;
ie109:;
ix109:;
            }
            goto ix106;
ie106:;
ix106:;
        }
        goto ix103;
ie103:;
ix103:;
    }
    {
        void * t126 = gm_l0;
        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t126) + 16) + 1));
        if (!t127) goto ie125;
        uint8_t t128;
        if (((((uint8_t *)&gm_l18))[16] != 0)) goto mn130;
        t128 = ((uint8_t)1);
        goto me129;
mn130:;
        t128 = ((uint8_t)0);
        goto me129;
mn131:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 653, 26);
me129:;
        if (!t128) goto ie125;
        {
            void * t134 = gm_l0;
            uint8_t t135;
            if ((((((uint8_t *)t134) + 64))[40] != 0)) goto mn137;
            t135 = ((uint8_t)1);
            goto me136;
mn137:;
            t135 = ((uint8_t)0);
            goto me136;
mn138:;
            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 654, 12);
me136:;
            if (!t135) goto ie133;
            {
                gm_b16_8 t140 = GM_LD(gm_b16_8, gm_clp0sh2cgr2);
                gm_tq6bq450l0idg t141;
                memset(&t141, 0, sizeof t141);
                ((uint8_t *)&t141)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t141 + 8), t140);
                gm_tq6bq450l0idg t142 = t141;
                uint8_t t143 = ((((uint8_t *)&t142))[0] != 0);
                F.ret = t141;
                goto gm_fret;
            }
            goto ix133;
ie133:;
ix133:;
        }
        {
            gm_t6j6smj7qoctu t144;
            gm_b16_8 t145 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t144 + 0, t145);
            gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t144 + 16, t146);
            gm_tabb710d9mq2e t147;
            gm_f_format_cvqjb45((void *)&t147, (void *)NULL, cx, NULL, &t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t148 = t147;
            if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_srcpsceljs9unl1, 655, 17);
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
            gm_b16_8 t150 = t149;
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
            gm_tq6bq450l0idg t152;
            memset(&t152, 0, sizeof t152);
            ((uint8_t *)&t152)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t152 + 8), t151);
            gm_tq6bq450l0idg t153 = t152;
            uint8_t t154 = ((((uint8_t *)&t153))[0] != 0);
            F.ret = t152;
            goto gm_fret;
        }
        goto ix125;
ie125:;
ix125:;
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie155;
        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l21 = t156;
        {
            void * t158 = gm_l0;
            void * t159 = (void *)(((uint8_t *)t158) + 48);
            gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_b16_8 t161 = gm_l21;
            gm_b16_8 t162 = gm_f_archive_dir_srvvp3g((void *)NULL, cx, NULL, t159, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t162;
        }
        {
            gm_b16_8 t164 = gm_l22;
            uint8_t t165 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t165) goto ie163;
            {
                gm_b16_8 t167 = gm_l22;
                gm_b0_1 t168 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t169 = gm_l1;
                gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                gm_b16_8 t171 = gm_l21;
                gm_b16_8 t172 = gm_l22;
                gm_tq6bq450l0idg t173;
                gm_f_summarize_archive_cicsplu((void *)&t173, (void *)NULL, cx, NULL, t169, t170, t171, t172, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l18)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq6bq450l0idg t174 = t173;
                if (((((uint8_t *)&t174))[0] != 0)) {
                    gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&t174) + 8));
                    gm_tq6bq450l0idg t176;
                    memset(&t176, 0, sizeof t176);
                    ((uint8_t *)&t176)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t176 + 8), t175);
                    F.ret = t176;
                    goto gm_fret;
                }
                gm_t0uuhaljsf410 t177 = GM_LD(gm_t0uuhaljsf410, (((uint8_t *)&t174) + 8));
                F.l23 = t177;
            }
            {
                void * t178 = gm_l0;
                void * t179 = (void *)(((uint8_t *)t178) + 232);
                gm_tftb1odc739t5 t180 = gm_f_push_mt615h0(t179, &(*(gm_t0uuhaljsf410 *)((uint8_t *)&F.l23)));
                gm_tftb1odc739t5 t181 = t180;
                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcpsceljs9unl1, 666, 13);
                gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
            }
            {
                gm_t0uuhaljsf410 t183 = F.l23;
                gm_tq6bq450l0idg t184;
                GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                ((uint8_t *)&t184)[0] = 0;
                GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t184 + 8), t183);
                gm_tq6bq450l0idg t185 = t184;
                uint8_t t186 = ((((uint8_t *)&t185))[0] != 0);
                F.ret = t184;
                goto gm_fret;
            }
            goto ix163;
ie163:;
ix163:;
        }
        goto ix155;
ie155:;
ix155:;
    }
    {
        void * t188 = gm_l0;
        uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t188) + 16) + 0));
        if (!t189) goto ie187;
        {
            gm_t6j6smj7qoctu t191;
            gm_b16_8 t192 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t191 + 0, t192);
            gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t191 + 16, t193);
            gm_tabb710d9mq2e t194;
            gm_f_format_g1540jg((void *)&t194, (void *)NULL, cx, NULL, &t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t195 = t194;
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 8, gm_srcpsceljs9unl1, 671, 17);
            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&t195) + 8));
            gm_b16_8 t197 = t196;
            gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
            gm_tq6bq450l0idg t199;
            memset(&t199, 0, sizeof t199);
            ((uint8_t *)&t199)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t199 + 8), t198);
            gm_tq6bq450l0idg t200 = t199;
            uint8_t t201 = ((((uint8_t *)&t200))[0] != 0);
            F.ret = t199;
            goto gm_fret;
        }
        goto ix187;
ie187:;
ix187:;
    }
    {
        gm_b16_8 t202 = gm_l1;
        gm_b16_8 t203 = t202;
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t205 = t204;
        if (gm_pf454qfrpt6r(cx, gm_s5ujsjbtr77bt, 13, gm_srcpsceljs9unl1, 673, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t203), gm_srcpsceljs9unl1, 673, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sp02qqgucmpt7, 2, gm_srcpsceljs9unl1, 673, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t205), gm_srcpsceljs9unl1, 673, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_ssv5povjldd7c, 2, gm_srcpsceljs9unl1, 673, 5)) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        void * t207 = gm_l0;
        void * t208 = (void *)(((uint8_t *)t207) + 48);
        gm_b16_8 t209 = gm_f_downloads_vdenqu5((void *)NULL, cx, NULL, t208);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t22b1ebhvur5a t210;
        gm_f_download_eek737p((void *)&t210, (void *)NULL, cx, NULL, t206, t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t22b1ebhvur5a t211 = t210;
        if (((((uint8_t *)&t211))[0] != 0)) {
            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&t211) + 8));
            gm_tq6bq450l0idg t213;
            memset(&t213, 0, sizeof t213);
            ((uint8_t *)&t213)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 8), t212);
            F.ret = t213;
            goto gm_fret;
        }
        gm_t6j6smj7qoctu t214 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t211) + 8));
        gm_l24 = t214;
    }
    {
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie216;
        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l25 = t217;
        {
            gm_b16_8 t220 = gm_l25;
            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
            gm_b16_8 t222 = t220;
            gm_b16_8 t223 = t221;
            uint8_t t224 = (!gm_eq0iqiubfm7e(((uint8_t *)&t222), ((uint8_t *)&t223)));
            if (!t224) goto ie219;
            {
                gm_b16_8 t226 = gm_l1;
                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
                gm_b16_8 t229 = gm_l25;
                gm_b16_8 t230 = gm_l15;
                gm_b16_8 t231 = gm_f_sha_mismatch_lq7bp8e((void *)NULL, cx, NULL, t226, t227, t228, t229, t230);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_tq6bq450l0idg t232;
                memset(&t232, 0, sizeof t232);
                ((uint8_t *)&t232)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t232 + 8), t231);
                gm_tq6bq450l0idg t233 = t232;
                uint8_t t234 = ((((uint8_t *)&t233))[0] != 0);
                F.ret = t232;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t234;
                goto gm_ld1_1;
            }
            goto ix219;
ie219:;
ix219:;
        }
        goto ix216;
ie216:;
ix216:;
    }
    {
        void * t235 = gm_l0;
        void * t236 = (void *)(((uint8_t *)t235) + 48);
        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
        gm_b16_8 t239 = gm_f_archive_dir_srvvp3g((void *)NULL, cx, NULL, t236, t237, t238);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l26 = t239;
    }
    {
        gm_b16_8 t241 = gm_l26;
        uint8_t t242 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t241);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t243 = (uint8_t)!t242;
        if (!t243) goto ie240;
        {
            gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
            gm_b16_8 t246 = gm_l26;
            gm_tl572d7579hns t247;
            gm_f_unpack_eb2aboi((void *)&t247, (void *)NULL, cx, NULL, t245, t246);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_tl572d7579hns t248 = t247;
            gm_b0_1 t249;
            if (!((((uint8_t *)&t248))[0] != 0)) {
                gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t248) + 8));
                t249 = t250;
            } else {
                gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&t248) + 8));
                gm_l27 = t251;
                gm_tfeab279c16rr t253;
                gm_b16_8 t254 = gm_l1;
                GM_ST(gm_b16_8, (uint8_t *)&t253 + 0, t254);
                gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t253 + 16, t255);
                gm_b16_8 t256 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t253 + 32, t256);
                gm_tabb710d9mq2e t257;
                gm_f_format_mv68p71((void *)&t257, (void *)NULL, cx, NULL, &t253);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_tabb710d9mq2e t258 = t257;
                if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 8, gm_srcpsceljs9unl1, 681, 51);
                gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&t258) + 8));
                gm_b16_8 t260 = t259;
                gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
                gm_tq6bq450l0idg t262;
                memset(&t262, 0, sizeof t262);
                ((uint8_t *)&t262)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t262 + 8), t261);
                gm_tq6bq450l0idg t263 = t262;
                uint8_t t264 = ((((uint8_t *)&t263))[0] != 0);
                F.ret = t262;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t264;
                goto gm_ld1_1;
            }
        }
        {
            gm_b16_8 t265 = gm_l26;
            gm_t3ujhu6qph7de t266;
            gm_f_tree_checksum_k6o2imm((void *)&t266, (void *)NULL, cx, NULL, t265);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t3ujhu6qph7de t267 = t266;
            gm_b16_8 t268;
            if (!((((uint8_t *)&t267))[0] != 0)) {
                gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
                t268 = t269;
            } else {
                gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
                gm_l28 = t270;
                {
                    {
                        gm_b16_8 t273 = gm_l26;
                        gm_ttln0d0hd70en t274 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t273);
                        if (gm_unw.kind) {
                            goto gm_ld1_1;
                        }
                    }
                }
                gm_tfeab279c16rr t275;
                gm_b16_8 t276 = gm_l1;
                GM_ST(gm_b16_8, (uint8_t *)&t275 + 0, t276);
                gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t275 + 16, t277);
                gm_b16_8 t278 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t275 + 32, t278);
                gm_tabb710d9mq2e t279;
                gm_f_format_mv68p71((void *)&t279, (void *)NULL, cx, NULL, &t275);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_tabb710d9mq2e t280 = t279;
                if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 8, gm_srcpsceljs9unl1, 684, 21);
                gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&t280) + 8));
                gm_b16_8 t282 = t281;
                gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                gm_tq6bq450l0idg t284;
                memset(&t284, 0, sizeof t284);
                ((uint8_t *)&t284)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t284 + 8), t283);
                gm_tq6bq450l0idg t285 = t284;
                uint8_t t286 = ((((uint8_t *)&t285))[0] != 0);
                F.ret = t284;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t286;
                goto gm_ld1_1;
            }
            gm_l29 = t268;
        }
        {
            if (!((((uint8_t *)&gm_l18))[16] != 0)) goto ie287;
            gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
            gm_l30 = t288;
            {
                gm_b16_8 t291 = gm_l30;
                gm_b16_8 t292 = gm_l29;
                gm_b16_8 t293 = t291;
                gm_b16_8 t294 = t292;
                uint8_t t295 = (!gm_eq0iqiubfm7e(((uint8_t *)&t293), ((uint8_t *)&t294)));
                if (!t295) goto ie290;
                {
                    {
                        gm_b16_8 t298 = gm_l26;
                        gm_ttln0d0hd70en t299 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t298);
                        if (gm_unw.kind) {
                            goto gm_ld1_1;
                        }
                    }
                }
                {
                    gm_tfeab279c16rr t300;
                    gm_b16_8 t301 = gm_l1;
                    GM_ST(gm_b16_8, (uint8_t *)&t300 + 0, t301);
                    gm_b16_8 t302 = gm_l30;
                    GM_ST(gm_b16_8, (uint8_t *)&t300 + 16, t302);
                    gm_b16_8 t303 = gm_l29;
                    GM_ST(gm_b16_8, (uint8_t *)&t300 + 32, t303);
                    gm_tabb710d9mq2e t304;
                    gm_f_format_vakr6l2((void *)&t304, (void *)NULL, cx, NULL, &t300);
                    if (gm_unw.kind) {
                        goto gm_ld1_1;
                    }
                    gm_tabb710d9mq2e t305 = t304;
                    if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 8, gm_srcpsceljs9unl1, 689, 25);
                    gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&t305) + 8));
                    gm_b16_8 t307 = t306;
                    gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&t307));
                    gm_tq6bq450l0idg t309;
                    memset(&t309, 0, sizeof t309);
                    ((uint8_t *)&t309)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t309 + 8), t308);
                    gm_tq6bq450l0idg t310 = t309;
                    uint8_t t311 = ((((uint8_t *)&t310))[0] != 0);
                    F.ret = t309;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t311;
                    goto gm_ld1_1;
                }
                goto ix290;
ie290:;
ix290:;
            }
            goto ix287;
ie287:;
ix287:;
        }
        {
            gm_b16_8 t312 = gm_l26;
            gm_b0_1 t313 = gm_f_make_read_only_a29eok8((void *)NULL, cx, NULL, t312);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_b16_8 t314 = gm_l26;
            gm_b0_1 t315 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t314);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_b16_8 t316 = gm_l26;
            gm_th2lqbo77pvsc t317;
            gm_f_canonicalize_epo7cln((void *)&t317, (void *)NULL, cx, NULL, t316);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_th2lqbo77pvsc t318 = t317;
            gm_b16_8 t319;
            if (((((uint8_t *)&t318))[16] != 0)) {
                gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 0));
                t319 = t320;
            } else {
                gm_b16_8 t321 = gm_l26;
                gm_tnpifv20irnph t322;
                GM_ST(gm_b16_8, ((uint8_t *)&t322 + 0), t321);
                gm_tabb710d9mq2e t323;
                gm_f_format_bpl33sh((void *)&t323, (void *)NULL, cx, NULL, t322);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_tabb710d9mq2e t324 = t323;
                if (((((uint8_t *)&t324))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t324) + 8, gm_srcpsceljs9unl1, 694, 48);
                gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&t324) + 8));
                gm_b16_8 t326 = t325;
                gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                gm_tq6bq450l0idg t328;
                memset(&t328, 0, sizeof t328);
                ((uint8_t *)&t328)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t328 + 8), t327);
                gm_tq6bq450l0idg t329 = t328;
                uint8_t t330 = ((((uint8_t *)&t329))[0] != 0);
                F.ret = t328;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t330;
                goto gm_ld1_1;
            }
            gm_l31 = t319;
        }
        {
            gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 0, t331);
            gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 16, t332);
            gm_b16_8 t333 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 32, t333);
            gm_b16_8 t334 = gm_l29;
            GM_ST(gm_b16_8, (uint8_t *)&F.l32 + 48, t334);
        }
        {
            void * t335 = gm_l0;
            void * t336 = (void *)(((uint8_t *)t335) + 232);
            gm_tftb1odc739t5 t337 = gm_f_push_mt615h0(t336, &(*(gm_t0uuhaljsf410 *)((uint8_t *)&F.l32)));
            gm_tftb1odc739t5 t338 = t337;
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srcpsceljs9unl1, 696, 9);
            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
        }
        {
            gm_t0uuhaljsf410 t340 = F.l32;
            gm_tq6bq450l0idg t341;
            GM_ST(uint8_t, (uint8_t *)&t341 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t341 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t341 + 4, 0);
            ((uint8_t *)&t341)[0] = 0;
            GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t341 + 8), t340);
            gm_tq6bq450l0idg t342 = t341;
            uint8_t t343 = ((((uint8_t *)&t342))[0] != 0);
            F.ret = t341;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t343;
            goto gm_ld1_1;
        }
        goto ix240;
ie240:;
ix240:;
    }
    {
        gm_b16_8 t344 = gm_l26;
        gm_b0_1 t345 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t344);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_b16_8 t346 = gm_l1;
        gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 16));
        gm_b16_8 t349 = gm_l26;
        gm_tq6bq450l0idg t350;
        gm_f_summarize_archive_cicsplu((void *)&t350, (void *)NULL, cx, NULL, t346, t347, t348, t349, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l18)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tq6bq450l0idg t351 = t350;
        if (((((uint8_t *)&t351))[0] != 0)) {
            gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&t351) + 8));
            gm_tq6bq450l0idg t353;
            memset(&t353, 0, sizeof t353);
            ((uint8_t *)&t353)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t353 + 8), t352);
            F.ret = t353;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_t0uuhaljsf410 t354 = GM_LD(gm_t0uuhaljsf410, (((uint8_t *)&t351) + 8));
        F.l33 = t354;
    }
    {
        void * t355 = gm_l0;
        void * t356 = (void *)(((uint8_t *)t355) + 232);
        gm_tftb1odc739t5 t357 = gm_f_push_mt615h0(t356, &(*(gm_t0uuhaljsf410 *)((uint8_t *)&F.l33)));
        gm_tftb1odc739t5 t358 = t357;
        if (((((uint8_t *)&t358))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t358) + 1, gm_srcpsceljs9unl1, 701, 5);
        gm_b0_1 t359 = GM_LD(gm_b0_1, (((uint8_t *)&t358) + 1));
    }
    {
        gm_t0uuhaljsf410 t360 = F.l33;
        gm_tq6bq450l0idg t361;
        GM_ST(uint8_t, (uint8_t *)&t361 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t361 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t361 + 4, 0);
        ((uint8_t *)&t361)[0] = 0;
        GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t361 + 8), t360);
        gm_tq6bq450l0idg t362 = t361;
        uint8_t t363 = ((((uint8_t *)&t362))[0] != 0);
        F.ret = t361;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t363;
        goto gm_ld1_1;
    }
    {
        gm_b0_1 t364 = gm_f_discard_bdpju80((void *)NULL, cx, NULL, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l24)));
    }
    goto gm_ls365;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b0_1 t366 = gm_f_discard_bdpju80((void *)NULL, cx, NULL, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l24)));
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls365:;
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
__attribute__((aligned(4))) void gm_f_ensure_checkout_hvmsso1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_ensure_checkout_hvmsso1 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* dest */
    gm_b16_8 gm_l6; /* dep */
    gm_b16_8 gm_l7; /* url */
    gm_b16_8 gm_l8; /* db_dir */
    uint8_t gm_l9; /* has */
    uint8_t gm_l10; /* has2 */
    gm_b16_8 gm_l11; /* e */
    gm_b16_8 gm_l12; /* sum */
    gm_b16_8 gm_l15; /* sum */
    gm_b16_8 gm_l16; /* canon */
    GM_STACK_CHECK(&F, 4708u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 392);
        gm_th2lqbo77pvsc t5;
        gm_f_get_vc34oj2((void *)&t5, t4, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            gm_t3ujhu6qph7de t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
            gm_t3ujhu6qph7de t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 48);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t17 = gm_f_checkout_dir_mvijcuu((void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t17;
    }
    {
        uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 112);
        gm_tqetrgo0q24b5 t21;
        gm_f_get_9fn35sd((void *)&t21, t20, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tqetrgo0q24b5 t22 = t21;
        if (!((((uint8_t *)&t22))[40] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 523, 14);
        gm_trqfvmjtt4sij t23 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t22));
        gm_trqfvmjtt4sij t24 = t23;
        uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)&t24) + 0) + 8);
        if (t18 >= (uintptr_t)(t25)) gm_trap_index(t18, t25, gm_srcpsceljs9unl1, 523, 14);
        gm_t2f6hp2vik01f t26 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, (((uint8_t *)&t24) + 0)) + t18 * 96));
        F.l5 = t26;
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
        gm_l6 = t27;
    }
    {
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
        gm_l7 = t28;
    }
    {
        gm_b16_8 t30 = gm_l4;
        uint8_t t31 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = (uint8_t)!t31;
        if (!t32) goto ie29;
        {
            void * t34 = gm_l0;
            void * t35 = (void *)(((uint8_t *)t34) + 48);
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
            gm_b16_8 t37 = gm_f_db_tssgvk0((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t37;
        }
        {
            uint8_t t38 = 0;
            gm_b16_8 t39 = gm_l8;
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sbpcdsg6ifd91));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(4));
            gm_b16_8 t41 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t42) {
                gm_b16_8 t43 = gm_l8;
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                gm_th2lqbo77pvsc t45;
                gm_f_rev_parse_3l092b4((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t46 = t45;
                gm_b16_8 t47;
                if (((((uint8_t *)&t46))[16] != 0)) {
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 0));
                    t47 = t48;
                } else {
                    gm_b16_8 t49;
                    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(0));
                    t47 = t49;
                }
                gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                gm_b16_8 t51 = t47;
                gm_b16_8 t52 = t50;
                uint8_t t53 = (gm_eq0iqiubfm7e(((uint8_t *)&t51), ((uint8_t *)&t52)));
                t38 = t53;
            } else t38 = 0;
            gm_l9 = t38;
        }
        {
            uint8_t t55 = gm_l9;
            uint8_t t56 = (uint8_t)!t55;
            if (!t56) goto ie54;
            {
                void * t59 = gm_l0;
                uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t59) + 16) + 0));
                if (!t60) goto ie58;
                {
                    gm_tfeab279c16rr t62;
                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    gm_b16_8 t64 = gm_f_short_rev_uin3i2s(t63);
                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t64);
                    gm_b16_8 t65 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t65);
                    gm_b16_8 t66 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 32, t66);
                    gm_tabb710d9mq2e t67;
                    gm_f_format_nsoplg3((void *)&t67, (void *)NULL, cx, NULL, &t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t68 = t67;
                    if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcpsceljs9unl1, 531, 25);
                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                    gm_b16_8 t70 = t69;
                    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                    gm_t3ujhu6qph7de t72;
                    GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                    ((uint8_t *)&t72)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t71);
                    gm_t3ujhu6qph7de t73 = t72;
                    uint8_t t74 = ((((uint8_t *)&t73))[0] != 0);
                    F.ret = t72;
                    goto gm_fret;
                }
                goto ix58;
ie58:;
ix58:;
            }
            {
                void * t75 = gm_l0;
                gm_tl572d7579hns t76;
                gm_f_mirror_8u3u6oe((void *)&t76, (void *)NULL, cx, NULL, t75, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl572d7579hns t77 = t76;
                if (((((uint8_t *)&t77))[0] != 0)) {
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                    gm_t3ujhu6qph7de t79;
                    GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                    ((uint8_t *)&t79)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t79 + 8), t78);
                    F.ret = t79;
                    goto gm_fret;
                }
                gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 8));
            }
            {
                uint8_t t81 = 0;
                gm_b16_8 t82 = gm_l8;
                gm_b16_8 t83;
                GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sbpcdsg6ifd91));
                GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(4));
                gm_b16_8 t84 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t85 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t85) {
                    gm_b16_8 t86 = gm_l8;
                    gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    gm_th2lqbo77pvsc t88;
                    gm_f_rev_parse_3l092b4((void *)&t88, (void *)NULL, cx, NULL, t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t89 = t88;
                    gm_b16_8 t90;
                    if (((((uint8_t *)&t89))[16] != 0)) {
                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t89) + 0));
                        t90 = t91;
                    } else {
                        gm_b16_8 t92;
                        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(0));
                        t90 = t92;
                    }
                    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    gm_b16_8 t94 = t90;
                    gm_b16_8 t95 = t93;
                    uint8_t t96 = (gm_eq0iqiubfm7e(((uint8_t *)&t94), ((uint8_t *)&t95)));
                    t81 = t96;
                } else t81 = 0;
                gm_l10 = t81;
            }
            {
                uint8_t t98 = gm_l10;
                uint8_t t99 = (uint8_t)!t98;
                if (!t99) goto ie97;
                {
                    gm_t0uuhaljsf410 t101;
                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
                    gm_b16_8 t103 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t101 + 16, t103);
                    gm_b16_8 t104 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t101 + 32, t104);
                    gm_b16_8 t105 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t101 + 48, t105);
                    gm_tabb710d9mq2e t106;
                    gm_f_format_2orhd15((void *)&t106, (void *)NULL, cx, NULL, &t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t107 = t106;
                    if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srcpsceljs9unl1, 541, 25);
                    gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
                    gm_b16_8 t109 = t108;
                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                    gm_t3ujhu6qph7de t111;
                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                    ((uint8_t *)&t111)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
                    gm_t3ujhu6qph7de t112 = t111;
                    uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
                    F.ret = t111;
                    goto gm_fret;
                }
                goto ix97;
ie97:;
ix97:;
            }
            goto ix54;
ie54:;
ix54:;
        }
        {
            gm_b16_8 t114 = gm_l8;
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_b16_8 t116 = gm_l4;
            gm_tl572d7579hns t117;
            gm_f_checkout_j42th18((void *)&t117, (void *)NULL, cx, NULL, t114, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl572d7579hns t118 = t117;
            if (((((uint8_t *)&t118))[0] != 0)) {
                gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                gm_t3ujhu6qph7de t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                ((uint8_t *)&t120)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t120 + 8), t119);
                F.ret = t120;
                goto gm_fret;
            }
            gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 8));
        }
        {
            gm_b16_8 t122 = gm_l4;
            gm_t3ujhu6qph7de t123;
            gm_f_tree_checksum_k6o2imm((void *)&t123, (void *)NULL, cx, NULL, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3ujhu6qph7de t124 = t123;
            gm_b16_8 t125;
            if (!((((uint8_t *)&t124))[0] != 0)) {
                gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                t125 = t126;
            } else {
                gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                gm_l11 = t127;
                {
                    {
                        gm_b16_8 t130 = gm_l4;
                        gm_ttln0d0hd70en t131 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t130);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                gm_b16_8 t132 = gm_l11;
                gm_t3ujhu6qph7de t133;
                GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                ((uint8_t *)&t133)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t133 + 8), t132);
                gm_t3ujhu6qph7de t134 = t133;
                uint8_t t135 = ((((uint8_t *)&t134))[0] != 0);
                F.ret = t133;
                goto gm_fret;
            }
            gm_l12 = t125;
        }
        {
            void * t137 = gm_l0;
            gm_tie69pa8pplgc t138;
            gm_f_locked_entry_4693mpc((void *)&t138, t137, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
            gm_tie69pa8pplgc t139 = t138;
            if (!((((uint8_t *)&t139))[168] != 0)) goto ie136;
            gm_ts1nru0t7g7aj t140 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&t139));
            F.l13 = t140;
            {
                gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 96));
                gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                gm_b16_8 t145 = t143;
                gm_b16_8 t146 = t144;
                uint8_t t147 = (gm_eq0iqiubfm7e(((uint8_t *)&t145), ((uint8_t *)&t146)));
                if (!t147) goto ie142;
                gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 136));
                gm_b16_8 t149 = gm_l12;
                gm_b16_8 t150 = t148;
                gm_b16_8 t151 = t149;
                uint8_t t152 = (!gm_eq0iqiubfm7e(((uint8_t *)&t150), ((uint8_t *)&t151)));
                if (!t152) goto ie142;
                {
                    {
                        gm_b16_8 t155 = gm_l4;
                        gm_ttln0d0hd70en t156 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t155);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                {
                    gm_tfeab279c16rr t157;
                    gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t158);
                    gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 136));
                    GM_ST(gm_b16_8, (uint8_t *)&t157 + 16, t159);
                    gm_b16_8 t160 = gm_l12;
                    GM_ST(gm_b16_8, (uint8_t *)&t157 + 32, t160);
                    gm_tabb710d9mq2e t161;
                    gm_f_format_vakr6l2((void *)&t161, (void *)NULL, cx, NULL, &t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t162 = t161;
                    if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 8, gm_srcpsceljs9unl1, 558, 25);
                    gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
                    gm_b16_8 t164 = t163;
                    gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                    gm_t3ujhu6qph7de t166;
                    GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                    ((uint8_t *)&t166)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 8), t165);
                    gm_t3ujhu6qph7de t167 = t166;
                    uint8_t t168 = ((((uint8_t *)&t167))[0] != 0);
                    F.ret = t166;
                    goto gm_fret;
                }
                goto ix142;
ie142:;
ix142:;
            }
            goto ix136;
ie136:;
ix136:;
        }
        {
            void * t169 = gm_l0;
            void * t170 = (void *)(((uint8_t *)t169) + 432);
            gm_b16_8 t171 = gm_l12;
            gm_b0_1 t172 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t170, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t173 = gm_l4;
            gm_b0_1 t174 = gm_f_make_read_only_a29eok8((void *)NULL, cx, NULL, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix29;
ie29:;
        void * t176 = gm_l0;
        void * t177 = (void *)(((uint8_t *)t176) + 432);
        gm_th2lqbo77pvsc t178;
        gm_f_get_vc34oj2((void *)&t178, t177, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_th2lqbo77pvsc t179 = t178;
        uint8_t t180;
        if (((((uint8_t *)&t179))[16] != 0)) goto mn182;
        t180 = ((uint8_t)1);
        goto me181;
mn182:;
        t180 = ((uint8_t)0);
        goto me181;
mn183:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 563, 15);
me181:;
        if (!t180) goto ie175;
        {
            void * t186 = gm_l0;
            gm_tie69pa8pplgc t187;
            gm_f_locked_entry_4693mpc((void *)&t187, t186, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
            gm_tie69pa8pplgc t188 = t187;
            if (!((((uint8_t *)&t188))[168] != 0)) goto ie185;
            gm_ts1nru0t7g7aj t189 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&t188));
            F.l14 = t189;
            {
                gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 96));
                gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                gm_b16_8 t194 = t192;
                gm_b16_8 t195 = t193;
                uint8_t t196 = (gm_eq0iqiubfm7e(((uint8_t *)&t194), ((uint8_t *)&t195)));
                if (!t196) goto ie191;
                {
                    gm_b16_8 t198 = gm_l4;
                    gm_t3ujhu6qph7de t199;
                    gm_f_tree_checksum_k6o2imm((void *)&t199, (void *)NULL, cx, NULL, t198);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t3ujhu6qph7de t200 = t199;
                    if (((((uint8_t *)&t200))[0] != 0)) {
                        gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&t200) + 8));
                        gm_t3ujhu6qph7de t202;
                        GM_ST(uint8_t, (uint8_t *)&t202 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t202 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
                        ((uint8_t *)&t202)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 8), t201);
                        F.ret = t202;
                        goto gm_fret;
                    }
                    gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&t200) + 8));
                    gm_l15 = t203;
                }
                {
                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 136));
                    gm_b16_8 t206 = gm_l15;
                    gm_b16_8 t207 = t205;
                    gm_b16_8 t208 = t206;
                    uint8_t t209 = (!gm_eq0iqiubfm7e(((uint8_t *)&t207), ((uint8_t *)&t208)));
                    if (!t209) goto ie204;
                    {
                        gm_t0uuhaljsf410 t211;
                        gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t211 + 0, t212);
                        gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 136));
                        GM_ST(gm_b16_8, (uint8_t *)&t211 + 16, t213);
                        gm_b16_8 t214 = gm_l15;
                        GM_ST(gm_b16_8, (uint8_t *)&t211 + 32, t214);
                        gm_b16_8 t215 = gm_l4;
                        GM_ST(gm_b16_8, (uint8_t *)&t211 + 48, t215);
                        gm_tabb710d9mq2e t216;
                        gm_f_format_jth3mgf((void *)&t216, (void *)NULL, cx, NULL, &t211);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t217 = t216;
                        if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 8, gm_srcpsceljs9unl1, 569, 29);
                        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&t217) + 8));
                        gm_b16_8 t219 = t218;
                        gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                        gm_t3ujhu6qph7de t221;
                        GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                        ((uint8_t *)&t221)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t221 + 8), t220);
                        gm_t3ujhu6qph7de t222 = t221;
                        uint8_t t223 = ((((uint8_t *)&t222))[0] != 0);
                        F.ret = t221;
                        goto gm_fret;
                    }
                    goto ix204;
ie204:;
ix204:;
                }
                {
                    void * t224 = gm_l0;
                    void * t225 = (void *)(((uint8_t *)t224) + 432);
                    gm_b16_8 t226 = gm_l15;
                    gm_b0_1 t227 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t225, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), t226);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix191;
ie191:;
ix191:;
            }
            goto ix185;
ie185:;
ix185:;
        }
        goto ix175;
ie175:;
ix175:;
ix29:;
    }
    {
        gm_b16_8 t228 = gm_l4;
        gm_b0_1 t229 = gm_f_mark_used_5dtkpil((void *)NULL, cx, NULL, t228);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t230 = gm_l4;
        gm_th2lqbo77pvsc t231;
        gm_f_canonicalize_epo7cln((void *)&t231, (void *)NULL, cx, NULL, t230);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t232 = t231;
        gm_b16_8 t233;
        if (((((uint8_t *)&t232))[16] != 0)) {
            gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&t232) + 0));
            t233 = t234;
        } else {
            gm_b16_8 t235 = gm_l4;
            gm_tnpifv20irnph t236;
            GM_ST(gm_b16_8, ((uint8_t *)&t236 + 0), t235);
            gm_tabb710d9mq2e t237;
            gm_f_format_bpl33sh((void *)&t237, (void *)NULL, cx, NULL, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t238 = t237;
            if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 8, gm_srcpsceljs9unl1, 582, 46);
            gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t238) + 8));
            gm_b16_8 t240 = t239;
            gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
            gm_t3ujhu6qph7de t242;
            GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
            ((uint8_t *)&t242)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t242 + 8), t241);
            gm_t3ujhu6qph7de t243 = t242;
            uint8_t t244 = ((((uint8_t *)&t243))[0] != 0);
            F.ret = t242;
            goto gm_fret;
        }
        gm_l16 = t233;
    }
    {
        void * t245 = gm_l0;
        void * t246 = (void *)(((uint8_t *)t245) + 392);
        gm_b16_8 t247 = gm_l16;
        gm_b0_1 t248 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t246, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)), t247);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t249 = gm_l16;
        gm_t3ujhu6qph7de t250;
        GM_ST(uint8_t, (uint8_t *)&t250 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t250 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
        ((uint8_t *)&t250)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t250 + 8), t249);
        gm_t3ujhu6qph7de t251 = t250;
        uint8_t t252 = ((((uint8_t *)&t251))[0] != 0);
        F.ret = t250;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_entry_name_qnlemsm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_entry_name_qnlemsm F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 759u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 472);
        gm_th2lqbo77pvsc t5;
        gm_f_get_vc34oj2((void *)&t5, t4, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_b16_8 t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 112);
        gm_tqetrgo0q24b5 t14;
        gm_f_get_9fn35sd((void *)&t14, t13, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tqetrgo0q24b5 t15 = t14;
        if (!((((uint8_t *)&t15))[40] != 0)) goto ie11;
        gm_trqfvmjtt4sij t16 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t15));
        F.l4 = t16;
        {
            {
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                gm_l5 = t19;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t21 = gm_l6;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t23 = (t21 < t22);
                    if (!t23) goto lb20;
                    {
                        uint64_t t26 = gm_l6;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t2f6hp2vik01f t31 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t27 * 96));
                        F.l7 = t31;
                        {
                            void * t33 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                            gm_tftb1odc739t5 t35 = gm_f_push_3ef746b(t33, t34);
                            gm_tftb1odc739t5 t36 = t35;
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcpsceljs9unl1, 398, 31);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        goto me29;
mn30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 398, 9);
me29:;
                    }
lc20:;
                    {
                        uint64_t t39 = gm_l6;
                        uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                        uint64_t t42 = r40;
                        gm_l6 = t42;
                    }
                }
lb20:;
            }
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_f_sort_skeree7(t43);
    }
    {
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t45 = t44;
        void * t46 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t45)));
        uint64_t t47 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t45) + 8);
        uint64_t t48 = (uintptr_t)0;
        gm_th2lqbo77pvsc t49;
        if (t48 < t47) {
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)t46 + t48 * 16));
        gm_th2lqbo77pvsc t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
        ((uint8_t *)&t51)[16] = 1;
            t49 = t51;
        } else {
        gm_th2lqbo77pvsc t52;
        memset(&t52, 0, sizeof t52);
            t49 = t52;
        }
        gm_th2lqbo77pvsc t53 = t49;
        gm_b16_8 t54;
        if (((((uint8_t *)&t53))[16] != 0)) {
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 0));
            t54 = t55;
        } else {
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_snqvo5bgc2fch));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(7));
            t54 = t56;
        }
        uint8_t t57 = 0;
        F.ret = t54;
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
inline __attribute__((aligned(4))) void gm_f_find_ref_h89e731(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_ref_h89e731 F;
    gm_b16_8 gm_l0; /* refs */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
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
                    gm_t6j6smj7qoctu t15 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 32));
                    gm_l4 = t15;
                    {
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        gm_b16_8 t19 = gm_l1;
                        gm_b16_8 t20 = t18;
                        gm_b16_8 t21 = t19;
                        uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (!t22) goto ie17;
                        {
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            gm_th2lqbo77pvsc t25;
                            GM_ST(uint8_t, (uint8_t *)&t25 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t25 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t25 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t25), t24);
                            ((uint8_t *)&t25)[16] = 1;
                            uint8_t t26 = 0;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 136, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb4:;
        }
    }
    {
        gm_th2lqbo77pvsc t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_find_ref_h89e731_c(gm_b16_8 a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_find_ref_h89e731(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_default_p4tis8s_c(gm_toq2f2q4opoa3 a0) { return gm_f_is_default_p4tis8s(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_is_refreshed_ifi7dst(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_is_refreshed_ifi7dst F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* names */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* n */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 1191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t3;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 24) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; default: goto ma8; }
ma5:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 24) + 0));
        if (t11 != ((uint8_t)0ULL)) goto mn10;
        t3 = ((uint8_t)0);
        goto me4;
mn10:;
ma6:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 24) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        t3 = ((uint8_t)1);
        goto me4;
mn12:;
ma7:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 24) + 0));
        if (t15 != ((uint8_t)2ULL)) goto mn14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 24) + 8));
        gm_l2 = t16;
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 112);
            gm_tqetrgo0q24b5 t20;
            gm_f_get_9fn35sd((void *)&t20, t19, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
            gm_tqetrgo0q24b5 t21 = t20;
            if (!((((uint8_t *)&t21))[40] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 448, 21);
            gm_trqfvmjtt4sij t22 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t21));
            F.l3 = t22;
        }
        {
            {
                gm_b16_8 t24 = gm_l2;
                gm_l4 = t24;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l5;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l5;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 16));
                        gm_l6 = t36;
                        {
                            {
                                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                                gm_l7 = t39;
                            }
                            {
                                gm_l8 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t41 = gm_l8;
                                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    uint8_t t43 = (t41 < t42);
                                    if (!t43) goto lb40;
                                    {
                                        uint64_t t46 = gm_l8;
                                        uint64_t t47 = (uintptr_t)t46;
                                        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        gm_t2f6hp2vik01f t51 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t47 * 96));
                                        F.l9 = t51;
                                        {
                                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                                            gm_b16_8 t55 = gm_l6;
                                            gm_b16_8 t56 = t54;
                                            gm_b16_8 t57 = t55;
                                            uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                                            if (!t58) goto ie53;
                                            {
                                                uint8_t t60 = 0;
                                                F.ret = ((uint8_t)1);
                                                goto gm_fret;
                                            }
                                            goto ix53;
ie53:;
ix53:;
                                        }
                                        goto me49;
mn50:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 450, 17);
me49:;
                                    }
lc40:;
                                    {
                                        uint64_t t62 = gm_l8;
                                        uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                                        uint64_t t65 = r63;
                                        gm_l8 = t65;
                                    }
                                }
lb40:;
                            }
                        }
                        {
                            void * t67 = gm_l0;
                            gm_tie69pa8pplgc t68;
                            gm_f_locked_entry_4693mpc((void *)&t68, t67, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
                            gm_tie69pa8pplgc t69 = t68;
                            if (!((((uint8_t *)&t69))[168] != 0)) goto ie66;
                            gm_ts1nru0t7g7aj t70 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&t69));
                            F.l10 = t70;
                            {
                                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_b16_8 t74 = gm_l6;
                                gm_b16_8 t75 = t73;
                                gm_b16_8 t76 = t74;
                                uint8_t t77 = (gm_eq0iqiubfm7e(((uint8_t *)&t75), ((uint8_t *)&t76)));
                                if (!t77) goto ie72;
                                {
                                    uint8_t t79 = 0;
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto ix72;
ie72:;
ix72:;
                            }
                            goto ix66;
ie66:;
ix66:;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 449, 13);
me34:;
                    }
lc25:;
                    {
                        uint64_t t81 = gm_l5;
                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                        uint64_t t84 = r82;
                        gm_l5 = t84;
                    }
                }
lb25:;
            }
        }
        uint8_t t85 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn14:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 444, 12);
me4:;
        uint8_t t86 = 0;
        F.ret = t3;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_key_equal_n71njnf_c(gm_t6ok5tvcjc200 a0, gm_t6ok5tvcjc200 a1) { return gm_f_key_equal_n71njnf(&a0, &a1); }
extern __attribute__((aligned(4))) uint8_t gm_f_key_less_9obq0rq(void * a0, void * a1);
extern __attribute__((aligned(4))) void gm_f_locked_entry_4693mpc(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tie69pa8pplgc gm_f_locked_entry_4693mpc_c(void * a0, gm_t6ok5tvcjc200 a1) { gm_tie69pa8pplgc gm_r; gm_f_locked_entry_4693mpc(&gm_r, a0, &a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_mirror_8u3u6oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_mirror_8u3u6oe F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* u */
    gm_b16_8 gm_l6; /* db_dir */
    GM_STACK_CHECK(&F, 1436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 152) + 0));
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
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            gm_b0_1 t25;
                            gm_tl572d7579hns t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t26 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t26 + 16, 0);
                            ((uint8_t *)&t26)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t26 + 8), t25);
                            gm_tl572d7579hns t27 = t26;
                            uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 430, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t30 = gm_l3;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l3 = t33;
                }
            }
lb5:;
        }
    }
    {
        uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
        void * t35 = gm_l0;
        void * t36 = (void *)(((uint8_t *)t35) + 112);
        gm_tqetrgo0q24b5 t37;
        gm_f_get_9fn35sd((void *)&t37, t36, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a1)));
        gm_tqetrgo0q24b5 t38 = t37;
        if (!((((uint8_t *)&t38))[40] != 0)) gm_trap("unwrap of null", gm_srcpsceljs9unl1, 433, 14);
        gm_trqfvmjtt4sij t39 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t38));
        gm_trqfvmjtt4sij t40 = t39;
        uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)&t40) + 0) + 8);
        if (t34 >= (uintptr_t)(t41)) gm_trap_index(t34, t41, gm_srcpsceljs9unl1, 433, 14);
        gm_t2f6hp2vik01f t42 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, (((uint8_t *)&t40) + 0)) + t34 * 96));
        F.l5 = t42;
    }
    {
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
        gm_b16_8 t44 = t43;
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
        gm_b16_8 t46 = t45;
        if (gm_pf454qfrpt6r(cx, gm_s5ujsjbtr77bt, 13, gm_srcpsceljs9unl1, 434, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t44), gm_srcpsceljs9unl1, 434, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sp02qqgucmpt7, 2, gm_srcpsceljs9unl1, 434, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t46), gm_srcpsceljs9unl1, 434, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_ssv5povjldd7c, 2, gm_srcpsceljs9unl1, 434, 5)) {
            goto gm_lad;
        }
    }
    {
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 48);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_b16_8 t50 = gm_f_db_tssgvk0((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t50;
    }
    {
        gm_b16_8 t51 = gm_l6;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
        gm_tl572d7579hns t53;
        gm_f_fetch_mirror_q06gvv0((void *)&t53, (void *)NULL, cx, NULL, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) {
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
            gm_tl572d7579hns t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
            ((uint8_t *)&t56)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t56 + 8), t55);
            F.ret = t56;
            goto gm_fret;
        }
        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 8));
    }
    {
        gm_b16_8 t58 = gm_l6;
        gm_tv61mo81rn7eb t59;
        gm_f_list_refs_j3is1o5((void *)&t59, (void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tv61mo81rn7eb t60 = t59;
        if (((((uint8_t *)&t60))[0] != 0)) {
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
            gm_tl572d7579hns t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            ((uint8_t *)&t62)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
            F.ret = t62;
            goto gm_fret;
        }
        gm_tmf08dt0mom0q t63 = GM_LD(gm_tmf08dt0mom0q, (((uint8_t *)&t60) + 8));
        F.l7 = t63;
    }
    {
        void * t64 = gm_l0;
        void * t65 = (void *)(((uint8_t *)t64) + 152);
        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_tftb1odc739t5 t67 = gm_f_push_3ef746b(t65, t66);
        gm_tftb1odc739t5 t68 = t67;
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcpsceljs9unl1, 438, 5);
        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
    }
    {
        void * t70 = gm_l0;
        void * t71 = (void *)(((uint8_t *)t70) + 192);
        gm_t3rj5o15e922s t72;
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 0, t73);
        memcpy((uint8_t *)&t72 + 16, ((uint8_t *)&F.l7), 104);
        gm_tftb1odc739t5 t74 = gm_f_push_r6o112l(t71, &t72);
        gm_tftb1odc739t5 t75 = t74;
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srcpsceljs9unl1, 439, 5);
        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
    }
    {
        gm_b0_1 t77;
        gm_tl572d7579hns t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t78 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t78 + 16, 0);
        ((uint8_t *)&t78)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t78 + 8), t77);
        gm_tl572d7579hns t79 = t78;
        uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
        F.ret = t78;
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
__attribute__((aligned(4))) void gm_f_pick_ronijl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_pick_ronijl9 F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* all */
    void * gm_l2; /* refs */
    gm_b16_8 gm_l3; /* db_dir */
    gm_b16_8 gm_l4; /* reqs */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l9; /* dup */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* conflict */
    gm_b16_8 gm_l20; /* t */
    gm_b16_8 gm_l21; /* rev */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l26; /* b */
    gm_b16_8 gm_l27; /* rev */
    gm_b16_8 gm_l28; /* r */
    gm_b16_8 gm_l29; /* rev */
    gm_b16_8 gm_l31; /* best_name */
    gm_b16_8 gm_l32; /* best_rev */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_t6j6smj7qoctu gm_l35; /* t */
    uint8_t gm_l37; /* ok */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l43; /* rev */
    GM_STACK_CHECK(&F, 7436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_effective_vikkdqh((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t3;
    }
    {
        gm_f_list_vsi9vun((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l4;
            gm_l6 = t5;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l7;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l7;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t2f6hp2vik01f t17 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t13 * 96));
                    F.l8 = t17;
                    {
                        uint8_t t20 = 0;
                        uint8_t t21;
                        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 48) + 0));
                        if (t24 != ((uint8_t)1ULL)) goto mn23;
                        t21 = ((uint8_t)1);
                        goto me22;
mn23:;
                        t21 = ((uint8_t)0);
                        goto me22;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 147, 12);
me22:;
                        if (!t21) {
                            uint8_t t26 = ((GM_LD(uint8_t, (((uint8_t *)&F.l8) + 48) + 0) == ((uint8_t)0ULL)));
                            t20 = t26;
                        } else t20 = 1;
                        if (!t20) goto ie19;
                        {
                            goto lc6;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    {
                        gm_l9 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_l10 = t29;
                        }
                        {
                            gm_l11 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t31 = gm_l11;
                                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint8_t t33 = (t31 < t32);
                                if (!t33) goto lb30;
                                {
                                    uint64_t t36 = gm_l11;
                                    uint64_t t37 = (uintptr_t)t36;
                                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    gm_t2f6hp2vik01f t41 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t37 * 96));
                                    F.l12 = t41;
                                    {
                                        uint8_t t44 = gm_f_spec_equal_foa1umn(&(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l12) + 48)), &(*(gm_toq2f2q4opoa3 *)(((uint8_t *)&F.l8) + 48)));
                                        if (!t44) goto ie43;
                                        {
                                            gm_l9 = ((uint8_t)1);
                                        }
                                        goto ix43;
ie43:;
ix43:;
                                    }
                                    goto me39;
mn40:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 149, 9);
me39:;
                                }
lc30:;
                                {
                                    uint64_t t47 = gm_l11;
                                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                                    uint64_t t50 = r48;
                                    gm_l11 = t50;
                                }
                            }
lb30:;
                        }
                    }
                    {
                        uint8_t t52 = gm_l9;
                        uint8_t t53 = (uint8_t)!t52;
                        if (!t53) goto ie51;
                        {
                            void * t55 = (void *)((uint8_t *)&F.l5);
                            gm_tftb1odc739t5 t56 = gm_f_push_vgb8k63(t55, &(*(gm_t2f6hp2vik01f *)((uint8_t *)&F.l8)));
                            gm_tftb1odc739t5 t57 = t56;
                            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcpsceljs9unl1, 152, 22);
                            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                        }
                        goto ix51;
ie51:;
ix51:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 146, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t60 = gm_l7;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l7 = t63;
                }
            }
lb6:;
        }
    }
    {
        gm_f_list_1q70459((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t65 = gm_l4;
            gm_l14 = t65;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t67 = gm_l15;
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t69 = (t67 < t68);
                if (!t69) goto lb66;
                {
                    uint64_t t72 = gm_l15;
                    uint64_t t73 = (uintptr_t)t72;
                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_t2f6hp2vik01f t77 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t73 * 96));
                    F.l16 = t77;
                    {
                        uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 48) + 0));
                        if (t80 != ((uint8_t)1ULL)) goto ie79;
                        gm_tin97tt4ud0s1 t81 = GM_LD(gm_tin97tt4ud0s1, ((((uint8_t *)&F.l16) + 48) + 8));
                        F.l17 = t81;
                        {
                            void * t83 = (void *)((uint8_t *)&F.l13);
                            gm_tftb1odc739t5 t84 = gm_f_push_ljifgtt(t83, &(*(gm_tin97tt4ud0s1 *)((uint8_t *)&F.l17)));
                            gm_tftb1odc739t5 t85 = t84;
                            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcpsceljs9unl1, 156, 36);
                            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
                        }
                        goto ix79;
ie79:;
ix79:;
                    }
                    goto me75;
mn76:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 155, 5);
me75:;
                }
lc66:;
                {
                    uint64_t t88 = gm_l15;
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                    uint64_t t91 = r89;
                    gm_l15 = t91;
                }
            }
lb66:;
        }
    }
    {
        gm_t6j6smj7qoctu t92;
        gm_b16_8 t93 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t92 + 0, t93);
        gm_b16_8 t94 = gm_l4;
        gm_b16_8 t95 = gm_f_describe_mp0u2mq((void *)NULL, cx, NULL, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t92 + 16, t95);
        gm_tabb710d9mq2e t96;
        gm_f_format_n8h20c1((void *)&t96, (void *)NULL, cx, NULL, &t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t97 = t96;
        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_srcpsceljs9unl1, 158, 17);
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
        gm_l18 = t98;
    }
    {
        uint64_t t100 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
        uint8_t t101 = (t100 > ((uint64_t)1ULL));
        if (!t101) goto ie99;
        {
            gm_b16_8 t103 = gm_l18;
            gm_b16_8 t104 = t103;
            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
            gm_tmfqp43enl3pa t106;
            memset(&t106, 0, sizeof t106);
            ((uint8_t *)&t106)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t106 + 8), t105);
            gm_tmfqp43enl3pa t107 = t106;
            uint8_t t108 = ((((uint8_t *)&t107))[0] != 0);
            F.ret = t106;
            goto gm_fret;
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
        uint8_t t111 = (t110 > ((uint64_t)0ULL));
        if (!t111) goto ie109;
        {
            uint64_t t113 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t114 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srcpsceljs9unl1, 161, 16);
            gm_t2f6hp2vik01f t115 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t113 * 96));
            F.l19 = t115;
        }
        {
            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
            switch (t122) { case ((uint8_t)0ULL): goto ma120; case ((uint8_t)1ULL): goto ma120; case ((uint8_t)2ULL): goto ma117; case ((uint8_t)3ULL): goto ma118; case ((uint8_t)4ULL): goto ma119; default: goto ma120; }
ma117:;
            uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
            if (t124 != ((uint8_t)2ULL)) goto mn123;
            gm_b16_8 t125 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 48) + 8));
            gm_l20 = t125;
            {
                void * t127 = gm_l2;
                gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)t127) + 0) + 0));
                gm_b16_8 t129 = t128;
                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                gm_b16_8 t131 = gm_l20;
                gm_th2lqbo77pvsc t132;
                gm_f_find_ref_h89e731((void *)&t132, t130, t131);
                gm_th2lqbo77pvsc t133 = t132;
                gm_b16_8 t134;
                if (((((uint8_t *)&t133))[16] != 0)) {
                    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 0));
                    t134 = t135;
                } else {
                    gm_tfeab279c16rr t137;
                    gm_b16_8 t138 = gm_l0;
                    GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
                    gm_b16_8 t139 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t139);
                    gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t137 + 32, t140);
                    gm_tabb710d9mq2e t141;
                    gm_f_format_l821hfj((void *)&t141, (void *)NULL, cx, NULL, &t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t142 = t141;
                    if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_srcpsceljs9unl1, 165, 29);
                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
                    gm_b16_8 t144 = t143;
                    gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                    gm_tmfqp43enl3pa t146;
                    memset(&t146, 0, sizeof t146);
                    ((uint8_t *)&t146)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t146 + 8), t145);
                    gm_tmfqp43enl3pa t147 = t146;
                    uint8_t t148 = ((((uint8_t *)&t147))[0] != 0);
                    F.ret = t146;
                    goto gm_fret;
                }
                gm_l21 = t134;
            }
            {
                uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                uint8_t t151 = (t150 > ((uint64_t)0ULL));
                if (!t151) goto ie149;
                {
                    gm_b16_8 t153 = gm_l20;
                    gm_tom1ad5bcqgi1 t154;
                    gm_f_tag_version_aloeltn((void *)&t154, (void *)NULL, cx, NULL, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tom1ad5bcqgi1 t155 = t154;
                    gm_tu1089hhijadf t156;
                    if (((((uint8_t *)&t155))[56] != 0)) {
                        gm_tu1089hhijadf t157 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t155) + 0));
                        t156 = t157;
                    } else {
                        gm_b16_8 t158 = gm_l18;
                        gm_b16_8 t159 = t158;
                        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                        gm_tmfqp43enl3pa t161;
                        memset(&t161, 0, sizeof t161);
                        ((uint8_t *)&t161)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t161 + 8), t160);
                        gm_tmfqp43enl3pa t162 = t161;
                        uint8_t t163 = ((((uint8_t *)&t162))[0] != 0);
                        F.ret = t161;
                        goto gm_fret;
                    }
                    F.l22 = t156;
                }
                {
                    {
                        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                        gm_l23 = t165;
                    }
                    {
                        gm_l24 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t167 = gm_l24;
                            uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            uint8_t t169 = (t167 < t168);
                            if (!t169) goto lb166;
                            {
                                uint64_t t172 = gm_l24;
                                uint64_t t173 = (uintptr_t)t172;
                                uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                gm_tin97tt4ud0s1 t177 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t173 * 40));
                                F.l25 = t177;
                                {
                                    void * t180 = (void *)((uint8_t *)&F.l25);
                                    uint8_t t181 = gm_f_req_matches_dflcrek(t180, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l22)));
                                    uint8_t t182 = (uint8_t)!t181;
                                    if (!t182) goto ie179;
                                    {
                                        gm_b16_8 t184 = gm_l18;
                                        gm_b16_8 t185 = t184;
                                        gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                                        gm_tmfqp43enl3pa t187;
                                        memset(&t187, 0, sizeof t187);
                                        ((uint8_t *)&t187)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 8), t186);
                                        gm_tmfqp43enl3pa t188 = t187;
                                        uint8_t t189 = ((((uint8_t *)&t188))[0] != 0);
                                        F.ret = t187;
                                        goto gm_fret;
                                    }
                                    goto ix179;
ie179:;
ix179:;
                                }
                                goto me175;
mn176:;
                                gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 169, 21);
me175:;
                            }
lc166:;
                            {
                                uint64_t t191 = gm_l24;
                                uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                                uint64_t t194 = r192;
                                gm_l24 = t194;
                            }
                        }
lb166:;
                    }
                }
                goto ix149;
ie149:;
ix149:;
            }
            {
                gm_t8qqrlhb610a2 t195;
                gm_b16_8 t196 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t196);
                gm_th2lqbo77pvsc t197;
                memset(&t197, 0, sizeof t197);
                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t195 + 16, t197);
                gm_b16_8 t198 = gm_l20;
                gm_tnpifv20irnph t199;
                GM_ST(gm_b16_8, ((uint8_t *)&t199 + 0), t198);
                gm_tabb710d9mq2e t200;
                gm_f_format_7sll6e8((void *)&t200, (void *)NULL, cx, NULL, t199);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t201 = t200;
                if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 8, gm_srcpsceljs9unl1, 173, 67);
                gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&t201) + 8));
                gm_b16_8 t203 = t202;
                gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                GM_ST(gm_b16_8, (uint8_t *)&t195 + 40, t204);
                gm_tmfqp43enl3pa t205;
                GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                ((uint8_t *)&t205)[0] = 0;
                GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t205 + 8), t195);
                gm_tmfqp43enl3pa t206 = t205;
                uint8_t t207 = ((((uint8_t *)&t206))[0] != 0);
                F.ret = t205;
                goto gm_fret;
            }
            goto me116;
mn123:;
ma118:;
            uint8_t t209 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
            if (t209 != ((uint8_t)3ULL)) goto mn208;
            gm_b16_8 t210 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 48) + 8));
            gm_l26 = t210;
            {
                void * t212 = gm_l2;
                gm_b16_8 t213 = GM_LD(gm_b16_8, ((((uint8_t *)t212) + 40) + 0));
                gm_b16_8 t214 = t213;
                gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
                gm_b16_8 t216 = gm_l26;
                gm_th2lqbo77pvsc t217;
                gm_f_find_ref_h89e731((void *)&t217, t215, t216);
                gm_th2lqbo77pvsc t218 = t217;
                gm_b16_8 t219;
                if (((((uint8_t *)&t218))[16] != 0)) {
                    gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t218) + 0));
                    t219 = t220;
                } else {
                    gm_tfeab279c16rr t222;
                    gm_b16_8 t223 = gm_l0;
                    GM_ST(gm_b16_8, (uint8_t *)&t222 + 0, t223);
                    gm_b16_8 t224 = gm_l26;
                    GM_ST(gm_b16_8, (uint8_t *)&t222 + 16, t224);
                    gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t222 + 32, t225);
                    gm_tabb710d9mq2e t226;
                    gm_f_format_3pk07bs((void *)&t226, (void *)NULL, cx, NULL, &t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t227 = t226;
                    if (((((uint8_t *)&t227))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t227) + 8, gm_srcpsceljs9unl1, 177, 29);
                    gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&t227) + 8));
                    gm_b16_8 t229 = t228;
                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                    gm_tmfqp43enl3pa t231;
                    memset(&t231, 0, sizeof t231);
                    ((uint8_t *)&t231)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t231 + 8), t230);
                    gm_tmfqp43enl3pa t232 = t231;
                    uint8_t t233 = ((((uint8_t *)&t232))[0] != 0);
                    F.ret = t231;
                    goto gm_fret;
                }
                gm_l27 = t219;
            }
            {
                uint64_t t235 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                uint8_t t236 = (t235 > ((uint64_t)0ULL));
                if (!t236) goto ie234;
                {
                    gm_b16_8 t238 = gm_l18;
                    gm_b16_8 t239 = t238;
                    gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                    gm_tmfqp43enl3pa t241;
                    memset(&t241, 0, sizeof t241);
                    ((uint8_t *)&t241)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t241 + 8), t240);
                    gm_tmfqp43enl3pa t242 = t241;
                    uint8_t t243 = ((((uint8_t *)&t242))[0] != 0);
                    F.ret = t241;
                    goto gm_fret;
                }
                goto ix234;
ie234:;
ix234:;
            }
            {
                gm_t8qqrlhb610a2 t244;
                gm_b16_8 t245 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t244 + 0, t245);
                gm_th2lqbo77pvsc t246;
                memset(&t246, 0, sizeof t246);
                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t244 + 16, t246);
                gm_b16_8 t247 = gm_l26;
                gm_tnpifv20irnph t248;
                GM_ST(gm_b16_8, ((uint8_t *)&t248 + 0), t247);
                gm_tabb710d9mq2e t249;
                gm_f_format_asjuqmr((void *)&t249, (void *)NULL, cx, NULL, t248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t250 = t249;
                if (((((uint8_t *)&t250))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t250) + 8, gm_srcpsceljs9unl1, 180, 67);
                gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&t250) + 8));
                gm_b16_8 t252 = t251;
                gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
                GM_ST(gm_b16_8, (uint8_t *)&t244 + 40, t253);
                gm_tmfqp43enl3pa t254;
                GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
                ((uint8_t *)&t254)[0] = 0;
                GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t254 + 8), t244);
                gm_tmfqp43enl3pa t255 = t254;
                uint8_t t256 = ((((uint8_t *)&t255))[0] != 0);
                F.ret = t254;
                goto gm_fret;
            }
            goto me116;
mn208:;
ma119:;
            uint8_t t258 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 48) + 0));
            if (t258 != ((uint8_t)4ULL)) goto mn257;
            gm_b16_8 t259 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 48) + 8));
            gm_l28 = t259;
            {
                gm_b16_8 t261 = gm_l3;
                gm_b16_8 t262 = gm_l28;
                gm_th2lqbo77pvsc t263;
                gm_f_rev_parse_3l092b4((void *)&t263, (void *)NULL, cx, NULL, t261, t262);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t264 = t263;
                gm_b16_8 t265;
                if (((((uint8_t *)&t264))[16] != 0)) {
                    gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&t264) + 0));
                    t265 = t266;
                } else {
                    gm_tfeab279c16rr t268;
                    gm_b16_8 t269 = gm_l0;
                    GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t269);
                    gm_b16_8 t270 = gm_l28;
                    GM_ST(gm_b16_8, (uint8_t *)&t268 + 16, t270);
                    gm_b16_8 t271 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t268 + 32, t271);
                    gm_tabb710d9mq2e t272;
                    gm_f_format_rpqj2mb((void *)&t272, (void *)NULL, cx, NULL, &t268);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t273 = t272;
                    if (((((uint8_t *)&t273))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t273) + 8, gm_srcpsceljs9unl1, 184, 29);
                    gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)&t273) + 8));
                    gm_b16_8 t275 = t274;
                    gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                    gm_tmfqp43enl3pa t277;
                    memset(&t277, 0, sizeof t277);
                    ((uint8_t *)&t277)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t277 + 8), t276);
                    gm_tmfqp43enl3pa t278 = t277;
                    uint8_t t279 = ((((uint8_t *)&t278))[0] != 0);
                    F.ret = t277;
                    goto gm_fret;
                }
                gm_l29 = t265;
            }
            {
                uint64_t t281 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                uint8_t t282 = (t281 > ((uint64_t)0ULL));
                if (!t282) goto ie280;
                {
                    gm_b16_8 t284 = gm_l18;
                    gm_b16_8 t285 = t284;
                    gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                    gm_tmfqp43enl3pa t287;
                    memset(&t287, 0, sizeof t287);
                    ((uint8_t *)&t287)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t287 + 8), t286);
                    gm_tmfqp43enl3pa t288 = t287;
                    uint8_t t289 = ((((uint8_t *)&t288))[0] != 0);
                    F.ret = t287;
                    goto gm_fret;
                }
                goto ix280;
ie280:;
ix280:;
            }
            {
                gm_t8qqrlhb610a2 t290;
                gm_b16_8 t291 = gm_l29;
                GM_ST(gm_b16_8, (uint8_t *)&t290 + 0, t291);
                gm_th2lqbo77pvsc t292;
                memset(&t292, 0, sizeof t292);
                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t290 + 16, t292);
                gm_b16_8 t293 = gm_l28;
                gm_tnpifv20irnph t294;
                GM_ST(gm_b16_8, ((uint8_t *)&t294 + 0), t293);
                gm_tabb710d9mq2e t295;
                gm_f_format_m5mroph((void *)&t295, (void *)NULL, cx, NULL, t294);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t296 = t295;
                if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 8, gm_srcpsceljs9unl1, 187, 67);
                gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&t296) + 8));
                gm_b16_8 t298 = t297;
                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                GM_ST(gm_b16_8, (uint8_t *)&t290 + 40, t299);
                gm_tmfqp43enl3pa t300;
                GM_ST(uint8_t, (uint8_t *)&t300 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t300 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t300 + 4, 0);
                ((uint8_t *)&t300)[0] = 0;
                GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t300 + 8), t290);
                gm_tmfqp43enl3pa t301 = t300;
                uint8_t t302 = ((((uint8_t *)&t301))[0] != 0);
                F.ret = t300;
                goto gm_fret;
            }
            goto me116;
mn257:;
ma120:;
            goto me116;
mn303:;
ma121:;
            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 162, 9);
me116:;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        uint64_t t306 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
        uint8_t t307 = (t306 > ((uint64_t)0ULL));
        if (!t307) goto ie305;
        {
            gm_tom1ad5bcqgi1 t309;
            memset(&t309, 0, sizeof t309);
            F.l30 = t309;
        }
        {
            gm_b16_8 t310;
            GM_ST(void *, (uint8_t *)&t310, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(0));
            gm_l31 = t310;
        }
        {
            gm_b16_8 t311;
            GM_ST(void *, (uint8_t *)&t311, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(0));
            gm_l32 = t311;
        }
        {
            {
                void * t313 = gm_l2;
                gm_b16_8 t314 = GM_LD(gm_b16_8, ((((uint8_t *)t313) + 0) + 0));
                gm_l33 = t314;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t316 = gm_l34;
                    uint64_t t317 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t318 = (t316 < t317);
                    if (!t318) goto lb315;
                    {
                        uint64_t t321 = gm_l34;
                        uint64_t t322 = (uintptr_t)t321;
                        uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        gm_t6j6smj7qoctu t326 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t322 * 32));
                        gm_l35 = t326;
                        {
                            gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 0));
                            gm_tom1ad5bcqgi1 t329;
                            gm_f_tag_version_aloeltn((void *)&t329, (void *)NULL, cx, NULL, t328);
                            if (gm_unw.kind) {
                                goto gm_ll319;
                            }
                            gm_tom1ad5bcqgi1 t330 = t329;
                            gm_tu1089hhijadf t331;
                            if (((((uint8_t *)&t330))[56] != 0)) {
                                gm_tu1089hhijadf t332 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t330) + 0));
                                t331 = t332;
                            } else {
                                goto lc315;
                            }
                            F.l36 = t331;
                        }
                        {
                            gm_l37 = ((uint8_t)1);
                        }
                        {
                            {
                                gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                gm_l38 = t334;
                            }
                            {
                                gm_l39 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t336 = gm_l39;
                                    uint64_t t337 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                    uint8_t t338 = (t336 < t337);
                                    if (!t338) goto lb335;
                                    {
                                        uint64_t t341 = gm_l39;
                                        uint64_t t342 = (uintptr_t)t341;
                                        uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                        gm_tin97tt4ud0s1 t346 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t342 * 40));
                                        F.l40 = t346;
                                        {
                                            void * t349 = (void *)((uint8_t *)&F.l40);
                                            uint8_t t350 = gm_f_req_matches_dflcrek(t349, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l36)));
                                            uint8_t t351 = (uint8_t)!t350;
                                            if (!t351) goto ie348;
                                            {
                                                gm_l37 = ((uint8_t)0);
                                            }
                                            goto ix348;
ie348:;
ix348:;
                                        }
                                        goto me344;
mn345:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 199, 13);
me344:;
                                    }
lc335:;
                                    {
                                        uint64_t t354 = gm_l39;
                                        uint64_t r355; bool o356 = __builtin_add_overflow(t354, ((uint64_t)1ULL), &r355);
                                        uint64_t t357 = r355;
                                        gm_l39 = t357;
                                    }
                                }
lb335:;
                            }
                        }
                        {
                            uint8_t t359 = gm_l37;
                            uint8_t t360 = (uint8_t)!t359;
                            if (!t360) goto ie358;
                            {
                                goto lc315;
                            }
                            goto ix358;
ie358:;
ix358:;
                        }
                        {
                            if (!((((uint8_t *)&F.l30))[56] != 0)) goto ie362;
                            gm_tu1089hhijadf t363 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l30));
                            F.l41 = t363;
                            {
                                int32_t t366 = gm_f_cmp_version_0oibvro(&(*(gm_tu1089hhijadf *)((uint8_t *)&F.l36)), &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l41)));
                                uint8_t t367 = (t366 < ((int32_t)0LL));
                                if (!t367) goto ie365;
                                {
                                    goto lc315;
                                }
                                goto ix365;
ie365:;
ix365:;
                            }
                            goto ix362;
ie362:;
ix362:;
                        }
                        {
                            gm_tu1089hhijadf t369 = F.l36;
                            gm_tom1ad5bcqgi1 t370;
                            GM_ST(uint8_t, (uint8_t *)&t370 + 57, 0);
                            GM_ST(uint16_t, (uint8_t *)&t370 + 58, 0);
                            GM_ST(uint32_t, (uint8_t *)&t370 + 60, 0);
                            GM_ST(gm_tu1089hhijadf, ((uint8_t *)&t370), t369);
                            ((uint8_t *)&t370)[56] = 1;
                            F.l30 = t370;
                        }
                        {
                            gm_b16_8 t371 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 0));
                            gm_l31 = t371;
                        }
                        {
                            gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 16));
                            gm_l32 = t372;
                        }
                        goto me324;
mn325:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 196, 9);
me324:;
                    }
lc315:;
                    {
                        uint64_t t374 = gm_l34;
                        uint64_t r375; bool o376 = __builtin_add_overflow(t374, ((uint64_t)1ULL), &r375);
                        uint64_t t377 = r375;
                        gm_l34 = t377;
                    }
                }
gm_ll319:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb315; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc315; }
                }
                goto gm_lad;
lb315:;
            }
        }
        {
            gm_tom1ad5bcqgi1 t378 = F.l30;
            gm_tom1ad5bcqgi1 t379 = t378;
            gm_tu1089hhijadf t380;
            if (((((uint8_t *)&t379))[56] != 0)) {
                gm_tu1089hhijadf t381 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t379) + 0));
                t380 = t381;
            } else {
                gm_t6j6smj7qoctu t383;
                gm_b16_8 t384 = gm_l0;
                GM_ST(gm_b16_8, (uint8_t *)&t383 + 0, t384);
                gm_b16_8 t385 = gm_l4;
                gm_b16_8 t386 = gm_f_describe_mp0u2mq((void *)NULL, cx, NULL, t385);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t383 + 16, t386);
                gm_tabb710d9mq2e t387;
                gm_f_format_eagbsnc((void *)&t387, (void *)NULL, cx, NULL, &t383);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t388 = t387;
                if (((((uint8_t *)&t388))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t388) + 8, gm_srcpsceljs9unl1, 212, 21);
                gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)&t388) + 8));
                gm_b16_8 t390 = t389;
                gm_b16_8 t391 = GM_LD(gm_b16_8, ((uint8_t *)&t390));
                gm_tmfqp43enl3pa t392;
                memset(&t392, 0, sizeof t392);
                ((uint8_t *)&t392)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t392 + 8), t391);
                gm_tmfqp43enl3pa t393 = t392;
                uint8_t t394 = ((((uint8_t *)&t393))[0] != 0);
                F.ret = t392;
                goto gm_fret;
            }
            F.l42 = t380;
        }
        {
            gm_t8qqrlhb610a2 t395;
            gm_b16_8 t396 = gm_l32;
            GM_ST(gm_b16_8, (uint8_t *)&t395 + 0, t396);
            gm_b16_8 t397 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l42)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t398;
            GM_ST(uint8_t, (uint8_t *)&t398 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t398 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t398 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t398), t397);
            ((uint8_t *)&t398)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t395 + 16, t398);
            gm_b16_8 t399 = gm_l31;
            gm_tnpifv20irnph t400;
            GM_ST(gm_b16_8, ((uint8_t *)&t400 + 0), t399);
            gm_tabb710d9mq2e t401;
            gm_f_format_7sll6e8((void *)&t401, (void *)NULL, cx, NULL, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t402 = t401;
            if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 8, gm_srcpsceljs9unl1, 214, 82);
            gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&t402) + 8));
            gm_b16_8 t404 = t403;
            gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
            GM_ST(gm_b16_8, (uint8_t *)&t395 + 40, t405);
            gm_tmfqp43enl3pa t406;
            GM_ST(uint8_t, (uint8_t *)&t406 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t406 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
            ((uint8_t *)&t406)[0] = 0;
            GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t406 + 8), t395);
            gm_tmfqp43enl3pa t407 = t406;
            uint8_t t408 = ((((uint8_t *)&t407))[0] != 0);
            F.ret = t406;
            goto gm_fret;
        }
        goto ix305;
ie305:;
ix305:;
    }
    {
        void * t409 = gm_l2;
        gm_th2lqbo77pvsc t410 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t409) + 80));
        gm_th2lqbo77pvsc t411 = t410;
        gm_b16_8 t412;
        if (((((uint8_t *)&t411))[16] != 0)) {
            gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)&t411) + 0));
            t412 = t413;
        } else {
            gm_t6j6smj7qoctu t415;
            gm_b16_8 t416 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t415 + 0, t416);
            uint64_t t417 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t418 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t417 >= (uintptr_t)(t418)) gm_trap_index(t417, t418, gm_srcpsceljs9unl1, 217, 87);
            gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t417 * 96) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t415 + 16, t419);
            gm_tabb710d9mq2e t420;
            gm_f_format_95e0vi0((void *)&t420, (void *)NULL, cx, NULL, &t415);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t421 = t420;
            if (((((uint8_t *)&t421))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t421) + 8, gm_srcpsceljs9unl1, 217, 17);
            gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&t421) + 8));
            gm_b16_8 t423 = t422;
            gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)&t423));
            gm_tmfqp43enl3pa t425;
            memset(&t425, 0, sizeof t425);
            ((uint8_t *)&t425)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t425 + 8), t424);
            gm_tmfqp43enl3pa t426 = t425;
            uint8_t t427 = ((((uint8_t *)&t426))[0] != 0);
            F.ret = t425;
            goto gm_fret;
        }
        gm_l43 = t412;
    }
    {
        gm_t8qqrlhb610a2 t428;
        gm_b16_8 t429 = gm_l43;
        GM_ST(gm_b16_8, (uint8_t *)&t428 + 0, t429);
        gm_th2lqbo77pvsc t430;
        memset(&t430, 0, sizeof t430);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t428 + 16, t430);
        gm_b16_8 t431;
        GM_ST(void *, (uint8_t *)&t431, (void *)(gm_ses9qtp6kdd22));
        GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&t428 + 40, t431);
        gm_tmfqp43enl3pa t432;
        GM_ST(uint8_t, (uint8_t *)&t432 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t432 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t432 + 4, 0);
        ((uint8_t *)&t432)[0] = 0;
        GM_ST(gm_t8qqrlhb610a2, ((uint8_t *)&t432 + 8), t428);
        gm_tmfqp43enl3pa t433 = t432;
        uint8_t t434 = ((((uint8_t *)&t433))[0] != 0);
        F.ret = t432;
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
inline __attribute__((aligned(4))) void gm_f_read_manifest_u9uajvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_read_manifest_u9uajvn F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* text */
    gm_tf1bjg81u0glv gm_l3; /* e */
    GM_STACK_CHECK(&F, 3370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t4;
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_t3ujhu6qph7de t6;
        gm_f_read_text_g0qbj1t((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
            gm_tikairc198460 t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l2 = t10;
    }
    {
        gm_b16_8 t11 = gm_l2;
        gm_tjgmtdrk88t7m t12;
        gm_f_parse_manifest_b61s3ln((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjgmtdrk88t7m t13 = t12;
        gm_tm43kqe3e8b4a t14;
        if (!((((uint8_t *)&t13))[0] != 0)) {
            gm_tm43kqe3e8b4a t15 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t13) + 8));
            t14 = t15;
        } else {
            gm_tf1bjg81u0glv t16 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t13) + 8));
            gm_l3 = t16;
            gm_tq3m5qgvkpje8 t18;
            gm_b16_8 t19 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            GM_ST(uint64_t, (uint8_t *)&t18 + 16, t20);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t18 + 24, t21);
            gm_tabb710d9mq2e t22;
            gm_f_format_n6eprd4((void *)&t22, (void *)NULL, cx, NULL, &t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcpsceljs9unl1, 226, 58);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_tikairc198460 t27;
            memset(&t27, 0, sizeof t27);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 8), t26);
            gm_tikairc198460 t28 = t27;
            uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
            F.ret = t27;
            goto gm_fret;
        }
        gm_tikairc198460 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        ((uint8_t *)&t30)[0] = 0;
        GM_ST(gm_tm43kqe3e8b4a, ((uint8_t *)&t30 + 8), t14);
        gm_tikairc198460 t31 = t30;
        uint8_t t32 = ((((uint8_t *)&t31))[0] != 0);
        F.ret = t30;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_record_dep_84vd3la(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_record_dep_84vd3la F;
    void * gm_l0; /* r */
    void * gm_l4; /* keys */
    GM_STACK_CHECK(&F, 338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tqa5rsrlv432m t2 = GM_LD(gm_tqa5rsrlv432m, ((uint8_t *)a1));
        gm_tqa5rsrlv432m t3 = t2;
        gm_tbqru6ibafrpr t4;
        if (((((uint8_t *)&t3))[56] != 0)) {
            gm_tbqru6ibafrpr t5 = GM_LD(gm_tbqru6ibafrpr, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        F.l3 = t4;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 512);
        void * t11 = gm_f_slot_5e9u9id(t10, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l3) + 0)));
        void * t12 = t11;
        if (!(GM_LD(void *, ((uint8_t *)&t12)) != 0)) goto ie8;
        void * t13 = GM_LD(void *, ((uint8_t *)&t12));
        gm_l4 = t13;
        {
            void * t15 = gm_l4;
            gm_tftb1odc739t5 t16 = gm_f_push_d61dqrj(t15, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a2)));
            gm_tftb1odc739t5 t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcpsceljs9unl1, 716, 9);
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_f_list_qeqmcmu((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l5);
        gm_tftb1odc739t5 t22 = gm_f_push_d61dqrj(t21, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)a2)));
        gm_tftb1odc739t5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcpsceljs9unl1, 720, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 512);
        gm_b0_1 t27 = gm_f_put_jng5s3d((void *)NULL, cx, NULL, t26, &(*(gm_t6ok5tvcjc200 *)(((uint8_t *)&F.l3) + 0)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) void gm_f_recorded_version_lb5b4vt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_recorded_version_lb5b4vt F;
    gm_b16_8 gm_l0; /* all */
    gm_b16_8 gm_l1; /* reference */
    gm_b16_8 gm_l2; /* reqs */
    uint8_t gm_l3; /* pinned */
    uint8_t gm_l4; /* versioned */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* tag */
    GM_STACK_CHECK(&F, 782u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_effective_vikkdqh((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t5 = gm_l2;
            gm_l5 = t5;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l6;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l6;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t2f6hp2vik01f t17 = GM_LD(gm_t2f6hp2vik01f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t13 * 96));
                    F.l7 = t17;
                    {
                        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 48) + 0));
                        switch (t24) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; default: goto ma22; }
ma20:;
                        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 48) + 0));
                        if (t26 != ((uint8_t)1ULL)) goto mn25;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto me19;
mn25:;
ma21:;
                        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 48) + 0));
                        if (t29 != ((uint8_t)0ULL)) goto mn28;
                        goto me19;
mn28:;
ma22:;
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto me19;
mn31:;
ma23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 95, 9);
me19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 94, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t34 = gm_l6;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l6 = t37;
                }
            }
lb6:;
        }
    }
    {
        uint8_t t39 = 0;
        uint8_t t40 = gm_l3;
        if (!t40) {
            uint8_t t41 = gm_l4;
            uint8_t t42 = (uint8_t)!t41;
            t39 = t42;
        } else t39 = 1;
        if (!t39) goto ie38;
        {
            gm_th2lqbo77pvsc t44;
            memset(&t44, 0, sizeof t44);
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        gm_b16_8 t46 = gm_l1;
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sn2t7jm5mf8pm));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(4));
        gm_th2lqbo77pvsc t48;
        gm_f_strip_prefix_b4949s8((void *)&t48, t46, t47);
        gm_th2lqbo77pvsc t49 = t48;
        gm_b16_8 t50;
        if (((((uint8_t *)&t49))[16] != 0)) {
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
            t50 = t51;
        } else {
            gm_th2lqbo77pvsc t52;
            memset(&t52, 0, sizeof t52);
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        gm_l8 = t50;
    }
    {
        gm_b16_8 t54 = gm_l8;
        gm_tom1ad5bcqgi1 t55;
        gm_f_tag_version_aloeltn((void *)&t55, (void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tom1ad5bcqgi1 t56 = t55;
        gm_tu1089hhijadf t57;
        if (((((uint8_t *)&t56))[56] != 0)) {
            gm_tu1089hhijadf t58 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t56) + 0));
            t57 = t58;
        } else {
            gm_th2lqbo77pvsc t59;
            memset(&t59, 0, sizeof t59);
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        F.l9 = t57;
    }
    {
        gm_b16_8 t61 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[16] = 1;
        uint8_t t63 = 0;
        F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_refs_of_jfglrbh(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_refs_of_jfglrbh F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 644u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 192) + 0));
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
                    gm_t3rj5o15e922s t16 = GM_LD(gm_t3rj5o15e922s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 120));
                    F.l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            gm_tmf08dt0mom0q t25 = GM_LD(gm_tmf08dt0mom0q, (((uint8_t *)&F.l4) + 16));
                            gm_tcmp1j2cnqi8i t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 105, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 106, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 108, 0);
                            GM_ST(gm_tmf08dt0mom0q, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[104] = 1;
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
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 465, 5);
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
        gm_tcmp1j2cnqi8i t33;
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
__attribute__((aligned(4))) gm_tcmp1j2cnqi8i gm_f_refs_of_jfglrbh_c(void * a0, gm_b16_8 a1) { gm_tcmp1j2cnqi8i gm_r; gm_f_refs_of_jfglrbh(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_req_same_h2pl2bq_c(gm_t2f6hp2vik01f a0, gm_t2f6hp2vik01f a1) { return gm_f_req_same_h2pl2bq(&a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_resolver_ms661bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, const void * a3) {
    struct gm_fr_resolver_ms661bf F;
    gm_b16_8 gm_l0; /* root */
    gm_tqs5p4n2tq19v gm_l1; /* opts */
    gm_tl572d7579hns gm_l2; /* refresh */
    GM_STACK_CHECK(&F, 684u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 18, 0, 6);
        memset((uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 553, 0, 7);
        gm_b16_8 t2 = gm_l0;
        gm_tqs5p4n2tq19v t3 = gm_l1;
        gm_tl572d7579hns t4 = GM_LD(gm_tl572d7579hns, ((uint8_t *)a2));
        gm_b16_8 t5 = gm_l0;
        gm_tnpifv20irnph t6 = gm_f_store_for_package_347124r((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tv37d3dpt2opu t7 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)a3));
        gm_tin97tt4ud0s1 t8;
        gm_f_key_map_ihrh7ij((void *)&t8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t9;
        gm_f_list_epvducg((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t10;
        gm_f_list_jsujn2k((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t11;
        gm_f_list_agmtpnt((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t12;
        gm_f_list_epvducg((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t13;
        gm_f_key_map_136uk7q((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t14;
        gm_f_list_qeqmcmu((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t15;
        gm_f_key_map_kjnah27((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t16;
        gm_f_key_map_kjnah27((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t17;
        gm_f_key_map_kjnah27((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t18;
        gm_f_key_map_e8nmg3h((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t19;
        gm_f_list_dc4p80b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t19 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t20;
        gm_f_list_nccb2fo((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 0, t2);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 16, t3);
        GM_ST(gm_tl572d7579hns, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 24, t4);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 48, t6);
        GM_ST(gm_tv37d3dpt2opu, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 64, t7);
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 112, t8);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 152, t9);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 192, t10);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 232, t11);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 272, t12);
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 312, t13);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 352, t14);
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 392, t15);
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 432, t16);
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 472, t17);
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 512, t18);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 552, ((uint8_t)0));
        GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 560, t19);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tlhfcpum9vunj *)gm_out) + 600, t20);
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
__attribute__((aligned(4))) void gm_f_run_resolver_tf2ua9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_run_resolver_tf2ua9j F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l3; /* name */
    GM_STACK_CHECK(&F, 3319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_th2lqbo77pvsc t4;
        gm_f_canonicalize_epo7cln((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 0));
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_bpl33sh((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srcpsceljs9unl1, 350, 47);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_t8a9rhgj2oa25 t16;
            memset(&t16, 0, sizeof t16);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
            gm_t8a9rhgj2oa25 t17 = t16;
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t16;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        gm_b16_8 t19 = gm_l1;
        gm_tikairc198460 t20;
        gm_f_read_manifest_u9uajvn((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tikairc198460 t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            gm_t8a9rhgj2oa25 t23;
            memset(&t23, 0, sizeof t23);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
            F.ret = t23;
            goto gm_fret;
        }
        gm_tm43kqe3e8b4a t24 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t21) + 8));
        F.l2 = t24;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb25;
            {
                void * t28 = gm_l0;
                void * t29 = (void *)((((uint8_t *)t28) + 312) + 0);
                gm_f_clear_pirklev(t29);
            }
            {
                void * t30 = gm_l0;
                void * t31 = (void *)(((uint8_t *)t30) + 352);
                gm_f_clear_s0loit2(t31);
            }
            {
                void * t32 = gm_l0;
                void * t33 = (void *)((((uint8_t *)t32) + 392) + 0);
                gm_f_clear_qd87n65(t33);
            }
            {
                void * t34 = gm_l0;
                void * t35 = (void *)((((uint8_t *)t34) + 472) + 0);
                gm_f_clear_qd87n65(t35);
            }
            {
                void * t36 = gm_l0;
                void * t37 = (void *)((((uint8_t *)t36) + 512) + 0);
                gm_f_clear_m4blb4l(t37);
            }
            {
                void * t38 = gm_l0;
                uint8_t *t39 = (((uint8_t *)t38) + 560);
                gm_trqfvmjtt4sij t40;
                gm_f_list_dc4p80b((void *)&t40, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll26;
                }
                GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)t39) + 0, t40);
            }
            {
                void * t41 = gm_l0;
                void * t42 = (void *)(((uint8_t *)t41) + 600);
                gm_f_clear_rm1b3rc(t42);
            }
            {
                void * t43 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t43) + 552), ((uint8_t)0));
            }
            {
                gm_th2lqbo77pvsc t44 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 0));
                gm_th2lqbo77pvsc t45 = t44;
                gm_b16_8 t46;
                if (((((uint8_t *)&t45))[16] != 0)) {
                    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 0));
                    t46 = t47;
                } else {
                    gm_b16_8 t48 = gm_l1;
                    gm_th2lqbo77pvsc t49;
                    gm_f_file_name_em7v8lv((void *)&t49, (void *)NULL, cx, NULL, t48);
                    if (gm_unw.kind) {
                        goto gm_ll26;
                    }
                    gm_th2lqbo77pvsc t50 = t49;
                    gm_b16_8 t51;
                    if (((((uint8_t *)&t50))[16] != 0)) {
                        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 0));
                        t51 = t52;
                    } else {
                        gm_b16_8 t53;
                        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sr0hr1ep2j2vv));
                        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(4));
                        t51 = t53;
                    }
                    t46 = t51;
                }
                gm_l3 = t46;
            }
            {
                void * t54 = gm_l0;
                gm_b16_8 t55 = gm_l1;
                void * t56 = (void *)((uint8_t *)&F.l2);
                gm_b16_8 t57 = gm_l3;
                gm_tqa5rsrlv432m t58;
                memset(&t58, 0, sizeof t58);
                gm_tl572d7579hns t59;
                gm_f_walk_59naf6k((void *)&t59, (void *)NULL, cx, NULL, t54, t55, t56, t57, &t58);
                if (gm_unw.kind) {
                    goto gm_ll26;
                }
                gm_tl572d7579hns t60 = t59;
                if (((((uint8_t *)&t60))[0] != 0)) {
                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
                    gm_t8a9rhgj2oa25 t62;
                    memset(&t62, 0, sizeof t62);
                    ((uint8_t *)&t62)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
                    F.ret = t62;
                    goto gm_fret;
                }
                gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 8));
            }
            {
                void * t65 = gm_l0;
                uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t65) + 552));
                uint8_t t67 = (uint8_t)!t66;
                if (!t67) goto ie64;
                {
                    goto lb25;
                }
                goto ix64;
ie64:;
ix64:;
            }
lc25:;
        }
gm_ll26:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
        }
        goto gm_lad;
lb25:;
    }
    {
        void * t70 = gm_l0;
        uint64_t t71 = GM_LD(uintptr_t, (((((uint8_t *)t70) + 312) + 0) + 0) + 8);
        uint8_t t72 = ((t71 == ((uint64_t)0ULL)));
        if (!t72) goto ie69;
        void * t73 = gm_l0;
        uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 352) + 0) + 8);
        uint8_t t75 = ((t74 == ((uint64_t)0ULL)));
        if (!t75) goto ie69;
        {
            gm_tuu655st500b0 t77;
            memset(&t77, 0, sizeof t77);
            gm_t8a9rhgj2oa25 t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            ((uint8_t *)&t78)[0] = 0;
            GM_ST(gm_tuu655st500b0, ((uint8_t *)&t78 + 8), t77);
            gm_t8a9rhgj2oa25 t79 = t78;
            uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
            F.ret = t78;
            goto gm_fret;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        void * t81 = gm_l0;
        gm_tl572d7579hns t82;
        gm_f_check_refresh_names_9ih3ok9((void *)&t82, (void *)NULL, cx, NULL, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t83 = t82;
        if (((((uint8_t *)&t83))[0] != 0)) {
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
            gm_t8a9rhgj2oa25 t85;
            memset(&t85, 0, sizeof t85);
            ((uint8_t *)&t85)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t85 + 8), t84);
            F.ret = t85;
            goto gm_fret;
        }
        gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 8));
    }
    {
        void * t87 = gm_l0;
        gm_t62efos6ssciq t88;
        gm_f_build_lock_hmfdkj8((void *)&t88, (void *)NULL, cx, NULL, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t62efos6ssciq t89 = t88;
        if (((((uint8_t *)&t89))[0] != 0)) {
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&t89) + 8));
            gm_t8a9rhgj2oa25 t91;
            memset(&t91, 0, sizeof t91);
            ((uint8_t *)&t91)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 8), t90);
            F.ret = t91;
            goto gm_fret;
        }
        gm_tin97tt4ud0s1 t92 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)&t89) + 8));
        F.l4 = t92;
    }
    {
        gm_t08dlk5fmn1l9 t93;
        void * t94 = gm_l0;
        memcpy((uint8_t *)&t93 + 0, (((uint8_t *)t94) + 560), 40);
        memcpy((uint8_t *)&t93 + 40, ((uint8_t *)&F.l4), 40);
        gm_tuu655st500b0 t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 81, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 82, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 84, 0);
        GM_ST(gm_t08dlk5fmn1l9, ((uint8_t *)&t95), t93);
        ((uint8_t *)&t95)[80] = 1;
        gm_t8a9rhgj2oa25 t96;
        GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
        ((uint8_t *)&t96)[0] = 0;
        GM_ST(gm_tuu655st500b0, ((uint8_t *)&t96 + 8), t95);
        gm_t8a9rhgj2oa25 t97 = t96;
        uint8_t t98 = ((((uint8_t *)&t97))[0] != 0);
        F.ret = t96;
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
__attribute__((aligned(4))) uint8_t gm_f_satisfies_mlodg56(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_satisfies_mlodg56 F;
    gm_b16_8 gm_l0; /* reference */
    gm_b16_8 gm_l1; /* rev */
    gm_b16_8 gm_l3; /* t */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* r */
    gm_b16_8 gm_l7; /* tag */
    GM_STACK_CHECK(&F, 962u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t2;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; default: goto ma9; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        uint8_t t13 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_ses9qtp6kdd22, 7) == 0));
        t2 = t13;
        goto me3;
mn11:;
ma5:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t15 != ((uint8_t)2ULL)) goto mn14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
        gm_l3 = t16;
        gm_b16_8 t17 = gm_l0;
        gm_b16_8 t18 = gm_l3;
        gm_tnpifv20irnph t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
        gm_tabb710d9mq2e t20;
        gm_f_format_7sll6e8((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srcpsceljs9unl1, 111, 33);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_b16_8 t25 = t17;
        gm_b16_8 t26 = t24;
        uint8_t t27 = (gm_eq0iqiubfm7e(((uint8_t *)&t25), ((uint8_t *)&t26)));
        t2 = t27;
        goto me3;
mn14:;
ma6:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t29 != ((uint8_t)3ULL)) goto mn28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
        gm_l4 = t30;
        gm_b16_8 t31 = gm_l0;
        gm_b16_8 t32 = gm_l4;
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_asjuqmr((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcpsceljs9unl1, 112, 36);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_b16_8 t39 = t31;
        gm_b16_8 t40 = t38;
        uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
        t2 = t41;
        goto me3;
mn28:;
ma7:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t43 != ((uint8_t)4ULL)) goto mn42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
        gm_l5 = t44;
        gm_b16_8 t45 = gm_l1;
        gm_b16_8 t46 = gm_l5;
        uint8_t t47 = gm_f_starts_with_6vvt01e(t45, t46);
        t2 = t47;
        goto me3;
mn42:;
ma8:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t49 != ((uint8_t)1ULL)) goto mn48;
        gm_tin97tt4ud0s1 t50 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a2) + 8));
        F.l6 = t50;
        {
            gm_b16_8 t52 = gm_l0;
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sn2t7jm5mf8pm));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(4));
            gm_th2lqbo77pvsc t54;
            gm_f_strip_prefix_b4949s8((void *)&t54, t52, t53);
            gm_th2lqbo77pvsc t55 = t54;
            gm_b16_8 t56;
            if (((((uint8_t *)&t55))[16] != 0)) {
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 0));
                t56 = t57;
            } else {
                uint8_t t58 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l7 = t56;
        }
        {
            gm_b16_8 t59 = gm_l7;
            gm_tom1ad5bcqgi1 t60;
            gm_f_tag_version_aloeltn((void *)&t60, (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tom1ad5bcqgi1 t61 = t60;
            gm_tu1089hhijadf t62;
            if (((((uint8_t *)&t61))[56] != 0)) {
                gm_tu1089hhijadf t63 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t61) + 0));
                t62 = t63;
            } else {
                uint8_t t64 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            F.l8 = t62;
        }
        void * t65 = (void *)((uint8_t *)&F.l6);
        uint8_t t66 = gm_f_req_matches_dflcrek(t65, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l8)));
        uint8_t t67 = 0;
        F.ret = t66;
        goto gm_fret;
        goto me3;
mn48:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 109, 12);
me3:;
        uint8_t t68 = 0;
        F.ret = t2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha_mismatch_lq7bp8e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sha_mismatch_lq7bp8e F;
    gm_b16_8 gm_l0; /* dep */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* found */
    gm_b16_8 gm_l3; /* expected */
    gm_b16_8 gm_l4; /* origin */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_tud7oovfvnf43 t2;
        gm_b16_8 t3 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
        gm_b16_8 t4 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t4);
        gm_b16_8 t5 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t5);
        gm_b16_8 t6 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t6);
        gm_b16_8 t7 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 64, t7);
        gm_tabb710d9mq2e t8;
        gm_f_format_ia6bs6h((void *)&t8, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcpsceljs9unl1, 588, 12);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_b16_8 t11 = t10;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        uint8_t t13 = 0;
        F.ret = t12;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_short_rev_uin3i2s(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_subdir_of_gjqk3qb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_subdir_of_gjqk3qb F;
    gm_b16_8 gm_l0; /* co */
    gm_th2lqbo77pvsc gm_l1; /* sub */
    gm_b16_8 gm_l2; /* s */
    gm_b16_8 gm_l3; /* p */
    GM_STACK_CHECK(&F, 446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_th2lqbo77pvsc t2 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a1));
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b16_8 t6 = gm_l0;
            gm_th2lqbo77pvsc t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
            ((uint8_t *)&t7)[16] = 1;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10 = gm_l2;
        gm_b16_8 t11 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t12;
        gm_f_canonicalize_epo7cln((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t13 = t12;
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_th2lqbo77pvsc t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t14;
    }
    {
        gm_b16_8 t19 = gm_l3;
        gm_b16_8 t20 = gm_l0;
        uint8_t t21 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)!t21;
        if (!t22) goto ie18;
        {
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        gm_b16_8 t26 = gm_l3;
        gm_th2lqbo77pvsc t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t27), t26);
        ((uint8_t *)&t27)[16] = 1;
        uint8_t t28 = 0;
        F.ret = t27;
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
inline __attribute__((aligned(4))) void gm_f_summarize_archive_cicsplu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_summarize_archive_cicsplu F;
    gm_b16_8 gm_l0; /* dep */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* hash */
    gm_b16_8 gm_l3; /* dir */
    gm_th2lqbo77pvsc gm_l4; /* locked */
    gm_b16_8 gm_l5; /* checksum */
    gm_b16_8 gm_l6; /* l */
    gm_b16_8 gm_l7; /* canon */
    GM_STACK_CHECK(&F, 1396u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    {
        gm_b16_8 t2 = gm_l3;
        gm_t3ujhu6qph7de t3;
        gm_f_tree_checksum_k6o2imm((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_tq6bq450l0idg t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l5 = t7;
    }
    {
        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l6 = t9;
        {
            gm_b16_8 t12 = gm_l6;
            gm_b16_8 t13 = gm_l5;
            gm_b16_8 t14 = t12;
            gm_b16_8 t15 = t13;
            uint8_t t16 = (!gm_eq0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t15)));
            if (!t16) goto ie11;
            {
                gm_t0uuhaljsf410 t18;
                gm_b16_8 t19 = gm_l0;
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                gm_b16_8 t20 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
                gm_b16_8 t21 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 32, t21);
                gm_b16_8 t22 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 48, t22);
                gm_tabb710d9mq2e t23;
                gm_f_format_dvcii4b((void *)&t23, (void *)NULL, cx, NULL, &t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t24 = t23;
                if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcpsceljs9unl1, 596, 21);
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
                gm_b16_8 t26 = t25;
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                gm_tq6bq450l0idg t28;
                memset(&t28, 0, sizeof t28);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
                gm_tq6bq450l0idg t29 = t28;
                uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
                F.ret = t28;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_b16_8 t31 = gm_l3;
        gm_th2lqbo77pvsc t32;
        gm_f_canonicalize_epo7cln((void *)&t32, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t33 = t32;
        gm_b16_8 t34;
        if (((((uint8_t *)&t33))[16] != 0)) {
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 0));
            t34 = t35;
        } else {
            gm_b16_8 t36 = gm_l3;
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_tabb710d9mq2e t38;
            gm_f_format_bpl33sh((void *)&t38, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t39 = t38;
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcpsceljs9unl1, 605, 45);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_tq6bq450l0idg t43;
            memset(&t43, 0, sizeof t43);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 8), t42);
            gm_tq6bq450l0idg t44 = t43;
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t43;
            goto gm_fret;
        }
        gm_l7 = t34;
    }
    {
        gm_t0uuhaljsf410 t46;
        gm_b16_8 t47 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
        gm_b16_8 t48 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t48);
        gm_b16_8 t49 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 32, t49);
        gm_b16_8 t50 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 48, t50);
        gm_tq6bq450l0idg t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t51 + 8), t46);
        gm_tq6bq450l0idg t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
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
inline __attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_tag_version_aloeltn F;
    gm_b16_8 gm_l0; /* tag */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssdi9g220if8h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t4;
        gm_f_strip_prefix_b4949s8((void *)&t4, t2, t3);
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b16_8 t8 = gm_l0;
            t6 = t8;
        }
        gm_l1 = t6;
    }
    {
        gm_b16_8 t9 = gm_l1;
        gm_tvtl3n8qta358 t10;
        gm_f_parse_version_9a6ns50((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvtl3n8qta358 t11 = t10;
        gm_tom1ad5bcqgi1 t12;
        if (!((((uint8_t *)&t11))[0] != 0)) {
            gm_tu1089hhijadf t13 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t11) + 8));
            gm_tom1ad5bcqgi1 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 60, 0);
            GM_ST(gm_tu1089hhijadf, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[56] = 1;
            t12 = t14;
        } else {
            gm_tom1ad5bcqgi1 t15;
            memset(&t15, 0, sizeof t15);
            t12 = t15;
        }
        uint8_t t16 = 0;
        F.ret = t12;
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
__attribute__((aligned(4))) void gm_f_walk_59naf6k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_walk_59naf6k F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* dir */
    void * gm_l2; /* manifest */
    gm_b16_8 gm_l3; /* display */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint8_t gm_l9; /* any_fetched */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l21; /* p */
    gm_b16_8 gm_l22; /* joined */
    gm_th2lqbo77pvsc gm_l23; /* canon */
    gm_b16_8 gm_l25; /* c */
    gm_b16_8 gm_l26; /* target */
    gm_b16_8 gm_l31; /* co */
    gm_b16_8 gm_l32; /* pkg_dir */
    gm_b16_8 gm_l34; /* n */
    gm_b16_8 gm_l38; /* co */
    gm_b16_8 gm_l39; /* pkg_dir */
    uint8_t gm_l41; /* dup */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    GM_STACK_CHECK(&F, 9739u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 600) + 0));
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l5 = t6;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l6;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_toa5kh9gm9738 t13;
                    uint64_t t14 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l6;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t16 * 40), 40);
                    gm_toa5kh9gm9738 t18 = t13;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l7 = t21;
                    gm_tk9n3vlila9qg t22 = GM_LD(gm_tk9n3vlila9qg, (((uint8_t *)&t18) + 8));
                    F.l8 = t22;
                    {
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        gm_b16_8 t26 = gm_l1;
                        gm_b16_8 t27 = t25;
                        gm_b16_8 t28 = t26;
                        uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
                        if (!t29) goto ie24;
                        {
                            gm_l9 = ((uint8_t)0);
                        }
                        {
                            {
                                void * t32 = gm_l0;
                                uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 600) + 0) + 8);
                                void * t34 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 600) + 0)));
                                uint64_t t35 = gm_l7;
                                uint64_t t36 = (uintptr_t)t35;
                                if (t36 > (uintptr_t)t33 || (uintptr_t)t33 > (uintptr_t)t33) gm_trap_slice(t36, (uintptr_t)t33, t33, gm_srcpsceljs9unl1, 730, 22);
                                gm_b16_8 t37;
                                GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t34 + t36 * 40)));
                                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)((uintptr_t)t33 - t36));
                                gm_l10 = t37;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t39 = gm_l11;
                                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t41 = (t39 < t40);
                                    if (!t41) goto lb38;
                                    {
                                        uint64_t t44 = gm_l11;
                                        uint64_t t45 = (uintptr_t)t44;
                                        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        gm_tk9n3vlila9qg t49 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t45 * 40));
                                        F.l12 = t49;
                                        {
                                            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 32));
                                            if (!t52) goto ie51;
                                            {
                                                gm_l9 = ((uint8_t)1);
                                            }
                                            goto ix51;
ie51:;
ix51:;
                                        }
                                        goto me47;
mn48:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 730, 13);
me47:;
                                    }
lc38:;
                                    {
                                        uint64_t t55 = gm_l11;
                                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                        uint64_t t58 = r56;
                                        gm_l11 = t58;
                                    }
                                }
lb38:;
                            }
                        }
                        {
                            uint8_t t60 = 0;
                            uint8_t t61;
                            if (((((uint8_t *)&F.l4))[56] != 0)) goto mn63;
                            t61 = ((uint8_t)0);
                            goto me62;
mn63:;
                            t61 = ((uint8_t)1);
                            goto me62;
mn64:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 733, 16);
me62:;
                            if (!t61) {
                                uint8_t t65 = gm_l9;
                                t60 = t65;
                            } else t60 = 1;
                            if (!t60) goto ie59;
                            {
                                gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll11;
                                }
                            }
                            {
                                {
                                    void * t68 = gm_l0;
                                    uint64_t t69 = GM_LD(uintptr_t, ((((uint8_t *)t68) + 600) + 0) + 8);
                                    void * t70 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t68) + 600) + 0)));
                                    uint64_t t71 = gm_l7;
                                    uint64_t t72 = (uintptr_t)t71;
                                    if (t72 > (uintptr_t)t69 || (uintptr_t)t69 > (uintptr_t)t69) gm_trap_slice(t72, (uintptr_t)t69, t69, gm_srcpsceljs9unl1, 735, 26);
                                    gm_b16_8 t73;
                                    GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t70 + t72 * 40)));
                                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)((uintptr_t)t69 - t72));
                                    gm_l14 = t73;
                                }
                                {
                                    gm_l15 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t75 = gm_l15;
                                        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                        uint8_t t77 = (t75 < t76);
                                        if (!t77) goto lb74;
                                        {
                                            uint64_t t80 = gm_l15;
                                            uint64_t t81 = (uintptr_t)t80;
                                            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                            gm_tk9n3vlila9qg t85 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t81 * 40));
                                            F.l16 = t85;
                                            {
                                                void * t87 = (void *)((uint8_t *)&F.l13);
                                                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 16));
                                                gm_tftb1odc739t5 t89 = gm_f_push_3ef746b(t87, t88);
                                                gm_tftb1odc739t5 t90 = t89;
                                                if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcpsceljs9unl1, 735, 47);
                                                gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                                            }
                                            goto me83;
mn84:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 735, 17);
me83:;
                                        }
lc74:;
                                        {
                                            uint64_t t93 = gm_l15;
                                            uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                            uint64_t t96 = r94;
                                            gm_l15 = t96;
                                        }
                                    }
lb74:;
                                }
                            }
                            {
                                void * t97 = (void *)((uint8_t *)&F.l13);
                                gm_b16_8 t98 = gm_l3;
                                gm_tftb1odc739t5 t99 = gm_f_push_3ef746b(t97, t98);
                                gm_tftb1odc739t5 t100 = t99;
                                if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srcpsceljs9unl1, 736, 17);
                                gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                            }
                            {
                                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                gm_b16_8 t103 = t102;
                                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                                gm_b16_8 t105;
                                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sdbvaa960ee0f));
                                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(4));
                                gm_tabb710d9mq2e t106;
                                gm_f_join_sfknljb((void *)&t106, (void *)NULL, cx, NULL, t104, t105);
                                if (gm_unw.kind) {
                                    goto gm_ll11;
                                }
                                gm_tabb710d9mq2e t107 = t106;
                                if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srcpsceljs9unl1, 737, 60);
                                gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
                                gm_tnpifv20irnph t109;
                                GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                                gm_tabb710d9mq2e t110;
                                gm_f_format_fnh5q7b((void *)&t110, (void *)NULL, cx, NULL, t109);
                                if (gm_unw.kind) {
                                    goto gm_ll11;
                                }
                                gm_tabb710d9mq2e t111 = t110;
                                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcpsceljs9unl1, 737, 25);
                                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                                gm_b16_8 t113 = t112;
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                                gm_tl572d7579hns t115;
                                GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                                ((uint8_t *)&t115)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t115 + 8), t114);
                                gm_tl572d7579hns t116 = t115;
                                uint8_t t117 = ((((uint8_t *)&t116))[0] != 0);
                                F.ret = t115;
                                goto gm_fret;
                            }
                            goto ix59;
ie59:;
ix59:;
                        }
                        {
                            gm_b0_1 t118;
                            gm_tl572d7579hns t119;
                            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t119 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t119 + 16, 0);
                            ((uint8_t *)&t119)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t119 + 8), t118);
                            gm_tl572d7579hns t120 = t119;
                            uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
                            F.ret = t119;
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 727, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t123 = gm_l6;
                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                    uint64_t t126 = r124;
                    gm_l6 = t126;
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
        void * t128 = gm_l0;
        void * t129 = (void *)(((uint8_t *)t128) + 560);
        gm_b16_8 t130 = gm_l1;
        gm_tqetrgo0q24b5 t131;
        gm_f_deps_of_i4no6j9((void *)&t131, t129, t130);
        gm_tqetrgo0q24b5 t132 = t131;
        uint8_t t133;
        if (((((uint8_t *)&t132))[40] != 0)) goto mn135;
        t133 = ((uint8_t)0);
        goto me134;
mn135:;
        t133 = ((uint8_t)1);
        goto me134;
mn136:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 742, 8);
me134:;
        if (!t133) goto ie127;
        {
            gm_b0_1 t138;
            gm_tl572d7579hns t139;
            GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 16, 0);
            ((uint8_t *)&t139)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t139 + 8), t138);
            gm_tl572d7579hns t140 = t139;
            uint8_t t141 = ((((uint8_t *)&t140))[0] != 0);
            F.ret = t139;
            goto gm_fret;
        }
        goto ix127;
ie127:;
ix127:;
    }
    {
        void * t142 = gm_l0;
        void * t143 = (void *)(((uint8_t *)t142) + 600);
        gm_tk9n3vlila9qg t144;
        memset((uint8_t *)&t144 + 33, 0, 7);
        gm_b16_8 t145 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 0, t145);
        gm_b16_8 t146 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 16, t146);
        uint8_t t147;
        if (((((uint8_t *)&F.l4))[56] != 0)) goto mn149;
        t147 = ((uint8_t)0);
        goto me148;
mn149:;
        t147 = ((uint8_t)1);
        goto me148;
mn150:;
        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 743, 62);
me148:;
        GM_ST(uint8_t, (uint8_t *)&t144 + 32, t147);
        gm_tftb1odc739t5 t151 = gm_f_push_fp6nnvl(t143, &t144);
        gm_tftb1odc739t5 t152 = t151;
        if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_srcpsceljs9unl1, 743, 5);
        gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
    }
    {
        gm_f_list_0pjrgaq((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t155 = gm_l2;
            gm_b16_8 t156 = GM_LD(gm_b16_8, ((((uint8_t *)t155) + 48) + 0));
            gm_l18 = t156;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t158 = gm_l19;
                uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t160 = (t158 < t159);
                if (!t160) goto lb157;
                {
                    uint64_t t163 = gm_l19;
                    uint64_t t164 = (uintptr_t)t163;
                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_t7tiv6vif86qg t168 = GM_LD(gm_t7tiv6vif86qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t164 * 120));
                    F.l20 = t168;
                    {
                        uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                        switch (t175) { case ((uint8_t)0ULL): goto ma171; case ((uint8_t)1ULL): goto ma172; case ((uint8_t)2ULL): goto ma173; default: goto ma174; }
ma171:;
                        uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                        if (t177 != ((uint8_t)0ULL)) goto mn176;
                        gm_b16_8 t178 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l20) + 24) + 8));
                        gm_l21 = t178;
                        {
                            gm_b16_8 t180 = gm_l1;
                            gm_b16_8 t181 = gm_l21;
                            gm_b16_8 t182 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t180, t181);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_b16_8 t183 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t182);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_l22 = t183;
                        }
                        {
                            gm_b16_8 t184 = gm_l22;
                            gm_f_canonicalize_epo7cln((void *)&gm_l23, (void *)NULL, cx, NULL, t184);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                        }
                        {
                            if (!((((uint8_t *)&F.l4))[56] != 0)) goto ie185;
                            gm_tbqru6ibafrpr t186 = GM_LD(gm_tbqru6ibafrpr, ((uint8_t *)&F.l4));
                            F.l24 = t186;
                            {
                                if (!((((uint8_t *)&gm_l23))[16] != 0)) goto ie188;
                                gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                gm_l25 = t189;
                                {
                                    gm_b16_8 t192 = gm_l25;
                                    gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 40));
                                    uint8_t t194 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t192, t193);
                                    if (gm_unw.kind) {
                                        goto gm_ll161;
                                    }
                                    uint8_t t195 = (uint8_t)!t194;
                                    if (!t195) goto ie191;
                                    {
                                        gm_tfeab279c16rr t197;
                                        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t198);
                                        gm_b16_8 t199 = gm_l3;
                                        GM_ST(gm_b16_8, (uint8_t *)&t197 + 16, t199);
                                        gm_b16_8 t200 = gm_l21;
                                        GM_ST(gm_b16_8, (uint8_t *)&t197 + 32, t200);
                                        gm_tabb710d9mq2e t201;
                                        gm_f_format_0c1hioc((void *)&t201, (void *)NULL, cx, NULL, &t197);
                                        if (gm_unw.kind) {
                                            goto gm_ll161;
                                        }
                                        gm_tabb710d9mq2e t202 = t201;
                                        if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 8, gm_srcpsceljs9unl1, 753, 37);
                                        gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&t202) + 8));
                                        gm_b16_8 t204 = t203;
                                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                                        gm_tl572d7579hns t206;
                                        GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                                        ((uint8_t *)&t206)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t206 + 8), t205);
                                        gm_tl572d7579hns t207 = t206;
                                        uint8_t t208 = ((((uint8_t *)&t207))[0] != 0);
                                        F.ret = t206;
                                        goto gm_fret;
                                    }
                                    goto ix191;
ie191:;
ix191:;
                                }
                                goto ix188;
ie188:;
                                {
                                    gm_tfeab279c16rr t210;
                                    gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t210 + 0, t211);
                                    gm_b16_8 t212 = gm_l3;
                                    GM_ST(gm_b16_8, (uint8_t *)&t210 + 16, t212);
                                    gm_b16_8 t213 = gm_l21;
                                    GM_ST(gm_b16_8, (uint8_t *)&t210 + 32, t213);
                                    gm_tabb710d9mq2e t214;
                                    gm_f_format_o83d7mn((void *)&t214, (void *)NULL, cx, NULL, &t210);
                                    if (gm_unw.kind) {
                                        goto gm_ll161;
                                    }
                                    gm_tabb710d9mq2e t215 = t214;
                                    if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 8, gm_srcpsceljs9unl1, 756, 33);
                                    gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t215) + 8));
                                    gm_b16_8 t217 = t216;
                                    gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                                    gm_tl572d7579hns t219;
                                    GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                                    ((uint8_t *)&t219)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t219 + 8), t218);
                                    gm_tl572d7579hns t220 = t219;
                                    uint8_t t221 = ((((uint8_t *)&t220))[0] != 0);
                                    F.ret = t219;
                                    goto gm_fret;
                                }
ix188:;
                            }
                            goto ix185;
ie185:;
ix185:;
                        }
                        {
                            gm_th2lqbo77pvsc t222 = gm_l23;
                            gm_th2lqbo77pvsc t223 = t222;
                            gm_b16_8 t224;
                            if (((((uint8_t *)&t223))[16] != 0)) {
                                gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t223) + 0));
                                t224 = t225;
                            } else {
                                gm_b16_8 t226 = gm_l22;
                                t224 = t226;
                            }
                            gm_l26 = t224;
                        }
                        {
                            void * t227 = (void *)((uint8_t *)&F.l17);
                            gm_t6j6smj7qoctu t228;
                            gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t229);
                            gm_b16_8 t230 = gm_l26;
                            GM_ST(gm_b16_8, (uint8_t *)&t228 + 16, t230);
                            gm_tftb1odc739t5 t231 = gm_f_push_i535kr1(t227, &t228);
                            gm_tftb1odc739t5 t232 = t231;
                            if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 1, gm_srcpsceljs9unl1, 760, 17);
                            gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t232) + 1));
                        }
                        {
                            uint8_t t235;
                            if (((((uint8_t *)&gm_l23))[16] != 0)) goto mn237;
                            t235 = ((uint8_t)0);
                            goto me236;
mn237:;
                            t235 = ((uint8_t)1);
                            goto me236;
mn238:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 761, 20);
me236:;
                            if (!t235) goto ie234;
                            gm_b16_8 t239 = gm_l26;
                            gm_b16_8 t240;
                            GM_ST(void *, (uint8_t *)&t240, (void *)(gm_se24c7hpp401n));
                            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(10));
                            gm_b16_8 t241 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t239, t240);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            uint8_t t242 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t241);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            if (!t242) goto ie234;
                            {
                                gm_b16_8 t244 = gm_l26;
                                gm_tikairc198460 t245;
                                gm_f_read_manifest_u9uajvn((void *)&t245, (void *)NULL, cx, NULL, t244);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tikairc198460 t246 = t245;
                                if (((((uint8_t *)&t246))[0] != 0)) {
                                    gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&t246) + 8));
                                    gm_tl572d7579hns t248;
                                    GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
                                    ((uint8_t *)&t248)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t248 + 8), t247);
                                    F.ret = t248;
                                    goto gm_fret;
                                }
                                gm_tm43kqe3e8b4a t249 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t246) + 8));
                                F.l27 = t249;
                            }
                            {
                                void * t250 = gm_l0;
                                gm_b16_8 t251 = gm_l26;
                                void * t252 = (void *)((uint8_t *)&F.l27);
                                gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                gm_tl572d7579hns t254;
                                gm_f_walk_59naf6k((void *)&t254, (void *)NULL, cx, NULL, t250, t251, t252, t253, &(*(gm_tqa5rsrlv432m *)((uint8_t *)&F.l4)));
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tl572d7579hns t255 = t254;
                                if (((((uint8_t *)&t255))[0] != 0)) {
                                    gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
                                    gm_tl572d7579hns t257;
                                    GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                                    ((uint8_t *)&t257)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t257 + 8), t256);
                                    F.ret = t257;
                                    goto gm_fret;
                                }
                                gm_b0_1 t258 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 8));
                            }
                            goto ix234;
ie234:;
ix234:;
                        }
                        goto me170;
mn176:;
ma172:;
                        uint8_t t260 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                        if (t260 != ((uint8_t)1ULL)) goto mn259;
                        gm_te5jk64pjl28k t261 = GM_LD(gm_te5jk64pjl28k, ((((uint8_t *)&F.l20) + 24) + 8));
                        F.l28 = t261;
                        {
                            gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                            gm_b16_8 t264 = gm_f_normalize_url_faghdja(t263);
                            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 0, t264);
                            memcpy((uint8_t *)&F.l29 + 16, (((uint8_t *)&F.l28) + 64), 24);
                        }
                        {
                            void * t265 = gm_l0;
                            gm_t2f6hp2vik01f t266;
                            gm_b16_8 t267 = gm_l3;
                            GM_ST(gm_b16_8, (uint8_t *)&t266 + 0, t267);
                            gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t266 + 16, t268);
                            gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t266 + 32, t269);
                            memcpy((uint8_t *)&t266 + 48, (((uint8_t *)&F.l28) + 16), 48);
                            gm_b0_1 t270 = gm_f_add_req_klu87ju((void *)NULL, cx, NULL, t265, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)), &t266);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                        }
                        {
                            void * t271 = gm_l0;
                            gm_tmfqp43enl3pa t272;
                            gm_f_choose_9ee155u((void *)&t272, (void *)NULL, cx, NULL, t271, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tmfqp43enl3pa t273 = t272;
                            if (((((uint8_t *)&t273))[0] != 0)) {
                                gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)&t273) + 8));
                                gm_tl572d7579hns t275;
                                GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
                                ((uint8_t *)&t275)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t275 + 8), t274);
                                F.ret = t275;
                                goto gm_fret;
                            }
                            gm_t8qqrlhb610a2 t276 = GM_LD(gm_t8qqrlhb610a2, (((uint8_t *)&t273) + 8));
                            F.l30 = t276;
                        }
                        {
                            void * t277 = gm_l0;
                            gm_t3ujhu6qph7de t278;
                            gm_f_ensure_checkout_hvmsso1((void *)&t278, (void *)NULL, cx, NULL, t277, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)), &(*(gm_t8qqrlhb610a2 *)((uint8_t *)&F.l30)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_t3ujhu6qph7de t279 = t278;
                            if (((((uint8_t *)&t279))[0] != 0)) {
                                gm_b16_8 t280 = GM_LD(gm_b16_8, (((uint8_t *)&t279) + 8));
                                gm_tl572d7579hns t281;
                                GM_ST(uint8_t, (uint8_t *)&t281 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t281 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t281 + 4, 0);
                                ((uint8_t *)&t281)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t281 + 8), t280);
                                F.ret = t281;
                                goto gm_fret;
                            }
                            gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&t279) + 8));
                            gm_l31 = t282;
                        }
                        {
                            gm_b16_8 t283 = gm_l31;
                            gm_th2lqbo77pvsc t284;
                            gm_f_subdir_of_gjqk3qb((void *)&t284, (void *)NULL, cx, NULL, t283, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l28) + 64)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_th2lqbo77pvsc t285 = t284;
                            gm_b16_8 t286;
                            if (((((uint8_t *)&t285))[16] != 0)) {
                                gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&t285) + 0));
                                t286 = t287;
                            } else {
                                gm_tfeab279c16rr t289;
                                gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t289 + 0, t290);
                                gm_b16_8 t291 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t289 + 16, t291);
                                gm_th2lqbo77pvsc t292 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l28) + 64));
                                gm_th2lqbo77pvsc t293 = t292;
                                gm_b16_8 t294;
                                if (((((uint8_t *)&t293))[16] != 0)) {
                                    gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&t293) + 0));
                                    t294 = t295;
                                } else {
                                    gm_b16_8 t296;
                                    GM_ST(void *, (uint8_t *)&t296, (void *)(gm_sr4qbrk6tlgaa));
                                    GM_ST(uintptr_t, (uint8_t *)&t296 + 8, (uintptr_t)(0));
                                    t294 = t296;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t289 + 32, t294);
                                gm_tabb710d9mq2e t297;
                                gm_f_format_lbar0n6((void *)&t297, (void *)NULL, cx, NULL, &t289);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tabb710d9mq2e t298 = t297;
                                if (((((uint8_t *)&t298))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t298) + 8, gm_srcpsceljs9unl1, 772, 29);
                                gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&t298) + 8));
                                gm_b16_8 t300 = t299;
                                gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
                                gm_tl572d7579hns t302;
                                GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                                ((uint8_t *)&t302)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t302 + 8), t301);
                                gm_tl572d7579hns t303 = t302;
                                uint8_t t304 = ((((uint8_t *)&t303))[0] != 0);
                                F.ret = t302;
                                goto gm_fret;
                            }
                            gm_l32 = t286;
                        }
                        {
                            gm_b16_8 t306 = gm_l32;
                            gm_b16_8 t307;
                            GM_ST(void *, (uint8_t *)&t307, (void *)(gm_se24c7hpp401n));
                            GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(10));
                            gm_b16_8 t308 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t306, t307);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            uint8_t t309 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t308);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            uint8_t t310 = (uint8_t)!t309;
                            if (!t310) goto ie305;
                            {
                                gm_t6j6smj7qoctu t312;
                                gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t312 + 0, t313);
                                gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t312 + 16, t314);
                                gm_tabb710d9mq2e t315;
                                gm_f_format_ro2n9te((void *)&t315, (void *)NULL, cx, NULL, &t312);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tabb710d9mq2e t316 = t315;
                                if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 8, gm_srcpsceljs9unl1, 775, 29);
                                gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&t316) + 8));
                                gm_b16_8 t318 = t317;
                                gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
                                gm_tl572d7579hns t320;
                                GM_ST(uint8_t, (uint8_t *)&t320 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t320 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t320 + 4, 0);
                                ((uint8_t *)&t320)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t320 + 8), t319);
                                gm_tl572d7579hns t321 = t320;
                                uint8_t t322 = ((((uint8_t *)&t321))[0] != 0);
                                F.ret = t320;
                                goto gm_fret;
                            }
                            goto ix305;
ie305:;
ix305:;
                        }
                        {
                            void * t323 = gm_l0;
                            gm_b0_1 t324 = gm_f_record_dep_84vd3la((void *)NULL, cx, NULL, t323, &(*(gm_tqa5rsrlv432m *)((uint8_t *)&F.l4)), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                        }
                        {
                            void * t325 = (void *)((uint8_t *)&F.l17);
                            gm_t6j6smj7qoctu t326;
                            gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t326 + 0, t327);
                            gm_b16_8 t328 = gm_l32;
                            GM_ST(gm_b16_8, (uint8_t *)&t326 + 16, t328);
                            gm_tftb1odc739t5 t329 = gm_f_push_i535kr1(t325, &t326);
                            gm_tftb1odc739t5 t330 = t329;
                            if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 1, gm_srcpsceljs9unl1, 778, 17);
                            gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 1));
                        }
                        {
                            gm_b16_8 t332 = gm_l32;
                            gm_tikairc198460 t333;
                            gm_f_read_manifest_u9uajvn((void *)&t333, (void *)NULL, cx, NULL, t332);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tikairc198460 t334 = t333;
                            if (((((uint8_t *)&t334))[0] != 0)) {
                                gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&t334) + 8));
                                gm_tl572d7579hns t336;
                                GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
                                ((uint8_t *)&t336)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t336 + 8), t335);
                                F.ret = t336;
                                goto gm_fret;
                            }
                            gm_tm43kqe3e8b4a t337 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t334) + 8));
                            F.l33 = t337;
                        }
                        {
                            if (!(((((uint8_t *)&F.l33) + 0))[16] != 0)) goto ie338;
                            gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                            gm_l34 = t339;
                            {
                                void * t341 = gm_l0;
                                void * t342 = (void *)(((uint8_t *)t341) + 472);
                                gm_b16_8 t343 = gm_l34;
                                gm_b0_1 t344 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t342, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l29)), t343);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                            }
                            goto ix338;
ie338:;
ix338:;
                        }
                        {
                            void * t345 = gm_l0;
                            gm_b16_8 t346 = gm_l32;
                            void * t347 = (void *)((uint8_t *)&F.l33);
                            gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            gm_tbqru6ibafrpr t349;
                            memcpy((uint8_t *)&t349 + 0, ((uint8_t *)&F.l29), 40);
                            gm_b16_8 t350 = gm_l31;
                            GM_ST(gm_b16_8, (uint8_t *)&t349 + 40, t350);
                            gm_tqa5rsrlv432m t351;
                            GM_ST(uint8_t, (uint8_t *)&t351 + 57, 0);
                            GM_ST(uint16_t, (uint8_t *)&t351 + 58, 0);
                            GM_ST(uint32_t, (uint8_t *)&t351 + 60, 0);
                            GM_ST(gm_tbqru6ibafrpr, ((uint8_t *)&t351), t349);
                            ((uint8_t *)&t351)[56] = 1;
                            gm_tl572d7579hns t352;
                            gm_f_walk_59naf6k((void *)&t352, (void *)NULL, cx, NULL, t345, t346, t347, t348, &t351);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tl572d7579hns t353 = t352;
                            if (((((uint8_t *)&t353))[0] != 0)) {
                                gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&t353) + 8));
                                gm_tl572d7579hns t355;
                                GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
                                ((uint8_t *)&t355)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t355 + 8), t354);
                                F.ret = t355;
                                goto gm_fret;
                            }
                            gm_b0_1 t356 = GM_LD(gm_b0_1, (((uint8_t *)&t353) + 8));
                        }
                        goto me170;
mn259:;
ma173:;
                        uint8_t t358 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 24) + 0));
                        if (t358 != ((uint8_t)2ULL)) goto mn357;
                        gm_tt008mgut8glb t359 = GM_LD(gm_tt008mgut8glb, ((((uint8_t *)&F.l20) + 24) + 8));
                        F.l35 = t359;
                        {
                            void * t361 = gm_l0;
                            gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            gm_tq6bq450l0idg t363;
                            gm_f_ensure_archive_70kh53q((void *)&t363, (void *)NULL, cx, NULL, t361, t362, &(*(gm_tt008mgut8glb *)((uint8_t *)&F.l35)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tq6bq450l0idg t364 = t363;
                            if (((((uint8_t *)&t364))[0] != 0)) {
                                gm_b16_8 t365 = GM_LD(gm_b16_8, (((uint8_t *)&t364) + 8));
                                gm_tl572d7579hns t366;
                                GM_ST(uint8_t, (uint8_t *)&t366 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t366 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t366 + 4, 0);
                                ((uint8_t *)&t366)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t366 + 8), t365);
                                F.ret = t366;
                                goto gm_fret;
                            }
                            gm_t0uuhaljsf410 t367 = GM_LD(gm_t0uuhaljsf410, (((uint8_t *)&t364) + 8));
                            F.l36 = t367;
                        }
                        {
                            gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                            gm_tnpifv20irnph t369;
                            GM_ST(gm_b16_8, ((uint8_t *)&t369 + 0), t368);
                            gm_tabb710d9mq2e t370;
                            gm_f_format_s20j3pl((void *)&t370, (void *)NULL, cx, NULL, t369);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tabb710d9mq2e t371 = t370;
                            if (((((uint8_t *)&t371))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t371) + 8, gm_srcpsceljs9unl1, 785, 33);
                            gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)&t371) + 8));
                            gm_b16_8 t373 = t372;
                            gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&t373));
                            GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 0, t374);
                            memcpy((uint8_t *)&F.l37 + 16, (((uint8_t *)&F.l35) + 40), 24);
                        }
                        {
                            gm_b16_8 t375 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 32));
                            gm_l38 = t375;
                        }
                        {
                            gm_b16_8 t376 = gm_l38;
                            gm_th2lqbo77pvsc t377;
                            gm_f_subdir_of_gjqk3qb((void *)&t377, (void *)NULL, cx, NULL, t376, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l35) + 40)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_th2lqbo77pvsc t378 = t377;
                            gm_b16_8 t379;
                            if (((((uint8_t *)&t378))[16] != 0)) {
                                gm_b16_8 t380 = GM_LD(gm_b16_8, (((uint8_t *)&t378) + 0));
                                t379 = t380;
                            } else {
                                gm_tfeab279c16rr t382;
                                gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t382 + 0, t383);
                                gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t382 + 16, t384);
                                gm_th2lqbo77pvsc t385 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l35) + 40));
                                gm_th2lqbo77pvsc t386 = t385;
                                gm_b16_8 t387;
                                if (((((uint8_t *)&t386))[16] != 0)) {
                                    gm_b16_8 t388 = GM_LD(gm_b16_8, (((uint8_t *)&t386) + 0));
                                    t387 = t388;
                                } else {
                                    gm_b16_8 t389;
                                    GM_ST(void *, (uint8_t *)&t389, (void *)(gm_sr4qbrk6tlgaa));
                                    GM_ST(uintptr_t, (uint8_t *)&t389 + 8, (uintptr_t)(0));
                                    t387 = t389;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t382 + 32, t387);
                                gm_tabb710d9mq2e t390;
                                gm_f_format_lbar0n6((void *)&t390, (void *)NULL, cx, NULL, &t382);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tabb710d9mq2e t391 = t390;
                                if (((((uint8_t *)&t391))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t391) + 8, gm_srcpsceljs9unl1, 788, 29);
                                gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)&t391) + 8));
                                gm_b16_8 t393 = t392;
                                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                                gm_tl572d7579hns t395;
                                GM_ST(uint8_t, (uint8_t *)&t395 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t395 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t395 + 4, 0);
                                ((uint8_t *)&t395)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t395 + 8), t394);
                                gm_tl572d7579hns t396 = t395;
                                uint8_t t397 = ((((uint8_t *)&t396))[0] != 0);
                                F.ret = t395;
                                goto gm_fret;
                            }
                            gm_l39 = t379;
                        }
                        {
                            gm_b16_8 t399 = gm_l39;
                            gm_b16_8 t400;
                            GM_ST(void *, (uint8_t *)&t400, (void *)(gm_se24c7hpp401n));
                            GM_ST(uintptr_t, (uint8_t *)&t400 + 8, (uintptr_t)(10));
                            gm_b16_8 t401 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t399, t400);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            uint8_t t402 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t401);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            uint8_t t403 = (uint8_t)!t402;
                            if (!t403) goto ie398;
                            {
                                gm_t6j6smj7qoctu t405;
                                gm_b16_8 t406 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t405 + 0, t406);
                                gm_b16_8 t407 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t405 + 16, t407);
                                gm_tabb710d9mq2e t408;
                                gm_f_format_ro2n9te((void *)&t408, (void *)NULL, cx, NULL, &t405);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                gm_tabb710d9mq2e t409 = t408;
                                if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 8, gm_srcpsceljs9unl1, 791, 29);
                                gm_b16_8 t410 = GM_LD(gm_b16_8, (((uint8_t *)&t409) + 8));
                                gm_b16_8 t411 = t410;
                                gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
                                gm_tl572d7579hns t413;
                                GM_ST(uint8_t, (uint8_t *)&t413 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t413 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t413 + 4, 0);
                                ((uint8_t *)&t413)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t413 + 8), t412);
                                gm_tl572d7579hns t414 = t413;
                                uint8_t t415 = ((((uint8_t *)&t414))[0] != 0);
                                F.ret = t413;
                                goto gm_fret;
                            }
                            goto ix398;
ie398:;
ix398:;
                        }
                        {
                            void * t416 = gm_l0;
                            gm_b0_1 t417 = gm_f_record_dep_84vd3la((void *)NULL, cx, NULL, t416, &(*(gm_tqa5rsrlv432m *)((uint8_t *)&F.l4)), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l37)));
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                        }
                        {
                            void * t418 = (void *)((uint8_t *)&F.l17);
                            gm_t6j6smj7qoctu t419;
                            gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t419 + 0, t420);
                            gm_b16_8 t421 = gm_l39;
                            GM_ST(gm_b16_8, (uint8_t *)&t419 + 16, t421);
                            gm_tftb1odc739t5 t422 = gm_f_push_i535kr1(t418, &t419);
                            gm_tftb1odc739t5 t423 = t422;
                            if (((((uint8_t *)&t423))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t423) + 1, gm_srcpsceljs9unl1, 794, 17);
                            gm_b0_1 t424 = GM_LD(gm_b0_1, (((uint8_t *)&t423) + 1));
                        }
                        {
                            gm_b16_8 t425 = gm_l39;
                            gm_tikairc198460 t426;
                            gm_f_read_manifest_u9uajvn((void *)&t426, (void *)NULL, cx, NULL, t425);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tikairc198460 t427 = t426;
                            if (((((uint8_t *)&t427))[0] != 0)) {
                                gm_b16_8 t428 = GM_LD(gm_b16_8, (((uint8_t *)&t427) + 8));
                                gm_tl572d7579hns t429;
                                GM_ST(uint8_t, (uint8_t *)&t429 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t429 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t429 + 4, 0);
                                ((uint8_t *)&t429)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t429 + 8), t428);
                                F.ret = t429;
                                goto gm_fret;
                            }
                            gm_tm43kqe3e8b4a t430 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t427) + 8));
                            F.l40 = t430;
                        }
                        {
                            void * t431 = gm_l0;
                            void * t432 = (void *)(((uint8_t *)t431) + 472);
                            gm_t6ok5tvcjc200 t433 = F.l37;
                            gm_th2lqbo77pvsc t434 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l40) + 0));
                            gm_th2lqbo77pvsc t435 = t434;
                            gm_b16_8 t436;
                            if (((((uint8_t *)&t435))[16] != 0)) {
                                gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)&t435) + 0));
                                t436 = t437;
                            } else {
                                gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                                t436 = t438;
                            }
                            gm_b0_1 t439 = gm_f_put_1jc6f83((void *)NULL, cx, NULL, t432, &t433, t436);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                        }
                        {
                            void * t440 = gm_l0;
                            void * t441 = (void *)(((uint8_t *)t440) + 272);
                            gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            gm_tftb1odc739t5 t443 = gm_f_push_3ef746b(t441, t442);
                            gm_tftb1odc739t5 t444 = t443;
                            if (((((uint8_t *)&t444))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t444) + 1, gm_srcpsceljs9unl1, 797, 17);
                            gm_b0_1 t445 = GM_LD(gm_b0_1, (((uint8_t *)&t444) + 1));
                        }
                        {
                            gm_l41 = ((uint8_t)0);
                        }
                        {
                            {
                                void * t447 = gm_l0;
                                gm_b16_8 t448 = GM_LD(gm_b16_8, ((((uint8_t *)t447) + 352) + 0));
                                gm_l42 = t448;
                            }
                            {
                                gm_l43 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t450 = gm_l43;
                                    uint64_t t451 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                    uint8_t t452 = (t450 < t451);
                                    if (!t452) goto lb449;
                                    {
                                        uint64_t t455 = gm_l43;
                                        uint64_t t456 = (uintptr_t)t455;
                                        uint64_t t457 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        gm_t6ok5tvcjc200 t460 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t456 * 40));
                                        F.l44 = t460;
                                        {
                                            uint8_t t463 = gm_f_key_equal_n71njnf(&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l44)), &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l37)));
                                            if (!t463) goto ie462;
                                            {
                                                gm_l41 = ((uint8_t)1);
                                            }
                                            goto ix462;
ie462:;
ix462:;
                                        }
                                        goto me458;
mn459:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 799, 17);
me458:;
                                    }
lc449:;
                                    {
                                        uint64_t t466 = gm_l43;
                                        uint64_t r467; bool o468 = __builtin_add_overflow(t466, ((uint64_t)1ULL), &r467);
                                        uint64_t t469 = r467;
                                        gm_l43 = t469;
                                    }
                                }
lb449:;
                            }
                        }
                        {
                            uint8_t t471 = gm_l41;
                            uint8_t t472 = (uint8_t)!t471;
                            if (!t472) goto ie470;
                            {
                                void * t474 = gm_l0;
                                void * t475 = (void *)(((uint8_t *)t474) + 352);
                                gm_tftb1odc739t5 t476 = gm_f_push_d61dqrj(t475, &(*(gm_t6ok5tvcjc200 *)((uint8_t *)&F.l37)));
                                gm_tftb1odc739t5 t477 = t476;
                                if (((((uint8_t *)&t477))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t477) + 1, gm_srcpsceljs9unl1, 802, 30);
                                gm_b0_1 t478 = GM_LD(gm_b0_1, (((uint8_t *)&t477) + 1));
                            }
                            goto ix470;
ie470:;
ix470:;
                        }
                        {
                            void * t479 = gm_l0;
                            gm_b16_8 t480 = gm_l39;
                            void * t481 = (void *)((uint8_t *)&F.l40);
                            gm_b16_8 t482 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            gm_tbqru6ibafrpr t483;
                            memcpy((uint8_t *)&t483 + 0, ((uint8_t *)&F.l37), 40);
                            gm_b16_8 t484 = gm_l38;
                            GM_ST(gm_b16_8, (uint8_t *)&t483 + 40, t484);
                            gm_tqa5rsrlv432m t485;
                            GM_ST(uint8_t, (uint8_t *)&t485 + 57, 0);
                            GM_ST(uint16_t, (uint8_t *)&t485 + 58, 0);
                            GM_ST(uint32_t, (uint8_t *)&t485 + 60, 0);
                            GM_ST(gm_tbqru6ibafrpr, ((uint8_t *)&t485), t483);
                            ((uint8_t *)&t485)[56] = 1;
                            gm_tl572d7579hns t486;
                            gm_f_walk_59naf6k((void *)&t486, (void *)NULL, cx, NULL, t479, t480, t481, t482, &t485);
                            if (gm_unw.kind) {
                                goto gm_ll161;
                            }
                            gm_tl572d7579hns t487 = t486;
                            if (((((uint8_t *)&t487))[0] != 0)) {
                                gm_b16_8 t488 = GM_LD(gm_b16_8, (((uint8_t *)&t487) + 8));
                                gm_tl572d7579hns t489;
                                GM_ST(uint8_t, (uint8_t *)&t489 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t489 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t489 + 4, 0);
                                ((uint8_t *)&t489)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t489 + 8), t488);
                                F.ret = t489;
                                goto gm_fret;
                            }
                            gm_b0_1 t490 = GM_LD(gm_b0_1, (((uint8_t *)&t487) + 8));
                        }
                        goto me170;
mn357:;
ma174:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 746, 9);
me170:;
                    }
                    goto me166;
mn167:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpsceljs9unl1, 745, 5);
me166:;
                }
lc157:;
                {
                    uint64_t t492 = gm_l19;
                    uint64_t r493; bool o494 = __builtin_add_overflow(t492, ((uint64_t)1ULL), &r493);
                    uint64_t t495 = r493;
                    gm_l19 = t495;
                }
            }
gm_ll161:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb157; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc157; }
            }
            goto gm_lad;
lb157:;
        }
    }
    {
        {
            void * t497 = gm_l0;
            void * t498 = (void *)(((uint8_t *)t497) + 600);
            gm_ta2tdjm589013 t499;
            gm_f_pop_rnfkbb9((void *)&t499, t498);
        }
    }
    {
        void * t500 = gm_l0;
        void * t501 = (void *)((((uint8_t *)t500) + 560) + 0);
        gm_tl7hpf5nlilqq t502;
        gm_b16_8 t503 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t502 + 0, t503);
        memcpy((uint8_t *)&t502 + 16, ((uint8_t *)&F.l17), 40);
        gm_tftb1odc739t5 t504 = gm_f_push_7mbittu(t501, &t502);
        gm_tftb1odc739t5 t505 = t504;
        if (((((uint8_t *)&t505))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t505) + 1, gm_srcpsceljs9unl1, 808, 5);
        gm_b0_1 t506 = GM_LD(gm_b0_1, (((uint8_t *)&t505) + 1));
    }
    {
        gm_b0_1 t507;
        gm_tl572d7579hns t508;
        GM_ST(uint8_t, (uint8_t *)&t508 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t508 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t508 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t508 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t508 + 16, 0);
        ((uint8_t *)&t508)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t508 + 8), t507);
        gm_tl572d7579hns t509 = t508;
        uint8_t t510 = ((((uint8_t *)&t509))[0] != 0);
        F.ret = t508;
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
