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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_layouts_au8nfbt {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_t4grvdhp6dkf1 ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_pointer_words_6hm9th1 {
    gm_frame h;
    gm_tpautr4br7h3l l19; /* def */
    gm_t0mglksrkfa5e l24; /* f */
    uint8_t ret;
};
struct gm_fr_may_hold_pointers_iuorcah {
    gm_frame h;
    gm_tpautr4br7h3l l11; /* def */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t0mglksrkfa5e l18; /* f */
    gm_tckdobunaulee l22; /* v */
    uint8_t ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pointer_at_0dc5rq1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pointer_words_ljph5no {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* words */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_t4grvdhp6dkf1 gm_f_layouts_au8nfbt_c(void * a0, gm_t0rfe1ou14h21 a1);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_mark_pointer_words_6hm9th1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_may_hold_pointers_iuorcah(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_pointer_at_0dc5rq1(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_pointer_words_ljph5no(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcochlrb3nk64u[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_jcnt4hm F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
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
    memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56), 56);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_layouts_au8nfbt F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy((uint8_t *)&(*(gm_t4grvdhp6dkf1 *)gm_out) + 0, ((uint8_t *)a1), 64);
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_t4grvdhp6dkf1 *)gm_out) + 64, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_s1lbe5s();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hkmvuvc(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
extern __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_t4grvdhp6dkf1 gm_f_layouts_au8nfbt_c(void * a0, gm_t0rfe1ou14h21 a1) { gm_t4grvdhp6dkf1 gm_r; gm_f_layouts_au8nfbt(&gm_r, a0, &a1); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_mark_pointer_words_6hm9th1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_mark_pointer_words_6hm9th1 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* off */
    gm_b16_8 gm_l3; /* words */
    uint64_t gm_l4; /* p */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    uint64_t gm_l6; /* len */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    uint64_t gm_l8; /* size */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l12; /* elems */
    gm_b16_8 gm_l13; /* offs */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    gm_t88rcucg1b7uk gm_l17; /* e */
    uint32_t gm_l18; /* id */
    gm_b16_8 gm_l20; /* offs */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    GM_STACK_CHECK(&F, 1543u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 0) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 64));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma17; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma17; default: goto ma17; }
ma10:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t23 != ((uint8_t)7ULL)) goto pa22;
        goto po21;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t25 != ((uint8_t)8ULL)) goto pa24;
        goto po21;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t27 != ((uint8_t)9ULL)) goto pa26;
        goto po21;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t29 != ((uint8_t)16ULL)) goto pa28;
        goto po21;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t31 != ((uint8_t)10ULL)) goto pa30;
        goto po21;
pa30:;
        goto mn20;
po21:;
        gm_b16_8 t32 = gm_l3;
        uint64_t t33 = gm_l2;
        uint64_t t34 = gm_l4;
        uint8_t t35 = gm_f_pointer_at_0dc5rq1(t32, t33, t34);
        uint8_t t36 = 0;
        F.ret = t35;
        goto gm_fret;
        goto me9;
