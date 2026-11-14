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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint64_t f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0nqnmillkbs6;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0nqnmillkbs6) == 32 && _Alignof(gm_t0nqnmillkbs6) == 8 && offsetof(gm_t0nqnmillkbs6, f0) == 0 && offsetof(gm_t0nqnmillkbs6, f1) == 8 && offsetof(gm_t0nqnmillkbs6, f2) == 16, "layout of `Edge`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_t08olq97rscn3 f2; } gm_tqtt2php6e8lk;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_b16_8 f5; uint64_t f6; gm_trqfvmjtt4sij f7; } gm_tt0nlfbhb3hu6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_trqfvmjtt4sij v1; } f1; } gm_ttevl7jqjigkp;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint64_t f0; gm_tl7hpf5nlilqq f1; } gm_tm101l8077enc;
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
typedef struct { uint64_t f0; gm_tt0nlfbhb3hu6 f1; } gm_to8irbp065ot2;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqtt2php6e8lk) == 56 && _Alignof(gm_tqtt2php6e8lk) == 8 && offsetof(gm_tqtt2php6e8lk, f0) == 0 && offsetof(gm_tqtt2php6e8lk, f1) == 8 && offsetof(gm_tqtt2php6e8lk, f2) == 24, "layout of `Parsed`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_tt0nlfbhb3hu6) == 184 && _Alignof(gm_tt0nlfbhb3hu6) == 8 && offsetof(gm_tt0nlfbhb3hu6, f0) == 0 && offsetof(gm_tt0nlfbhb3hu6, f1) == 16 && offsetof(gm_tt0nlfbhb3hu6, f2) == 56 && offsetof(gm_tt0nlfbhb3hu6, f3) == 112 && offsetof(gm_tt0nlfbhb3hu6, f4) == 113 && offsetof(gm_tt0nlfbhb3hu6, f5) == 120 && offsetof(gm_tt0nlfbhb3hu6, f6) == 136 && offsetof(gm_tt0nlfbhb3hu6, f7) == 144, "layout of `Module`");
_Static_assert(sizeof(gm_ttevl7jqjigkp) == 48 && _Alignof(gm_ttevl7jqjigkp) == 8 && offsetof(gm_ttevl7jqjigkp, f0) == 0 && offsetof(gm_ttevl7jqjigkp, f1) == 8, "layout of `IoError!List(string)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_trqfvmjtt4sij f5; gm_b16_8 f6; gm_t0rfe1ou14h21 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_trqfvmjtt4sij f13; gm_tf5srh4mp4b3i f14; gm_trqfvmjtt4sij f15; gm_tv37d3dpt2opu f16; void * f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; } gm_tcuh1vih4g9cr;
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tm101l8077enc) == 64 && _Alignof(gm_tm101l8077enc) == 8 && offsetof(gm_tm101l8077enc, f0) == 0 && offsetof(gm_tm101l8077enc, f1) == 8, "layout of `(i: usize, x: Fix)`");
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_to8irbp065ot2) == 192 && _Alignof(gm_to8irbp065ot2) == 8 && offsetof(gm_to8irbp065ot2, f0) == 0 && offsetof(gm_to8irbp065ot2, f1) == 8, "layout of `(i: usize, x: Module)`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { gm_t08olq97rscn3 f0; uint64_t f1; gm_tvpnktv5fo8nl f2; gm_t8c5h7u09n10a f3; } gm_t52j636u3vgk3;
_Static_assert(sizeof(gm_tcuh1vih4g9cr) == 552 && _Alignof(gm_tcuh1vih4g9cr) == 8 && offsetof(gm_tcuh1vih4g9cr, f0) == 0 && offsetof(gm_tcuh1vih4g9cr, f1) == 8 && offsetof(gm_tcuh1vih4g9cr, f2) == 16 && offsetof(gm_tcuh1vih4g9cr, f3) == 24 && offsetof(gm_tcuh1vih4g9cr, f4) == 25 && offsetof(gm_tcuh1vih4g9cr, f5) == 32 && offsetof(gm_tcuh1vih4g9cr, f6) == 72 && offsetof(gm_tcuh1vih4g9cr, f7) == 88 && offsetof(gm_tcuh1vih4g9cr, f8) == 152 && offsetof(gm_tcuh1vih4g9cr, f9) == 168 && offsetof(gm_tcuh1vih4g9cr, f10) == 184 && offsetof(gm_tcuh1vih4g9cr, f11) == 200 && offsetof(gm_tcuh1vih4g9cr, f12) == 240 && offsetof(gm_tcuh1vih4g9cr, f13) == 280 && offsetof(gm_tcuh1vih4g9cr, f14) == 320 && offsetof(gm_tcuh1vih4g9cr, f15) == 336 && offsetof(gm_tcuh1vih4g9cr, f16) == 376 && offsetof(gm_tcuh1vih4g9cr, f17) == 424 && offsetof(gm_tcuh1vih4g9cr, f18) == 432 && offsetof(gm_tcuh1vih4g9cr, f19) == 472 && offsetof(gm_tcuh1vih4g9cr, f20) == 512, "layout of `Loader`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t52j636u3vgk3) == 216 && _Alignof(gm_t52j636u3vgk3) == 8 && offsetof(gm_t52j636u3vgk3, f0) == 0 && offsetof(gm_t52j636u3vgk3, f1) == 32 && offsetof(gm_t52j636u3vgk3, f2) == 40 && offsetof(gm_t52j636u3vgk3, f3) == 200, "layout of `Decl`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_absolute_path_gj3a0ni {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_member_0jvs7ug {
    gm_frame h;
    gm_tvpnktv5fo8nl l3; /* item */
    uint64_t ret;
};
struct gm_fr_add_module_3cgsegf {
    gm_frame h;
    gm_tvpnktv5fo8nl l9; /* item */
    uint64_t ret;
};
struct gm_fr_add_named_i40bl11 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bad_rest_oc5mkat {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_canonical_path_q6lq4b6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_canonicalize_n3e3qri {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_import_cycles_2u7oqo0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stack */
    gm_b0_1 ret;
};
struct gm_fr_collect_item_d4tfq2h {
    gm_frame h;
    gm_tvpnktv5fo8nl l2; /* item */
    gm_tv2uf4leil5hq l3; /* b */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_collect_whens_ktf37vb {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* queue */
    gm_tvpnktv5fo8nl l4; /* item */
    gm_t7bjbmukv26e7 l5; /* w */
    gm_tvpnktv5fo8nl l9; /* it */
    gm_b0_1 ret;
};
struct gm_fr_contains_c222vll {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_dep_dir_viuo66n {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_deps_of_i4no6j9 {
    gm_frame h;
    gm_tl7hpf5nlilqq l4; /* p */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_describe_read_error_uikkr59 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_dir_gm_files_0s2opd8 {
    gm_frame h;
    gm_b16_8 l0; /* dir */
    gm_trqfvmjtt4sij l1; /* paths */
    gm_ttevl7jqjigkp ret;
    gm_b0_1 w0;
};
struct gm_fr_dump_fugpeqi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt0nlfbhb3hu6 l5; /* m */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_30da7i6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3dleea8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_738b90k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_aiebmtn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n7sb6c8_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nhbtm4l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oq9aajs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pkfv52g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tjobtaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uphdt5g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_find_package_root_fs4fuqj {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_6gkpg0v_2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_bq6tmqb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_fnpff4s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_m8t719h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_lljdp3h {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_import_decl_vuv14lf {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* e */
    gm_t8c5h7u09n10a ret;
};
struct gm_fr_import_module_8345fv4 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_import_path_any_q8ttsvi {
    gm_frame h;
    gm_tv2uf4leil5hq l1; /* b */
    gm_b16_8 ret;
};
struct gm_fr_import_path_na25mjr {
    gm_frame h;
    gm_tv2uf4leil5hq l1; /* b */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_imports_of_5rkfdia {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_import_1c0aicu {
    gm_frame h;
    gm_tv2uf4leil5hq l1; /* b */
    uint8_t ret;
};
struct gm_fr_is_reserved_type_name_tuq4qsj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_016fo30_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0pjrgaq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_12lt7k0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5msmod6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6kkl75k {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g6e4eeu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j41vvuc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j6fu443 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oo6l3qa {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vdg8k7c {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_dir_module_a75lmvr {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* paths */
    gm_trqfvmjtt4sij l10; /* files */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_load_file_rdflf4e {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_load_modules_2vujekg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_load_package_modules_t4hh6gf {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* dirs */
    gm_b16_8 l9; /* dir */
    gm_trqfvmjtt4sij l10; /* subdirs */
    uint8_t l11; /* readable */
    gm_trqfvmjtt4sij l18; /* files */
    gm_b0_1 ret;
    gm_b0_1 w1;
};
struct gm_fr_load_root_af4ud7b {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* files */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_loader_on0up0a {
    gm_frame h;
    gm_t0rfe1ou14h21 l9; /* <tmp> */
    gm_trqfvmjtt4sij l10; /* <tmp> */
    gm_trqfvmjtt4sij l14; /* <tmp> */
    gm_trqfvmjtt4sij l15; /* <tmp> */
    gm_trqfvmjtt4sij l16; /* <tmp> */
    gm_trqfvmjtt4sij l17; /* <tmp> */
    gm_trqfvmjtt4sij l18; /* <tmp> */
    gm_trqfvmjtt4sij l19; /* <tmp> */
    gm_trqfvmjtt4sij l20; /* <tmp> */
    gm_tcuh1vih4g9cr l21; /* l */
    gm_tcuh1vih4g9cr ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_files_8j8gjpm {
    gm_frame h;
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_module_files_with_abkh6r4 {
    gm_frame h;
    gm_tqetrgo0q24b5 l2; /* paths */
    gm_trqfvmjtt4sij l4; /* out */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_overlay_text_haaaurk {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_package_of_hul50on {
    gm_frame h;
    gm_tl7hpf5nlilqq l12; /* p */
    gm_trqfvmjtt4sij l13; /* deps */
    uint64_t ret;
};
struct gm_fr_parse_text_j7gp86k {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* lexed */
    gm_t08olq97rscn3 ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_read_manifest_qr219nu {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* res */
    gm_trqfvmjtt4sij l3; /* deps */
    gm_trqfvmjtt4sij l4; /* deps */
    gm_tm43kqe3e8b4a l9; /* manifest */
    gm_t7tiv6vif86qg l12; /* dep */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_import_944uqa9 {
    gm_frame h;
    gm_t52j636u3vgk3 l2; /* d */
    gm_tv2uf4leil5hq l3; /* b */
    gm_b0_1 ret;
};
struct gm_fr_reverse_cgmjfos {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_import_dav5vf0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_show_khjih0n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_skipped_dir_orfng8m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sort_o401rjn {
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
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_visit_fs6p4tu {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* names */
    gm_b0_1 ret;
};
struct gm_fr_working_dir_dj7eq93 {
    gm_frame h;
    gm_tpbv7nqggnvqr l0; /* buf */
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk27942ja0g(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_add_member_0jvs7ug(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint64_t gm_f_add_module_3cgsegf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_named_i40bl11(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_bad_rest_oc5mkat(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_item_d4tfq2h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_whens_ktf37vb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_c222vll(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_dep_dir_viuo66n(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_dep_dir_viuo66n_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqetrgo0q24b5 gm_f_deps_of_i4no6j9_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_dir_gm_files_0s2opd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_fugpeqi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_30da7i6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3dleea8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_738b90k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_aiebmtn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nhbtm4l(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tjobtaj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sbacjg5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_siirbaa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_file_stem_v6vrs6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_bq6tmqb(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_m8t719h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8meo3jv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ajjbrpr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_atch2pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bl6khrb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cgpq8i6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pl7hkg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q451tst(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qpmmpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t2lplfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_host_target_vsu78om(void *gm_out);
__attribute__((aligned(4))) gm_t8c5h7u09n10a gm_f_import_decl_vuv14lf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_import_module_8345fv4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_import_path_any_q8ttsvi(void * a0);
__attribute__((aligned(4))) void gm_f_import_path_na25mjr(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_import_path_na25mjr_c(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_imports_of_5rkfdia(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_s9trr09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_import_1c0aicu(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_016fo30_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_12lt7k0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5msmod6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6kkl75k(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g6e4eeu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j41vvuc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j6fu443(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oo6l3qa(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vdg8k7c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_dir_module_a75lmvr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_tf5srh4mp4b3i a3, const void * a4);
__attribute__((aligned(4))) void gm_f_load_file_rdflf4e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_load_modules_2vujekg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_load_package_modules_t4hh6gf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_module_files_8j8gjpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_module_files_with_abkh6r4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_overlay_text_haaaurk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_package_of_hul50on(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_text_j7gp86k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1e500nc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_46vfpsn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_5h5ofgv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_88e5hkq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89v6mq9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_9oh3uqv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_n0od5si(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u9lban8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vhbtmrs(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_manifest_qr219nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_reverse_cgmjfos(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_reverse_range_kcrihc4(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_same_import_dav5vf0(void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_khjih0n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_slashes_0qtfkuu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_stnipdi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_fs6p4tu(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2l4ia51(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gf33g1q(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cuqbj62gqhf_3[16] __attribute__((aligned(8)));
extern void *const gm_hl3gh4gojekg[];
extern void *const gm_hle6hel8vh71[];
extern void *const gm_hlrpdhe6kjfr[];
extern uint8_t gm_k5c0o81h2qv[40] __attribute__((aligned(8)));
extern uint8_t gm_kn7qjfrkcl7[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6lnp3h2ojl1f[77];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_sa0l93d5jbk7k[69];
extern const uint8_t gm_sad9qq5tf3nqa[36];
extern const uint8_t gm_san68r6np95il[6];
extern const uint8_t gm_sbgl5dgur8udg[7];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_sdsu0imtl17e3[3];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shqn2dgvujf5o[13];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sjkg27kmogh66[7];
extern const uint8_t gm_sjq0dshsov83m[20];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_spq5ja4c7ciga[11];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3onm9035msn9[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srv05plrkjhf8[7];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_strcbt1c3upss[4];
extern const uint8_t gm_su5dv07glhc06[37];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqk27942ja0g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_c222vll(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_c222vll F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_ovslcai(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me7:;
        uint8_t t10 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_describe_read_error_uikkr59 F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3onm9035msn9, 85, 73);
me2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_30da7i6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_30da7i6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3dleea8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3dleea8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_738b90k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_738b90k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7hq6cu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_aiebmtn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_aiebmtn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 160)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n7sb6c8_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 216)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nhbtm4l(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nhbtm4l F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oq9aajs F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tjobtaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tjobtaj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uphdt5g F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                        void * t19 = gm_l2;
                        uint8_t t20 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v_2 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)t1));
    void * t3 = gm_l1;
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)t3));
    uint8_t t5 = (t2 < t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_lljdp3h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_7hq6cu2();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_import_1c0aicu(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_import_1c0aicu F;
    void * gm_l0; /* item */
    GM_STACK_CHECK_SHALLOW(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 64) + 0));
        if (t4 != ((uint8_t)0ULL)) goto ie2;
        gm_tv2uf4leil5hq t5 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t3) + 64) + 8));
        F.l1 = t5;
        {
            void * t7 = GM_LD(void *, (((uint8_t *)&F.l1) + 40));
            uint8_t t8;
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
            if (t11 != ((uint8_t)38ULL)) goto mn10;
            t8 = ((uint8_t)1);
            goto me9;
mn10:;
            t8 = ((uint8_t)0);
            goto me9;
mn12:;
            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 214, 12);
me9:;
            uint8_t t13 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t15 = 0;
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
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) void gm_f_list_016fo30_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_016fo30_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n7sb6c8_2();
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
inline __attribute__((aligned(4))) void gm_f_list_12lt7k0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_12lt7k0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3dleea8();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_5msmod6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5msmod6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_nhbtm4l();
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
inline __attribute__((aligned(4))) void gm_f_list_6kkl75k(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6kkl75k F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_738b90k();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4();
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
inline __attribute__((aligned(4))) void gm_f_list_g6e4eeu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g6e4eeu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_30da7i6();
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
inline __attribute__((aligned(4))) void gm_f_list_j41vvuc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j41vvuc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oq9aajs();
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
inline __attribute__((aligned(4))) void gm_f_list_j6fu443(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j6fu443 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_uphdt5g();
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
inline __attribute__((aligned(4))) void gm_f_list_oo6l3qa(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oo6l3qa F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_aiebmtn();
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
inline __attribute__((aligned(4))) void gm_f_list_vdg8k7c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vdg8k7c F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_tjobtaj();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
            gm_tf5srh4mp4b3i t8;
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
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 8));
        gm_l2 = t17;
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint64_t t29 = gm_l2;
        gm_tf5srh4mp4b3i t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
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
            gm_th2lqbo77pvsc t8;
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
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
        gm_l2 = t17;
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        gm_th2lqbo77pvsc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
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
inline __attribute__((aligned(4))) void gm_f_reverse_cgmjfos(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reverse_cgmjfos F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_f_reverse_range_kcrihc4(t2, ((uint64_t)0ULL), t3);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skipped_dir_orfng8m F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 55u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    gm_b16_8 t3 = gm_l0;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
    uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
    if (!t5) {
        uint8_t t6 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_snoai0j6d9u36, 6) == 0));
        t2 = t6;
    } else t2 = 1;
    if (!t2) {
        uint8_t t7 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_shqn2dgvujf5o, 12) == 0));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_o401rjn F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v_2), NULL, cx});
        gm_f_introsort_6k4m7ds(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_add_member_0jvs7ug(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, const void * a3) {
    struct gm_fr_add_member_0jvs7ug F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint64_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 405u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 240);
        gm_t52j636u3vgk3 t4;
        memcpy((uint8_t *)&t4 + 0, ((uint8_t *)a2), 32);
        uint64_t t5 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t4 + 32, t5);
        memcpy((uint8_t *)&t4 + 40, ((uint8_t *)a3), 160);
        gm_t8c5h7u09n10a t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t8c5h7u09n10a, (uint8_t *)&t4 + 200, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_rda4o9n_2(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcc2qutubarto1, 813, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 240) + 0) + 8);
        uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 814, 11);
        uint64_t t14 = r12;
        gm_l4 = t14;
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = gm_l1;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        uint64_t t18 = gm_l4;
        gm_b0_1 t19 = gm_f_add_named_i40bl11((void *)NULL, cx, NULL, t15, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t20 = gm_l4;
        uint8_t t21 = 0;
        F.ret = t20;
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
__attribute__((aligned(4))) uint64_t gm_f_add_module_3cgsegf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_add_module_3cgsegf F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* files */
    uint64_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t08olq97rscn3 gm_l6; /* file */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 805u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 200) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 200);
        gm_tt0nlfbhb3hu6 t6;
        memset((uint8_t *)&t6 + 114, 0, 6);
        gm_b16_8 t7 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
        gm_f_list_oo6l3qa((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t6 + 16)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_lljdp3h((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&t6 + 56)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&t6 + 112, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t6 + 113, ((uint8_t)0));
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 120, t8);
        GM_ST(uint64_t, (uint8_t *)&t6 + 136, ((uint64_t)0ULL));
        gm_f_list_f0gjjbh((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t6 + 144)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t9 = gm_f_push_vhbtmrs(t5, &t6);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcc2qutubarto1, 459, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        {
            gm_b16_8 t13 = gm_l2;
            gm_l4 = t13;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l5;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t08olq97rscn3 t25 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 32));
                    gm_l6 = t25;
                    {
                        {
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                            gm_l7 = t28;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t30 = gm_l8;
                                uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t32 = (t30 < t31);
                                if (!t32) goto lb29;
                                {
                                    uint64_t t35 = gm_l8;
                                    uint64_t t36 = (uintptr_t)t35;
                                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_tvpnktv5fo8nl t40 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t36 * 160));
                                    F.l9 = t40;
                                    {
                                        uint8_t t43;
                                        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 64) + 0));
                                        if (t46 != ((uint8_t)1ULL)) goto mn45;
                                        t43 = ((uint8_t)1);
                                        goto me44;
mn45:;
                                        t43 = ((uint8_t)0);
                                        goto me44;
mn47:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 471, 16);
me44:;
                                        if (!t43) goto ie42;
                                        {
                                            uint64_t t49 = gm_l3;
                                            uint64_t t50 = (uintptr_t)t49;
                                            void * t51 = gm_l0;
                                            uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)t51) + 200) + 0) + 8);
                                            if (t50 >= (uintptr_t)(t52)) gm_trap_index(t50, t52, gm_srcc2qutubarto1, 472, 17);
                                            void * t53 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t51) + 200) + 0)) + t50 * 184) + 144);
                                            gm_tftb1odc739t5 t54 = gm_f_push_1e500nc(t53, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                            gm_tftb1odc739t5 t55 = t54;
                                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcc2qutubarto1, 472, 17);
                                            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                                        }
                                        goto ix42;
ie42:;
                                        {
                                            void * t58 = gm_l0;
                                            uint64_t t59 = gm_l3;
                                            gm_b0_1 t60 = gm_f_collect_item_d4tfq2h((void *)NULL, cx, NULL, t58, t59, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                            if (gm_unw.kind) {
                                                goto gm_ll33;
                                            }
                                        }
ix42:;
                                    }
                                    goto me38;
mn39:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 470, 9);
me38:;
                                }
lc29:;
                                {
                                    uint64_t t62 = gm_l8;
                                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                                    uint64_t t65 = r63;
                                    gm_l8 = t65;
                                }
                            }
gm_ll33:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb29; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc29; }
                            }
                            goto gm_ll18;
lb29:;
                        }
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 469, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t67 = gm_l5;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l5 = t70;
                }
            }
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
            }
            goto gm_lad;
lb14:;
        }
    }
    {
        uint64_t t71 = gm_l3;
        uint8_t t72 = 0;
        F.ret = t71;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_named_i40bl11(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_add_named_i40bl11 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* decl */
    void * gm_l4; /* m */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcc2qutubarto1, 540, 11);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 200) + 0)) + t3 * 184);
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l4;
        void * t8 = (void *)(((uint8_t *)t7) + 16);
        gm_t3877pk7lv748 t9;
        gm_b16_8 t10 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t10);
        uint64_t t11 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t9 + 16, t11);
        gm_tftb1odc739t5 t12 = gm_f_push_88e5hkq(t8, &t9);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcc2qutubarto1, 541, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t16 = gm_l4;
        void * t17 = (void *)(((uint8_t *)t16) + 56);
        gm_b16_8 t18 = gm_l2;
        uint8_t t19 = gm_f_contains_c222vll(t17, t18);
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie15;
        {
            void * t22 = gm_l4;
            void * t23 = (void *)(((uint8_t *)t22) + 56);
            gm_b16_8 t24 = gm_l2;
            uint64_t t25 = gm_l3;
            gm_tftb1odc739t5 t26 = gm_f_put_qoojoqv(t23, t24, t25);
            gm_tftb1odc739t5 t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcc2qutubarto1, 542, 37);
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
        }
        goto ix15;
ie15:;
ix15:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bad_rest_oc5mkat(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bad_rest_oc5mkat F;
    gm_b16_8 gm_l0; /* rest */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l1 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlrpdhe6kjfr, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        gm_b0_1 t10 = gm_f_split_obtinjv_stnipdi((void *)NULL, (&wn5), NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls11:;
we6:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        uint8_t t14 = 0;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonical_path_q6lq4b6 F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* c */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_canonicalize_n3e3qri((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l1 = t6;
        {
            gm_b16_8 t8 = gm_l1;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_th2lqbo77pvsc t11;
        gm_f_file_name_em7v8lv((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t12 = t11;
        gm_b16_8 t13;
        if (((((uint8_t *)&t12))[16] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            gm_b16_8 t15 = gm_l0;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        gm_l2 = t13;
    }
    {
        gm_b16_8 t17 = gm_l0;
        gm_th2lqbo77pvsc t18;
        gm_f_parent_lk0iero((void *)&t18, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t19 = t18;
        gm_b16_8 t20;
        if (((((uint8_t *)&t19))[16] != 0)) {
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 0));
            t20 = t21;
        } else {
            gm_b16_8 t22 = gm_l0;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        gm_l3 = t20;
    }
    {
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
        if (!t26) goto ie24;
        {
            gm_b16_8 t28 = gm_l0;
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_b16_8 t31 = gm_l3;
        gm_th2lqbo77pvsc t32;
        gm_f_canonicalize_n3e3qri((void *)&t32, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t33 = t32;
        if (!((((uint8_t *)&t33))[16] != 0)) goto ie30;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_l4 = t34;
        {
            gm_b16_8 t36 = gm_l4;
            gm_b16_8 t37 = gm_l2;
            gm_b16_8 t38 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_b16_8 t40 = gm_l0;
        uint8_t t41 = 0;
        F.ret = t40;
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
inline __attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_n3e3qri F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 195u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 241, 8);
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
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 241, 8);
me8:;
        if (!t7) goto ie2;
        {
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t14 = gm_l0;
        gm_b16_8 t15 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t16), t15);
        ((uint8_t *)&t16)[16] = 1;
        uint8_t t17 = 0;
        F.ret = t16;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_import_cycles_2u7oqo0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* state */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* start */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 200) + 0) + 8);
        gm_l1 = t3;
    }
    {
        uint64_t t4 = gm_l1;
        gm_tabb710d9mq2e t5;
        gm_f_make_2ip78bj((void *)&t5, (void *)NULL, cx, NULL, t4, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5c0o81h2qv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcc2qutubarto1, 737, 14);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l2 = t7;
    }
    {
        gm_b16_8 t8 = gm_l2;
        gm_f_fill_abmf5g5(t8, ((uint8_t)0ULL));
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t10 = gm_l1;
            gm_l4 = t10;
        }
        {
            for (;;) {
                uint64_t t12 = gm_l5;
                uint64_t t13 = gm_l4;
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l5;
                    uint64_t t18 = t17;
                    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&t18));
                    gm_l6 = t21;
                    {
                        uint64_t t24 = gm_l6;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcc2qutubarto1, 741, 12);
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t25 * 1));
                        uint8_t t28 = ((t27 == ((uint8_t)0ULL)));
                        if (!t28) goto ie23;
                        {
                            void * t30 = gm_l0;
                            uint64_t t31 = gm_l6;
                            gm_b16_8 t32 = gm_l2;
                            void * t33 = (void *)((uint8_t *)&F.l3);
                            gm_b0_1 t34 = gm_f_visit_fs6p4tu((void *)NULL, cx, NULL, t30, t31, t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 740, 5);
me19:;
                }
lc11:;
                {
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l5 = t39;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_item_d4tfq2h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_collect_item_d4tfq2h F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l4; /* name */
    uint64_t gm_l5; /* prev */
    GM_STACK_CHECK(&F, 1279u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        uint8_t t3 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 64) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        gm_tv2uf4leil5hq t4 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)&F.l2) + 64) + 8));
        F.l3 = t4;
        {
            uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 8) + 8);
            uint8_t t8 = (!(t7 == ((uint64_t)1ULL)));
            if (!t8) goto ie6;
            {
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
                gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 48));
                gm_b16_8 t13;
                GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sa0l93d5jbk7k));
                GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(68));
                gm_b0_1 t14 = gm_f_report_eardf07((void *)NULL, cx, NULL, t11, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t15;
                uint8_t t16 = 0;
                F.ret = t15;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 8) + 8);
            if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcc2qutubarto1, 516, 13);
            gm_t08olq97rscn3 t19 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 8)) + t17 * 32));
            gm_l4 = t19;
        }
        {
            uint8_t t21 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l4) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l4) + 0)), gm_sfdbfrv46lihd, 1) == 0));
            if (!t21) goto ie20;
            {
                void * t23 = gm_l0;
                void * t24 = GM_LD(void *, (((uint8_t *)t23) + 8));
                gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sad9qq5tf3nqa));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(35));
                gm_b0_1 t27 = gm_f_report_eardf07((void *)NULL, cx, NULL, t24, t25, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t28;
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t32 = gm_f_is_reserved_type_name_tuq4qsj(t31);
            if (!t32) goto ie30;
            {
                void * t34 = gm_l0;
                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 8));
                gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                gm_tnpifv20irnph t38;
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
                gm_tabb710d9mq2e t39;
                gm_f_format_9i45r57((void *)&t39, (void *)NULL, cx, NULL, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t40 = t39;
                if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcc2qutubarto1, 522, 35);
                gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
                gm_b16_8 t42 = t41;
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                gm_b0_1 t44 = gm_f_report_eardf07((void *)NULL, cx, NULL, t35, t36, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t45;
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t48 = gm_l0;
            uint64_t t49 = gm_l1;
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t51 = gm_f_lookup_59juqoo(t48, t49, t50);
            gm_tf5srh4mp4b3i t52 = t51;
            if (!((((uint8_t *)&t52))[8] != 0)) goto ie47;
            uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&t52));
            gm_l5 = t53;
            {
                void * t56 = gm_l0;
                uint64_t t57 = gm_l5;
                void * t58 = (void *)((uint8_t *)&F.l2);
                uint8_t t59 = gm_f_same_import_dav5vf0(t56, t57, t58);
                if (!t59) goto ie55;
                {
                    gm_b0_1 t61;
                    uint8_t t62 = 0;
                    F.ret = t61;
                    goto gm_fret;
                }
                goto ix55;
ie55:;
ix55:;
            }
            {
                gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                gm_tnpifv20irnph t65;
                GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
                gm_tabb710d9mq2e t66;
                gm_f_format_8meo3jv((void *)&t66, (void *)NULL, cx, NULL, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t67 = t66;
                if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcc2qutubarto1, 529, 36);
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                gm_b16_8 t69 = t68;
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                gm_f_error_s6hn871((void *)&F.l6, (void *)NULL, cx, NULL, t63, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t71 = (void *)((uint8_t *)&F.l6);
                uint64_t t72 = gm_l5;
                uint64_t t73 = (uintptr_t)t72;
                void * t74 = gm_l0;
                uint64_t t75 = GM_LD(uintptr_t, ((((uint8_t *)t74) + 240) + 0) + 8);
                if (t73 >= (uintptr_t)(t75)) gm_trap_index(t73, t75, gm_srcc2qutubarto1, 530, 21);
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t74) + 240) + 0)) + t73 * 216) + 0) + 16));
                gm_b16_8 t77;
                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sjq0dshsov83m));
                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(19));
                gm_f_add_label_50fj7vv(t71, GM_CONV(gm_cseahvg8v7p0vs, t76), t77);
            }
            {
                void * t78 = gm_l0;
                void * t79 = GM_LD(void *, (((uint8_t *)t78) + 8));
                gm_f_push_o4ckdr1(t79, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
            }
            {
                gm_b0_1 t80;
                uint8_t t81 = 0;
                F.ret = t80;
                goto gm_fret;
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            void * t82 = gm_l0;
            void * t83 = (void *)(((uint8_t *)t82) + 240);
            gm_t52j636u3vgk3 t84;
            memcpy((uint8_t *)&t84 + 0, ((uint8_t *)&gm_l4), 32);
            uint64_t t85 = gm_l1;
            GM_ST(uint64_t, (uint8_t *)&t84 + 32, t85);
            memcpy((uint8_t *)&t84 + 40, ((uint8_t *)&F.l2), 160);
            gm_t8c5h7u09n10a t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t86 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t8c5h7u09n10a, (uint8_t *)&t84 + 200, t86);
            gm_tftb1odc739t5 t87 = gm_f_push_rda4o9n_2(t83, &t84);
            gm_tftb1odc739t5 t88 = t87;
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_srcc2qutubarto1, 534, 5);
            gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
        }
        {
            void * t90 = gm_l0;
            uint64_t t91 = gm_l1;
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            void * t93 = gm_l0;
            uint64_t t94 = GM_LD(uintptr_t, ((((uint8_t *)t93) + 240) + 0) + 8);
            uint64_t r95; bool o96 = __builtin_sub_overflow(t94, ((uint64_t)1ULL), &r95);
            if (o96) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 535, 36);
            uint64_t t97 = r95;
            gm_b0_1 t98 = gm_f_add_named_i40bl11((void *)NULL, cx, NULL, t90, t91, t92, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        {
            {
                void * t101 = gm_l0;
                void * t102 = (void *)(((uint8_t *)t101) + 280);
                gm_tp8h583g7s5hd t103;
                uint64_t t104 = gm_l1;
                GM_ST(uint64_t, (uint8_t *)&t103 + 0, t104);
                memcpy((uint8_t *)&t103 + 8, ((uint8_t *)&F.l2), 160);
                gm_tftb1odc739t5 t105 = gm_f_push_u9lban8(t102, &t103);
                gm_tftb1odc739t5 t106 = t105;
                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcc2qutubarto1, 509, 9);
                gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
            }
            {
                gm_b0_1 t108;
                uint8_t t109 = 0;
                F.ret = t108;
                goto gm_fret;
            }
        }