mn20:;
ma11:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t38 != ((uint8_t)13ULL)) goto mn37;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 8));
        gm_l5 = t39;
        {
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 64));
            gm_t88rcucg1b7uk t44 = gm_l5;
            uint8_t t45 = gm_f_nullable_pointer_7qfmn6o(t43, GM_CONV(gm_csguankko4lmhd, t44));
            if (!t45) goto ie41;
            {
                gm_b16_8 t47 = gm_l3;
                uint64_t t48 = gm_l2;
                uint64_t t49 = gm_l4;
                uint8_t t50 = gm_f_pointer_at_0dc5rq1(t47, t48, t49);
                uint8_t t51 = 0;
                F.ret = t50;
                goto gm_fret;
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            void * t52 = gm_l0;
            gm_t88rcucg1b7uk t53 = gm_l1;
            uint8_t t54 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = (uint8_t)!t54;
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto me9;
mn37:;
ma12:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t58 != ((uint8_t)15ULL)) goto mn57;
        uint8_t t59 = 0;
        uint8_t t60 = 0;
        gm_b16_8 t61 = gm_l3;
        uint64_t t62 = gm_l2;
        uint64_t t63 = gm_l4;
        uint8_t t64 = gm_f_pointer_at_0dc5rq1(t61, t62, t63);
        if (t64) {
            gm_b16_8 t65 = gm_l3;
            uint64_t t66 = gm_l2;
            uint64_t t67 = gm_l4;
            uint64_t r68; bool o69 = __builtin_add_overflow(t66, t67, &r68);
            if (o69) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 88, 74);
            uint64_t t70 = r68;
            uint64_t t71 = gm_l4;
            uint8_t t72 = gm_f_pointer_at_0dc5rq1(t65, t70, t71);
            t60 = t72;
        } else t60 = 0;
        if (t60) {
            gm_b16_8 t73 = gm_l3;
            uint64_t t74 = gm_l2;
            uint64_t t75 = gm_l4;
            uint64_t r76; bool o77 = __builtin_mul_overflow(((uint64_t)2ULL), t75, &r76);
            if (o77) gm_trap("integer overflow in `*`", gm_srcochlrb3nk64u, 88, 114);
            uint64_t t78 = r76;
            uint64_t r79; bool o80 = __builtin_add_overflow(t74, t78, &r79);
            if (o80) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 88, 108);
            uint64_t t81 = r79;
            uint64_t t82 = gm_l4;
            uint8_t t83 = gm_f_pointer_at_0dc5rq1(t73, t81, t82);
            t59 = t83;
        } else t59 = 0;
        uint8_t t84 = 0;
        F.ret = t59;
        goto gm_fret;
        goto me9;
mn57:;
ma13:;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t86 != ((uint8_t)25ULL)) goto mn85;
        uint8_t t87 = 0;
        gm_b16_8 t88 = gm_l3;
        uint64_t t89 = gm_l2;
        uint64_t t90 = gm_l4;
        uint8_t t91 = gm_f_pointer_at_0dc5rq1(t88, t89, t90);
        if (t91) {
            gm_b16_8 t92 = gm_l3;
            uint64_t t93 = gm_l2;
            uint64_t t94 = gm_l4;
            uint64_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
            if (o96) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 89, 79);
            uint64_t t97 = r95;
            uint64_t t98 = gm_l4;
            uint8_t t99 = gm_f_pointer_at_0dc5rq1(t92, t97, t98);
            t87 = t99;
        } else t87 = 0;
        uint8_t t100 = 0;
        F.ret = t87;
        goto gm_fret;
        goto me9;
mn85:;
ma14:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t102 != ((uint8_t)11ULL)) goto mn101;
        uint64_t t103 = GM_LD(uint64_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l6 = t103;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 8));
        gm_l7 = t104;
        {
            void * t106 = gm_l0;
            gm_t88rcucg1b7uk t107 = gm_l7;
            gm_tbccvikg6b96e t108;
            gm_f_layout_of_e6hfj1r((void *)&t108, (void *)NULL, cx, NULL, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&t108) + 0));
            gm_l8 = t109;
        }
        {
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                uint64_t t111 = gm_l6;
                gm_l9 = t111;
            }
            {
                for (;;) {
                    uint64_t t113 = gm_l10;
                    uint64_t t114 = gm_l9;
                    uint8_t t115 = (t113 < t114);
                    if (!t115) goto lb112;
                    {
                        uint64_t t118 = gm_l10;
                        uint64_t t121 = GM_LD(uint64_t, ((uint8_t *)&t118));
                        gm_l11 = t121;
                        {
                            void * t124 = gm_l0;
                            gm_t88rcucg1b7uk t125 = gm_l7;
                            uint64_t t126 = gm_l2;
                            uint64_t t127 = gm_l11;
                            uint64_t t128 = gm_l8;
                            uint64_t r129; bool o130 = __builtin_mul_overflow(t127, t128, &r129);
                            if (o130) gm_trap("integer overflow in `*`", gm_srcochlrb3nk64u, 93, 59);
                            uint64_t t131 = r129;
                            uint64_t r132; bool o133 = __builtin_add_overflow(t126, t131, &r132);
                            if (o133) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 93, 53);
                            uint64_t t134 = r132;
                            gm_b16_8 t135 = gm_l3;
                            uint8_t t136 = gm_f_mark_pointer_words_6hm9th1((void *)NULL, cx, NULL, t124, t125, t134, t135);
                            if (gm_unw.kind) {
                                goto gm_ll116;
                            }
                            uint8_t t137 = (uint8_t)!t136;
                            if (!t137) goto ie123;
                            {
                                uint8_t t139 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix123;
ie123:;
ix123:;
                        }
                        goto me119;
mn120:;
                        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 92, 13);
me119:;
                    }
lc112:;
                    {
                        uint64_t t141 = gm_l10;
                        uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
                        uint64_t t144 = r142;
                        gm_l10 = t144;
                    }
                }