ix2:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_whens_ktf37vb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_collect_whens_ktf37vb F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* id */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l6; /* branch */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 975u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcc2qutubarto1, 484, 14);
        gm_trqfvmjtt4sij t6 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 200) + 0)) + t3 * 184) + 144));
        F.l2 = t6;
    }
    {
        uint64_t t7 = gm_l1;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 200) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcc2qutubarto1, 485, 5);
        uint8_t *t11 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 200) + 0)) + t8 * 184) + 144);
        gm_f_list_f0gjjbh((void *)&(*(gm_trqfvmjtt4sij *)t11), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t15 = (t13 < t14);
            if (!t15) goto lb12;
            {
                uint64_t t18 = gm_l3;
                uint64_t t19 = (uintptr_t)t18;
                uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcc2qutubarto1, 488, 17);
                gm_tvpnktv5fo8nl t21 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t19 * 160));
                F.l4 = t21;
            }
            {
                uint64_t t22 = gm_l3;
                uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                if (o24) gm_trap("integer overflow in `+`", gm_srcc2qutubarto1, 489, 9);
                uint64_t t25 = r23;
                gm_l3 = t25;
            }
            {
                uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 64) + 0));
                if (t27 != ((uint8_t)1ULL)) goto ie26;
                gm_t7bjbmukv26e7 t28 = GM_LD(gm_t7bjbmukv26e7, ((((uint8_t *)&F.l4) + 64) + 8));
                F.l5 = t28;
                {
                    gm_b16_8 t30 = gm_f_empty_slice_lpi80k4();
                    gm_b16_8 t31 = t30;
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                    gm_l6 = t32;
                }
                {
                    void * t33 = gm_l0;
                    uint64_t t34 = gm_l1;
                    void * t35 = GM_LD(void *, (((uint8_t *)&F.l5) + 0));
                    gm_tqs5p4n2tq19v t36 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t33, t34, t35);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    gm_tqs5p4n2tq19v t37 = t36;
                    if (!((((uint8_t *)&t37))[1] != 0)) goto mn39;
                    uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)&t37));
                    if (!(t40 == ((uint8_t)1))) goto mn39;
                    {
                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                        gm_b16_8 t43 = t42;
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                        gm_l6 = t44;
                    }
                    goto me38;
mn39:;
                    if (!((((uint8_t *)&t37))[1] != 0)) goto mn45;
                    uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)&t37));
                    if (!(t46 == ((uint8_t)0))) goto mn45;
                    {
                        gm_th2lqbo77pvsc t48 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l5) + 24));
                        gm_th2lqbo77pvsc t49 = t48;
                        gm_b16_8 t50;
                        if (((((uint8_t *)&t49))[16] != 0)) {
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
                            t50 = t51;
                        } else {
                            gm_b16_8 t52 = gm_f_empty_slice_lpi80k4();
                            t50 = t52;
                        }
                        gm_b16_8 t53 = t50;
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                        gm_l6 = t54;
                    }
                    goto me38;
mn45:;
                    if (((((uint8_t *)&t37))[1] != 0)) goto mn55;
                    goto me38;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 492, 9);
me38:;
                }
                {
                    {
                        gm_b16_8 t58 = gm_l6;
                        gm_l7 = t58;
                    }
                    {
                        gm_l8 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t60 = gm_l8;
                            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            uint8_t t62 = (t60 < t61);
                            if (!t62) goto lb59;
                            {
                                uint64_t t65 = gm_l8;
                                uint64_t t66 = (uintptr_t)t65;
                                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                gm_tvpnktv5fo8nl t70 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t66 * 160));
                                F.l9 = t70;
                                {
                                    uint8_t t73;
                                    uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 64) + 0));
                                    if (t76 != ((uint8_t)1ULL)) goto mn75;
                                    t73 = ((uint8_t)1);
                                    goto me74;
mn75:;
                                    t73 = ((uint8_t)0);
                                    goto me74;
mn77:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 498, 16);
me74:;
                                    if (!t73) goto ie72;
                                    {
                                        void * t79 = (void *)((uint8_t *)&F.l2);
                                        gm_tftb1odc739t5 t80 = gm_f_push_1e500nc(t79, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                        gm_tftb1odc739t5 t81 = t80;
                                        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcc2qutubarto1, 499, 17);
                                        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                                    }
                                    goto ix72;