gm_ll116:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb112; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc112; }
                }
                goto gm_lad;
lb112:;
            }
        }
        {
            uint8_t t145 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me9;
mn101:;
ma15:;
        uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t147 != ((uint8_t)12ULL)) goto mn146;
        gm_b16_8 t148 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 0));
        gm_l12 = t148;
        {
            void * t150 = gm_l0;
            gm_t88rcucg1b7uk t151 = gm_l1;
            gm_tbccvikg6b96e t152;
            gm_f_layout_of_e6hfj1r((void *)&t152, (void *)NULL, cx, NULL, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 16));
            gm_l13 = t153;
        }
        {
            {
                gm_b16_8 t155 = gm_l12;
                gm_l14 = t155;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t157 = gm_l15;
                    uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t159 = (t157 < t158);
                    if (!t159) goto lb156;
                    {
                        uint64_t t162 = gm_l15;
                        uint64_t t163 = gm_l15;
                        uint64_t t164 = (uintptr_t)t163;
                        uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t164 * 4));
                        gm_tlcm4lvu3g1a8 t167;
                        GM_ST(uint32_t, (uint8_t *)&t167 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t167 + 0), t162);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t167 + 8), t166);
                        uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&t167) + 0));
                        gm_l16 = t170;
                        gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t167) + 8));
                        gm_l17 = t171;
                        {
                            void * t174 = gm_l0;
                            gm_t88rcucg1b7uk t175 = gm_l17;
                            uint64_t t176 = gm_l2;
                            uint64_t t177 = gm_l16;
                            uint64_t t178 = (uintptr_t)t177;
                            uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            if (t178 >= (uintptr_t)(t179)) gm_trap_index(t178, t179, gm_srcochlrb3nk64u, 100, 56);
                            uint64_t t180 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t178 * 8));
                            uint64_t r181; bool o182 = __builtin_add_overflow(t176, t180, &r181);
                            if (o182) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 100, 50);
                            uint64_t t183 = r181;
                            gm_b16_8 t184 = gm_l3;
                            uint8_t t185 = gm_f_mark_pointer_words_6hm9th1((void *)NULL, cx, NULL, t174, t175, t183, t184);
                            if (gm_unw.kind) {
                                goto gm_ll160;
                            }
                            uint8_t t186 = (uint8_t)!t185;
                            if (!t186) goto ie173;
                            {
                                uint8_t t188 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix173;
ie173:;
ix173:;
                        }
                        goto me168;
mn169:;
                        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 99, 13);
me168:;
                    }
lc156:;
                    {
                        uint64_t t190 = gm_l15;
                        uint64_t r191; bool o192 = __builtin_add_overflow(t190, ((uint64_t)1ULL), &r191);
                        uint64_t t193 = r191;
                        gm_l15 = t193;
                    }
                }
gm_ll160:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb156; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc156; }
                }
                goto gm_lad;