ie72:;
                                    {
                                        void * t84 = gm_l0;
                                        uint64_t t85 = gm_l1;
                                        gm_b0_1 t86 = gm_f_collect_item_d4tfq2h((void *)NULL, cx, NULL, t84, t85, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                        if (gm_unw.kind) {
                                            goto gm_ll63;
                                        }
                                    }
ix72:;
                                }
                                goto me68;
mn69:;
                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 497, 9);
me68:;
                            }
lc59:;
                            {
                                uint64_t t88 = gm_l8;
                                uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                                uint64_t t91 = r89;
                                gm_l8 = t91;
                            }
                        }
gm_ll63:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
                        }
                        goto gm_ll16;
lb59:;
                    }
                }
                goto ix26;
ie26:;
                {
                    goto lc12;
                }
ix26:;
            }
lc12:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
        }
        goto gm_lad;
lb12:;
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
inline __attribute__((aligned(4))) void gm_f_dep_dir_viuo66n(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dep_dir_viuo66n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* p */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t6j6smj7qoctu gm_l5; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint64_t t3 = gm_l1;
            uint64_t t4 = (uintptr_t)t3;
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 432) + 0) + 8);
            if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcc2qutubarto1, 674, 14);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 432) + 0)) + t4 * 56) + 16) + 0));
            gm_l3 = t7;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l4;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l4;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t6j6smj7qoctu t19 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 32));
                    gm_l5 = t19;
                    {
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                        gm_b16_8 t23 = gm_l2;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie21;
                        {
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                            gm_th2lqbo77pvsc t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t29), t28);
                            ((uint8_t *)&t29)[16] = 1;
                            uint8_t t30 = 0;
                            F.ret = t29;
                            goto gm_fret;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 674, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t32 = gm_l4;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l4 = t35;
                }
            }
lb8:;
        }
    }
    {
        gm_th2lqbo77pvsc t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_dep_dir_viuo66n_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_th2lqbo77pvsc gm_r; gm_f_dep_dir_viuo66n(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deps_of_i4no6j9 F;
    void * gm_l0; /* res */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
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
                    gm_tl7hpf5nlilqq t16 = GM_LD(gm_tl7hpf5nlilqq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 56));
                    F.l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l4) + 16));
                            gm_tqetrgo0q24b5 t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 44, 0);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[40] = 1;
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
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 92, 5);
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
        gm_tqetrgo0q24b5 t33;
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
__attribute__((aligned(4))) gm_tqetrgo0q24b5 gm_f_deps_of_i4no6j9_c(void * a0, gm_b16_8 a1) { gm_tqetrgo0q24b5 gm_r; gm_f_deps_of_i4no6j9(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_dir_gm_files_0s2opd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_gm_files_0s2opd8 F;
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hle6hel8vh71, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_entries_44o5n9f_sbacjg5((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
    }
    {
        gm_trqfvmjtt4sij t13 = F.l1;
        gm_ttevl7jqjigkp t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        ((uint8_t *)&t14)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t14 + 8), t13);
        gm_ttevl7jqjigkp t15 = t14;
        uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
        F.ret = t14;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_fugpeqi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_fugpeqi F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* mi */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* d */
    uint64_t gm_l10; /* t */
    gm_b16_8 gm_l11; /* target */
    GM_STACK_CHECK(&F, 1510u);
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
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 200) + 0));
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_to8irbp065ot2 t13;
                    uint64_t t14 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l3;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 184), 184);
                    gm_to8irbp065ot2 t18 = t13;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l4 = t21;
                    gm_tt0nlfbhb3hu6 t22 = GM_LD(gm_tt0nlfbhb3hu6, (((uint8_t *)&t18) + 8));
                    F.l5 = t22;
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 112));
                        if (!t25) goto ie24;
                        {
                            goto lc7;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    {
                        void * t27 = (void *)((uint8_t *)&F.l1);
                        gm_tfeab279c16rr t28;
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
                        void * t30 = gm_l0;
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 120));
                        gm_b16_8 t32 = gm_f_show_khjih0n((void *)NULL, cx, NULL, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t32);
                        void * t33 = gm_l0;
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 136));
                        uint64_t t35 = (uintptr_t)t34;
                        void * t36 = gm_l0;
                        uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 432) + 0) + 8);
                        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcc2qutubarto1, 777, 88);
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 432) + 0)) + t35 * 56) + 0));
                        gm_b16_8 t39 = gm_f_show_khjih0n((void *)NULL, cx, NULL, t33, t38);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t28 + 32, t39);
                        gm_tftb1odc739t5 t40 = gm_f_write_otk7ft7(t27, &t28);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcc2qutubarto1, 777, 9);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                    }
                    {
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 113));
                        if (!t44) goto ie43;
                        {
                            void * t46 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_srv05plrkjhf8));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(6));
                            gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t46, t47);
                            gm_tftb1odc739t5 t49 = t48;
                            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcc2qutubarto1, 778, 23);
                            gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    {
                        {
                            void * t52 = gm_l0;
                            uint64_t t53 = gm_l4;
                            gm_b16_8 t54 = gm_f_imports_of_5rkfdia((void *)NULL, cx, NULL, t52, t53);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                            gm_l6 = t54;
                        }
                        {
                            gm_l7 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t56 = gm_l7;
                                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                uint8_t t58 = (t56 < t57);
                                if (!t58) goto lb55;
                                {
                                    uint64_t t61 = gm_l7;
                                    uint64_t t62 = (uintptr_t)t61;
                                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t62 * 8));
                                    uint64_t t65 = t64;
                                    uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&t65));
                                    gm_l8 = t68;
                                    {
                                        uint64_t t70 = gm_l8;
                                        uint64_t t71 = (uintptr_t)t70;
                                        void * t72 = gm_l0;
                                        uint64_t t73 = GM_LD(uintptr_t, ((((uint8_t *)t72) + 240) + 0) + 8);
                                        if (t71 >= (uintptr_t)(t73)) gm_trap_index(t71, t73, gm_srcc2qutubarto1, 780, 19);
                                        void * t74 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t72) + 240) + 0)) + t71 * 216);
                                        gm_l9 = t74;
                                    }
                                    {
                                        void * t75 = gm_l9;
                                        gm_b16_8 t76;
                                        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t75) + 200) + 0));
                                        if (t79 != ((uint8_t)1ULL)) goto mn78;
                                        uint64_t t80 = GM_LD(uint64_t, ((((uint8_t *)t75) + 200) + 8));
                                        gm_l10 = t80;
                                        void * t81 = gm_l0;
                                        uint64_t t82 = gm_l10;
                                        uint64_t t83 = (uintptr_t)t82;
                                        void * t84 = gm_l0;
                                        uint64_t t85 = GM_LD(uintptr_t, ((((uint8_t *)t84) + 200) + 0) + 8);
                                        if (t83 >= (uintptr_t)(t85)) gm_trap_index(t83, t85, gm_srcc2qutubarto1, 782, 38);
                                        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t84) + 200) + 0)) + t83 * 184) + 120));
                                        gm_b16_8 t87 = gm_f_show_khjih0n((void *)NULL, cx, NULL, t81, t86);
                                        if (gm_unw.kind) {
                                            goto gm_ll59;
                                        }
                                        t76 = t87;
                                        goto me77;
mn78:;
                                        gm_b16_8 t89;
                                        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s79s3h98matgr));
                                        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(5));
                                        t76 = t89;
                                        goto me77;
mn88:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 781, 23);
me77:;
                                        gm_l11 = t76;
                                    }
                                    {
                                        void * t90 = (void *)((uint8_t *)&F.l1);
                                        void * t91 = gm_l9;
                                        gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t91) + 0) + 0));
                                        gm_tnpifv20irnph t93;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t92);
                                        gm_tftb1odc739t5 t94 = gm_f_write_gf33g1q(t90, t93);
                                        gm_tftb1odc739t5 t95 = t94;
                                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcc2qutubarto1, 785, 13);
                                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                                    }
                                    {
                                        void * t97 = gm_l9;
                                        void * t98 = (void *)(((uint8_t *)t97) + 40);
                                        gm_b16_8 t99 = gm_f_import_path_any_q8ttsvi(t98);
                                        void * t100 = (void *)((uint8_t *)&F.l1);
                                        gm_b0_1 t101 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t99, t100);
                                        if (gm_unw.kind) {
                                            goto gm_ll59;
                                        }
                                    }
                                    {
                                        void * t102 = (void *)((uint8_t *)&F.l1);
                                        gm_b16_8 t103 = gm_l11;
                                        gm_tnpifv20irnph t104;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                                        gm_tftb1odc739t5 t105 = gm_f_write_2l4ia51(t102, t104);
                                        gm_tftb1odc739t5 t106 = t105;
                                        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcc2qutubarto1, 787, 13);
                                        gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                                    }
                                    goto me66;
mn67:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 779, 9);
me66:;
                                }
lc55:;
                                {
                                    uint64_t t109 = gm_l7;
                                    uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                                    uint64_t t112 = r110;
                                    gm_l7 = t112;
                                }
                            }
gm_ll59:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb55; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc55; }
                            }
                            goto gm_ll11;
lb55:;
                        }
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 775, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t114 = gm_l3;
                    uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                    uint64_t t117 = r115;
                    gm_l3 = t117;
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
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t119 = t118;
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        uint8_t t121 = 0;
        F.ret = t120;
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
inline __attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_package_root_fs4fuqj F;
    gm_b16_8 gm_l0; /* start */
    gm_b16_8 gm_l1; /* abs */
    gm_th2lqbo77pvsc gm_l2; /* cur */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t3;
    }
    {
        gm_th2lqbo77pvsc t4;
        gm_b16_8 t6 = gm_l1;
        uint8_t t7 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t7) goto ie5;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        gm_th2lqbo77pvsc t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[16] = 1;
        t4 = t11;
        goto ix5;
ie5:;
        gm_b16_8 t13 = gm_l1;
        gm_th2lqbo77pvsc t14;
        gm_f_parent_lk0iero((void *)&t14, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t15 = t14;
        t4 = t15;
ix5:;
        gm_l2 = t4;
    }
    {
        for (;;) {
            if (!((((uint8_t *)&gm_l2))[16] != 0)) goto lb16;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_l3 = t17;
            {
                gm_b16_8 t21 = gm_l3;
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_se24c7hpp401n));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(10));
                gm_b16_8 t23 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t21, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                uint8_t t24 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t23);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                if (!t24) goto ie20;
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
                goto ix20;
ie20:;
ix20:;
            }
            {
                gm_b16_8 t29 = gm_l3;
                gm_f_parent_lk0iero((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l2)), (void *)NULL, cx, NULL, t29);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc16:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
        }
        goto gm_lad;
lb16:;
    }
    {
        gm_th2lqbo77pvsc t30;
        memset(&t30, 0, sizeof t30);
        uint8_t t31 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_bq6tmqb(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_bq6tmqb F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    GM_STACK_CHECK(&F, 409u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_te2n4j2vtm53d t6 = t5;
        gm_tibupt4dmcqai t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tibupt4dmcqai t8 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            {
                GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_load_package_modules_t4hh6gf *)((&F.h)->up))->l11), ((uint8_t)0));
            }
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_load_package_modules_t4hh6gf *)((&F.h)->up))->l9));
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t12 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t12;
    }
    {
        gm_b16_8 t14 = gm_l3;
        uint8_t t15 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t15) goto ie13;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        uint8_t t17 = gm_f_skipped_dir_orfng8m(t16);
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie13;
        gm_b16_8 t19 = gm_l3;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(10));
        gm_b16_8 t21 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = (uint8_t)!t22;
        if (!t23) goto ie13;
        {
            void * t25 = (void *)((uint8_t *)&((struct gm_fr_load_package_modules_t4hh6gf *)((&F.h)->up))->l10);
            gm_b16_8 t26 = gm_l3;
            gm_tftb1odc739t5 t27 = gm_f_push_3ef746b(t25, t26);
            gm_tftb1odc739t5 t28 = t27;
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcc2qutubarto1, 434, 17);
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
        }
        goto ix13;
ie13:;
ix13:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 427, 9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_fnpff4s F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* err */
    gm_tibupt4dmcqai gm_l3; /* e */
    gm_b16_8 gm_l4; /* p */
    GM_STACK_CHECK(&F, 504u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_te2n4j2vtm53d t6 = t5;
        gm_tibupt4dmcqai t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tibupt4dmcqai t8 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_toe5fmr3vvf4d t9 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t6) + 8));
            gm_l2 = t9;
            gm_toe5fmr3vvf4d t10 = gm_l2;
            gm_ttevl7jqjigkp t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t11 + 8), t10);
            gm_ttevl7jqjigkp t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            ((struct gm_fr_dir_gm_files_0s2opd8 *)((&F.h)->up))->ret = t11;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t13;
            goto gm_lad;
        }
        gm_l3 = t7;
    }
    {
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8);
        uint8_t t16 = (t15 > ((uint64_t)3ULL));
        if (!t16) goto ie14;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s0ucactj6hj0a));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
        uint8_t t19 = gm_f_ends_with_biriu5l(t17, t18);
        if (!t19) goto ie14;
        {
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_dir_gm_files_0s2opd8 *)((&F.h)->up))->l0));
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t23 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t23;
        }
        {
            gm_b16_8 t25 = gm_l4;
            uint8_t t26 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t26) goto ie24;
            {
                void * t28 = (void *)((uint8_t *)&((struct gm_fr_dir_gm_files_0s2opd8 *)((&F.h)->up))->l1);
                gm_b16_8 t29 = gm_l4;
                gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t28, t29);
                gm_tftb1odc739t5 t31 = t30;
                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcc2qutubarto1, 282, 29);
                gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
            }
            goto ix24;
ie24:;
ix24:;
        }
        goto ix14;
ie14:;
ix14:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 278, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_m8t719h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_m8t719h F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t9 = ((t8 == ((uint64_t)0ULL)));
        if (!t9) {
            uint8_t t10 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_snenkhfev166s, 2) == 0));
            t7 = t10;
        } else t7 = 1;
        if (!t7) {
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12 = t11;
            uint8_t t13 = 0;
            uint8_t t14 = ((uint8_t)92ULL);
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t12) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t12))) + i * 1, ((uint8_t *)&t14))) { t13 = 1; break; }
            uint8_t t15 = (uint8_t)(t13);
            t6 = t15;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            uint8_t t17 = 0;
            ((struct gm_fr_bad_rest_oc5mkat *)((&F.h)->up))->ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t17;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 666, 5);
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
__attribute__((aligned(4))) gm_t8c5h7u09n10a gm_f_import_decl_vuv14lf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, void * a3) {
    struct gm_fr_import_decl_vuv14lf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    void * gm_l3; /* b */
    gm_tl874t2atnlh3 gm_l5; /* between */
    gm_b16_8 gm_l6; /* text */
    gm_tibupt4dmcqai gm_l7; /* path */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 826u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l3;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 32) + 0) == ((uint8_t)1ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l3;
            gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t6) + 48));
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s6lnp3h2ojl1f));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(76));
            gm_f_error_s6hn871((void *)&F.l4, (void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t9 = gm_l3;
            uint32_t t10 = GM_LD(uint32_t, ((((uint8_t *)t9) + 48) + 0));
            void * t11 = gm_l2;
            uint32_t t12 = GM_LD(uint32_t, (((((uint8_t *)t11) + 0) + 16) + 8));
            void * t13 = gm_l3;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 40));
            uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t14) + 72) + 4));
            gm_tl874t2atnlh3 t16;
            GM_ST(uint32_t, ((uint8_t *)&t16 + 0), t10);
            GM_ST(uint32_t, ((uint8_t *)&t16 + 4), t12);
            GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t15);
            gm_l5 = t16;
        }
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
            void * t20 = gm_f_file_o15fqku(t18, t19);
            uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 16) + 8);
            void * t22 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 16)));
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
            uint64_t t24 = (uint64_t)t23;
            uint64_t t25 = (uintptr_t)t24;
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t27 = (uint64_t)t26;
            uint64_t t28 = (uintptr_t)t27;
            if (t25 > t28 || t28 > (uintptr_t)t21) gm_trap_slice(t25, t28, t21, gm_srcc2qutubarto1, 600, 17);
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t22 + t25 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t25));
            gm_l6 = t29;
        }
        {
            void * t31 = gm_l3;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 24));
            void * t33 = t32;
            uint8_t t34;
            if ((GM_LD(void *, ((uint8_t *)&t33)) != 0)) goto mn36;
            t34 = ((uint8_t)1);
            goto me35;
mn36:;
            t34 = ((uint8_t)0);
            goto me35;
mn37:;
            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 601, 12);
me35:;
            if (!t34) goto ie30;
            gm_b16_8 t38 = gm_l6;
            gm_b16_8 t39 = gm_f_trim_fdrfe6i(t38);
            gm_b16_8 t40 = t39;
            uint8_t t41 = (((GM_LD(uintptr_t, ((uint8_t *)&t40) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&t40)), gm_shge5n743ua4h, 2) == 0));
            if (!t41) goto ie30;
            {
                void * t43 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t44;
                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_spq5ja4c7ciga));
                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(10));
                gm_tl874t2atnlh3 t45 = gm_l5;
                gm_b16_8 t46;
                GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s2urait735ofq));
                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
                gm_b0_1 t47 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t43, t44, t45, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t48 = gm_l0;
            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 8));
            gm_f_push_o4ckdr1(t49, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
        }
        {
            gm_t8c5h7u09n10a t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)2ULL));
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t53 = gm_l3;
        void * t54 = GM_LD(void *, (((uint8_t *)t53) + 24));
        void * t55 = t54;
        uint8_t t56;
        if ((GM_LD(void *, ((uint8_t *)&t55)) != 0)) goto mn58;
        t56 = ((uint8_t)0);
        goto me57;
mn58:;
        t56 = ((uint8_t)1);
        goto me57;