lb156:;
            }
        }
        {
            uint8_t t194 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me9;
mn146:;
ma16:;
        uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t196 != ((uint8_t)17ULL)) goto mn195;
        uint32_t t197 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l18 = t197;
        {
            uint32_t t200 = gm_l18;
            uint64_t t201 = (uint64_t)t200;
            uint64_t t202 = (uintptr_t)t201;
            void * t203 = gm_l0;
            void * t204 = GM_LD(void *, (((uint8_t *)t203) + 64));
            uint64_t t205 = GM_LD(uintptr_t, ((((uint8_t *)t204) + 1088) + 0) + 8);
            if (t202 >= (uintptr_t)(t205)) gm_trap_index(t202, t205, gm_srcochlrb3nk64u, 105, 32);
            uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t204) + 1088) + 0)) + t202 * 184) + 24) + 0));
            if (t206 != ((uint8_t)0ULL)) goto ie199;
            gm_tpautr4br7h3l t207 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t204) + 1088) + 0)) + t202 * 184) + 24) + 8));
            F.l19 = t207;
            uint8_t t208;
            if ((((((uint8_t *)&F.l19) + 40))[4] != 0)) goto mn210;
            t208 = ((uint8_t)1);
            goto me209;
mn210:;
            t208 = ((uint8_t)0);
            goto me209;
mn211:;
            gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 105, 69);
me209:;
            if (!t208) goto ie199;
            {
                void * t213 = gm_l0;
                gm_t88rcucg1b7uk t214 = gm_l1;
                gm_tbccvikg6b96e t215;
                gm_f_layout_of_e6hfj1r((void *)&t215, (void *)NULL, cx, NULL, t213, t214);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t215) + 16));
                gm_l20 = t216;
            }
            {
                {
                    gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                    gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                    gm_l21 = t219;
                }
                {
                    gm_l22 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t221 = gm_l22;
                        uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        uint8_t t223 = (t221 < t222);
                        if (!t223) goto lb220;
                        {
                            gm_t8ta482bl6be0 t226;
                            uint64_t t227 = gm_l22;
                            GM_ST(uint64_t, (uint8_t *)&t226 + 0, t227);
                            uint64_t t228 = gm_l22;
                            uint64_t t229 = (uintptr_t)t228;
                            uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                            memcpy((uint8_t *)&t226 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t229 * 40), 40);
                            uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&t226) + 0));
                            gm_l23 = t233;
                            F.l24 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t226) + 8));
                            {
                                void * t236 = gm_l0;
                                gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                                uint64_t t238 = gm_l2;
                                uint64_t t239 = gm_l23;
                                uint64_t t240 = (uintptr_t)t239;
                                uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                if (t240 >= (uintptr_t)(t241)) gm_trap_index(t240, t241, gm_srcochlrb3nk64u, 108, 63);
                                uint64_t t242 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t240 * 8));
                                uint64_t r243; bool o244 = __builtin_add_overflow(t238, t242, &r243);
                                if (o244) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 108, 57);
                                uint64_t t245 = r243;
                                gm_b16_8 t246 = gm_l3;
                                uint8_t t247 = gm_f_mark_pointer_words_6hm9th1((void *)NULL, cx, NULL, t236, t237, t245, t246);
                                if (gm_unw.kind) {
                                    goto gm_ll224;
                                }
                                uint8_t t248 = (uint8_t)!t247;
                                if (!t248) goto ie235;
                                {
                                    uint8_t t250 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix235;
ie235:;
ix235:;
                            }
                            goto me231;
mn232:;
                            gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 107, 17);
me231:;
                        }
lc220:;
                        {
                            uint64_t t252 = gm_l22;
                            uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                            uint64_t t255 = r253;
                            gm_l22 = t255;
                        }
                    }
gm_ll224:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb220; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc220; }
                    }
                    goto gm_lad;
lb220:;
                }
            }
            {
                uint8_t t256 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix199;
ie199:;
ix199:;
        }
        {
            void * t257 = gm_l0;
            gm_t88rcucg1b7uk t258 = gm_l1;
            uint8_t t259 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t257, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t260 = (uint8_t)!t259;
            uint8_t t261 = 0;
            F.ret = t260;
            goto gm_fret;
        }
        goto me9;
mn195:;
ma17:;
        void * t263 = gm_l0;
        gm_t88rcucg1b7uk t264 = gm_l1;
        uint8_t t265 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t263, t264);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t266 = (uint8_t)!t265;
        uint8_t t267 = 0;
        F.ret = t266;
        goto gm_fret;
        goto me9;
mn262:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 81, 5);
me9:;
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
__attribute__((aligned(4))) uint8_t gm_f_may_hold_pointers_iuorcah(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_may_hold_pointers_iuorcah F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_b16_8 gm_l3; /* elems */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* inner */
    gm_t88rcucg1b7uk gm_l8; /* err */
    gm_t88rcucg1b7uk gm_l9; /* ok */
    uint32_t gm_l10; /* id */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* fields */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l19; /* variants */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t88rcucg1b7uk gm_l23; /* pt */
    GM_STACK_CHECK(&F, 1018u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma8:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t20 != ((uint8_t)0ULL)) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t22 != ((uint8_t)1ULL)) goto pa21;
        goto po18;
pa21:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t24 != ((uint8_t)2ULL)) goto pa23;
        goto po18;
pa23:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t26 != ((uint8_t)3ULL)) goto pa25;
        goto po18;
pa25:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t28 != ((uint8_t)4ULL)) goto pa27;
        goto po18;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t30 != ((uint8_t)5ULL)) goto pa29;
        goto po18;
pa29:;
        goto mn17;
po18:;
        uint8_t t31 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn17:;
ma9:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t33 != ((uint8_t)11ULL)) goto mn32;
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l2 = t34;
        void * t35 = gm_l0;
        gm_t88rcucg1b7uk t36 = gm_l2;
        uint8_t t37 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
        goto me7;
mn32:;
ma10:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t40 != ((uint8_t)12ULL)) goto mn39;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l3 = t41;
        {
            {
                gm_b16_8 t44 = gm_l3;
                gm_l4 = t44;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t46 = gm_l5;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t48 = (t46 < t47);
                    if (!t48) goto lb45;
                    {
                        uint64_t t51 = gm_l5;
                        uint64_t t52 = (uintptr_t)t51;
                        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t52 * 4));
                        gm_l6 = t56;
                        {
                            void * t59 = gm_l0;
                            gm_t88rcucg1b7uk t60 = gm_l6;
                            uint8_t t61 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t59, t60);
                            if (gm_unw.kind) {
                                goto gm_ll49;
                            }
                            if (!t61) goto ie58;
                            {
                                uint8_t t63 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix58;
ie58:;
ix58:;
                        }
                        goto me54;
mn55:;
                        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 35, 13);
me54:;
                    }
lc45:;
                    {
                        uint64_t t65 = gm_l5;
                        uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                        uint64_t t68 = r66;
                        gm_l5 = t68;
                    }
                }
gm_ll49:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb45; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc45; }
                }
                goto gm_lad;
lb45:;
            }
        }
        {
            uint8_t t69 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me7;
mn39:;
ma11:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t71 != ((uint8_t)13ULL)) goto mn70;
        gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l7 = t72;
        void * t73 = gm_l0;
        gm_t88rcucg1b7uk t74 = gm_l7;
        uint8_t t75 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = 0;
        F.ret = t75;
        goto gm_fret;
        goto me7;
mn70:;
ma12:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t78 != ((uint8_t)14ULL)) goto mn77;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l8 = t79;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l9 = t80;
        uint8_t t81 = 0;
        void * t82 = gm_l0;
        gm_t88rcucg1b7uk t83 = gm_l8;
        uint8_t t84 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t84) {
            void * t85 = gm_l0;
            gm_t88rcucg1b7uk t86 = gm_l9;
            uint8_t t87 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t81 = t87;
        } else t81 = 1;
        uint8_t t88 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me7;