mn59:;
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 605, 8);
me57:;
        if (!t56) goto ie52;
        {
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, (((uint8_t *)t61) + 8));
            void * t63 = gm_l3;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 40));
            gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_su5dv07glhc06));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(36));
            gm_b0_1 t67 = gm_f_report_eardf07((void *)NULL, cx, NULL, t62, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t8c5h7u09n10a t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t68 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        void * t71 = gm_l3;
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 40));
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 8) + 0));
        if (t73 != ((uint8_t)38ULL)) goto ie70;
        gm_tibupt4dmcqai t74 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t72) + 8) + 8));
        gm_l7 = t74;
        {
            void * t76 = gm_l0;
            uint64_t t77 = gm_l1;
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_b16_8 t79 = t78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            void * t81 = gm_l3;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 40));
            gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t82) + 72));
            gm_tf5srh4mp4b3i t84 = gm_f_import_module_8345fv4((void *)NULL, cx, NULL, t76, t77, t80, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t85 = t84;
            uint64_t t86;
            if (((((uint8_t *)&t85))[8] != 0)) {
                uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&t85) + 0));
                t86 = t87;
            } else {
                gm_t8c5h7u09n10a t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t88 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)2ULL));
                uint8_t t89 = 0;
                F.ret = t88;
                goto gm_fret;
            }
            gm_l8 = t86;
        }
        {
            uint64_t t90 = gm_l8;
            gm_t8c5h7u09n10a t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t91 + 8), t90);
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix70;
ie70:;
        {
            gm_t8c5h7u09n10a t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t94 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)2ULL));
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
ix70:;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_import_module_8345fv4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_import_module_8345fv4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* path */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* from_dir */
    uint64_t gm_l5; /* package */
    uint8_t gm_l6; /* relative */
    gm_th2lqbo77pvsc gm_l7; /* name */
    gm_b16_8 gm_l8; /* dir */
    gm_b16_8 gm_l9; /* canon */
    gm_b16_8 gm_l10; /* first */
    gm_b16_8 gm_l11; /* rest */
    gm_t6j6smj7qoctu gm_l12; /* split */
    gm_b16_8 gm_l13; /* base */
    uint64_t gm_l14; /* m */
    GM_STACK_CHECK(&F, 2094u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcc2qutubarto1, 616, 17);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 200) + 0)) + t3 * 184) + 120));
        gm_l4 = t6;
    }
    {
        uint64_t t7 = gm_l1;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 200) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcc2qutubarto1, 617, 16);
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 200) + 0)) + t8 * 184) + 136));
        gm_l5 = t11;
    }
    {
        uint8_t t12 = 0;
        gm_b16_8 t13 = gm_l2;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sdsu0imtl17e3));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(2));
        uint8_t t15 = gm_f_starts_with_6vvt01e(t13, t14);
        if (!t15) {
            gm_b16_8 t16 = gm_l2;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_strcbt1c3upss));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
            uint8_t t18 = gm_f_starts_with_6vvt01e(t16, t17);
            t12 = t18;
        } else t12 = 1;
        gm_l6 = t12;
    }
    {
        gm_th2lqbo77pvsc t19;
        memset(&t19, 0, sizeof t19);
        gm_l7 = t19;
    }
    {
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(0));
        gm_l8 = t20;
    }
    {
        uint8_t t22 = gm_l6;
        if (!t22) goto ie21;
        {
            gm_b16_8 t24 = gm_l4;
            gm_b16_8 t25 = gm_l2;
            gm_b16_8 t26 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t27 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t27;
        }
        {
            gm_b16_8 t28 = gm_l8;
            gm_th2lqbo77pvsc t29;
            gm_f_canonicalize_n3e3qri((void *)&t29, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t30 = t29;
            gm_b16_8 t31;
            if (((((uint8_t *)&t30))[16] != 0)) {
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 0));
                t31 = t32;
            } else {
                gm_b16_8 t33 = gm_l8;
                gm_b16_8 t34 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t31 = t34;
            }
            gm_l9 = t31;
        }
        {
            gm_b16_8 t36 = gm_l9;
            uint64_t t37 = gm_l5;
            uint64_t t38 = (uintptr_t)t37;
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 432) + 0) + 8);
            if (t38 >= (uintptr_t)(t40)) gm_trap_index(t38, t40, gm_srcc2qutubarto1, 624, 35);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t39) + 432) + 0)) + t38 * 56) + 0));
            uint8_t t42 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t36, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = (uint8_t)!t42;
            if (!t43) goto ie35;
            {
                void * t45 = gm_l0;
                void * t46 = GM_LD(void *, (((uint8_t *)t45) + 8));
                gm_tl874t2atnlh3 t47 = gm_l3;
                gm_b16_8 t48 = gm_l2;
                gm_tnpifv20irnph t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                gm_tabb710d9mq2e t50;
                gm_f_format_cgpq8i6((void *)&t50, (void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t51 = t50;
                if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srcc2qutubarto1, 625, 32);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                gm_b16_8 t53 = t52;
                gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                gm_b0_1 t55 = gm_f_report_eardf07((void *)NULL, cx, NULL, t46, t47, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tf5srh4mp4b3i t56;
                memset(&t56, 0, sizeof t56);
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix35;
ie35:;
ix35:;
        }
        goto ix21;
ie21:;
        {
            gm_b16_8 t59 = gm_l2;
            gm_l10 = t59;
        }
        {
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(0));
            gm_l11 = t60;
        }
        {
            gm_b16_8 t62 = gm_l2;
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
            gm_ttu09lij36f8c t64;
            gm_f_split_once_mqcu1ce((void *)&t64, t62, t63);
            gm_ttu09lij36f8c t65 = t64;
            if (!((((uint8_t *)&t65))[32] != 0)) goto ie61;
            gm_t6j6smj7qoctu t66 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t65));
            gm_l12 = t66;
            {
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                gm_l10 = t68;
            }
            {
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                gm_l11 = t69;
            }
            goto ix61;
ie61:;
ix61:;
        }
        {
            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
            uint8_t t72 = (t71 > ((uint64_t)0ULL));
            if (!t72) goto ie70;
            gm_b16_8 t73 = gm_l11;
            uint8_t t74 = gm_f_bad_rest_oc5mkat(t73);
            if (!t74) goto ie70;
            {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)t76) + 8));
                gm_tl874t2atnlh3 t78 = gm_l3;
                gm_t6j6smj7qoctu t79;
                gm_b16_8 t80 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t80);
                gm_b16_8 t81 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t79 + 16, t81);
                gm_tabb710d9mq2e t82;
                gm_f_format_ajjbrpr((void *)&t82, (void *)NULL, cx, NULL, &t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t83 = t82;
                if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcc2qutubarto1, 636, 32);
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
                gm_b16_8 t85 = t84;
                gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                gm_b0_1 t87 = gm_f_report_eardf07((void *)NULL, cx, NULL, t77, t78, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tf5srh4mp4b3i t88;
                memset(&t88, 0, sizeof t88);
                uint8_t t89 = 0;
                F.ret = t88;
                goto gm_fret;
            }
            goto ix70;
ie70:;
ix70:;
        }
        {
            void * t90 = gm_l0;
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)t90) + 152));
            gm_l13 = t91;
        }
        {
            uint8_t t93 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l10)), gm_s8sv6qdepk5ba, 3) == 0));
            if (!t93) goto ie92;
            {
                void * t95 = gm_l0;
                uint64_t t96 = gm_l5;
                gm_b16_8 t97 = gm_l10;
                gm_th2lqbo77pvsc t98;
                gm_f_dep_dir_viuo66n((void *)&t98, t95, t96, t97);
                gm_th2lqbo77pvsc t99 = t98;
                gm_b16_8 t100;
                if (((((uint8_t *)&t99))[16] != 0)) {
                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 0));
                    t100 = t101;
                } else {
                    {
                        void * t103 = gm_l0;
                        void * t104 = GM_LD(void *, (((uint8_t *)t103) + 8));
                        gm_tl874t2atnlh3 t105 = gm_l3;
                        gm_b16_8 t106 = gm_l10;
                        gm_tnpifv20irnph t107;
                        GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
                        gm_tabb710d9mq2e t108;
                        gm_f_format_qpmmpss((void *)&t108, (void *)NULL, cx, NULL, t107);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t109 = t108;
                        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srcc2qutubarto1, 646, 36);
                        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                        gm_b16_8 t111 = t110;
                        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                        gm_b0_1 t113 = gm_f_report_eardf07((void *)NULL, cx, NULL, t104, t105, t112);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_tf5srh4mp4b3i t114;
                    memset(&t114, 0, sizeof t114);
                    uint8_t t115 = 0;
                    F.ret = t114;
                    goto gm_fret;
                }
                gm_l13 = t100;
            }
            {
                uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t118 = ((t117 == ((uint64_t)0ULL)));
                if (!t118) goto ie116;
                {
                    gm_b16_8 t120 = gm_l10;
                    gm_th2lqbo77pvsc t121;
                    GM_ST(uint8_t, (uint8_t *)&t121 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t121 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t121 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t121), t120);
                    ((uint8_t *)&t121)[16] = 1;
                    gm_l7 = t121;
                }
                goto ix116;
ie116:;
ix116:;
            }
            goto ix92;
ie92:;
ix92:;
        }
        {
            gm_b16_8 t122;
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)0ULL)));
            if (!t125) goto ie123;
            gm_b16_8 t127 = gm_l13;
            gm_b16_8 t128 = t127;
            t122 = t128;
            goto ix123;
ie123:;
            gm_b16_8 t130 = gm_l13;
            gm_b16_8 t131 = gm_l11;
            gm_b16_8 t132 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t130, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t133 = t132;
            t122 = t133;
ix123:;
            gm_b16_8 t134 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t134;
        }
ix21:;
    }
    {
        void * t135 = gm_l0;
        gm_b16_8 t136 = gm_l8;
        gm_tl874t2atnlh3 t137 = gm_l3;
        gm_tf5srh4mp4b3i t138;
        uint8_t t140 = gm_l6;
        if (!t140) goto ie139;
        uint64_t t142 = gm_l5;
        uint64_t t143 = t142;
        gm_tf5srh4mp4b3i t144;
        GM_ST(uint8_t, (uint8_t *)&t144 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t144 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t144 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t144), t143);
        ((uint8_t *)&t144)[8] = 1;
        t138 = t144;
        goto ix139;
ie139:;
        gm_tf5srh4mp4b3i t146;
        memset(&t146, 0, sizeof t146);
        gm_tf5srh4mp4b3i t147 = t146;
        t138 = t147;
ix139:;
        gm_tf5srh4mp4b3i t148 = gm_f_load_dir_module_a75lmvr((void *)NULL, cx, NULL, t135, t136, t137, t138, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t149 = t148;
        uint64_t t150;
        if (((((uint8_t *)&t149))[8] != 0)) {
            uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&t149) + 0));
            t150 = t151;
        } else {
            gm_tf5srh4mp4b3i t152;
            memset(&t152, 0, sizeof t152);
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
        }
        gm_l14 = t150;
    }
    {
        void * t154 = gm_l0;
        void * t155 = (void *)(((uint8_t *)t154) + 512);
        gm_t0nqnmillkbs6 t156;
        memset((uint8_t *)&t156 + 28, 0, 4);
        uint64_t t157 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t156 + 0, t157);
        uint64_t t158 = gm_l14;
        GM_ST(uint64_t, (uint8_t *)&t156 + 8, t158);
        gm_tl874t2atnlh3 t159 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t156 + 16, t159);
        gm_tftb1odc739t5 t160 = gm_f_push_89v6mq9(t155, &t156);
        gm_tftb1odc739t5 t161 = t160;
        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcc2qutubarto1, 659, 5);
        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
    }
    {
        uint64_t t163 = gm_l14;
        gm_tf5srh4mp4b3i t164;
        GM_ST(uint8_t, (uint8_t *)&t164 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t164 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t164 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t164), t163);
        ((uint8_t *)&t164)[8] = 1;
        uint8_t t165 = 0;
        F.ret = t164;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_import_path_any_q8ttsvi(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_import_path_any_q8ttsvi F;
    void * gm_l0; /* item */
    gm_tibupt4dmcqai gm_l2; /* path */
    GM_STACK_CHECK_SHALLOW(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 64) + 0));
        if (t4 != ((uint8_t)0ULL)) goto ie2;
        gm_tv2uf4leil5hq t5 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t3) + 64) + 8));
        F.l1 = t5;
        {
            void * t8 = GM_LD(void *, (((uint8_t *)&F.l1) + 40));
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            if (t9 != ((uint8_t)38ULL)) goto ie7;
            gm_tibupt4dmcqai t10 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t8) + 8) + 8));
            gm_l2 = t10;
            {
                gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_b16_8 t13 = t12;
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
                uint8_t t15 = 0;
                F.ret = t14;
                goto gm_fret;
            }
            goto ix7;
ie7:;
            {
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
            }
ix7:;
        }
        goto ix2;
ie2:;
        {
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(0));
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_import_path_na25mjr(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_import_path_na25mjr F;
    void * gm_l0; /* item */
    gm_tibupt4dmcqai gm_l2; /* path */
    GM_STACK_CHECK_SHALLOW(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 40) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 64) + 0));
        if (t11 != ((uint8_t)0ULL)) goto ie9;
        gm_tv2uf4leil5hq t12 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t10) + 64) + 8));
        F.l1 = t12;
        {
            void * t15 = GM_LD(void *, (((uint8_t *)&F.l1) + 24));
            void * t16 = t15;
            uint8_t t17;
            if ((GM_LD(void *, ((uint8_t *)&t16)) != 0)) goto mn19;
            t17 = ((uint8_t)0);
            goto me18;
mn19:;
            t17 = ((uint8_t)1);
            goto me18;
mn20:;
            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 562, 8);
me18:;
            if (!t17) goto ie14;
            {
                gm_th2lqbo77pvsc t22;
                memset(&t22, 0, sizeof t22);
                uint8_t t23 = 0;
                F.ret = t22;
                goto gm_fret;
            }
            goto ix14;
ie14:;
ix14:;
        }
        {
            void * t25 = GM_LD(void *, (((uint8_t *)&F.l1) + 40));
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            if (t26 != ((uint8_t)38ULL)) goto ie24;
            gm_tibupt4dmcqai t27 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t25) + 8) + 8));
            gm_l2 = t27;
            {
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_b16_8 t30 = t29;
                gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                gm_th2lqbo77pvsc t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t32), t31);
                ((uint8_t *)&t32)[16] = 1;
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix24;
ie24:;
            {
                gm_th2lqbo77pvsc t35;
                memset(&t35, 0, sizeof t35);
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
ix24:;
        }
        goto ix9;
ie9:;
        {
            gm_th2lqbo77pvsc t38;
            memset(&t38, 0, sizeof t38);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
ix9:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_import_path_na25mjr_c(void * a0) { gm_th2lqbo77pvsc gm_r; gm_f_import_path_na25mjr(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_imports_of_5rkfdia(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_imports_of_5rkfdia F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t3877pk7lv748 gm_l5; /* n */
    GM_STACK_CHECK(&F, 319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_5dhgg4i((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t3 = gm_l1;
            uint64_t t4 = (uintptr_t)t3;
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 200) + 0) + 8);
            if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcc2qutubarto1, 204, 14);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 200) + 0)) + t4 * 184) + 16) + 0));
            gm_l3 = t7;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l4;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l4;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t3877pk7lv748 t19 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 24));
                    gm_l5 = t19;
                    {
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 240) + 0) + 8);
                        if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcc2qutubarto1, 205, 23);
                        void * t26 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 240) + 0)) + t23 * 216) + 40);
                        uint8_t t27 = gm_f_is_import_1c0aicu(t26);
                        if (!t27) goto ie21;
                        {
                            void * t29 = (void *)((uint8_t *)&F.l2);
                            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                            gm_tftb1odc739t5 t31 = gm_f_push_ucgmlo2(t29, t30);
                            gm_tftb1odc739t5 t32 = t31;
                            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcc2qutubarto1, 205, 53);
                            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 204, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t35 = gm_l4;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l4 = t38;
                }
            }
lb8:;
        }
    }
    {
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_f_sort_o401rjn(t39);
    }
    {
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t41 = 0;
        F.ret = t40;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_import_1c0aicu(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_reserved_type_name_tuq4qsj F;
    gm_b16_8 gm_l0; /* name */
    uint32_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 775u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_tk6rvhh54g86p t4;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_shnig5tqtdedi));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 32, t7);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sm9e81djuncnu));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 48, t8);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sqqajgod2bb4o));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 64, t9);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 80, t10);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s3cv29r83pgn4));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 96, t11);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 112, t12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_slolcr6ikvg33));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 128, t13);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_st2dnokm1h0jd));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 144, t14);
        gm_tk6rvhh54g86p t15 = t4;
        uint8_t t16 = 0;
        gm_b16_8 t17 = t3;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 0, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 16, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 32, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 48, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 64, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 80, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 96, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 112, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 128, ((uint8_t *)&t17))) t16 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t15) + 144, ((uint8_t *)&t17))) t16 = 1;
        uint8_t t18 = (uint8_t)(t16);
        if (!t18) goto ie2;
        {
            uint8_t t20 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t22 = 0;
        uint8_t t23 = 0;
        uint8_t t24 = 0;
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t26 = (t25 < ((uint64_t)2ULL));
        if (!t26) {
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcc2qutubarto1, 571, 24);
            uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t27 * 1));
            gm_tqs5p4n2tq19v t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t30 + 1), ((uint8_t)105ULL));
            gm_tqs5p4n2tq19v t31 = t30;
            uint8_t t32 = 0;
            uint8_t t33 = t29;
            if (gm_eqjmccqdb90f(((uint8_t *)&t31) + 0, ((uint8_t *)&t33))) t32 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t31) + 1, ((uint8_t *)&t33))) t32 = 1;
            uint8_t t34 = (uint8_t)(!t32);
            t24 = t34;
        } else t24 = 1;
        if (!t24) {
            uint64_t t35 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srcc2qutubarto1, 571, 53);
            uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t35 * 1));
            uint8_t t38 = ((t37 == ((uint8_t)48ULL)));
            t23 = t38;
        } else t23 = 1;
        if (!t23) {
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t40 = (t39 > ((uint64_t)4ULL));
            t22 = t40;
        } else t22 = 1;
        if (!t22) goto ie21;
        {
            uint8_t t42 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_l1 = ((uint32_t)0ULL);
    }
    {
        {
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t46 = (uintptr_t)((uint64_t)1ULL);
            if (t46 > (uintptr_t)t44 || (uintptr_t)t44 > (uintptr_t)t44) gm_trap_slice(t46, (uintptr_t)t44, t44, gm_srcc2qutubarto1, 573, 14);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t45 + t46 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)((uintptr_t)t44 - t46));
            gm_l2 = t47;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l3;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l3;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t57 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t55 * 1));
                    uint8_t t58 = t57;
                    uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)&t58));
                    gm_l4 = t61;
                    {
                        {
                            uint8_t t65 = gm_l4;
                            gm_l5 = t65;
                        }
                        uint8_t t66 = 0;
                        uint8_t t67 = gm_l5;
                        uint8_t t68 = (((uint8_t)48ULL) <= t67);
                        if (t68) {
                            uint8_t t69 = gm_l5;
                            uint8_t t70 = (t69 <= ((uint8_t)57ULL));
                            t66 = t70;
                        } else t66 = 0;
                        uint8_t t71 = (uint8_t)!t66;
                        uint8_t t72 = t71;
                        if (!t72) goto ie63;
                        {
                            uint8_t t74 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    {
                        uint32_t t75 = gm_l1;
                        uint32_t r76; bool o77 = __builtin_mul_overflow(t75, ((uint32_t)10ULL), &r76);
                        if (o77) gm_trap("integer overflow in `*`", gm_srcc2qutubarto1, 575, 16);
                        uint32_t t78 = r76;
                        uint8_t t79 = gm_l4;
                        uint8_t r80; bool o81 = __builtin_sub_overflow(t79, ((uint8_t)48ULL), &r80);
                        if (o81) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 575, 32);
                        uint8_t t82 = r80;
                        uint32_t t83 = (uint32_t)t82;
                        uint32_t r84; bool o85 = __builtin_add_overflow(t78, t83, &r84);
                        if (o85) gm_trap("integer overflow in `+`", gm_srcc2qutubarto1, 575, 16);
                        uint32_t t86 = r84;
                        gm_l1 = t86;
                    }
                    goto me59;
mn60:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 573, 5);
me59:;
                }