mn77:;
ma13:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t90 != ((uint8_t)17ULL)) goto mn89;
        uint32_t t91 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l10 = t91;
        uint32_t t92 = gm_l10;
        uint64_t t93 = (uint64_t)t92;
        uint64_t t94 = (uintptr_t)t93;
        void * t95 = gm_l0;
        void * t96 = GM_LD(void *, (((uint8_t *)t95) + 64));
        uint64_t t97 = GM_LD(uintptr_t, ((((uint8_t *)t96) + 1088) + 0) + 8);
        if (t94 >= (uintptr_t)(t97)) gm_trap_index(t94, t97, gm_srcochlrb3nk64u, 42, 30);
        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 0));
        switch (t103) { case ((uint8_t)0ULL): goto ma99; case ((uint8_t)1ULL): goto ma101; case ((uint8_t)2ULL): goto ma100; default: goto ma102; }
ma99:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 0));
        if (t105 != ((uint8_t)0ULL)) goto mn104;
        gm_tpautr4br7h3l t106 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 8));
        F.l11 = t106;
        {
            uint8_t t109;
            if ((((((uint8_t *)&F.l11) + 40))[4] != 0)) goto mn111;
            t109 = ((uint8_t)0);
            goto me110;
mn111:;
            t109 = ((uint8_t)1);
            goto me110;
mn112:;
            gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 44, 20);
me110:;
            if (!t109) goto ie108;
            {
                uint8_t t114 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            {
                gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_l12 = t116;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t118 = gm_l13;
                    uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t120 = (t118 < t119);
                    if (!t120) goto lb117;
                    {
                        uint64_t t123 = gm_l13;
                        uint64_t t124 = (uintptr_t)t123;
                        uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_t0mglksrkfa5e t128 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t124 * 40));
                        F.l14 = t128;
                        {
                            void * t131 = gm_l0;
                            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                            uint8_t t133 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t131, t132);
                            if (gm_unw.kind) {
                                goto gm_ll121;
                            }
                            if (!t133) goto ie130;
                            {
                                uint8_t t135 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix130;
ie130:;
ix130:;
                        }
                        goto me126;
mn127:;
                        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 45, 17);
me126:;
                    }
lc117:;
                    {
                        uint64_t t137 = gm_l13;
                        uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                        uint64_t t140 = r138;
                        gm_l13 = t140;
                    }
                }
gm_ll121:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb117; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc117; }
                }
                goto gm_lad;
lb117:;
            }
        }
        {
            uint8_t t141 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me98;
mn104:;
ma100:;
        uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 0));
        if (t143 != ((uint8_t)2ULL)) goto mn142;
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 8));
        gm_l15 = t144;
        {
            {
                gm_b16_8 t147 = gm_l15;
                gm_l16 = t147;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t149 = gm_l17;
                    uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t151 = (t149 < t150);
                    if (!t151) goto lb148;
                    {
                        uint64_t t154 = gm_l17;
                        uint64_t t155 = (uintptr_t)t154;
                        uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_t0mglksrkfa5e t159 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t155 * 40));
                        F.l18 = t159;
                        {
                            void * t162 = gm_l0;
                            gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16));
                            uint8_t t164 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t162, t163);
                            if (gm_unw.kind) {
                                goto gm_ll152;
                            }
                            if (!t164) goto ie161;
                            {
                                uint8_t t166 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix161;
ie161:;
ix161:;
                        }
                        goto me157;
mn158:;
                        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 51, 17);
me157:;
                    }
lc148:;
                    {
                        uint64_t t168 = gm_l17;
                        uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                        uint64_t t171 = r169;
                        gm_l17 = t171;
                    }
                }
gm_ll152:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb148; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc148; }
                }
                goto gm_lad;