lc48:;
                {
                    uint64_t t88 = gm_l3;
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                    uint64_t t91 = r89;
                    gm_l3 = t91;
                }
            }
lb48:;
        }
    }
    {
        {
            uint32_t t93 = gm_l1;
            gm_l6 = t93;
        }
        uint8_t t94 = 0;
        uint32_t t95 = gm_l6;
        uint8_t t96 = (((uint32_t)1ULL) <= t95);
        if (t96) {
            uint32_t t97 = gm_l6;
            uint8_t t98 = (t97 <= ((uint32_t)128ULL));
            t94 = t98;
        } else t94 = 0;
        uint8_t t99 = t94;
        uint8_t t100 = 0;
        F.ret = t99;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_dir_module_a75lmvr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_tf5srh4mp4b3i a3, const void * a4) {
    struct gm_fr_load_dir_module_a75lmvr F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* dir */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_tf5srh4mp4b3i gm_l3; /* parent_package */
    gm_th2lqbo77pvsc gm_l4; /* name */
    gm_b16_8 gm_l5; /* key */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t3877pk7lv748 gm_l8; /* d */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    gm_b16_8 gm_l14; /* module_name */
    uint64_t gm_l15; /* package */
    uint64_t gm_l16; /* m */
    GM_STACK_CHECK(&F, 2054u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    {
        gm_b16_8 t2 = gm_l1;
        gm_th2lqbo77pvsc t3;
        gm_f_canonicalize_n3e3qri((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_l1;
            t5 = t7;
        }
        gm_l5 = t5;
    }
    {
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 472) + 0));
            gm_l6 = t10;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l7;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l7;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t3877pk7lv748 t22 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t18 * 24));
                    gm_l8 = t22;
                    {
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                        gm_b16_8 t26 = gm_l5;
                        gm_b16_8 t27 = t25;
                        gm_b16_8 t28 = t26;
                        uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
                        if (!t29) goto ie24;
                        {
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 16));
                            gm_tf5srh4mp4b3i t32;
                            GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t32), t31);
                            ((uint8_t *)&t32)[8] = 1;
                            uint8_t t33 = 0;
                            F.ret = t32;
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 383, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t35 = gm_l7;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l7 = t38;
                }
            }
lb11:;
        }
    }
    {
        gm_b16_8 t39 = gm_l1;
        void * t40 = gm_l0;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)t40) + 32) + 0));
        gm_b16_8 t42 = t41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_tqetrgo0q24b5 t44;
        gm_f_module_files_with_abkh6r4((void *)&t44, (void *)NULL, cx, NULL, t39, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqetrgo0q24b5 t45 = t44;
        gm_trqfvmjtt4sij t46;
        if (((((uint8_t *)&t45))[40] != 0)) {
            gm_trqfvmjtt4sij t47 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t45) + 0));
            t46 = t47;
        } else {
            {
                void * t49 = gm_l0;
                void * t50 = GM_LD(void *, (((uint8_t *)t49) + 8));
                gm_tl874t2atnlh3 t51 = gm_l2;
                gm_b16_8 t52 = gm_l1;
                gm_tnpifv20irnph t53;
                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                gm_tabb710d9mq2e t54;
                gm_f_format_atch2pp((void *)&t54, (void *)NULL, cx, NULL, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t55 = t54;
                if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srcc2qutubarto1, 387, 28);
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                gm_b16_8 t57 = t56;
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                gm_b0_1 t59 = gm_f_report_eardf07((void *)NULL, cx, NULL, t50, t51, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tf5srh4mp4b3i t60;
            memset(&t60, 0, sizeof t60);
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        F.l9 = t46;
    }
    {
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_f_sort_skeree7(t62);
    }
    {
        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint8_t t65 = ((t64 == ((uint64_t)0ULL)));
        if (!t65) goto ie63;
        {
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 8));
            gm_tl874t2atnlh3 t69 = gm_l2;
            gm_b16_8 t70 = gm_l1;
            gm_tnpifv20irnph t71;
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
            gm_tabb710d9mq2e t72;
            gm_f_format_t2lplfr((void *)&t72, (void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t73 = t72;
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcc2qutubarto1, 392, 28);
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
            gm_b16_8 t75 = t74;
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            gm_b0_1 t77 = gm_f_report_eardf07((void *)NULL, cx, NULL, t68, t69, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tf5srh4mp4b3i t78;
            memset(&t78, 0, sizeof t78);
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        gm_f_list_j6fu443((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_l11 = t81;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t83 = gm_l12;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t85 = (t83 < t84);
                if (!t85) goto lb82;
                {
                    uint64_t t88 = gm_l12;
                    uint64_t t89 = (uintptr_t)t88;
                    uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t89 * 16));
                    gm_l13 = t93;
                    {
                        void * t95 = (void *)((uint8_t *)&F.l10);
                        void * t96 = gm_l0;
                        gm_b16_8 t97 = gm_l13;
                        gm_t1s75i08qtptd t98;
                        gm_f_load_file_rdflf4e((void *)&t98, (void *)NULL, cx, NULL, t96, t97);
                        if (gm_unw.kind) {
                            goto gm_ll86;
                        }
                        gm_t1s75i08qtptd t99 = t98;
                        gm_t08olq97rscn3 t100;
                        if (((((uint8_t *)&t99))[32] != 0)) {
                            gm_t08olq97rscn3 t101 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t99) + 0));
                            t100 = t101;
                        } else {
                            gm_tf5srh4mp4b3i t102;
                            memset(&t102, 0, sizeof t102);
                            uint8_t t103 = 0;
                            F.ret = t102;
                            goto gm_fret;
                        }
                        gm_tftb1odc739t5 t104 = gm_f_push_5h5ofgv(t95, &t100);
                        gm_tftb1odc739t5 t105 = t104;
                        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srcc2qutubarto1, 396, 28);
                        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 396, 5);
me91:;
                }
lc82:;
                {
                    uint64_t t108 = gm_l12;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l12 = t111;
                }
            }
gm_ll86:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
            }
            goto gm_lad;
lb82:;
        }
    }
    {
        gm_th2lqbo77pvsc t112 = gm_l4;
        gm_th2lqbo77pvsc t113 = t112;
        gm_b16_8 t114;
        if (((((uint8_t *)&t113))[16] != 0)) {
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 0));
            t114 = t115;
        } else {
            gm_b16_8 t116 = gm_l5;
            gm_th2lqbo77pvsc t117;
            gm_f_file_name_em7v8lv((void *)&t117, (void *)NULL, cx, NULL, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t118 = t117;
            gm_b16_8 t119;
            if (((((uint8_t *)&t118))[16] != 0)) {
                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 0));
                t119 = t120;
            } else {
                gm_b16_8 t121;
                GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sbgl5dgur8udg));
                GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(6));
                t119 = t121;
            }
            t114 = t119;
        }
        gm_l14 = t114;
    }
    {
        void * t122 = gm_l0;
        gm_b16_8 t123 = gm_l5;
        gm_tf5srh4mp4b3i t124 = gm_l3;
        uint64_t t125 = gm_f_package_of_hul50on((void *)NULL, cx, NULL, t122, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t125;
    }
    {
        void * t126 = gm_l0;
        gm_b16_8 t127 = gm_l14;
        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t129 = t128;
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
        uint64_t t131 = gm_f_add_module_3cgsegf((void *)NULL, cx, NULL, t126, t127, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t131;
    }
    {
        uint64_t t132 = gm_l16;
        uint64_t t133 = (uintptr_t)t132;
        void * t134 = gm_l0;
        uint64_t t135 = GM_LD(uintptr_t, ((((uint8_t *)t134) + 200) + 0) + 8);
        if (t133 >= (uintptr_t)(t135)) gm_trap_index(t133, t135, gm_srcc2qutubarto1, 400, 5);
        gm_b16_8 t136 = gm_l1;
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t134) + 200) + 0)) + t133 * 184) + 120), t136);
    }
    {
        uint64_t t137 = gm_l16;
        uint64_t t138 = (uintptr_t)t137;
        void * t139 = gm_l0;
        uint64_t t140 = GM_LD(uintptr_t, ((((uint8_t *)t139) + 200) + 0) + 8);
        if (t138 >= (uintptr_t)(t140)) gm_trap_index(t138, t140, gm_srcc2qutubarto1, 402, 5);
        uint8_t *t141 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t139) + 200) + 0)) + t138 * 184) + 113);
        gm_b16_8 t142 = gm_l5;
        void * t143 = gm_l0;
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t143) + 168));
        uint8_t t145 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t142, t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, t141, t145);
    }
    {
        uint64_t t146 = gm_l16;
        uint64_t t147 = (uintptr_t)t146;
        void * t148 = gm_l0;
        uint64_t t149 = GM_LD(uintptr_t, ((((uint8_t *)t148) + 200) + 0) + 8);
        if (t147 >= (uintptr_t)(t149)) gm_trap_index(t147, t149, gm_srcc2qutubarto1, 403, 5);
        uint64_t t150 = gm_l15;
        GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t148) + 200) + 0)) + t147 * 184) + 136), t150);
    }
    {
        void * t151 = gm_l0;
        void * t152 = (void *)(((uint8_t *)t151) + 472);
        gm_t3877pk7lv748 t153;
        gm_b16_8 t154 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t153 + 0, t154);
        uint64_t t155 = gm_l16;
        GM_ST(uint64_t, (uint8_t *)&t153 + 16, t155);
        gm_tftb1odc739t5 t156 = gm_f_push_46vfpsn(t152, &t153);
        gm_tftb1odc739t5 t157 = t156;
        if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_srcc2qutubarto1, 404, 5);
        gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
    }
    {
        void * t159 = gm_l0;
        uint64_t t160 = gm_l16;
        gm_b0_1 t161 = gm_f_collect_whens_ktf37vb((void *)NULL, cx, NULL, t159, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t162 = gm_l16;
        gm_tf5srh4mp4b3i t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t163 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t163), t162);
        ((uint8_t *)&t163)[8] = 1;
        uint8_t t164 = 0;
        F.ret = t163;
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
inline __attribute__((aligned(4))) void gm_f_load_file_rdflf4e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_file_rdflf4e F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* text */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* text */
    GM_STACK_CHECK(&F, 811u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 32) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = gm_l1;
        gm_th2lqbo77pvsc t8;
        gm_f_overlay_text_haaaurk((void *)&t8, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t9 = t8;
        if (!((((uint8_t *)&t9))[16] != 0)) goto ie2;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_l2 = t10;
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = gm_l1;
            gm_b16_8 t14 = gm_l2;
            gm_t08olq97rscn3 t15;
            gm_f_parse_text_j7gp86k((void *)&t15, (void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1s75i08qtptd t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[32] = 1;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t18 = gm_l1;
        gm_tgvh15qshf643 t19;
        gm_f_read_all_r9rh9qd((void *)&t19, (void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t20 = t19;
        gm_b16_8 t21;
        if (!((((uint8_t *)&t20))[0] != 0)) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            t21 = t22;
        } else {
            gm_tj5jn79kq9pnm t23 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t20) + 8));
            gm_l3 = t23;
            {
                void * t25 = gm_l0;
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 8));
                gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_t6j6smj7qoctu t28;
                gm_b16_8 t29 = gm_l1;
                GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
                gm_tj5jn79kq9pnm t30 = gm_l3;
                gm_b16_8 t31 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t31);
                gm_tabb710d9mq2e t32;
                gm_f_format_04pispc((void *)&t32, (void *)NULL, cx, NULL, &t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t33 = t32;
                if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srcc2qutubarto1, 335, 38);
                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
                gm_b16_8 t35 = t34;
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_b0_1 t37 = gm_f_report_eardf07((void *)NULL, cx, NULL, t26, t27, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t1s75i08qtptd t38;
            memset(&t38, 0, sizeof t38);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        gm_l4 = t21;
    }
    {
        void * t40 = gm_l0;
        gm_b16_8 t41 = gm_l1;
        gm_b16_8 t42 = gm_l4;
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        gm_t08olq97rscn3 t45;
        gm_f_parse_text_j7gp86k((void *)&t45, (void *)NULL, cx, NULL, t40, t41, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1s75i08qtptd t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 36, 0);
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t46), t45);
        ((uint8_t *)&t46)[32] = 1;
        uint8_t t47 = 0;
        F.ret = t46;
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
inline __attribute__((aligned(4))) void gm_f_load_modules_2vujekg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_modules_2vujekg F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* root */
    uint64_t gm_l2; /* done */
    uint64_t gm_l3; /* m */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* d */
    GM_STACK_CHECK(&F, 437u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = gm_l1;
            gm_tf5srh4mp4b3i t5 = gm_f_load_root_af4ud7b((void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t6 = t5;
            uint64_t t7;
            if (((((uint8_t *)&t6))[8] != 0)) {
                uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
                t7 = t8;
            } else {
                gm_th2lqbo77pvsc t9;
                memset(&t9, 0, sizeof t9);
                uint8_t t10 = 0;
                F.ret = t9;
                goto gm_fret;
            }
        }
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t12 = gm_l2;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 200) + 0) + 8);
            uint8_t t15 = (t12 < t14);
            if (!t15) goto lb11;
            {
                uint64_t t18 = gm_l2;
                gm_l3 = t18;
            }
            {
                uint64_t t19 = gm_l2;
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srcc2qutubarto1, 193, 9);
                uint64_t t22 = r20;
                gm_l2 = t22;
            }
            {
                uint64_t t24 = gm_l3;
                uint64_t t25 = (uintptr_t)t24;
                void * t26 = gm_l0;
                uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 200) + 0) + 8);
                if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcc2qutubarto1, 194, 12);
                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 200) + 0)) + t25 * 184) + 112));
                if (!t28) goto ie23;
                {
                    goto lc11;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                {
                    void * t31 = gm_l0;
                    uint64_t t32 = gm_l3;
                    gm_b16_8 t33 = gm_f_imports_of_5rkfdia((void *)NULL, cx, NULL, t31, t32);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    gm_l4 = t33;
                }
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t35 = gm_l5;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t37 = (t35 < t36);
                        if (!t37) goto lb34;
                        {
                            uint64_t t40 = gm_l5;
                            uint64_t t41 = (uintptr_t)t40;
                            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t41 * 8));
                            uint64_t t44 = t43;
                            uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&t44));
                            gm_l6 = t47;
                            {
                                void * t49 = gm_l0;
                                uint64_t t50 = gm_l6;
                                gm_b0_1 t51 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t49, t50);
                                if (gm_unw.kind) {
                                    goto gm_ll38;
                                }
                            }
                            goto me45;
mn46:;
                            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 195, 9);
me45:;
                        }
lc34:;
                        {
                            uint64_t t53 = gm_l5;
                            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                            uint64_t t56 = r54;
                            gm_l5 = t56;
                        }
                    }
gm_ll38:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
                    }
                    goto gm_ll16;
lb34:;
                }
            }