lb148:;
            }
        }
        {
            uint8_t t172 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me98;
mn142:;
ma101:;
        uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 0));
        if (t174 != ((uint8_t)1ULL)) goto mn173;
        gm_b16_8 t175 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 1088) + 0)) + t94 * 184) + 24) + 8) + 8));
        gm_l19 = t175;
        {
            {
                gm_b16_8 t178 = gm_l19;
                gm_l20 = t178;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t180 = gm_l21;
                    uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t182 = (t180 < t181);
                    if (!t182) goto lb179;
                    {
                        uint64_t t185 = gm_l21;
                        uint64_t t186 = (uintptr_t)t185;
                        uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_tckdobunaulee t190 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t186 * 72));
                        F.l22 = t190;
                        {
                            if (!(((((uint8_t *)&F.l22) + 16))[4] != 0)) goto ie192;
                            gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l22) + 16));
                            gm_l23 = t193;
                            void * t194 = gm_l0;
                            gm_t88rcucg1b7uk t195 = gm_l23;
                            uint8_t t196 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t194, t195);
                            if (gm_unw.kind) {
                                goto gm_ll183;
                            }
                            if (!t196) goto ie192;
                            {
                                uint8_t t198 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix192;
ie192:;
ix192:;
                        }
                        goto me188;
mn189:;
                        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 57, 17);
me188:;
                    }
lc179:;
                    {
                        uint64_t t200 = gm_l21;
                        uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                        uint64_t t203 = r201;
                        gm_l21 = t203;
                    }
                }
gm_ll183:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb179; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc179; }
                }
                goto gm_lad;
lb179:;
            }
        }
        {
            uint8_t t204 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me98;
mn173:;
ma102:;
        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 42, 24);
me98:;
        goto me7;
mn89:;
ma14:;
        uint8_t t206 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me7;
mn205:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 31, 5);
me7:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_pointer_at_0dc5rq1(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pointer_at_0dc5rq1 F;
    gm_b16_8 gm_l0; /* words */
    uint64_t gm_l1; /* off */
    uint64_t gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_l2;
        if (t4 == 0) gm_trap("division by zero", gm_srcochlrb3nk64u, 120, 8);
        uint64_t t5 = (uint64_t)(t3 % t4);
        uint8_t t6 = (!(t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10 = t9;
        uint64_t t11 = gm_l1;
        uint64_t t12 = gm_l2;
        if (t12 == 0) gm_trap("division by zero", gm_srcochlrb3nk64u, 121, 12);
        uint64_t t13 = (uint64_t)(t11 / t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&t10) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcochlrb3nk64u, 121, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t10)) + t15 * 1), ((uint8_t)1));
    }
    {
        uint8_t t17 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_pointer_words_ljph5no(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_pointer_words_ljph5no F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* n */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 0) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_tbccvikg6b96e t6;
        gm_f_layout_of_e6hfj1r((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
        uint64_t t8 = gm_l2;
        uint64_t r9; bool o10 = __builtin_add_overflow(t7, t8, &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_srcochlrb3nk64u, 72, 11);
        uint64_t t11 = r9;
        uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcochlrb3nk64u, 72, 11);
        uint64_t t14 = r12;
        uint64_t t15 = gm_l2;
        if (t15 == 0) gm_trap("division by zero", gm_srcochlrb3nk64u, 72, 10);
        uint64_t t16 = (uint64_t)(t14 / t15);
        gm_l3 = t16;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t18 = gm_l3;
            gm_l5 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l6;
                uint64_t t21 = gm_l5;
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l6;
                    {
                        void * t29 = (void *)((uint8_t *)&F.l4);
                        gm_tftb1odc739t5 t30 = gm_f_push_vd1290p(t29, ((uint8_t)0));
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcochlrb3nk64u, 74, 21);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcochlrb3nk64u, 74, 5);
me26:;
                }
lc19:;
                {
                    uint64_t t33 = gm_l6;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l6 = t36;
                }
            }
lb19:;
        }
    }
    {
        void * t38 = gm_l0;
        gm_t88rcucg1b7uk t39 = gm_l1;
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        uint8_t t41 = gm_f_mark_pointer_words_6hm9th1((void *)NULL, cx, NULL, t38, t39, ((uint64_t)0ULL), t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t42 = (uint8_t)!t41;
        if (!t42) goto ie37;
        {
            gm_th2lqbo77pvsc t44;
            memset(&t44, 0, sizeof t44);
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t46);
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
extern __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