lc11:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
    }
    {
        void * t57 = gm_l0;
        gm_b0_1 t58 = gm_f_check_import_cycles_2u7oqo0((void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t59 = gm_l0;
        gm_b16_8 t60 = gm_f_dump_fugpeqi((void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t61 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t61), t60);
        ((uint8_t *)&t61)[16] = 1;
        uint8_t t62 = 0;
        F.ret = t61;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_load_package_modules_t4hh6gf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_load_package_modules_t4hh6gf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* root */
    uint64_t gm_l2; /* package */
    gm_b16_8 gm_l3; /* package_root */
    gm_b16_8 gm_l4; /* start */
    gm_b16_8 gm_l5; /* rel */
    gm_b16_8 gm_l6; /* root_dir */
    gm_b16_8 gm_l7; /* root_key */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* key */
    uint8_t gm_l14; /* loaded */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t3877pk7lv748 gm_l17; /* d */
    GM_STACK_CHECK(&F, 1493u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcc2qutubarto1, 414, 16);
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 200) + 0)) + t3 * 184) + 136));
        gm_l2 = t6;
    }
    {
        uint64_t t7 = gm_l2;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 432) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcc2qutubarto1, 415, 21);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 432) + 0)) + t8 * 56) + 0));
        gm_l3 = t11;
    }
    {
        gm_b16_8 t12 = gm_l3;
        gm_l4 = t12;
    }
    {
        gm_b16_8 t14 = gm_l3;
        void * t15 = gm_l0;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 184));
        gm_th2lqbo77pvsc t17;
        gm_f_strip_prefix_vl3t0h4((void *)&t17, (void *)NULL, cx, NULL, t14, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t18 = t17;
        if (!((((uint8_t *)&t18))[16] != 0)) goto ie13;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_l5 = t19;
        {
            gm_b16_8 t21;
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
            if (!t24) goto ie22;
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
            gm_b16_8 t27 = t26;
            t21 = t27;
            goto ix22;
ie22:;
            gm_b16_8 t29 = gm_l5;
            gm_b16_8 t30 = t29;
            t21 = t30;
ix22:;
            gm_l4 = t21;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint64_t t31 = gm_l1;
        uint64_t t32 = (uintptr_t)t31;
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 200) + 0) + 8);
        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srcc2qutubarto1, 420, 17);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 200) + 0)) + t32 * 184) + 120));
        gm_l6 = t35;
    }
    {
        gm_b16_8 t36 = gm_l6;
        gm_th2lqbo77pvsc t37;
        gm_f_canonicalize_n3e3qri((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t38 = t37;
        gm_b16_8 t39;
        if (((((uint8_t *)&t38))[16] != 0)) {
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 0));
            t39 = t40;
        } else {
            gm_b16_8 t41 = gm_l6;
            t39 = t41;
        }
        gm_l7 = t39;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t43 = gm_l4;
        gm_tftb1odc739t5 t44 = gm_f_push_3ef746b(t42, t43);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcc2qutubarto1, 423, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    {
        for (;;) {
            void * t48 = (void *)((uint8_t *)&F.l8);
            gm_th2lqbo77pvsc t49;
            gm_f_pop_vr08sii((void *)&t49, t48);
            gm_th2lqbo77pvsc t50 = t49;
            if (!((((uint8_t *)&t50))[16] != 0)) goto lb47;
            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
            F.l9 = t51;
            {
                gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll52;
                }
            }
            {
                F.l11 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t55 = F.l9;
                    gm_l12 = t55;
                }
                gm_hval t56 = ((gm_hval){(void *)&F.h, cx});
                gm_node wn57 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl3gh4gojekg, .env = t56.env, .hctx = t56.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
                gm_b16_8 t60 = gm_l12;
                gm_b0_1 t61 = gm_f_entries_44o5n9f_siirbaa((void *)NULL, (&wn57), NULL, t60);
                if (gm_unw.kind) {
                    goto gm_ll59;
                }
                F.w1 = t61;
                goto gm_ls62;
gm_ll59:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we58; }
                goto gm_ll52;
gm_ls62:;
we58:;
                gm_b0_1 t63 = F.w1;
                gm_b0_1 t64 = t63;
            }
            {
                uint8_t t66 = F.l11;
                uint8_t t67 = (uint8_t)!t66;
                if (!t67) goto ie65;
                {
                    goto lc47;
                }
                goto ix65;
ie65:;
ix65:;
            }
            {
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_f_sort_skeree7(t69);
            }
            {
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_f_reverse_cgmjfos(t70);
            }
            {
                void * t71 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_b16_8 t73 = t72;
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                gm_tftb1odc739t5 t75 = gm_f_extend_vogsd72(t71, t74);
                gm_tftb1odc739t5 t76 = t75;
                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcc2qutubarto1, 441, 9);
                gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
            }
            {
                gm_b16_8 t78 = F.l9;
                gm_th2lqbo77pvsc t79;
                gm_f_canonicalize_n3e3qri((void *)&t79, (void *)NULL, cx, NULL, t78);
                if (gm_unw.kind) {
                    goto gm_ll52;
                }
                gm_th2lqbo77pvsc t80 = t79;
                gm_b16_8 t81;
                if (((((uint8_t *)&t80))[16] != 0)) {
                    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 0));
                    t81 = t82;
                } else {
                    gm_b16_8 t83 = F.l9;
                    t81 = t83;
                }
                gm_l13 = t81;
            }
            {
                gm_b16_8 t85 = gm_l13;
                gm_b16_8 t86 = gm_l7;
                gm_b16_8 t87 = t85;
                gm_b16_8 t88 = t86;
                uint8_t t89 = (gm_eq0iqiubfm7e(((uint8_t *)&t87), ((uint8_t *)&t88)));
                if (!t89) goto ie84;
                {
                    goto lc47;
                }
                goto ix84;
ie84:;
ix84:;
            }
            {
                gm_l14 = ((uint8_t)0);
            }
            {
                {
                    void * t92 = gm_l0;
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((((uint8_t *)t92) + 472) + 0));
                    gm_l15 = t93;
                }
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t95 = gm_l16;
                        uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t97 = (t95 < t96);
                        if (!t97) goto lb94;
                        {
                            uint64_t t100 = gm_l16;
                            uint64_t t101 = (uintptr_t)t100;
                            uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            gm_t3877pk7lv748 t105 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t101 * 24));
                            gm_l17 = t105;
                            {
                                gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                gm_b16_8 t109 = gm_l13;
                                gm_b16_8 t110 = t108;
                                gm_b16_8 t111 = t109;
                                uint8_t t112 = (gm_eq0iqiubfm7e(((uint8_t *)&t110), ((uint8_t *)&t111)));
                                if (!t112) goto ie107;
                                {
                                    gm_l14 = ((uint8_t)1);
                                }
                                goto ix107;
ie107:;
ix107:;
                            }
                            goto me103;
mn104:;
                            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 445, 9);
me103:;
                        }
lc94:;
                        {
                            uint64_t t115 = gm_l16;
                            uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                            uint64_t t118 = r116;
                            gm_l16 = t118;
                        }
                    }
lb94:;
                }
            }
            {
                uint8_t t120 = gm_l14;
                if (!t120) goto ie119;
                {
                    goto lc47;
                }
                goto ix119;
ie119:;
ix119:;
            }
            {
                gm_b16_8 t123 = F.l9;
                gm_tqetrgo0q24b5 t124;
                gm_f_module_files_8j8gjpm((void *)&t124, (void *)NULL, cx, NULL, t123);
                if (gm_unw.kind) {
                    goto gm_ll52;
                }
                gm_tqetrgo0q24b5 t125 = t124;
                if (!((((uint8_t *)&t125))[40] != 0)) goto ie122;
                gm_trqfvmjtt4sij t126 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t125));
                F.l18 = t126;
                {
                    uint64_t t129 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
                    uint8_t t130 = (t129 > ((uint64_t)0ULL));
                    if (!t130) goto ie128;
                    {
                        {
                            void * t133 = gm_l0;
                            gm_b16_8 t134 = F.l9;
                            gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            uint64_t t136 = gm_l2;
                            gm_tf5srh4mp4b3i t137;
                            GM_ST(uint8_t, (uint8_t *)&t137 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t137 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t137 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t137), t136);
                            ((uint8_t *)&t137)[8] = 1;
                            gm_th2lqbo77pvsc t138;
                            memset(&t138, 0, sizeof t138);
                            gm_tf5srh4mp4b3i t139 = gm_f_load_dir_module_a75lmvr((void *)NULL, cx, NULL, t133, t134, t135, t137, &t138);
                            if (gm_unw.kind) {
                                goto gm_ll52;
                            }
                        }
                    }
                    goto ix128;
ie128:;
ix128:;
                }
                goto ix122;
ie122:;
ix122:;
            }
lc47:;
        }
gm_ll52:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
        }
        goto gm_lad;
lb47:;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_root_af4ud7b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* root */
    uint64_t gm_l2; /* m */
    gm_t08olq97rscn3 gm_l3; /* file */
    gm_b16_8 gm_l4; /* dir */
    gm_b16_8 gm_l5; /* canon */
    uint64_t gm_l6; /* package */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 1403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        gm_b16_8 t4 = gm_l1;
        uint8_t t5 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t5) {
            uint8_t t6 = 0;
            gm_b16_8 t7 = gm_l1;
            uint8_t t8 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = (uint8_t)!t8;
            if (t9) {
                gm_b16_8 t10 = gm_l1;
                void * t11 = gm_l0;
                gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 32) + 0));
                gm_b16_8 t13 = t12;
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
                gm_tqetrgo0q24b5 t15;
                gm_f_module_files_with_abkh6r4((void *)&t15, (void *)NULL, cx, NULL, t10, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqetrgo0q24b5 t16 = t15;
                uint8_t t17;
                if (((((uint8_t *)&t16))[40] != 0)) goto mn19;
                t17 = ((uint8_t)0);
                goto me18;
mn19:;
                t17 = ((uint8_t)1);
                goto me18;
mn20:;
                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 357, 47);
me18:;
                t6 = t17;
            } else t6 = 0;
            t3 = t6;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t22 = gm_l0;
            gm_b16_8 t23 = gm_l1;
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_tf5srh4mp4b3i t25;
            memset(&t25, 0, sizeof t25);
            gm_th2lqbo77pvsc t26;
            memset(&t26, 0, sizeof t26);
            gm_tf5srh4mp4b3i t27 = gm_f_load_dir_module_a75lmvr((void *)NULL, cx, NULL, t22, t23, t24, t25, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t28 = t27;
            uint64_t t29;
            if (((((uint8_t *)&t28))[8] != 0)) {
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t28) + 0));
                t29 = t30;
            } else {
                gm_tf5srh4mp4b3i t31;
                memset(&t31, 0, sizeof t31);
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            gm_l2 = t29;
        }
        {
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 8));
            uint8_t t36 = gm_f_has_errors_if7ai0m(t35);
            if (!t36) goto ie33;
            void * t37 = gm_l0;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 25));
            uint8_t t39 = (uint8_t)!t38;
            if (!t39) goto ie33;
            {
                gm_tf5srh4mp4b3i t41;
                memset(&t41, 0, sizeof t41);
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            uint64_t t43 = gm_l2;
            gm_tf5srh4mp4b3i t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t44), t43);
            ((uint8_t *)&t44)[8] = 1;
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t46 = gm_l0;
        gm_b16_8 t47 = gm_l1;
        gm_t1s75i08qtptd t48;
        gm_f_load_file_rdflf4e((void *)&t48, (void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1s75i08qtptd t49 = t48;
        gm_t08olq97rscn3 t50;
        if (((((uint8_t *)&t49))[32] != 0)) {
            gm_t08olq97rscn3 t51 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t49) + 0));
            t50 = t51;
        } else {
            gm_tf5srh4mp4b3i t52;
            memset(&t52, 0, sizeof t52);
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        gm_l3 = t50;
    }
    {
        void * t55 = gm_l0;
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 8));
        uint8_t t57 = gm_f_has_errors_if7ai0m(t56);
        if (!t57) goto ie54;
        void * t58 = gm_l0;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t58) + 25));
        uint8_t t60 = (uint8_t)!t59;
        if (!t60) goto ie54;
        {
            gm_tf5srh4mp4b3i t62;
            memset(&t62, 0, sizeof t62);
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        gm_b16_8 t64 = gm_l1;
        gm_th2lqbo77pvsc t65;
        gm_f_parent_lk0iero((void *)&t65, (void *)NULL, cx, NULL, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t66 = t65;
        gm_b16_8 t67;
        if (((((uint8_t *)&t66))[16] != 0)) {
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 0));
            t67 = t68;
        } else {
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(1));
            t67 = t69;
        }
        gm_l4 = t67;
    }
    {
        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t72 = ((t71 == ((uint64_t)0ULL)));
        if (!t72) goto ie70;
        {
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
            gm_l4 = t74;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_b16_8 t75 = gm_l4;
        gm_th2lqbo77pvsc t76;
        gm_f_canonicalize_n3e3qri((void *)&t76, (void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t77 = t76;
        gm_b16_8 t78;
        if (((((uint8_t *)&t77))[16] != 0)) {
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 0));
            t78 = t79;
        } else {
            gm_b16_8 t80 = gm_l4;
            t78 = t80;
        }
        gm_l5 = t78;
    }
    {
        void * t81 = gm_l0;
        gm_b16_8 t82 = gm_l5;
        gm_tf5srh4mp4b3i t83;
        memset(&t83, 0, sizeof t83);
        uint64_t t84 = gm_f_package_of_hul50on((void *)NULL, cx, NULL, t81, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t84;
    }
    {
        gm_f_list_j6fu443((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t85 = (void *)((uint8_t *)&F.l7);
        gm_tftb1odc739t5 t86 = gm_f_push_5h5ofgv(t85, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)));
        gm_tftb1odc739t5 t87 = t86;
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcc2qutubarto1, 370, 5);
        gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
    }
    {
        void * t89 = gm_l0;
        gm_b16_8 t90 = gm_l1;
        gm_th2lqbo77pvsc t91;
        gm_f_file_stem_v6vrs6i((void *)&t91, (void *)NULL, cx, NULL, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t92 = t91;
        gm_b16_8 t93;
        if (((((uint8_t *)&t92))[16] != 0)) {
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 0));
            t93 = t94;
        } else {
            gm_b16_8 t95;
            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sog4rksrmsqd2));
            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(4));
            t93 = t95;
        }
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t97 = t96;
        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
        uint64_t t99 = gm_f_add_module_3cgsegf((void *)NULL, cx, NULL, t89, t93, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t99;
    }
    {
        uint64_t t100 = gm_l8;
        uint64_t t101 = (uintptr_t)t100;
        void * t102 = gm_l0;
        uint64_t t103 = GM_LD(uintptr_t, ((((uint8_t *)t102) + 200) + 0) + 8);
        if (t101 >= (uintptr_t)(t103)) gm_trap_index(t101, t103, gm_srcc2qutubarto1, 372, 5);
        gm_b16_8 t104 = gm_l4;
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t102) + 200) + 0)) + t101 * 184) + 120), t104);
    }
    {
        uint64_t t105 = gm_l8;
        uint64_t t106 = (uintptr_t)t105;
        void * t107 = gm_l0;
        uint64_t t108 = GM_LD(uintptr_t, ((((uint8_t *)t107) + 200) + 0) + 8);
        if (t106 >= (uintptr_t)(t108)) gm_trap_index(t106, t108, gm_srcc2qutubarto1, 373, 5);
        uint64_t t109 = gm_l6;
        GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t107) + 200) + 0)) + t106 * 184) + 136), t109);
    }
    {
        void * t110 = gm_l0;
        uint64_t t111 = gm_l8;
        gm_b0_1 t112 = gm_f_collect_whens_ktf37vb((void *)NULL, cx, NULL, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t113 = gm_l8;
        gm_tf5srh4mp4b3i t114;
        GM_ST(uint8_t, (uint8_t *)&t114 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t114 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t114), t113);
        ((uint8_t *)&t114)[8] = 1;
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
inline __attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_loader_on0up0a F;
    void * gm_l0; /* sm */
    void * gm_l1; /* diags */
    void * gm_l2; /* ids */
    gm_b16_8 gm_l3; /* std_root */
    uint8_t gm_l4; /* recover */
    void * gm_l5; /* <tmp> */
    void * gm_l6; /* <tmp> */
    void * gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 452u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = gm_l0;
            gm_l5 = t3;
        }
        {
            void * t4 = gm_l1;
            gm_l6 = t4;
        }
        {
            void * t5 = gm_l2;
            gm_l7 = t5;
        }
        {
            uint8_t t6 = gm_l4;
            gm_l8 = t6;
        }
        {
            gm_f_host_target_vsu78om((void *)&F.l9);
        }
        {
            gm_f_list_j41vvuc((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t7 = gm_l3;
            gm_l11 = t7;
        }
        {
            gm_b16_8 t8 = gm_l3;
            gm_l12 = t8;
        }
        {
            gm_b16_8 t9 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t9;
        }
        {
            gm_f_list_5msmod6((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_016fo30_2((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_g6e4eeu((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_5dhgg4i((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_vdg8k7c((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_12lt7k0((void *)&F.l19, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_6kkl75k((void *)&F.l20, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        memset((uint8_t *)&F.l21 + 26, 0, 6);
        void * t10 = gm_l5;
        GM_ST(void *, (uint8_t *)&F.l21 + 0, t10);
        void * t11 = gm_l6;
        GM_ST(void *, (uint8_t *)&F.l21 + 8, t11);
        void * t12 = gm_l7;
        GM_ST(void *, (uint8_t *)&F.l21 + 16, t12);
        uint8_t t13 = gm_l8;
        GM_ST(uint8_t, (uint8_t *)&F.l21 + 24, t13);
        GM_ST(uint8_t, (uint8_t *)&F.l21 + 25, ((uint8_t)0));
        memcpy((uint8_t *)&F.l21 + 32, ((uint8_t *)&F.l10), 40);
        gm_b16_8 t14 = GM_LD(gm_b16_8, gm_cuqbj62gqhf_3);
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 72, t14);
        memcpy((uint8_t *)&F.l21 + 88, ((uint8_t *)&F.l9), 64);
        gm_b16_8 t15 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 152, t15);
        gm_b16_8 t16 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 168, t16);
        gm_b16_8 t17 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 184, t17);
        memcpy((uint8_t *)&F.l21 + 200, ((uint8_t *)&F.l14), 40);
        memcpy((uint8_t *)&F.l21 + 240, ((uint8_t *)&F.l15), 40);
        memcpy((uint8_t *)&F.l21 + 280, ((uint8_t *)&F.l16), 40);
        gm_tf5srh4mp4b3i t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&F.l21 + 320, t18);
        memcpy((uint8_t *)&F.l21 + 336, ((uint8_t *)&F.l17), 40);
        gm_tv37d3dpt2opu t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(gm_tv37d3dpt2opu, (uint8_t *)&F.l21 + 376, t19);
        GM_ST(void *, (uint8_t *)&F.l21 + 424, ((void *)0));
        memcpy((uint8_t *)&F.l21 + 432, ((uint8_t *)&F.l18), 40);
        memcpy((uint8_t *)&F.l21 + 472, ((uint8_t *)&F.l19), 40);
        memcpy((uint8_t *)&F.l21 + 512, ((uint8_t *)&F.l20), 40);
    }
    {
        gm_b16_8 t20 = gm_l3;
        gm_th2lqbo77pvsc t21;
        gm_f_canonicalize_n3e3qri((void *)&t21, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t22 = t21;
        gm_b16_8 t23;
        if (((((uint8_t *)&t22))[16] != 0)) {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
            t23 = t24;
        } else {
            gm_b16_8 t25 = gm_l3;
            t23 = t25;
        }
        GM_ST(gm_b16_8, (((uint8_t *)&F.l21) + 168), t23);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l21), 552);
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
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_module_files_8j8gjpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_module_files_8j8gjpm F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        uint8_t t4 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            gm_tqetrgo0q24b5 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t9 = gm_l0;
        gm_ttevl7jqjigkp t10;
        gm_f_dir_gm_files_0s2opd8((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttevl7jqjigkp t11 = t10;
        gm_tqetrgo0q24b5 t12;
        if (!((((uint8_t *)&t11))[0] != 0)) {
            gm_trqfvmjtt4sij t13 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t11) + 8));
            gm_tqetrgo0q24b5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 44, 0);
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[40] = 1;
            t12 = t14;
        } else {
            gm_tqetrgo0q24b5 t15;
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
__attribute__((aligned(4))) void gm_f_module_files_with_abkh6r4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_module_files_with_abkh6r4 F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* overlays */
    gm_b16_8 gm_l3; /* key */
    uint64_t gm_l5; /* n */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t6j6smj7qoctu gm_l8; /* o */
    gm_b16_8 gm_l9; /* name */
    uint8_t gm_l10; /* present */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    GM_STACK_CHECK(&F, 1277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_f_module_files_8j8gjpm((void *)&F.l2, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie3;
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 48);
            goto gm_fdone;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t8;
    }
    {
        gm_tqetrgo0q24b5 t9 = F.l2;
        gm_tqetrgo0q24b5 t10 = t9;
        gm_trqfvmjtt4sij t11;
        if (((((uint8_t *)&t10))[40] != 0)) {
            gm_trqfvmjtt4sij t12 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            gm_trqfvmjtt4sij t13;
            gm_f_list_epvducg((void *)&t13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t11 = t13;
        }
        F.l4 = t11;
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        gm_l5 = t14;
    }
    {
        {
            gm_b16_8 t16 = gm_l1;
            gm_l6 = t16;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l7;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l7;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t6j6smj7qoctu t28 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t24 * 32));
                    gm_l8 = t28;
                    {
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                        gm_th2lqbo77pvsc t31;
                        gm_f_file_name_em7v8lv((void *)&t31, (void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_th2lqbo77pvsc t32 = t31;
                        gm_b16_8 t33;
                        if (((((uint8_t *)&t32))[16] != 0)) {
                            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 0));
                            t33 = t34;
                        } else {
                            goto lc17;
                        }
                        gm_l9 = t33;
                    }
                    {
                        uint8_t t36 = 0;
                        uint8_t t37 = 0;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                        gm_th2lqbo77pvsc t39;
                        gm_f_parent_lk0iero((void *)&t39, (void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_th2lqbo77pvsc t40 = t39;
                        gm_b16_8 t41;
                        if (((((uint8_t *)&t40))[16] != 0)) {
                            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 0));
                            t41 = t42;
                        } else {
                            gm_b16_8 t43;
                            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(0));
                            t41 = t43;
                        }
                        gm_b16_8 t44 = gm_l3;
                        gm_b16_8 t45 = t41;
                        gm_b16_8 t46 = t44;
                        uint8_t t47 = (!gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t46)));
                        if (!t47) {
                            gm_b16_8 t48 = gm_l9;
                            gm_b16_8 t49;
                            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s0ucactj6hj0a));
                            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
                            uint8_t t50 = gm_f_ends_with_biriu5l(t48, t49);
                            uint8_t t51 = (uint8_t)!t50;
                            t37 = t51;
                        } else t37 = 1;
                        if (!t37) {
                            uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            uint8_t t53 = (t52 <= ((uint64_t)3ULL));
                            t36 = t53;
                        } else t36 = 1;
                        if (!t36) goto ie35;
                        {
                            goto lc17;
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    {
                        gm_l10 = ((uint8_t)0);
                    }
                    {
                        {
                            uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                            void * t57 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)));
                            uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t59 = gm_l5;
                            uint64_t t60 = (uintptr_t)t59;
                            if (t58 > t60 || t60 > (uintptr_t)t56) gm_trap_slice(t58, t60, t56, gm_srcc2qutubarto1, 322, 18);
                            gm_b16_8 t61;
                            GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t57 + t58 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60 - t58));
                            gm_l11 = t61;
                        }
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t63 = gm_l12;
                                uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                uint8_t t65 = (t63 < t64);
                                if (!t65) goto lb62;
                                {
                                    uint64_t t68 = gm_l12;
                                    uint64_t t69 = (uintptr_t)t68;
                                    uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t69 * 16));
                                    gm_l13 = t73;
                                    {
                                        gm_b16_8 t76 = gm_l13;
                                        gm_b16_8 t77 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t76);
                                        if (gm_unw.kind) {
                                            goto gm_ll66;
                                        }
                                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                                        gm_b16_8 t79 = t77;
                                        gm_b16_8 t80 = t78;
                                        uint8_t t81 = (gm_eq0iqiubfm7e(((uint8_t *)&t79), ((uint8_t *)&t80)));
                                        if (!t81) goto ie75;
                                        {
                                            gm_l10 = ((uint8_t)1);
                                        }
                                        goto ix75;
ie75:;
ix75:;
                                    }
                                    goto me71;
mn72:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 322, 9);
me71:;
                                }
lc62:;
                                {
                                    uint64_t t84 = gm_l12;
                                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                    uint64_t t87 = r85;
                                    gm_l12 = t87;
                                }
                            }
gm_ll66:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb62; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc62; }
                            }
                            goto gm_ll21;
lb62:;
                        }
                    }
                    {
                        uint8_t t89 = gm_l10;
                        uint8_t t90 = (uint8_t)!t89;
                        if (!t90) goto ie88;
                        {
                            void * t92 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t93 = gm_l0;
                            gm_b16_8 t94 = gm_l9;
                            gm_b16_8 t95 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t93, t94);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tftb1odc739t5 t96 = gm_f_push_3ef746b(t92, t95);
                            gm_tftb1odc739t5 t97 = t96;
                            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srcc2qutubarto1, 325, 26);
                            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                        }
                        goto ix88;
ie88:;
ix88:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 318, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t100 = gm_l7;
                    uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                    uint64_t t103 = r101;
                    gm_l7 = t103;
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
        uint8_t t105;
        if (((((uint8_t *)&F.l2))[40] != 0)) goto mn107;
        t105 = ((uint8_t)1);
        goto me106;
mn107:;
        t105 = ((uint8_t)0);
        goto me106;
mn108:;
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 327, 8);
me106:;
        if (!t105) goto ie104;
        uint64_t t109 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t110 = ((t109 == ((uint64_t)0ULL)));
        if (!t110) goto ie104;
        {
            gm_tqetrgo0q24b5 t112;
            memset(&t112, 0, sizeof t112);
            uint8_t t113 = 0;
            F.ret = t112;
            goto gm_fret;
        }
        goto ix104;
ie104:;
ix104:;
    }
    {
        gm_trqfvmjtt4sij t114 = F.l4;
        gm_tqetrgo0q24b5 t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t115), t114);
        ((uint8_t *)&t115)[40] = 1;
        uint8_t t116 = 0;
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
__attribute__((aligned(4))) void gm_f_overlay_text_haaaurk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_overlay_text_haaaurk F;
    gm_b16_8 gm_l0; /* overlays */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* name */
    uint8_t gm_l3; /* any */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l7; /* key */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t6j6smj7qoctu gm_l10; /* o */
    GM_STACK_CHECK(&F, 859u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_th2lqbo77pvsc t3;
        gm_f_file_name_em7v8lv((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t10 = gm_l0;
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
                    gm_t6j6smj7qoctu t22 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t18 * 32));
                    gm_l6 = t22;
                    {
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                        gm_th2lqbo77pvsc t26;
                        gm_f_file_name_em7v8lv((void *)&t26, (void *)NULL, cx, NULL, t25);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_th2lqbo77pvsc t27 = t26;
                        gm_b16_8 t28;
                        if (((((uint8_t *)&t27))[16] != 0)) {
                            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 0));
                            t28 = t29;
                        } else {
                            gm_b16_8 t30;
                            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(0));
                            t28 = t30;
                        }
                        gm_b16_8 t31 = gm_l2;
                        gm_b16_8 t32 = t28;
                        gm_b16_8 t33 = t31;
                        uint8_t t34 = (gm_eq0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t33)));
                        if (!t34) goto ie24;
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 299, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t37 = gm_l5;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l5 = t40;
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
    {
        uint8_t t42 = gm_l3;
        uint8_t t43 = (uint8_t)!t42;
        if (!t43) goto ie41;
        {
            gm_th2lqbo77pvsc t45;
            memset(&t45, 0, sizeof t45);
            uint8_t t46 = 0;
            F.ret = t45;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        gm_b16_8 t47 = gm_l1;
        gm_b16_8 t48 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t48;
    }
    {
        {
            gm_b16_8 t50 = gm_l0;
            gm_l8 = t50;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t52 = gm_l9;
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t54 = (t52 < t53);
                if (!t54) goto lb51;
                {
                    uint64_t t57 = gm_l9;
                    uint64_t t58 = (uintptr_t)t57;
                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t6j6smj7qoctu t62 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t58 * 32));
                    gm_l10 = t62;
                    {
                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
                        gm_b16_8 t66 = gm_l7;
                        gm_b16_8 t67 = t65;
                        gm_b16_8 t68 = t66;
                        uint8_t t69 = (gm_eq0iqiubfm7e(((uint8_t *)&t67), ((uint8_t *)&t68)));
                        if (!t69) goto ie64;
                        {
                            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                            gm_th2lqbo77pvsc t72;
                            GM_ST(uint8_t, (uint8_t *)&t72 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t72 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t72 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t72), t71);
                            ((uint8_t *)&t72)[16] = 1;
                            uint8_t t73 = 0;
                            F.ret = t72;
                            goto gm_fret;
                        }
                        goto ix64;
ie64:;
ix64:;
                    }
                    goto me60;
mn61:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 304, 5);
me60:;
                }
lc51:;
                {
                    uint64_t t75 = gm_l9;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l9 = t78;
                }
            }
lb51:;
        }
    }
    {
        gm_th2lqbo77pvsc t79;
        memset(&t79, 0, sizeof t79);
        uint8_t t80 = 0;
        F.ret = t79;
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
__attribute__((aligned(4))) uint64_t gm_f_package_of_hul50on(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2) {
    struct gm_fr_package_of_hul50on F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* dir */
    gm_tf5srh4mp4b3i gm_l2; /* parent_package */
    gm_b16_8 gm_l3; /* root */
    gm_th2lqbo77pvsc gm_l4; /* found */
    gm_th2lqbo77pvsc gm_l5; /* cur */
    gm_b16_8 gm_l6; /* d */
    gm_b16_8 gm_l7; /* r */
    uint64_t gm_l8; /* p */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    GM_STACK_CHECK(&F, 1284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        gm_th2lqbo77pvsc t3;
        memset(&t3, 0, sizeof t3);
        gm_l4 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[16] = 1;
        gm_l5 = t5;
    }
    {
        for (;;) {
            if (!((((uint8_t *)&gm_l5))[16] != 0)) goto lb6;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l6 = t7;
            {
                gm_b16_8 t11 = gm_l6;
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(gm_se24c7hpp401n));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(10));
                gm_b16_8 t13 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t11, t12);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                uint8_t t14 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t13);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (!t14) goto ie10;
                {
                    gm_b16_8 t16 = gm_l6;
                    gm_th2lqbo77pvsc t17;
                    GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t17), t16);
                    ((uint8_t *)&t17)[16] = 1;
                    gm_l4 = t17;
                }
                {
                    goto lb6;
                }
                goto ix10;
ie10:;
ix10:;
            }
            {
                gm_b16_8 t18 = gm_l6;
                gm_f_parent_lk0iero((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l5)), (void *)NULL, cx, NULL, t18);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
lc6:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
        }
        goto gm_lad;
lb6:;
    }
    {
        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie19;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l7 = t20;
        {
            gm_b16_8 t22 = gm_l7;
            gm_l3 = t22;
        }
        goto ix19;
ie19:;
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie23;
        uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l8 = t24;
        {
            gm_b16_8 t27 = gm_l1;
            uint64_t t28 = gm_l8;
            uint64_t t29 = (uintptr_t)t28;
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 432) + 0) + 8);
            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcc2qutubarto1, 697, 29);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 432) + 0)) + t29 * 56) + 0));
            uint8_t t33 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t27, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t33) goto ie26;
            {
                uint64_t t35 = gm_l8;
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        goto ix23;
ie23:;
ix23:;
ix19:;
    }
    {
        {
            void * t38 = gm_l0;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t38) + 432) + 0));
            gm_b16_8 t40 = t39;
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_l9 = t41;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t43 = gm_l10;
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t45 = (t43 < t44);
                if (!t45) goto lb42;
                {
                    gm_tm101l8077enc t48;
                    uint64_t t49 = gm_l10;
                    GM_ST(uint64_t, (uint8_t *)&t48 + 0, t49);
                    uint64_t t50 = gm_l10;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    memcpy((uint8_t *)&t48 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t51 * 56), 56);
                    gm_tm101l8077enc t53 = t48;
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&t53) + 0));
                    gm_l11 = t56;
                    gm_tl7hpf5nlilqq t57 = GM_LD(gm_tl7hpf5nlilqq, (((uint8_t *)&t53) + 8));
                    F.l12 = t57;
                    {
                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                        gm_b16_8 t61 = gm_l3;
                        gm_b16_8 t62 = t60;
                        gm_b16_8 t63 = t61;
                        uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                        if (!t64) goto ie59;
                        {
                            uint64_t t66 = gm_l11;
                            uint8_t t67 = 0;
                            F.ret = t66;
                            goto gm_fret;
                        }
                        goto ix59;
ie59:;
ix59:;
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 699, 5);
me54:;
                }
lc42:;
                {
                    uint64_t t69 = gm_l10;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l10 = t72;
                }
            }
lb42:;
        }
    }
    {
        gm_trqfvmjtt4sij t73;
        gm_b16_8 t75 = gm_l3;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(10));
        gm_b16_8 t77 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t78 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t78) goto ie74;
        void * t80 = gm_l0;
        gm_b16_8 t81 = gm_l3;
        gm_trqfvmjtt4sij t82;
        gm_f_read_manifest_qr219nu((void *)&t82, (void *)NULL, cx, NULL, t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t83 = t82;
        t73 = t83;
        goto ix74;
ie74:;
        gm_trqfvmjtt4sij t85;
        gm_f_list_0pjrgaq((void *)&t85, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t86 = t85;
        t73 = t86;
ix74:;
        F.l13 = t73;
    }
    {
        void * t87 = gm_l0;
        void * t88 = (void *)(((uint8_t *)t87) + 432);
        gm_tl7hpf5nlilqq t89;
        gm_b16_8 t90 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t89 + 0, t90);
        memcpy((uint8_t *)&t89 + 16, ((uint8_t *)&F.l13), 40);
        gm_tftb1odc739t5 t91 = gm_f_push_n0od5si(t88, &t89);
        gm_tftb1odc739t5 t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcc2qutubarto1, 703, 5);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
    }
    {
        void * t94 = gm_l0;
        uint64_t t95 = GM_LD(uintptr_t, ((((uint8_t *)t94) + 432) + 0) + 8);
        uint64_t r96; bool o97 = __builtin_sub_overflow(t95, ((uint64_t)1ULL), &r96);
        if (o97) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 704, 12);
        uint64_t t98 = r96;
        uint8_t t99 = 0;
        F.ret = t98;
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
inline __attribute__((aligned(4))) void gm_f_parse_text_j7gp86k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_parse_text_j7gp86k F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* text */
    uint32_t gm_l3; /* id */
    gm_t08olq97rscn3 gm_l5; /* tree */
    void * gm_l6; /* keep */
    GM_STACK_CHECK(&F, 1013u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        uint32_t t6 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t6;
    }
    {
        gm_tr8dvlu9ik4gj t7;
        void * t9 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 24));
        if (!t10) goto ie8;
        uint32_t t12 = gm_l3;
        gm_b16_8 t13 = gm_l2;
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 8));
        gm_tr8dvlu9ik4gj t16;
        gm_f_lex_recovering_obo2892((void *)&t16, (void *)NULL, cx, NULL, t12, t13, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tr8dvlu9ik4gj t17 = t16;
        t7 = t17;
        goto ix8;
ie8:;
        uint32_t t19 = gm_l3;
        gm_b16_8 t20 = gm_l2;
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 8));
        gm_tr8dvlu9ik4gj t23;
        gm_f_lex_dc8mbmf((void *)&t23, (void *)NULL, cx, NULL, t19, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tr8dvlu9ik4gj t24 = t23;
        t7 = t24;
ix8:;
        F.l4 = t7;
    }
    {
        uint32_t t25 = gm_l3;
        void * t26 = (void *)((uint8_t *)&F.l4);
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 16));
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 8));
        gm_f_parse_file_70g7vok((void *)&gm_l5, (void *)NULL, cx, NULL, t25, t26, t28, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 424));
        void * t34 = t33;
        if (!(GM_LD(void *, ((uint8_t *)&t34)) != 0)) goto ie31;
        void * t35 = GM_LD(void *, ((uint8_t *)&t34));
        gm_l6 = t35;
        {
            void * t37 = gm_l6;
            gm_tqtt2php6e8lk t38;
            memset((uint8_t *)&t38 + 4, 0, 4);
            uint32_t t39 = gm_l3;
            GM_ST(uint32_t, (uint8_t *)&t38 + 0, t39);
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 0) + 0));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            GM_ST(gm_b16_8, (uint8_t *)&t38 + 8, t42);
            memcpy((uint8_t *)&t38 + 24, ((uint8_t *)&gm_l5), 32);
            gm_tftb1odc739t5 t43 = gm_f_push_9oh3uqv(t37, &t38);
            gm_tftb1odc739t5 t44 = t43;
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srcc2qutubarto1, 346, 27);
            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l5), 32);
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
__attribute__((aligned(4))) void gm_f_read_manifest_qr219nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_read_manifest_qr219nu F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l5; /* path */
    gm_tj5jn79kq9pnm gm_l6; /* e */
    gm_b16_8 gm_l7; /* text */
    gm_tf1bjg81u0glv gm_l8; /* e */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    GM_STACK_CHECK(&F, 2997u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 376))[40] != 0)) goto ie2;
        gm_tin97tt4ud0s1 t4 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)t3) + 376));
        F.l2 = t4;
        {
            void * t7 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t8 = gm_l1;
            gm_tqetrgo0q24b5 t9;
            gm_f_deps_of_i4no6j9((void *)&t9, t7, t8);
            gm_tqetrgo0q24b5 t10 = t9;
            if (!((((uint8_t *)&t10))[40] != 0)) goto ie6;
            gm_trqfvmjtt4sij t11 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t10));
            F.l3 = t11;
            {
                memmove(gm_out, ((uint8_t *)&F.l3), 40);
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_0pjrgaq((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t13 = gm_l1;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(10));
        gm_b16_8 t15 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t15;
    }
    {
        gm_b16_8 t16 = gm_l5;
        gm_tgvh15qshf643 t17;
        gm_f_read_all_r9rh9qd((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t18 = t17;
        gm_b16_8 t19;
        if (!((((uint8_t *)&t18))[0] != 0)) {
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            t19 = t20;
        } else {
            gm_tj5jn79kq9pnm t21 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t18) + 8));
            gm_l6 = t21;
            {
                void * t23 = gm_l0;
                void * t24 = GM_LD(void *, (((uint8_t *)t23) + 8));
                gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_t6j6smj7qoctu t26;
                gm_b16_8 t27 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
                gm_tj5jn79kq9pnm t28 = gm_l6;
                gm_b16_8 t29 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t29);
                gm_tabb710d9mq2e t30;
                gm_f_format_04pispc((void *)&t30, (void *)NULL, cx, NULL, &t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t31 = t30;
                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcc2qutubarto1, 716, 38);
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                gm_b16_8 t33 = t32;
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                gm_b0_1 t35 = gm_f_report_eardf07((void *)NULL, cx, NULL, t24, t25, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            memmove(gm_out, ((uint8_t *)&F.l4), 40);
            goto gm_fdone;
        }
        gm_l7 = t19;
    }
    {
        gm_b16_8 t36 = gm_l7;
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_tjgmtdrk88t7m t39;
        gm_f_parse_manifest_b61s3ln((void *)&t39, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjgmtdrk88t7m t40 = t39;
        gm_tm43kqe3e8b4a t41;
        if (!((((uint8_t *)&t40))[0] != 0)) {
            gm_tm43kqe3e8b4a t42 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t40) + 8));
            t41 = t42;
        } else {
            gm_tf1bjg81u0glv t43 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t40) + 8));
            gm_l8 = t43;
            {
                void * t45 = gm_l0;
                void * t46 = GM_LD(void *, (((uint8_t *)t45) + 8));
                gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_tq3m5qgvkpje8 t48;
                gm_b16_8 t49 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 0));
                GM_ST(uint64_t, (uint8_t *)&t48 + 16, t50);
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t48 + 24, t51);
                gm_tabb710d9mq2e t52;
                gm_f_format_n6eprd4((void *)&t52, (void *)NULL, cx, NULL, &t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t53 = t52;
                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcc2qutubarto1, 720, 38);
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                gm_b16_8 t55 = t54;
                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                gm_b0_1 t57 = gm_f_report_eardf07((void *)NULL, cx, NULL, t46, t47, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            memmove(gm_out, ((uint8_t *)&F.l4), 40);
            goto gm_fdone;
        }
        F.l9 = t41;
    }
    {
        {
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l9) + 48) + 0));
            gm_l10 = t59;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t61 = gm_l11;
                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                {
                    uint64_t t66 = gm_l11;
                    uint64_t t67 = (uintptr_t)t66;
                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t7tiv6vif86qg t71 = GM_LD(gm_t7tiv6vif86qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t67 * 120));
                    F.l12 = t71;
                    {
                        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                        switch (t78) { case ((uint8_t)0ULL): goto ma74; case ((uint8_t)1ULL): goto ma75; case ((uint8_t)2ULL): goto ma76; default: goto ma77; }
ma74:;
                        uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                        if (t80 != ((uint8_t)0ULL)) goto mn79;
                        gm_b16_8 t81 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 24) + 8));
                        gm_l13 = t81;
                        {
                            void * t83 = (void *)((uint8_t *)&F.l4);
                            gm_t6j6smj7qoctu t84;
                            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t84 + 0, t85);
                            gm_b16_8 t86 = gm_l1;
                            gm_b16_8 t87 = gm_l13;
                            gm_b16_8 t88 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t86, t87);
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t84 + 16, t88);
                            gm_tftb1odc739t5 t89 = gm_f_push_i535kr1(t83, &t84);
                            gm_tftb1odc739t5 t90 = t89;
                            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcc2qutubarto1, 725, 27);
                            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                        }
                        goto me73;
mn79:;
ma75:;
                        uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                        if (t93 != ((uint8_t)1ULL)) goto mn92;
                        {
                            void * t95 = gm_l0;
                            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 8));
                            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            gm_tq3m5qgvkpje8 t98;
                            gm_b16_8 t99 = gm_l5;
                            GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
                            uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 16));
                            GM_ST(uint64_t, (uint8_t *)&t98 + 16, t100);
                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t98 + 24, t101);
                            gm_tabb710d9mq2e t102;
                            gm_f_format_q451tst((void *)&t102, (void *)NULL, cx, NULL, &t98);
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                            gm_tabb710d9mq2e t103 = t102;
                            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_srcc2qutubarto1, 726, 55);
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
                            gm_b16_8 t105 = t104;
                            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                            gm_b0_1 t107 = gm_f_report_eardf07((void *)NULL, cx, NULL, t96, t97, t106);
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                        }
                        goto me73;
mn92:;
ma76:;
                        uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                        if (t109 != ((uint8_t)2ULL)) goto mn108;
                        {
                            void * t111 = gm_l0;
                            void * t112 = GM_LD(void *, (((uint8_t *)t111) + 8));
                            gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            gm_tq3m5qgvkpje8 t114;
                            gm_b16_8 t115 = gm_l5;
                            GM_ST(gm_b16_8, (uint8_t *)&t114 + 0, t115);
                            uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 16));
                            GM_ST(uint64_t, (uint8_t *)&t114 + 16, t116);
                            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t114 + 24, t117);
                            gm_tabb710d9mq2e t118;
                            gm_f_format_pl7hkg4((void *)&t118, (void *)NULL, cx, NULL, &t114);
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                            gm_tabb710d9mq2e t119 = t118;
                            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 8, gm_srcc2qutubarto1, 727, 55);
                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
                            gm_b16_8 t121 = t120;
                            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                            gm_b0_1 t123 = gm_f_report_eardf07((void *)NULL, cx, NULL, t112, t113, t122);
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                        }
                        goto me73;
mn108:;
ma77:;
                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 724, 9);
me73:;
                    }
                    goto me69;
mn70:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 723, 5);
me69:;
                }
lc60:;
                {
                    uint64_t t125 = gm_l11;
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    uint64_t t128 = r126;
                    gm_l11 = t128;
                }
            }
gm_ll64:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
            }
            goto gm_lad;
lb60:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_resolve_import_944uqa9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* id */
    gm_t8c5h7u09n10a gm_l4; /* state */
    GM_STACK_CHECK(&F, 476u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcc2qutubarto1, 586, 8);
        uint8_t t7 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t4 * 216) + 200) + 0) == ((uint8_t)0ULL)));
        if (!t7) goto ie2;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t11 = gm_l1;
        uint64_t t12 = (uintptr_t)t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 240) + 0) + 8);
        if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcc2qutubarto1, 587, 10);
        gm_t52j636u3vgk3 t15 = GM_LD(gm_t52j636u3vgk3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 240) + 0)) + t12 * 216));
        F.l2 = t15;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((((uint8_t *)&F.l2) + 40) + 64) + 0));
        if (t17 != ((uint8_t)0ULL)) goto ie16;
        gm_tv2uf4leil5hq t18 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)&F.l2) + 40) + 64) + 8));
        F.l3 = t18;
        {
            void * t20 = gm_l0;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 32));
            void * t22 = (void *)((uint8_t *)&F.l2);
            void * t23 = (void *)((uint8_t *)&F.l3);
            gm_t8c5h7u09n10a t24 = gm_f_import_decl_vuv14lf((void *)NULL, cx, NULL, t20, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t24;
        }
        {
            uint64_t t25 = gm_l1;
            uint64_t t26 = (uintptr_t)t25;
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 240) + 0) + 8);
            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcc2qutubarto1, 592, 5);
            gm_t8c5h7u09n10a t29 = gm_l4;
            GM_ST(gm_t8c5h7u09n10a, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 240) + 0)) + t26 * 216) + 200), t29);
        }
        goto ix16;
ie16:;
        {
            gm_b0_1 t31;
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
ix16:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_import_dav5vf0(void * a0, uint64_t a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_import_dav5vf0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* prev */
    void * gm_l2; /* item */
    void * gm_l3; /* d */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 380u);
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
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcc2qutubarto1, 552, 11);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 240) + 0)) + t3 * 216);
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l3;
        void * t8 = (void *)(((uint8_t *)t7) + 40);
        gm_th2lqbo77pvsc t9;
        gm_f_import_path_na25mjr((void *)&t9, t8);
        gm_th2lqbo77pvsc t10 = t9;
        gm_b16_8 t11;
        if (((((uint8_t *)&t10))[16] != 0)) {
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t11;
    }
    {
        void * t14 = gm_l2;
        gm_th2lqbo77pvsc t15;
        gm_f_import_path_na25mjr((void *)&t15, t14);
        gm_th2lqbo77pvsc t16 = t15;
        gm_b16_8 t17;
        if (((((uint8_t *)&t16))[16] != 0)) {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            uint8_t t19 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l5 = t17;
    }
    {
        uint8_t t20 = 0;
        void * t21 = gm_l3;
        gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t21) + 40) + 56));
        void * t23 = gm_l2;
        gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t23) + 56));
        gm_t9qkh0rsooc8d t25 = t22;
        gm_t9qkh0rsooc8d t26 = t24;
        uint8_t t27 = (gm_eqk27942ja0g(((uint8_t *)&t25), ((uint8_t *)&t26)));
        if (t27) {
            gm_b16_8 t28 = gm_l4;
            gm_b16_8 t29 = gm_l5;
            gm_b16_8 t30 = t28;
            gm_b16_8 t31 = t29;
            uint8_t t32 = (gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t31)));
            t20 = t32;
        } else t20 = 0;
        uint8_t t33 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_khjih0n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_show_khjih0n F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* p */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* rest */
    GM_STACK_CHECK(&F, 685u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 152));
        gm_th2lqbo77pvsc t6;
        gm_f_strip_prefix_vl3t0h4((void *)&t6, (void *)NULL, cx, NULL, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t7 = t6;
        gm_th2lqbo77pvsc t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            t8 = t6;
        } else {
            gm_b16_8 t9 = gm_l1;
            void * t10 = gm_l0;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 168));
            gm_th2lqbo77pvsc t12;
            gm_f_strip_prefix_vl3t0h4((void *)&t12, (void *)NULL, cx, NULL, t9, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t8 = t12;
        }
        gm_th2lqbo77pvsc t13 = t8;
        if (!((((uint8_t *)&t13))[16] != 0)) goto ie2;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_l2 = t14;
        {
            gm_b16_8 t16;
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
            if (!t19) goto ie17;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sjkg27kmogh66));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(6));
            gm_b16_8 t22 = t21;
            t16 = t22;
            goto ix17;
ie17:;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_san68r6np95il));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(5));
            gm_b16_8 t25 = gm_l2;
            gm_b16_8 t26 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t27 = t26;
            t16 = t27;
ix17:;
            gm_b16_8 t28 = gm_f_slashes_0qtfkuu((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t31 = gm_l1;
        void * t32 = gm_l0;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 184));
        gm_th2lqbo77pvsc t34;
        gm_f_strip_prefix_vl3t0h4((void *)&t34, (void *)NULL, cx, NULL, t31, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t35 = t34;
        if (!((((uint8_t *)&t35))[16] != 0)) goto ie30;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_l3 = t36;
        {
            gm_b16_8 t38;
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t41 = ((t40 == ((uint64_t)0ULL)));
            if (!t41) goto ie39;
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
            gm_b16_8 t44 = t43;
            t38 = t44;
            goto ix39;
ie39:;
            gm_b16_8 t46 = gm_l3;
            gm_b16_8 t47 = t46;
            t38 = t47;
ix39:;
            gm_b16_8 t48 = gm_f_slashes_0qtfkuu((void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_b16_8 t50 = gm_l1;
        gm_b16_8 t51 = gm_f_slashes_0qtfkuu((void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t52 = 0;
        F.ret = t51;
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
extern __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_fs6p4tu(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_visit_fs6p4tu F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* state */
    void * gm_l3; /* stack */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t0nqnmillkbs6 gm_l6; /* e */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* x */
    GM_STACK_CHECK(&F, 1087u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srcc2qutubarto1, 746, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t3 * 1), ((uint8_t)1ULL));
    }
    {
        void * t5 = gm_l3;
        uint64_t t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_ucgmlo2(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcc2qutubarto1, 747, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 512) + 0));
            gm_l4 = t12;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l5;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l5;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t0nqnmillkbs6 t24 = GM_LD(gm_t0nqnmillkbs6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 32));
                    gm_l6 = t24;
                    {
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                        uint64_t t28 = gm_l1;
                        uint8_t t29 = (!(t27 == t28));
                        if (!t29) goto ie26;
                        {
                            goto lc13;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcc2qutubarto1, 750, 12);
                        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t33 * 1));
                        uint8_t t36 = ((t35 == ((uint8_t)0ULL)));
                        if (!t36) goto ie31;
                        {
                            void * t38 = gm_l0;
                            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            gm_b16_8 t40 = gm_l2;
                            void * t41 = gm_l3;
                            gm_b0_1 t42 = gm_f_visit_fs6p4tu((void *)NULL, cx, NULL, t38, t39, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        goto ix31;
ie31:;
                        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t45 = (uintptr_t)t44;
                        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcc2qutubarto1, 752, 19);
                        uint8_t t47 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t45 * 1));
                        uint8_t t48 = ((t47 == ((uint8_t)1ULL)));
                        if (!t48) goto ie43;
                        {
                            void * t50 = gm_l3;
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)t50) + 0));
                            gm_b16_8 t52 = t51;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            gm_tf5srh4mp4b3i t55 = gm_f_index_of_s9trr09(t53, t54);
                            gm_tf5srh4mp4b3i t56 = t55;
                            uint64_t t57;
                            if (((((uint8_t *)&t56))[8] != 0)) {
                                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&t56) + 0));
                                t57 = t58;
                            } else {
                                t57 = ((uint64_t)0ULL);
                            }
                            gm_l7 = t57;
                        }
                        {
                            gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        {
                            {
                                void * t60 = gm_l3;
                                uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)t60) + 0) + 8);
                                void * t62 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t60) + 0)));
                                uint64_t t63 = gm_l7;
                                uint64_t t64 = (uintptr_t)t63;
                                if (t64 > (uintptr_t)t61 || (uintptr_t)t61 > (uintptr_t)t61) gm_trap_slice(t64, (uintptr_t)t61, t61, gm_srcc2qutubarto1, 755, 22);
                                gm_b16_8 t65;
                                GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t62 + t64 * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)((uintptr_t)t61 - t64));
                                gm_l9 = t65;
                            }
                            {
                                gm_l10 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t67 = gm_l10;
                                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    uint8_t t69 = (t67 < t68);
                                    if (!t69) goto lb66;
                                    {
                                        uint64_t t72 = gm_l10;
                                        uint64_t t73 = (uintptr_t)t72;
                                        uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                        uint64_t t75 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t73 * 8));
                                        uint64_t t76 = t75;
                                        uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)&t76));
                                        gm_l11 = t79;
                                        {
                                            void * t81 = (void *)((uint8_t *)&F.l8);
                                            uint64_t t82 = gm_l11;
                                            uint64_t t83 = (uintptr_t)t82;
                                            void * t84 = gm_l0;
                                            uint64_t t85 = GM_LD(uintptr_t, ((((uint8_t *)t84) + 200) + 0) + 8);
                                            if (t83 >= (uintptr_t)(t85)) gm_trap_index(t83, t85, gm_srcc2qutubarto1, 755, 52);
                                            gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t84) + 200) + 0)) + t83 * 184) + 0));
                                            gm_tftb1odc739t5 t87 = gm_f_push_3ef746b(t81, t86);
                                            gm_tftb1odc739t5 t88 = t87;
                                            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_srcc2qutubarto1, 755, 41);
                                            gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
                                        }
                                        goto me77;
mn78:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 755, 13);
me77:;
                                    }
lc66:;
                                    {
                                        uint64_t t91 = gm_l10;
                                        uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                                        uint64_t t94 = r92;
                                        gm_l10 = t94;
                                    }
                                }
lb66:;
                            }
                        }
                        {
                            void * t95 = (void *)((uint8_t *)&F.l8);
                            uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint64_t t97 = (uintptr_t)t96;
                            void * t98 = gm_l0;
                            uint64_t t99 = GM_LD(uintptr_t, ((((uint8_t *)t98) + 200) + 0) + 8);
                            if (t97 >= (uintptr_t)(t99)) gm_trap_index(t97, t99, gm_srcc2qutubarto1, 756, 24);
                            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t98) + 200) + 0)) + t97 * 184) + 0));
                            gm_tftb1odc739t5 t101 = gm_f_push_3ef746b(t95, t100);
                            gm_tftb1odc739t5 t102 = t101;
                            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srcc2qutubarto1, 756, 13);
                            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                        }
                        {
                            void * t104 = gm_l0;
                            void * t105 = GM_LD(void *, (((uint8_t *)t104) + 8));
                            gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 16));
                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                            gm_b16_8 t108 = t107;
                            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                            gm_b16_8 t110;
                            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sdbvaa960ee0f));
                            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
                            gm_tabb710d9mq2e t111;
                            gm_f_join_sfknljb((void *)&t111, (void *)NULL, cx, NULL, t109, t110);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tabb710d9mq2e t112 = t111;
                            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcc2qutubarto1, 757, 67);
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
                            gm_tnpifv20irnph t114;
                            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
                            gm_tabb710d9mq2e t115;
                            gm_f_format_bl6khrb((void *)&t115, (void *)NULL, cx, NULL, t114);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tabb710d9mq2e t116 = t115;
                            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srcc2qutubarto1, 757, 36);
                            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                            gm_b16_8 t118 = t117;
                            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                            gm_b0_1 t120 = gm_f_report_eardf07((void *)NULL, cx, NULL, t105, t106, t119);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        goto ix43;
ie43:;
ix43:;
ix31:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 748, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t122 = gm_l5;
                    uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                    uint64_t t125 = r123;
                    gm_l5 = t125;
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
        {
            void * t127 = gm_l3;
            gm_tf5srh4mp4b3i t128 = gm_f_pop_j4rpvm5(t127);
        }
    }
    {
        uint64_t t129 = gm_l1;
        uint64_t t130 = (uintptr_t)t129;
        uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t130 >= (uintptr_t)(t131)) gm_trap_index(t130, t131, gm_srcc2qutubarto1, 761, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t130 * 1), ((uint8_t)2ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_working_dir_dj7eq93 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
    }
    {
        void * t2 = ((uint8_t *)&F.l0);
        if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srcc2qutubarto1, 224, 17);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(((uint8_t *)t2 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 224, 10);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[16])(pn4->env, pn4->hctx, pn4, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thf5kr0is9u9i t6 = t5;
        uint64_t t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l1 = t7;
    }
    {
        void * t11 = ((uint8_t *)&F.l0);
        uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t13 = gm_l1;
        uint64_t t14 = (uintptr_t)t13;
        if (t12 > t14 || t14 > (uintptr_t)4096) gm_trap_slice(t12, t14, 4096, gm_srcc2qutubarto1, 225, 17);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_tabb710d9mq2e t18;
        gm_f_dupe_g9iqpvn((void *)&t18, (void *)NULL, cx, NULL, t17, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kn7qjfrkcl7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcc2qutubarto1, 225, 12);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        uint8_t t23 = 0;
        F.ret = t22;
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
