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
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; } gm_tb1msgo1fu1n3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t4kmic8080rrj;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1msgo1fu1n3) == 5 && _Alignof(gm_tb1msgo1fu1n3) == 1 && offsetof(gm_tb1msgo1fu1n3, f0) == 0 && offsetof(gm_tb1msgo1fu1n3, f1) == 1 && offsetof(gm_tb1msgo1fu1n3, f2) == 2 && offsetof(gm_tb1msgo1fu1n3, f3) == 3 && offsetof(gm_tb1msgo1fu1n3, f4) == 4, "layout of `(u8, u8, u8, u8, u8)`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; gm_tqs5p4n2tq19v f2; gm_tqs5p4n2tq19v f3; gm_tqs5p4n2tq19v f4; } gm_tcdtdmenq84v9;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_trqfvmjtt4sij v1; } f1; } gm_t21m1eu5ta01n;
_Static_assert(sizeof(gm_t4kmic8080rrj) == 40 && _Alignof(gm_t4kmic8080rrj) == 8 && offsetof(gm_t4kmic8080rrj, f0) == 0 && offsetof(gm_t4kmic8080rrj, f1) == 16 && offsetof(gm_t4kmic8080rrj, f2) == 24 && offsetof(gm_t4kmic8080rrj, f3) == 32, "layout of `Reader`");
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tcdtdmenq84v9) == 10 && _Alignof(gm_tcdtdmenq84v9) == 1 && offsetof(gm_tcdtdmenq84v9, f0) == 0 && offsetof(gm_tcdtdmenq84v9, f1) == 2 && offsetof(gm_tcdtdmenq84v9, f2) == 4 && offsetof(gm_tcdtdmenq84v9, f3) == 6 && offsetof(gm_tcdtdmenq84v9, f4) == 8, "layout of `(?u8, ?u8, ?u8, ?u8, ?u8)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; } f1; } gm_tr1k5nl69lhjo;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_b16_8 v1; } f1; } gm_tu615l4qlr8b2;
_Static_assert(sizeof(gm_t21m1eu5ta01n) == 48 && _Alignof(gm_t21m1eu5ta01n) == 8 && offsetof(gm_t21m1eu5ta01n, f0) == 0 && offsetof(gm_t21m1eu5ta01n, f1) == 8, "layout of `TomlError!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_tktr4rmbindgv v1; } f1; } gm_tefknldmnh2si;
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr1k5nl69lhjo) == 40 && _Alignof(gm_tr1k5nl69lhjo) == 8 && offsetof(gm_tr1k5nl69lhjo, f0) == 0 && offsetof(gm_tr1k5nl69lhjo, f1) == 8, "layout of `TomlError!()`");
typedef struct { gm_b16_8 f0; gm_tktr4rmbindgv f1; uint64_t f2; } gm_trc3jdv2g5qre;
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tu615l4qlr8b2) == 40 && _Alignof(gm_tu615l4qlr8b2) == 8 && offsetof(gm_tu615l4qlr8b2, f0) == 0 && offsetof(gm_tu615l4qlr8b2, f1) == 8, "layout of `TomlError!string`");
_Static_assert(sizeof(gm_tefknldmnh2si) == 56 && _Alignof(gm_tefknldmnh2si) == 8 && offsetof(gm_tefknldmnh2si, f0) == 0 && offsetof(gm_tefknldmnh2si, f1) == 8, "layout of `TomlError!Value`");
_Static_assert(sizeof(gm_trc3jdv2g5qre) == 72 && _Alignof(gm_trc3jdv2g5qre) == 8 && offsetof(gm_trc3jdv2g5qre, f0) == 0 && offsetof(gm_trc3jdv2g5qre, f1) == 16 && offsetof(gm_trc3jdv2g5qre, f2) == 64, "layout of `Entry`");
struct gm_fr_array_canpt3e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tefknldmnh2si ret;
};
struct gm_fr_as_str_982o16v {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_at_line_6uoncqb {
    gm_frame h;
    gm_tob1snuii5bme ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mja1o69 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_of_line_056kej4 {
    gm_frame h;
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_error_96ci5lu {
    gm_frame h;
    gm_tob1snuii5bme ret;
};
struct gm_fr_get_9rme0gb {
    gm_frame h;
    void * ret;
};
struct gm_fr_hex_escape_eoc96dk {
    gm_frame h;
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_inline_table_cvvpgel {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* path */
    gm_tktr4rmbindgv l4; /* v */
    gm_tefknldmnh2si ret;
};
struct gm_fr_insert_path_cvuqfkc {
    gm_frame h;
    gm_tktr4rmbindgv l2; /* v */
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_key_5clrcin {
    gm_frame h;
    gm_tu615l4qlr8b2 ret;
};
struct gm_fr_key_path_7spb050 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* path */
    gm_t21m1eu5ta01n ret;
};
struct gm_fr_list_0202sdh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vvcbrjf {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_table_hh6cki7 {
    gm_frame h;
    void * ret;
};
struct gm_fr_number_4l95pjb {
    gm_frame h;
    gm_tefknldmnh2si ret;
};
struct gm_fr_parse_string_80niubc {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_tu615l4qlr8b2 ret;
};
struct gm_fr_parse_toml_gjtmf79 {
    gm_frame h;
    gm_t4kmic8080rrj l1; /* r */
    gm_trqfvmjtt4sij l6; /* path */
    gm_tktr4rmbindgv l7; /* v */
    gm_trssmtuv8jr0v ret;
};
struct gm_fr_peek_bkdvl65 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_skip_space_and_newlines_qagb1bk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_skip_space_voq272o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_table_header_j60mb6n {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* path */
    gm_trqfvmjtt4sij l13; /* items */
    gm_trqfvmjtt4sij l15; /* grown */
    gm_trqfvmjtt4sij l17; /* items */
    gm_trqfvmjtt4sij l20; /* items */
    gm_trssmtuv8jr0v ret;
};
struct gm_fr_value_oq952o2 {
    gm_frame h;
    gm_tefknldmnh2si ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_array_canpt3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_982o16v_c(gm_tktr4rmbindgv a0);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g_c(gm_tktr4rmbindgv a0);
inline __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_at_line_6uoncqb_c(gm_tob1snuii5bme a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mja1o69(void);
__attribute__((aligned(4))) void gm_f_end_of_line_056kej4(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tr1k5nl69lhjo gm_f_end_of_line_056kej4_c(void * a0);
__attribute__((aligned(4))) void gm_f_error_96ci5lu(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_error_96ci5lu_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_0pvgsic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u10ruvh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hex_escape_eoc96dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_inline_table_cvvpgel(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_insert_path_cvuqfkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_key_5clrcin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_key_path_7spb050(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vvcbrjf(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_9ani70g(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_number_4l95pjb(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tefknldmnh2si gm_f_number_4l95pjb_c(void * a0);
__attribute__((aligned(4))) void gm_f_parse_string_80niubc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0pgacsj_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_2pp1ri6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_skip_space_and_newlines_qagb1bk(void * a0);
__attribute__((aligned(4))) void gm_f_skip_space_voq272o(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_table_header_j60mb6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_value_oq952o2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_cal7njs7es4[16] __attribute__((aligned(8)));
extern uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
extern uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
extern const uint8_t gm_s2a4d8jj74v6o[49];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s70iivik802q4[26];
extern const uint8_t gm_s8lbhmq10rt7i[42];
extern const uint8_t gm_s8rm42n03sfss[4];
extern const uint8_t gm_scvuqmspcpb1m[6];
extern const uint8_t gm_sdq87al6fhs9i[35];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_shlilv0hq3bqu[33];
extern const uint8_t gm_sithisfj5qgfv[67];
extern const uint8_t gm_sj294dvmirbmr[47];
extern const uint8_t gm_skiffh4urhl7e[14];
extern const uint8_t gm_sl2lo3hki3t4d[36];
extern const uint8_t gm_sne7l93pr2tmb[34];
extern const uint8_t gm_sngfc91643s6a[55];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_soakgb2h3g40i[4];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_spl83f693c752[32];
extern const uint8_t gm_sq5stv2m4p0h1[37];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srjddrdnspo96[29];
extern const uint8_t gm_srsr8fvlin2g6[65];
extern const uint8_t gm_std877jr2ed05[29];
extern const uint8_t gm_suonmnnb7u0t1[55];
extern const uint8_t gm_sv5o4caljvbmj[28];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_982o16v F;
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 50, 51);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_table_4f7n31g F;
    void * gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    void * t5 = GM_LD(void *, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    void * t6 = gm_l1;
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((void *)0);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 56, 53);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_line_6uoncqb F;
    gm_tob1snuii5bme gm_l0; /* e */
    uint64_t gm_l1; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 0, t1);
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 8, t2);
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a0) + 24));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 24, t3);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mja1o69(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mja1o69 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi_2();
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
inline __attribute__((aligned(4))) void gm_f_list_vvcbrjf(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vvcbrjf F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_mja1o69();
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
inline __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_table_hh6cki7 F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tqetrgo0q24b5 t1;
    memset((uint8_t *)&t1 + 41, 0, 7);
    gm_f_list_vvcbrjf((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t1 + 0)), (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&t1 + 40, ((uint8_t)0));
    gm_tq7s98ihonkju t2 = gm_f_new_9ani70g((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kt2nhjs8rv5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t3 = t2;
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_src3nt8pf0qk5gn, 39, 44);
    void * t4 = GM_LD(void *, (((uint8_t *)&t3) + 8));
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_bkdvl65 F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 16));
    uint64_t t8 = (uintptr_t)t7;
    gm_tqs5p4n2tq19v t9;
    if (t8 < t5) {
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)t4 + t8 * 1));
    gm_tqs5p4n2tq19v t11;
    GM_ST(uint8_t, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
        t9 = t11;
    } else {
    gm_tqs5p4n2tq19v t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_array_canpt3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_array_canpt3e F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* first_line */
    uint64_t gm_l3; /* before */
    GM_STACK_CHECK(&F, 1698u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 473, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 16), t9);
    }
    {
        gm_f_list_0202sdh_2((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                void * t13 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t13);
            }
            {
                void * t15 = gm_l0;
                gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)93ULL));
                ((uint8_t *)&t17)[1] = 1;
                gm_tqs5p4n2tq19v t18 = t16;
                gm_tqs5p4n2tq19v t19 = t17;
                uint8_t t20 = (gm_eqb44cb3cf1n(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (!t20) goto ie14;
                {
                    goto lb10;
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                void * t23 = gm_l0;
                gm_tqs5p4n2tq19v t24 = gm_f_peek_bkdvl65(t23);
                gm_tqs5p4n2tq19v t25 = t24;
                uint8_t t26;
                if (((((uint8_t *)&t25))[1] != 0)) goto mn28;
                t26 = ((uint8_t)1);
                goto me27;
mn28:;
                t26 = ((uint8_t)0);
                goto me27;
mn29:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 478, 12);
me27:;
                if (!t26) goto ie22;
                {
                    void * t31 = gm_l0;
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_srjddrdnspo96));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(28));
                    gm_tob1snuii5bme t33;
                    gm_f_error_96ci5lu((void *)&t33, t31, t32);
                    uint64_t t34 = gm_l1;
                    gm_tob1snuii5bme t35;
                    gm_f_at_line_6uoncqb((void *)&t35, &t33, t34);
                    gm_tefknldmnh2si t36;
                    GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t36 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t36 + 48, 0);
                    ((uint8_t *)&t36)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t36 + 8), t35);
                    gm_tefknldmnh2si t37 = t36;
                    uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
                    F.ret = t36;
                    goto gm_fret;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                void * t40 = gm_l0;
                gm_tqs5p4n2tq19v t41 = gm_f_peek_bkdvl65(t40);
                gm_tqs5p4n2tq19v t42;
                GM_ST(uint8_t, ((uint8_t *)&t42), ((uint8_t)44ULL));
                ((uint8_t *)&t42)[1] = 1;
                gm_tqs5p4n2tq19v t43 = t41;
                gm_tqs5p4n2tq19v t44 = t42;
                uint8_t t45 = (gm_eqb44cb3cf1n(((uint8_t *)&t43), ((uint8_t *)&t44)));
                if (!t45) goto ie39;
                {
                    void * t47 = gm_l0;
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sq5stv2m4p0h1));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(36));
                    gm_tob1snuii5bme t49;
                    gm_f_error_96ci5lu((void *)&t49, t47, t48);
                    gm_tefknldmnh2si t50;
                    GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t50 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t50 + 48, 0);
                    ((uint8_t *)&t50)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t50 + 8), t49);
                    gm_tefknldmnh2si t51 = t50;
                    uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
                    F.ret = t50;
                    goto gm_fret;
                }
                goto ix39;
ie39:;
ix39:;
            }
            {
                void * t53 = (void *)((uint8_t *)&F.l2);
                void * t54 = gm_l0;
                gm_tefknldmnh2si t55;
                gm_f_value_oq952o2((void *)&t55, (void *)NULL, cx, NULL, t54);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                gm_tefknldmnh2si t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) {
                    gm_tob1snuii5bme t57 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t56) + 8));
                    gm_tefknldmnh2si t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t58 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t58 + 48, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t58 + 8), t57);
                    F.ret = t58;
                    goto gm_fret;
                }
                gm_tktr4rmbindgv t59 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)&t56) + 8));
                gm_tftb1odc739t5 t60 = gm_f_push_0pgacsj_2(t53, &t59);
                gm_tftb1odc739t5 t61 = t60;
                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_src3nt8pf0qk5gn, 480, 9);
                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
            }
            {
                void * t63 = gm_l0;
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 16));
                gm_l3 = t64;
            }
            {
                void * t65 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t65);
            }
            {
                void * t67 = gm_l0;
                gm_tqs5p4n2tq19v t68 = gm_f_peek_bkdvl65(t67);
                gm_tqs5p4n2tq19v t69;
                GM_ST(uint8_t, ((uint8_t *)&t69), ((uint8_t)44ULL));
                ((uint8_t *)&t69)[1] = 1;
                gm_tqs5p4n2tq19v t70 = t68;
                gm_tqs5p4n2tq19v t71 = t69;
                uint8_t t72 = (gm_eqb44cb3cf1n(((uint8_t *)&t70), ((uint8_t *)&t71)));
                if (!t72) goto ie66;
                {
                    void * t74 = gm_l0;
                    void * t75 = gm_l0;
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t75) + 16));
                    uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                    if (o78) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 484, 13);
                    uint64_t t79 = r77;
                    GM_ST(uint64_t, (((uint8_t *)t74) + 16), t79);
                }
                goto ix66;
ie66:;
                void * t81 = gm_l0;
                gm_tqs5p4n2tq19v t82 = gm_f_peek_bkdvl65(t81);
                gm_tqs5p4n2tq19v t83;
                GM_ST(uint8_t, ((uint8_t *)&t83), ((uint8_t)93ULL));
                ((uint8_t *)&t83)[1] = 1;
                gm_tqs5p4n2tq19v t84 = t82;
                gm_tqs5p4n2tq19v t85 = t83;
                uint8_t t86 = (!gm_eqb44cb3cf1n(((uint8_t *)&t84), ((uint8_t *)&t85)));
                if (!t86) goto ie80;
                {
                    void * t89 = gm_l0;
                    gm_tqs5p4n2tq19v t90 = gm_f_peek_bkdvl65(t89);
                    gm_tqs5p4n2tq19v t91 = t90;
                    uint8_t t92;
                    if (((((uint8_t *)&t91))[1] != 0)) goto mn94;
                    t92 = ((uint8_t)1);
                    goto me93;
mn94:;
                    t92 = ((uint8_t)0);
                    goto me93;
mn95:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 486, 16);
me93:;
                    if (!t92) goto ie88;
                    {
                        void * t97 = gm_l0;
                        gm_b16_8 t98;
                        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_srjddrdnspo96));
                        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(28));
                        gm_tob1snuii5bme t99;
                        gm_f_error_96ci5lu((void *)&t99, t97, t98);
                        uint64_t t100 = gm_l1;
                        gm_tob1snuii5bme t101;
                        gm_f_at_line_6uoncqb((void *)&t101, &t99, t100);
                        gm_tefknldmnh2si t102;
                        GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t102 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t102 + 48, 0);
                        ((uint8_t *)&t102)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t102 + 8), t101);
                        gm_tefknldmnh2si t103 = t102;
                        uint8_t t104 = ((((uint8_t *)&t103))[0] != 0);
                        F.ret = t102;
                        goto gm_fret;
                    }
                    goto ix88;
ie88:;
ix88:;
                }
                {
                    void * t106 = gm_l0;
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t106) + 32));
                    if (!t107) goto ie105;
                    void * t108 = gm_l0;
                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 16));
                    uint64_t t110 = gm_l3;
                    uint8_t t111 = (t109 > t110);
                    if (!t111) goto ie105;
                    {
                        void * t113 = gm_l0;
                        gm_b16_8 t114 = GM_LD(gm_b16_8, gm_cal7njs7es4);
                        gm_tob1snuii5bme t115;
                        gm_f_error_96ci5lu((void *)&t115, t113, t114);
                        gm_tefknldmnh2si t116;
                        GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t116 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t116 + 48, 0);
                        ((uint8_t *)&t116)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t116 + 8), t115);
                        gm_tefknldmnh2si t117 = t116;
                        uint8_t t118 = ((((uint8_t *)&t117))[0] != 0);
                        F.ret = t116;
                        goto gm_fret;
                    }
                    goto ix105;
ie105:;
ix105:;
                }
                {
                    void * t119 = gm_l0;
                    gm_b16_8 t120;
                    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_srjddrdnspo96));
                    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(28));
                    gm_tob1snuii5bme t121;
                    gm_f_error_96ci5lu((void *)&t121, t119, t120);
                    gm_tefknldmnh2si t122;
                    GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t122 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t122 + 48, 0);
                    ((uint8_t *)&t122)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t122 + 8), t121);
                    gm_tefknldmnh2si t123 = t122;
                    uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
                    F.ret = t122;
                    goto gm_fret;
                }
                goto ix80;
ie80:;
ix80:;
ix66:;
            }
lc10:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
        }
        goto gm_lad;
lb10:;
    }
    {
        void * t125 = gm_l0;
        void * t126 = gm_l0;
        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)t126) + 16));
        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
        if (o129) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 491, 5);
        uint64_t t130 = r128;
        GM_ST(uint64_t, (((uint8_t *)t125) + 16), t130);
    }
    {
        void * t131 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t131) + 32), ((uint8_t)0));
    }
    {
        gm_trqfvmjtt4sij t132 = F.l2;
        gm_tktr4rmbindgv t133;
        GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)3ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t133 + 8), t132);
        gm_tefknldmnh2si t134;
        GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
        ((uint8_t *)&t134)[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t134 + 8), t133);
        gm_tefknldmnh2si t135 = t134;
        uint8_t t136 = ((((uint8_t *)&t135))[0] != 0);
        F.ret = t134;
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
extern __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_982o16v_c(gm_tktr4rmbindgv a0) { gm_th2lqbo77pvsc gm_r; gm_f_as_str_982o16v(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g_c(gm_tktr4rmbindgv a0) { return gm_f_as_table_4f7n31g(&a0); }
extern __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_at_line_6uoncqb_c(gm_tob1snuii5bme a0, uint64_t a1) { gm_tob1snuii5bme gm_r; gm_f_at_line_6uoncqb(&gm_r, &a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_end_of_line_056kej4(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_end_of_line_056kej4 F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 668u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_f_skip_space_voq272o(t4);
    }
    {
        void * t6 = gm_l0;
        gm_tqs5p4n2tq19v t7 = gm_f_peek_bkdvl65(t6);
        gm_tqs5p4n2tq19v t8;
        GM_ST(uint8_t, ((uint8_t *)&t8), ((uint8_t)35ULL));
        ((uint8_t *)&t8)[1] = 1;
        gm_tqs5p4n2tq19v t9 = t7;
        gm_tqs5p4n2tq19v t10 = t8;
        uint8_t t11 = (gm_eqb44cb3cf1n(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        {
            for (;;) {
                void * t14 = gm_l0;
                gm_tqs5p4n2tq19v t15 = gm_f_peek_bkdvl65(t14);
                gm_tqs5p4n2tq19v t16 = t15;
                uint8_t t17;
                if (((((uint8_t *)&t16))[1] != 0)) goto mn19;
                t17 = ((uint8_t)0);
                goto me18;
mn19:;
                t17 = ((uint8_t)1);
                goto me18;
mn20:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 135, 15);
me18:;
                if (!t17) goto lb13;
                void * t21 = gm_l0;
                gm_tqs5p4n2tq19v t22 = gm_f_peek_bkdvl65(t21);
                gm_tqs5p4n2tq19v t23;
                GM_ST(uint8_t, ((uint8_t *)&t23), ((uint8_t)10ULL));
                ((uint8_t *)&t23)[1] = 1;
                gm_tqs5p4n2tq19v t24 = t22;
                gm_tqs5p4n2tq19v t25 = t23;
                uint8_t t26 = (!gm_eqb44cb3cf1n(((uint8_t *)&t24), ((uint8_t *)&t25)));
                if (!t26) goto lb13;
                {
                    void * t29 = gm_l0;
                    void * t30 = gm_l0;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 16));
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 135, 63);
                    uint64_t t34 = r32;
                    GM_ST(uint64_t, (((uint8_t *)t29) + 16), t34);
                }
lc13:;
            }
lb13:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t36 = gm_l0;
        gm_tqs5p4n2tq19v t37 = gm_f_peek_bkdvl65(t36);
        gm_tqs5p4n2tq19v t38;
        GM_ST(uint8_t, ((uint8_t *)&t38), ((uint8_t)13ULL));
        ((uint8_t *)&t38)[1] = 1;
        gm_tqs5p4n2tq19v t39 = t37;
        gm_tqs5p4n2tq19v t40 = t38;
        uint8_t t41 = (gm_eqb44cb3cf1n(((uint8_t *)&t39), ((uint8_t *)&t40)));
        if (!t41) goto ie35;
        {
            void * t43 = gm_l0;
            void * t44 = gm_l0;
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 16));
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            if (o47) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 137, 27);
            uint64_t t48 = r46;
            GM_ST(uint64_t, (((uint8_t *)t43) + 16), t48);
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        void * t50 = gm_l0;
        gm_tqs5p4n2tq19v t51 = gm_f_peek_bkdvl65(t50);
        gm_tqs5p4n2tq19v t52 = t51;
        uint8_t t53;
        if (((((uint8_t *)&t52))[1] != 0)) goto mn55;
        t53 = ((uint8_t)1);
        goto me54;
mn55:;
        t53 = ((uint8_t)0);
        goto me54;
mn56:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 138, 8);
me54:;
        if (!t53) goto ie49;
        {
            gm_b0_1 t58;
            gm_tr1k5nl69lhjo t59;
            memset(&t59, 0, sizeof t59);
            ((uint8_t *)&t59)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t59 + 8), t58);
            gm_tr1k5nl69lhjo t60 = t59;
            uint8_t t61 = ((((uint8_t *)&t60))[0] != 0);
            F.ret = t59;
            goto gm_fret;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        void * t63 = gm_l0;
        gm_tqs5p4n2tq19v t64 = gm_f_peek_bkdvl65(t63);
        gm_tqs5p4n2tq19v t65;
        GM_ST(uint8_t, ((uint8_t *)&t65), ((uint8_t)10ULL));
        ((uint8_t *)&t65)[1] = 1;
        gm_tqs5p4n2tq19v t66 = t64;
        gm_tqs5p4n2tq19v t67 = t65;
        uint8_t t68 = (!gm_eqb44cb3cf1n(((uint8_t *)&t66), ((uint8_t *)&t67)));
        if (!t68) goto ie62;
        {
            void * t71 = gm_l0;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t71) + 32));
            if (!t72) goto ie70;
            void * t73 = gm_l0;
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 16));
            uint64_t t75 = gm_l1;
            uint8_t t76 = (t74 > t75);
            if (!t76) goto ie70;
            {
                void * t78 = gm_l0;
                gm_b16_8 t79 = GM_LD(gm_b16_8, gm_cal7njs7es4);
                gm_tob1snuii5bme t80;
                gm_f_error_96ci5lu((void *)&t80, t78, t79);
                gm_tr1k5nl69lhjo t81;
                GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                ((uint8_t *)&t81)[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t81 + 8), t80);
                gm_tr1k5nl69lhjo t82 = t81;
                uint8_t t83 = ((((uint8_t *)&t82))[0] != 0);
                F.ret = t81;
                goto gm_fret;
            }
            goto ix70;
ie70:;
ix70:;
        }
        {
            void * t84 = gm_l0;
            gm_b16_8 t85;
            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sj294dvmirbmr));
            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(46));
            gm_tob1snuii5bme t86;
            gm_f_error_96ci5lu((void *)&t86, t84, t85);
            gm_tr1k5nl69lhjo t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
            ((uint8_t *)&t87)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t87 + 8), t86);
            gm_tr1k5nl69lhjo t88 = t87;
            uint8_t t89 = ((((uint8_t *)&t88))[0] != 0);
            F.ret = t87;
            goto gm_fret;
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        gm_b0_1 t90;
        gm_tr1k5nl69lhjo t91;
        memset(&t91, 0, sizeof t91);
        ((uint8_t *)&t91)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t91 + 8), t90);
        gm_tr1k5nl69lhjo t92 = t91;
        uint8_t t93 = ((((uint8_t *)&t92))[0] != 0);
        F.ret = t91;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tr1k5nl69lhjo gm_f_end_of_line_056kej4_c(void * a0) { gm_tr1k5nl69lhjo gm_r; gm_f_end_of_line_056kej4(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_error_96ci5lu(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_96ci5lu F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* message */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        gm_l3 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l3;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 99, 19);
                uint64_t t13 = r11;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src3nt8pf0qk5gn, 99, 12);
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 1));
                uint8_t t18 = ((t17 == ((uint8_t)10ULL)));
                if (!t18) goto ie9;
                {
                    uint64_t t20 = gm_l3;
                    gm_l2 = t20;
                }
                {
                    goto lb4;
                }
                goto ix9;
ie9:;
ix9:;
            }
            {
                uint64_t t21 = gm_l3;
                uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 103, 9);
                uint64_t t24 = r22;
                gm_l3 = t24;
            }
lc4:;
        }
lb4:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 24));
        gm_b16_8 t27 = gm_l1;
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
        uint64_t t30 = gm_l2;
        uint64_t r31; bool o32 = __builtin_sub_overflow(t29, t30, &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 105, 59);
        uint64_t t33 = r31;
        uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 105, 59);
        uint64_t t36 = r34;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 0, t26);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 8, t27);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 24, t36);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_error_96ci5lu_c(void * a0, gm_b16_8 a1) { gm_tob1snuii5bme gm_r; gm_f_error_96ci5lu(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_9rme0gb F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 0) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src3nt8pf0qk5gn, 44, 12);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 0) + 0)) + t19 * 72) + 0));
                        gm_b16_8 t23 = gm_l1;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l4;
                            uint64_t t29 = (uintptr_t)t28;
                            void * t30 = gm_l0;
                            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 0) + 0) + 8);
                            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_src3nt8pf0qk5gn, 44, 52);
                            void * t32 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 0) + 0)) + t29 * 72);
                            uint8_t t33 = 0;
                            F.ret = t32;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 43, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t35 = gm_l3;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l3 = t38;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t39 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_hex_escape_eoc96dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2) {
    struct gm_fr_hex_escape_eoc96dk F;
    void * gm_l0; /* r */
    void * gm_l1; /* out */
    uint64_t gm_l2; /* digits */
    uint32_t gm_l3; /* code */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* d */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* v */
    GM_STACK_CHECK(&F, 1020u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint32_t)0ULL);
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = gm_l4;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = t10;
                    {
                        void * t15 = gm_l0;
                        gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                        gm_tqs5p4n2tq19v t17 = t16;
                        uint8_t t18;
                        if (((((uint8_t *)&t17))[1] != 0)) {
                            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
                            t18 = t19;
                        } else {
                            void * t20 = gm_l0;
                            gm_b16_8 t21;
                            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_srsr8fvlin2g6));
                            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(64));
                            gm_tob1snuii5bme t22;
                            gm_f_error_96ci5lu((void *)&t22, t20, t21);
                            gm_tr1k5nl69lhjo t23;
                            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                            ((uint8_t *)&t23)[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t23 + 8), t22);
                            gm_tr1k5nl69lhjo t24 = t23;
                            uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
                            F.ret = t23;
                            goto gm_fret;
                        }
                        gm_l6 = t18;
                    }
                    {
                        uint32_t t26;
                        {
                            uint8_t t29 = gm_l6;
                            gm_l7 = t29;
                        }
                        uint8_t t30 = 0;
                        uint8_t t31 = gm_l7;
                        uint8_t t32 = (((uint8_t)48ULL) <= t31);
                        if (t32) {
                            uint8_t t33 = gm_l7;
                            uint8_t t34 = (t33 <= ((uint8_t)57ULL));
                            t30 = t34;
                        } else t30 = 0;
                        uint8_t t35 = t30;
                        if (!t35) goto ie27;
                        uint8_t t37 = gm_l6;
                        uint8_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint8_t)48ULL), &r38);
                        if (o39) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 437, 17);
                        uint8_t t40 = r38;
                        uint32_t t41 = (uint32_t)t40;
                        uint32_t t42 = t41;
                        t26 = t42;
                        goto ix27;
ie27:;
                        uint32_t t43;
                        {
                            uint8_t t46 = gm_l6;
                            gm_l8 = t46;
                        }
                        uint8_t t47 = 0;
                        uint8_t t48 = gm_l8;
                        uint8_t t49 = (((uint8_t)97ULL) <= t48);
                        if (t49) {
                            uint8_t t50 = gm_l8;
                            uint8_t t51 = (t50 <= ((uint8_t)102ULL));
                            t47 = t51;
                        } else t47 = 0;
                        uint8_t t52 = t47;
                        if (!t52) goto ie44;
                        uint8_t t54 = gm_l6;
                        uint8_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint8_t)97ULL), &r55);
                        if (o56) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 439, 17);
                        uint8_t t57 = r55;
                        uint32_t t58 = (uint32_t)t57;
                        uint32_t r59; bool o60 = __builtin_add_overflow(t58, ((uint32_t)10ULL), &r59);
                        if (o60) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 439, 13);
                        uint32_t t61 = r59;
                        uint32_t t62 = t61;
                        t43 = t62;
                        goto ix44;
ie44:;
                        uint32_t t63;
                        {
                            uint8_t t66 = gm_l6;
                            gm_l9 = t66;
                        }
                        uint8_t t67 = 0;
                        uint8_t t68 = gm_l9;
                        uint8_t t69 = (((uint8_t)65ULL) <= t68);
                        if (t69) {
                            uint8_t t70 = gm_l9;
                            uint8_t t71 = (t70 <= ((uint8_t)70ULL));
                            t67 = t71;
                        } else t67 = 0;
                        uint8_t t72 = t67;
                        if (!t72) goto ie64;
                        uint8_t t74 = gm_l6;
                        uint8_t r75; bool o76 = __builtin_sub_overflow(t74, ((uint8_t)65ULL), &r75);
                        if (o76) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 441, 17);
                        uint8_t t77 = r75;
                        uint32_t t78 = (uint32_t)t77;
                        uint32_t r79; bool o80 = __builtin_add_overflow(t78, ((uint32_t)10ULL), &r79);
                        if (o80) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 441, 13);
                        uint32_t t81 = r79;
                        uint32_t t82 = t81;
                        t63 = t82;
                        goto ix64;
ie64:;
                        void * t84 = gm_l0;
                        gm_b16_8 t85;
                        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_srsr8fvlin2g6));
                        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(64));
                        gm_tob1snuii5bme t86;
                        gm_f_error_96ci5lu((void *)&t86, t84, t85);
                        gm_tr1k5nl69lhjo t87;
                        GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
                        ((uint8_t *)&t87)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t87 + 8), t86);
                        gm_tr1k5nl69lhjo t88 = t87;
                        uint8_t t89 = ((((uint8_t *)&t88))[0] != 0);
                        F.ret = t87;
                        goto gm_fret;
                        t63 = ((uint32_t)0);
ix64:;
                        t43 = t63;
ix44:;
                        t26 = t43;
ix27:;
                        gm_l10 = t26;
                    }
                    {
                        uint32_t t90 = gm_l3;
                        uint32_t r91; bool o92 = __builtin_mul_overflow(t90, ((uint32_t)16ULL), &r91);
                        if (o92) gm_trap("integer overflow in `*`", gm_src3nt8pf0qk5gn, 445, 16);
                        uint32_t t93 = r91;
                        uint32_t t94 = gm_l10;
                        uint32_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
                        if (o96) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 445, 16);
                        uint32_t t97 = r95;
                        gm_l3 = t97;
                    }
                    {
                        void * t98 = gm_l0;
                        void * t99 = gm_l0;
                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)t99) + 16));
                        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                        if (o102) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 446, 9);
                        uint64_t t103 = r101;
                        GM_ST(uint64_t, (((uint8_t *)t98) + 16), t103);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 434, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t105 = gm_l5;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l5 = t108;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t110 = 0;
        uint32_t t111 = gm_l3;
        uint8_t t112 = (t111 > ((uint32_t)1114111ULL));
        if (!t112) {
            uint8_t t113 = 0;
            uint32_t t114 = gm_l3;
            uint8_t t115 = (t114 >= ((uint32_t)55296ULL));
            if (t115) {
                uint32_t t116 = gm_l3;
                uint8_t t117 = (t116 < ((uint32_t)57344ULL));
                t113 = t117;
            } else t113 = 0;
            t110 = t113;
        } else t110 = 1;
        if (!t110) goto ie109;
        {
            void * t119 = gm_l0;
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sngfc91643s6a));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(54));
            gm_tob1snuii5bme t121;
            gm_f_error_96ci5lu((void *)&t121, t119, t120);
            gm_tr1k5nl69lhjo t122;
            GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
            ((uint8_t *)&t122)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t122 + 8), t121);
            gm_tr1k5nl69lhjo t123 = t122;
            uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
            F.ret = t122;
            goto gm_fret;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        uint32_t t126 = gm_l3;
        uint8_t t127 = (t126 < ((uint32_t)128ULL));
        if (!t127) goto ie125;
        {
            void * t129 = gm_l1;
            uint32_t t130 = gm_l3;
            uint8_t t131 = (uint8_t)t130;
            gm_tftb1odc739t5 t132 = gm_f_push_t83rg2m(t129, t131);
            gm_tftb1odc739t5 t133 = t132;
            if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_src3nt8pf0qk5gn, 450, 9);
            gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
        }
        goto ix125;
ie125:;
        uint32_t t136 = gm_l3;
        uint8_t t137 = (t136 < ((uint32_t)2048ULL));
        if (!t137) goto ie135;
        {
            void * t139 = gm_l1;
            uint32_t t140 = gm_l3;
            unsigned __int128 n141 = (unsigned __int128)((uint64_t)6ULL);
            if (n141 >= 32) gm_trap_shift((uint64_t)n141, "u32", gm_src3nt8pf0qk5gn, 452, 27);
            uint32_t t142 = (uint32_t)(t140 >> (int)n141);
            uint32_t t143 = (uint32_t)(((uint32_t)192ULL) | t142);
            uint8_t t144 = (uint8_t)t143;
            gm_tftb1odc739t5 t145 = gm_f_push_t83rg2m(t139, t144);
            gm_tftb1odc739t5 t146 = t145;
            if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_src3nt8pf0qk5gn, 452, 9);
            gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
        }
        {
            void * t148 = gm_l1;
            uint32_t t149 = gm_l3;
            uint32_t t150 = (uint32_t)(t149 & ((uint32_t)63ULL));
            uint32_t t151 = (uint32_t)(((uint32_t)128ULL) | t150);
            uint8_t t152 = (uint8_t)t151;
            gm_tftb1odc739t5 t153 = gm_f_push_t83rg2m(t148, t152);
            gm_tftb1odc739t5 t154 = t153;
            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_src3nt8pf0qk5gn, 453, 9);
            gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
        }
        goto ix135;
ie135:;
        uint32_t t157 = gm_l3;
        uint8_t t158 = (t157 < ((uint32_t)65536ULL));
        if (!t158) goto ie156;
        {
            void * t160 = gm_l1;
            uint32_t t161 = gm_l3;
            unsigned __int128 n162 = (unsigned __int128)((uint64_t)12ULL);
            if (n162 >= 32) gm_trap_shift((uint64_t)n162, "u32", gm_src3nt8pf0qk5gn, 455, 27);
            uint32_t t163 = (uint32_t)(t161 >> (int)n162);
            uint32_t t164 = (uint32_t)(((uint32_t)224ULL) | t163);
            uint8_t t165 = (uint8_t)t164;
            gm_tftb1odc739t5 t166 = gm_f_push_t83rg2m(t160, t165);
            gm_tftb1odc739t5 t167 = t166;
            if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_src3nt8pf0qk5gn, 455, 9);
            gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
        }
        {
            void * t169 = gm_l1;
            uint32_t t170 = gm_l3;
            unsigned __int128 n171 = (unsigned __int128)((uint64_t)6ULL);
            if (n171 >= 32) gm_trap_shift((uint64_t)n171, "u32", gm_src3nt8pf0qk5gn, 456, 28);
            uint32_t t172 = (uint32_t)(t170 >> (int)n171);
            uint32_t t173 = (uint32_t)(t172 & ((uint32_t)63ULL));
            uint32_t t174 = (uint32_t)(((uint32_t)128ULL) | t173);
            uint8_t t175 = (uint8_t)t174;
            gm_tftb1odc739t5 t176 = gm_f_push_t83rg2m(t169, t175);
            gm_tftb1odc739t5 t177 = t176;
            if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 1, gm_src3nt8pf0qk5gn, 456, 9);
            gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 1));
        }
        {
            void * t179 = gm_l1;
            uint32_t t180 = gm_l3;
            uint32_t t181 = (uint32_t)(t180 & ((uint32_t)63ULL));
            uint32_t t182 = (uint32_t)(((uint32_t)128ULL) | t181);
            uint8_t t183 = (uint8_t)t182;
            gm_tftb1odc739t5 t184 = gm_f_push_t83rg2m(t179, t183);
            gm_tftb1odc739t5 t185 = t184;
            if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 1, gm_src3nt8pf0qk5gn, 457, 9);
            gm_b0_1 t186 = GM_LD(gm_b0_1, (((uint8_t *)&t185) + 1));
        }
        goto ix156;
ie156:;
        {
            void * t188 = gm_l1;
            uint32_t t189 = gm_l3;
            unsigned __int128 n190 = (unsigned __int128)((uint64_t)18ULL);
            if (n190 >= 32) gm_trap_shift((uint64_t)n190, "u32", gm_src3nt8pf0qk5gn, 459, 27);
            uint32_t t191 = (uint32_t)(t189 >> (int)n190);
            uint32_t t192 = (uint32_t)(((uint32_t)240ULL) | t191);
            uint8_t t193 = (uint8_t)t192;
            gm_tftb1odc739t5 t194 = gm_f_push_t83rg2m(t188, t193);
            gm_tftb1odc739t5 t195 = t194;
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_src3nt8pf0qk5gn, 459, 9);
            gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
        }
        {
            void * t197 = gm_l1;
            uint32_t t198 = gm_l3;
            unsigned __int128 n199 = (unsigned __int128)((uint64_t)12ULL);
            if (n199 >= 32) gm_trap_shift((uint64_t)n199, "u32", gm_src3nt8pf0qk5gn, 460, 28);
            uint32_t t200 = (uint32_t)(t198 >> (int)n199);
            uint32_t t201 = (uint32_t)(t200 & ((uint32_t)63ULL));
            uint32_t t202 = (uint32_t)(((uint32_t)128ULL) | t201);
            uint8_t t203 = (uint8_t)t202;
            gm_tftb1odc739t5 t204 = gm_f_push_t83rg2m(t197, t203);
            gm_tftb1odc739t5 t205 = t204;
            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_src3nt8pf0qk5gn, 460, 9);
            gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
        }
        {
            void * t207 = gm_l1;
            uint32_t t208 = gm_l3;
            unsigned __int128 n209 = (unsigned __int128)((uint64_t)6ULL);
            if (n209 >= 32) gm_trap_shift((uint64_t)n209, "u32", gm_src3nt8pf0qk5gn, 461, 28);
            uint32_t t210 = (uint32_t)(t208 >> (int)n209);
            uint32_t t211 = (uint32_t)(t210 & ((uint32_t)63ULL));
            uint32_t t212 = (uint32_t)(((uint32_t)128ULL) | t211);
            uint8_t t213 = (uint8_t)t212;
            gm_tftb1odc739t5 t214 = gm_f_push_t83rg2m(t207, t213);
            gm_tftb1odc739t5 t215 = t214;
            if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 1, gm_src3nt8pf0qk5gn, 461, 9);
            gm_b0_1 t216 = GM_LD(gm_b0_1, (((uint8_t *)&t215) + 1));
        }
        {
            void * t217 = gm_l1;
            uint32_t t218 = gm_l3;
            uint32_t t219 = (uint32_t)(t218 & ((uint32_t)63ULL));
            uint32_t t220 = (uint32_t)(((uint32_t)128ULL) | t219);
            uint8_t t221 = (uint8_t)t220;
            gm_tftb1odc739t5 t222 = gm_f_push_t83rg2m(t217, t221);
            gm_tftb1odc739t5 t223 = t222;
            if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 1, gm_src3nt8pf0qk5gn, 462, 9);
            gm_b0_1 t224 = GM_LD(gm_b0_1, (((uint8_t *)&t223) + 1));
        }
ix156:;
ix135:;
ix125:;
    }
    {
        gm_b0_1 t225;
        gm_tr1k5nl69lhjo t226;
        memset(&t226, 0, sizeof t226);
        ((uint8_t *)&t226)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t226 + 8), t225);
        gm_tr1k5nl69lhjo t227 = t226;
        uint8_t t228 = ((((uint8_t *)&t227))[0] != 0);
        F.ret = t226;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_inline_table_cvvpgel(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_inline_table_cvvpgel F;
    void * gm_l0; /* r */
    void * gm_l1; /* t */
    uint64_t gm_l2; /* line */
    GM_STACK_CHECK(&F, 1883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 498, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 16), t7);
    }
    {
        void * t8 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t8;
    }
    {
        void * t9 = gm_l1;
        GM_ST(uint8_t, (((uint8_t *)t9) + 40), ((uint8_t)1));
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                void * t13 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t13);
            }
            {
                void * t15 = gm_l0;
                gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)125ULL));
                ((uint8_t *)&t17)[1] = 1;
                gm_tqs5p4n2tq19v t18 = t16;
                gm_tqs5p4n2tq19v t19 = t17;
                uint8_t t20 = (gm_eqb44cb3cf1n(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (!t20) goto ie14;
                {
                    goto lb10;
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                void * t23 = gm_l0;
                gm_tqs5p4n2tq19v t24 = gm_f_peek_bkdvl65(t23);
                gm_tqs5p4n2tq19v t25 = t24;
                uint8_t t26;
                if (((((uint8_t *)&t25))[1] != 0)) goto mn28;
                t26 = ((uint8_t)1);
                goto me27;
mn28:;
                t26 = ((uint8_t)0);
                goto me27;
mn29:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 504, 12);
me27:;
                if (!t26) goto ie22;
                {
                    void * t31 = gm_l0;
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sl2lo3hki3t4d));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(35));
                    gm_tob1snuii5bme t33;
                    gm_f_error_96ci5lu((void *)&t33, t31, t32);
                    gm_tefknldmnh2si t34;
                    GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t34 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t34 + 48, 0);
                    ((uint8_t *)&t34)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t34 + 8), t33);
                    gm_tefknldmnh2si t35 = t34;
                    uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
                    F.ret = t34;
                    goto gm_fret;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                void * t37 = gm_l0;
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 24));
                gm_l2 = t38;
            }
            {
                void * t39 = gm_l0;
                gm_t21m1eu5ta01n t40;
                gm_f_key_path_7spb050((void *)&t40, (void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                gm_t21m1eu5ta01n t41 = t40;
                if (((((uint8_t *)&t41))[0] != 0)) {
                    gm_tob1snuii5bme t42 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t41) + 8));
                    gm_tefknldmnh2si t43;
                    GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t43 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t43 + 48, 0);
                    ((uint8_t *)&t43)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t43 + 8), t42);
                    F.ret = t43;
                    goto gm_fret;
                }
                gm_trqfvmjtt4sij t44 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t41) + 8));
                F.l3 = t44;
            }
            {
                void * t45 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t45);
            }
            {
                void * t47 = gm_l0;
                gm_tqs5p4n2tq19v t48 = gm_f_peek_bkdvl65(t47);
                gm_tqs5p4n2tq19v t49;
                GM_ST(uint8_t, ((uint8_t *)&t49), ((uint8_t)61ULL));
                ((uint8_t *)&t49)[1] = 1;
                gm_tqs5p4n2tq19v t50 = t48;
                gm_tqs5p4n2tq19v t51 = t49;
                uint8_t t52 = (!gm_eqb44cb3cf1n(((uint8_t *)&t50), ((uint8_t *)&t51)));
                if (!t52) goto ie46;
                {
                    void * t54 = gm_l0;
                    gm_b16_8 t55;
                    GM_ST(void *, (uint8_t *)&t55, (void *)(gm_spl83f693c752));
                    GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(31));
                    gm_tob1snuii5bme t56;
                    gm_f_error_96ci5lu((void *)&t56, t54, t55);
                    gm_tefknldmnh2si t57;
                    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t57 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t57 + 48, 0);
                    ((uint8_t *)&t57)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t57 + 8), t56);
                    gm_tefknldmnh2si t58 = t57;
                    uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
                    F.ret = t57;
                    goto gm_fret;
                }
                goto ix46;
ie46:;
ix46:;
            }
            {
                void * t60 = gm_l0;
                void * t61 = gm_l0;
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 16));
                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                if (o64) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 509, 9);
                uint64_t t65 = r63;
                GM_ST(uint64_t, (((uint8_t *)t60) + 16), t65);
            }
            {
                void * t66 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t66);
            }
            {
                void * t67 = gm_l0;
                gm_tefknldmnh2si t68;
                gm_f_value_oq952o2((void *)&t68, (void *)NULL, cx, NULL, t67);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                gm_tefknldmnh2si t69 = t68;
                if (((((uint8_t *)&t69))[0] != 0)) {
                    gm_tob1snuii5bme t70 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t69) + 8));
                    gm_tefknldmnh2si t71;
                    GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t71 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t71 + 48, 0);
                    ((uint8_t *)&t71)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t71 + 8), t70);
                    F.ret = t71;
                    goto gm_fret;
                }
                gm_tktr4rmbindgv t72 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)&t69) + 8));
                F.l4 = t72;
            }
            {
                void * t73 = gm_l1;
                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_b16_8 t75 = t74;
                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                uint64_t t77 = gm_l2;
                gm_tr1k5nl69lhjo t78;
                gm_f_insert_path_cvuqfkc((void *)&t78, (void *)NULL, cx, NULL, t73, t76, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l4)), t77);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                gm_tr1k5nl69lhjo t79 = t78;
                if (((((uint8_t *)&t79))[0] != 0)) {
                    gm_tob1snuii5bme t80 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t79) + 8));
                    gm_tefknldmnh2si t81;
                    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t81 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t81 + 48, 0);
                    ((uint8_t *)&t81)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t81 + 8), t80);
                    F.ret = t81;
                    goto gm_fret;
                }
                gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 8));
            }
            {
                void * t83 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t83);
            }
            {
                void * t85 = gm_l0;
                gm_tqs5p4n2tq19v t86 = gm_f_peek_bkdvl65(t85);
                gm_tqs5p4n2tq19v t87;
                GM_ST(uint8_t, ((uint8_t *)&t87), ((uint8_t)125ULL));
                ((uint8_t *)&t87)[1] = 1;
                gm_tqs5p4n2tq19v t88 = t86;
                gm_tqs5p4n2tq19v t89 = t87;
                uint8_t t90 = (gm_eqb44cb3cf1n(((uint8_t *)&t88), ((uint8_t *)&t89)));
                if (!t90) goto ie84;
                {
                    goto lb10;
                }
                goto ix84;
ie84:;
ix84:;
            }
            {
                void * t93 = gm_l0;
                gm_tqs5p4n2tq19v t94 = gm_f_peek_bkdvl65(t93);
                gm_tqs5p4n2tq19v t95 = t94;
                uint8_t t96;
                if (((((uint8_t *)&t95))[1] != 0)) goto mn98;
                t96 = ((uint8_t)1);
                goto me97;
mn98:;
                t96 = ((uint8_t)0);
                goto me97;
mn99:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 515, 12);
me97:;
                if (!t96) goto ie92;
                {
                    void * t101 = gm_l0;
                    gm_b16_8 t102;
                    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sl2lo3hki3t4d));
                    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(35));
                    gm_tob1snuii5bme t103;
                    gm_f_error_96ci5lu((void *)&t103, t101, t102);
                    gm_tefknldmnh2si t104;
                    GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t104 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t104 + 48, 0);
                    ((uint8_t *)&t104)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t104 + 8), t103);
                    gm_tefknldmnh2si t105 = t104;
                    uint8_t t106 = ((((uint8_t *)&t105))[0] != 0);
                    F.ret = t104;
                    goto gm_fret;
                }
                goto ix92;
ie92:;
ix92:;
            }
            {
                void * t108 = gm_l0;
                gm_tqs5p4n2tq19v t109 = gm_f_peek_bkdvl65(t108);
                gm_tqs5p4n2tq19v t110;
                GM_ST(uint8_t, ((uint8_t *)&t110), ((uint8_t)44ULL));
                ((uint8_t *)&t110)[1] = 1;
                gm_tqs5p4n2tq19v t111 = t109;
                gm_tqs5p4n2tq19v t112 = t110;
                uint8_t t113 = (!gm_eqb44cb3cf1n(((uint8_t *)&t111), ((uint8_t *)&t112)));
                if (!t113) goto ie107;
                {
                    void * t115 = gm_l0;
                    gm_b16_8 t116;
                    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_suonmnnb7u0t1));
                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(54));
                    gm_tob1snuii5bme t117;
                    gm_f_error_96ci5lu((void *)&t117, t115, t116);
                    gm_tefknldmnh2si t118;
                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t118 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t118 + 48, 0);
                    ((uint8_t *)&t118)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t118 + 8), t117);
                    gm_tefknldmnh2si t119 = t118;
                    uint8_t t120 = ((((uint8_t *)&t119))[0] != 0);
                    F.ret = t118;
                    goto gm_fret;
                }
                goto ix107;
ie107:;
ix107:;
            }
            {
                void * t121 = gm_l0;
                void * t122 = gm_l0;
                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)t122) + 16));
                uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                if (o125) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 517, 9);
                uint64_t t126 = r124;
                GM_ST(uint64_t, (((uint8_t *)t121) + 16), t126);
            }
lc10:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
        }
        goto gm_lad;
lb10:;
    }
    {
        void * t127 = gm_l0;
        void * t128 = gm_l0;
        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 16));
        uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
        if (o131) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 519, 5);
        uint64_t t132 = r130;
        GM_ST(uint64_t, (((uint8_t *)t127) + 16), t132);
    }
    {
        void * t133 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t133) + 32), ((uint8_t)0));
    }
    {
        void * t134 = gm_l1;
        gm_tktr4rmbindgv t135;
        memset(&t135, 0, sizeof t135);
        GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t135 + 8), t134);
        gm_tefknldmnh2si t136;
        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
        ((uint8_t *)&t136)[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t136 + 8), t135);
        gm_tefknldmnh2si t137 = t136;
        uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
        F.ret = t136;
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
__attribute__((aligned(4))) void gm_f_insert_path_cvuqfkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint64_t a3) {
    struct gm_fr_insert_path_cvuqfkc F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* path */
    uint64_t gm_l3; /* line */
    void * gm_l4; /* target */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* name */
    void * gm_l8; /* e */
    void * gm_l9; /* sub */
    void * gm_l10; /* fresh */
    gm_b16_8 gm_l11; /* name */
    GM_STACK_CHECK(&F, 1109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_l4 = t2;
    }
    {
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 234, 25);
            uint64_t t10 = r8;
            uint64_t t11 = (uintptr_t)t10;
            if (t6 > t11 || t11 > (uintptr_t)t4) gm_trap_slice(t6, t11, t4, gm_src3nt8pf0qk5gn, 234, 17);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t5 + t6 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(t11 - t6));
            gm_l5 = t12;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l6;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l6;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t20 * 16));
                    gm_l7 = t24;
                    {
                        void * t27 = gm_l4;
                        gm_b16_8 t28 = gm_l7;
                        void * t29 = gm_f_get_9rme0gb(t27, t28);
                        void * t30 = t29;
                        if (!(GM_LD(void *, ((uint8_t *)&t30)) != 0)) goto ie26;
                        void * t31 = GM_LD(void *, ((uint8_t *)&t30));
                        gm_l8 = t31;
                        {
                            void * t33 = gm_l8;
                            void * t34 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t33) + 16)));
                            void * t35 = t34;
                            void * t36;
                            if ((GM_LD(void *, ((uint8_t *)&t35)) != 0)) {
                                void * t37 = GM_LD(void *, (((uint8_t *)&t35) + 0));
                                t36 = t37;
                            } else {
                                gm_tob1snuii5bme t38;
                                uint64_t t39 = gm_l3;
                                GM_ST(uint64_t, (uint8_t *)&t38 + 0, t39);
                                gm_b16_8 t40;
                                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_skiffh4urhl7e));
                                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(13));
                                GM_ST(gm_b16_8, (uint8_t *)&t38 + 8, t40);
                                GM_ST(uint64_t, (uint8_t *)&t38 + 24, ((uint64_t)1ULL));
                                gm_tr1k5nl69lhjo t41;
                                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                                ((uint8_t *)&t41)[0] = 1;
                                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t41 + 8), t38);
                                gm_tr1k5nl69lhjo t42 = t41;
                                uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
                                F.ret = t41;
                                goto gm_fret;
                            }
                            gm_l9 = t36;
                        }
                        {
                            void * t44 = gm_l9;
                            gm_l4 = t44;
                        }
                        goto ix26;
ie26:;
                        {
                            void * t46 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_l10 = t46;
                        }
                        {
                            void * t47 = gm_l4;
                            void * t48 = (void *)(((uint8_t *)t47) + 0);
                            gm_trc3jdv2g5qre t49;
                            gm_b16_8 t50 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
                            void * t51 = gm_l10;
                            gm_tktr4rmbindgv t52;
                            memset(&t52, 0, sizeof t52);
                            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)4ULL));
                            GM_ST(void *, ((uint8_t *)&t52 + 8), t51);
                            GM_ST(gm_tktr4rmbindgv, (uint8_t *)&t49 + 16, t52);
                            uint64_t t53 = gm_l3;
                            GM_ST(uint64_t, (uint8_t *)&t49 + 64, t53);
                            gm_tftb1odc739t5 t54 = gm_f_push_2pp1ri6(t48, &t49);
                            gm_tftb1odc739t5 t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src3nt8pf0qk5gn, 240, 13);
                            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                        }
                        {
                            void * t57 = gm_l10;
                            gm_l4 = t57;
                        }
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 234, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t59 = gm_l6;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l6 = t62;
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
        uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r64; bool o65 = __builtin_sub_overflow(t63, ((uint64_t)1ULL), &r64);
        if (o65) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 244, 18);
        uint64_t t66 = r64;
        uint64_t t67 = (uintptr_t)t66;
        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src3nt8pf0qk5gn, 244, 13);
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t67 * 16));
        gm_l11 = t69;
    }
    {
        void * t71 = gm_l4;
        gm_b16_8 t72 = gm_l11;
        void * t73 = gm_f_get_9rme0gb(t71, t72);
        void * t74 = t73;
        uint8_t t75;
        if ((GM_LD(void *, ((uint8_t *)&t74)) != 0)) goto mn77;
        t75 = ((uint8_t)0);
        goto me76;
mn77:;
        t75 = ((uint8_t)1);
        goto me76;
mn78:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 245, 8);
me76:;
        if (!t75) goto ie70;
        {
            gm_tob1snuii5bme t80;
            uint64_t t81 = gm_l3;
            GM_ST(uint64_t, (uint8_t *)&t80 + 0, t81);
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_skiffh4urhl7e));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(13));
            GM_ST(gm_b16_8, (uint8_t *)&t80 + 8, t82);
            GM_ST(uint64_t, (uint8_t *)&t80 + 24, ((uint64_t)1ULL));
            gm_tr1k5nl69lhjo t83;
            GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
            ((uint8_t *)&t83)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t83 + 8), t80);
            gm_tr1k5nl69lhjo t84 = t83;
            uint8_t t85 = ((((uint8_t *)&t84))[0] != 0);
            F.ret = t83;
            goto gm_fret;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        void * t86 = gm_l4;
        void * t87 = (void *)(((uint8_t *)t86) + 0);
        gm_trc3jdv2g5qre t88;
        gm_b16_8 t89 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t88 + 0, t89);
        memcpy((uint8_t *)&t88 + 16, ((uint8_t *)a2), 48);
        uint64_t t90 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t88 + 64, t90);
        gm_tftb1odc739t5 t91 = gm_f_push_2pp1ri6(t87, &t88);
        gm_tftb1odc739t5 t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_src3nt8pf0qk5gn, 246, 5);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
    }
    {
        gm_b0_1 t94;
        gm_tr1k5nl69lhjo t95;
        memset(&t95, 0, sizeof t95);
        ((uint8_t *)&t95)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t95 + 8), t94);
        gm_tr1k5nl69lhjo t96 = t95;
        uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
        F.ret = t95;
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
__attribute__((aligned(4))) void gm_f_key_5clrcin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_key_5clrcin F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    gm_tob1snuii5bme gm_l2; /* e */
    uint64_t gm_l3; /* start */
    uint8_t gm_l4; /* k */
    uint8_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 999u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tqs5p4n2tq19v t3 = gm_f_peek_bkdvl65(t2);
        gm_tqs5p4n2tq19v t4 = t3;
        uint8_t t5;
        if (((((uint8_t *)&t4))[1] != 0)) {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            void * t7 = gm_l0;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_spl83f693c752));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(31));
            gm_tob1snuii5bme t9;
            gm_f_error_96ci5lu((void *)&t9, t7, t8);
            gm_tu615l4qlr8b2 t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t10 + 8), t9);
            gm_tu615l4qlr8b2 t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t14 = gm_l1;
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)34ULL));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 1), ((uint8_t)39ULL));
        gm_tqs5p4n2tq19v t16 = t15;
        uint8_t t17 = 0;
        uint8_t t18 = t14;
        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 0, ((uint8_t *)&t18))) t17 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t16) + 1, ((uint8_t *)&t18))) t17 = 1;
        uint8_t t19 = (uint8_t)(t17);
        if (!t19) goto ie13;
        {
            void * t21 = gm_l0;
            gm_tu615l4qlr8b2 t22;
            gm_f_parse_string_80niubc((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tu615l4qlr8b2 t23 = t22;
            gm_b16_8 t24;
            if (!((((uint8_t *)&t23))[0] != 0)) {
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
                t24 = t25;
            } else {
                gm_tob1snuii5bme t26 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t23) + 8));
                gm_l2 = t26;
                gm_tob1snuii5bme t28;
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
                GM_ST(uint64_t, (uint8_t *)&t28 + 0, t29);
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_spl83f693c752));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(31));
                GM_ST(gm_b16_8, (uint8_t *)&t28 + 8, t30);
                GM_ST(uint64_t, (uint8_t *)&t28 + 24, ((uint64_t)1ULL));
                gm_tu615l4qlr8b2 t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t31 + 8), t28);
                gm_tu615l4qlr8b2 t32 = t31;
                uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
                F.ret = t31;
                goto gm_fret;
            }
            gm_tu615l4qlr8b2 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t34 + 32, 0);
            ((uint8_t *)&t34)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t34 + 8), t24);
            gm_tu615l4qlr8b2 t35 = t34;
            uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
            F.ret = t34;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t37 = gm_l0;
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 16));
        gm_l3 = t38;
    }
    {
        for (;;) {
            void * t40 = gm_l0;
            gm_tqs5p4n2tq19v t41 = gm_f_peek_bkdvl65(t40);
            gm_tqs5p4n2tq19v t42 = t41;
            if (!((((uint8_t *)&t42))[1] != 0)) goto lb39;
            uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&t42));
            gm_l4 = t43;
            {
                uint8_t t47 = 0;
                uint8_t t48 = 0;
                uint8_t t49 = 0;
                {
                    uint8_t t51 = gm_l4;
                    gm_l5 = t51;
                }
                uint8_t t52 = 0;
                uint8_t t53 = gm_l5;
                uint8_t t54 = (((uint8_t)97ULL) <= t53);
                if (t54) {
                    uint8_t t55 = gm_l5;
                    uint8_t t56 = (t55 <= ((uint8_t)122ULL));
                    t52 = t56;
                } else t52 = 0;
                uint8_t t57 = t52;
                if (!t57) {
                    {
                        uint8_t t59 = gm_l4;
                        gm_l6 = t59;
                    }
                    uint8_t t60 = 0;
                    uint8_t t61 = gm_l6;
                    uint8_t t62 = (((uint8_t)65ULL) <= t61);
                    if (t62) {
                        uint8_t t63 = gm_l6;
                        uint8_t t64 = (t63 <= ((uint8_t)90ULL));
                        t60 = t64;
                    } else t60 = 0;
                    uint8_t t65 = t60;
                    t49 = t65;
                } else t49 = 1;
                if (!t49) {
                    {
                        uint8_t t67 = gm_l4;
                        gm_l7 = t67;
                    }
                    uint8_t t68 = 0;
                    uint8_t t69 = gm_l7;
                    uint8_t t70 = (((uint8_t)48ULL) <= t69);
                    if (t70) {
                        uint8_t t71 = gm_l7;
                        uint8_t t72 = (t71 <= ((uint8_t)57ULL));
                        t68 = t72;
                    } else t68 = 0;
                    uint8_t t73 = t68;
                    t48 = t73;
                } else t48 = 1;
                if (!t48) {
                    uint8_t t74 = gm_l4;
                    gm_tqs5p4n2tq19v t75;
                    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)95ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t75 + 1), ((uint8_t)45ULL));
                    gm_tqs5p4n2tq19v t76 = t75;
                    uint8_t t77 = 0;
                    uint8_t t78 = t74;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t76) + 0, ((uint8_t *)&t78))) t77 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t76) + 1, ((uint8_t *)&t78))) t77 = 1;
                    uint8_t t79 = (uint8_t)(t77);
                    t47 = t79;
                } else t47 = 1;
                uint8_t t80 = (uint8_t)!t47;
                if (!t80) goto ie46;
                {
                    goto lb39;
                }
                goto ix46;
ie46:;
ix46:;
            }
            {
                void * t82 = gm_l0;
                void * t83 = gm_l0;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)t83) + 16));
                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                if (o86) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 225, 9);
                uint64_t t87 = r85;
                GM_ST(uint64_t, (((uint8_t *)t82) + 16), t87);
            }
lc39:;
        }
lb39:;
    }
    {
        void * t89 = gm_l0;
        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)t89) + 16));
        uint64_t t91 = gm_l3;
        uint8_t t92 = ((t90 == t91));
        if (!t92) goto ie88;
        {
            void * t94 = gm_l0;
            gm_b16_8 t95;
            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sithisfj5qgfv));
            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(66));
            gm_tob1snuii5bme t96;
            gm_f_error_96ci5lu((void *)&t96, t94, t95);
            gm_tu615l4qlr8b2 t97;
            GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
            ((uint8_t *)&t97)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t97 + 8), t96);
            gm_tu615l4qlr8b2 t98 = t97;
            uint8_t t99 = ((((uint8_t *)&t98))[0] != 0);
            F.ret = t97;
            goto gm_fret;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        void * t100 = gm_l0;
        uint64_t t101 = GM_LD(uintptr_t, (((uint8_t *)t100) + 0) + 8);
        void * t102 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t100) + 0)));
        uint64_t t103 = gm_l3;
        uint64_t t104 = (uintptr_t)t103;
        void * t105 = gm_l0;
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)t105) + 16));
        uint64_t t107 = (uintptr_t)t106;
        if (t104 > t107 || t107 > (uintptr_t)t101) gm_trap_slice(t104, t107, t101, gm_src3nt8pf0qk5gn, 228, 12);
        gm_b16_8 t108;
        GM_ST(void *, (uint8_t *)&t108, (void *)(((uint8_t *)t102 + t104 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(t107 - t104));
        gm_tu615l4qlr8b2 t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t109 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t109 + 32, 0);
        ((uint8_t *)&t109)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 8), t108);
        gm_tu615l4qlr8b2 t110 = t109;
        uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
        F.ret = t109;
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
inline __attribute__((aligned(4))) void gm_f_key_path_7spb050(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_key_path_7spb050 F;
    void * gm_l0; /* r */
    GM_STACK_CHECK(&F, 406u);
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
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = gm_l0;
                gm_f_skip_space_voq272o(t5);
            }
            {
                void * t6 = (void *)((uint8_t *)&F.l1);
                void * t7 = gm_l0;
                gm_tu615l4qlr8b2 t8;
                gm_f_key_5clrcin((void *)&t8, (void *)NULL, cx, NULL, t7);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                gm_tu615l4qlr8b2 t9 = t8;
                if (((((uint8_t *)&t9))[0] != 0)) {
                    gm_tob1snuii5bme t10 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t9) + 8));
                    gm_t21m1eu5ta01n t11;
                    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
                    ((uint8_t *)&t11)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t11 + 8), t10);
                    F.ret = t11;
                    goto gm_fret;
                }
                gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
                gm_tftb1odc739t5 t13 = gm_f_push_3ef746b(t6, t12);
                gm_tftb1odc739t5 t14 = t13;
                if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_src3nt8pf0qk5gn, 209, 9);
                gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
            }
            {
                void * t16 = gm_l0;
                gm_f_skip_space_voq272o(t16);
            }
            {
                void * t18 = gm_l0;
                gm_tqs5p4n2tq19v t19 = gm_f_peek_bkdvl65(t18);
                gm_tqs5p4n2tq19v t20;
                GM_ST(uint8_t, ((uint8_t *)&t20), ((uint8_t)46ULL));
                ((uint8_t *)&t20)[1] = 1;
                gm_tqs5p4n2tq19v t21 = t19;
                gm_tqs5p4n2tq19v t22 = t20;
                uint8_t t23 = (!gm_eqb44cb3cf1n(((uint8_t *)&t21), ((uint8_t *)&t22)));
                if (!t23) goto ie17;
                {
                    goto lb2;
                }
                goto ix17;
ie17:;
ix17:;
            }
            {
                void * t25 = gm_l0;
                void * t26 = gm_l0;
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 16));
                uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 212, 9);
                uint64_t t30 = r28;
                GM_ST(uint64_t, (((uint8_t *)t25) + 16), t30);
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        gm_trqfvmjtt4sij t31 = F.l1;
        gm_t21m1eu5ta01n t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 8), t31);
        gm_t21m1eu5ta01n t33 = t32;
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t32;
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
extern __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_number_4l95pjb(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_number_4l95pjb F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* negative */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    int64_t gm_l5; /* radix */
    uint8_t gm_l6; /* n */
    int64_t gm_l7; /* n */
    int32_t gm_l8; /* digits */
    uint8_t gm_l9; /* last_underscore */
    uint64_t gm_l10; /* first */
    uint8_t gm_l11; /* d */
    uint8_t gm_l12; /* <tmp> */
    uint8_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* <tmp> */
    int64_t gm_l15; /* v */
    gm_t913bb735pvt5 gm_l16; /* <tmp> */
    int64_t gm_l17; /* <tmp> */
    int64_t gm_l18; /* <tmp> */
    uint8_t gm_l19; /* k */
    uint8_t gm_l20; /* <tmp> */
    uint8_t gm_l21; /* <tmp> */
    uint8_t gm_l22; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 3134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t4) + 32), ((uint8_t)1));
    }
    {
        void * t5 = gm_l0;
        gm_tqs5p4n2tq19v t6 = gm_f_peek_bkdvl65(t5);
        gm_tqs5p4n2tq19v t7;
        GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)45ULL));
        ((uint8_t *)&t7)[1] = 1;
        gm_tqs5p4n2tq19v t8 = t6;
        gm_tqs5p4n2tq19v t9 = t7;
        uint8_t t10 = (gm_eqb44cb3cf1n(((uint8_t *)&t8), ((uint8_t *)&t9)));
        gm_l2 = t10;
    }
    {
        void * t12 = gm_l0;
        gm_tqs5p4n2tq19v t13 = gm_f_peek_bkdvl65(t12);
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), ((uint8_t)43ULL));
        ((uint8_t *)&t14)[1] = 1;
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), ((uint8_t)45ULL));
        ((uint8_t *)&t15)[1] = 1;
        gm_t5r6k0fdcfsb3 t16;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t16 + 0), t14);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t16 + 2), t15);
        gm_t5r6k0fdcfsb3 t17 = t16;
        uint8_t t18 = 0;
        gm_tqs5p4n2tq19v t19 = t13;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t17) + 0, ((uint8_t *)&t19))) t18 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t17) + 2, ((uint8_t *)&t19))) t18 = 1;
        uint8_t t20 = (uint8_t)(t18);
        if (!t20) goto ie11;
        {
            void * t22 = gm_l0;
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 16));
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            if (o26) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 289, 33);
            uint64_t t27 = r25;
            GM_ST(uint64_t, (((uint8_t *)t22) + 16), t27);
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        uint8_t t29 = 0;
        void * t30 = gm_l0;
        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 0) + 8);
        void * t32 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 0)));
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 16));
        uint64_t t35 = (uintptr_t)t34;
        if (t35 > (uintptr_t)t31 || (uintptr_t)t31 > (uintptr_t)t31) gm_trap_slice(t35, (uintptr_t)t31, t31, gm_src3nt8pf0qk5gn, 290, 8);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t32 + t35 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)((uintptr_t)t31 - t35));
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sns75reqq32d2));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
        uint8_t t38 = gm_f_starts_with_6vvt01e(t36, t37);
        if (!t38) {
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uintptr_t, (((uint8_t *)t39) + 0) + 8);
            void * t41 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t39) + 0)));
            void * t42 = gm_l0;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 16));
            uint64_t t44 = (uintptr_t)t43;
            if (t44 > (uintptr_t)t40 || (uintptr_t)t40 > (uintptr_t)t40) gm_trap_slice(t44, (uintptr_t)t40, t40, gm_src3nt8pf0qk5gn, 290, 46);
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t41 + t44 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)((uintptr_t)t40 - t44));
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sfa86erpod86s));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(3));
            uint8_t t47 = gm_f_starts_with_6vvt01e(t45, t46);
            t29 = t47;
        } else t29 = 1;
        if (!t29) goto ie28;
        {
            void * t49 = gm_l0;
            void * t50 = gm_l0;
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)3ULL), &r52);
            if (o53) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 291, 9);
            uint64_t t54 = r52;
            GM_ST(uint64_t, (((uint8_t *)t49) + 16), t54);
        }
        {
            void * t55 = gm_l0;
            uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)t55) + 0) + 8);
            void * t57 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t55) + 0)));
            uint64_t t58 = gm_l1;
            uint64_t t59 = (uintptr_t)t58;
            void * t60 = gm_l0;
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 16));
            uint64_t t62 = (uintptr_t)t61;
            if (t59 > t62 || t62 > (uintptr_t)t56) gm_trap_slice(t59, t62, t56, gm_src3nt8pf0qk5gn, 292, 28);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t57 + t59 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62 - t59));
            gm_tktr4rmbindgv t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t64 + 8), t63);
            gm_tefknldmnh2si t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
            ((uint8_t *)&t65)[0] = 0;
            GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t65 + 8), t64);
            gm_tefknldmnh2si t66 = t65;
            uint8_t t67 = ((((uint8_t *)&t66))[0] != 0);
            F.ret = t65;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t68 = gm_l0;
        gm_tqs5p4n2tq19v t69 = gm_f_peek_bkdvl65(t68);
        gm_tqs5p4n2tq19v t70 = t69;
        uint8_t t71;
        if (((((uint8_t *)&t70))[1] != 0)) {
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
            t71 = t72;
        } else {
            void * t73 = gm_l0;
            gm_b16_8 t74 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t75;
            gm_f_error_96ci5lu((void *)&t75, t73, t74);
            gm_tefknldmnh2si t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 48, 0);
            ((uint8_t *)&t76)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t76 + 8), t75);
            gm_tefknldmnh2si t77 = t76;
            uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
            F.ret = t76;
            goto gm_fret;
        }
        gm_l3 = t71;
    }
    {
        {
            uint8_t t81 = gm_l3;
            gm_l4 = t81;
        }
        uint8_t t82 = 0;
        uint8_t t83 = gm_l4;
        uint8_t t84 = (((uint8_t)48ULL) <= t83);
        if (t84) {
            uint8_t t85 = gm_l4;
            uint8_t t86 = (t85 <= ((uint8_t)57ULL));
            t82 = t86;
        } else t82 = 0;
        uint8_t t87 = (uint8_t)!t82;
        uint8_t t88 = t87;
        if (!t88) goto ie79;
        {
            void * t90 = gm_l0;
            gm_b16_8 t91 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t92;
            gm_f_error_96ci5lu((void *)&t92, t90, t91);
            gm_tefknldmnh2si t93;
            GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t93 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t93 + 48, 0);
            ((uint8_t *)&t93)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t93 + 8), t92);
            gm_tefknldmnh2si t94 = t93;
            uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
            F.ret = t93;
            goto gm_fret;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        gm_l5 = ((int64_t)10LL);
    }
    {
        uint8_t t97 = gm_l3;
        uint8_t t98 = ((t97 == ((uint8_t)48ULL)));
        if (!t98) goto ie96;
        {
            void * t101 = gm_l0;
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)t101) + 0));
            gm_b16_8 t103 = t102;
            void * t104 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t103)));
            uint64_t t105 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t103) + 8);
            void * t106 = gm_l0;
            uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)t106) + 16));
            uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
            if (o109) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 298, 28);
            uint64_t t110 = r108;
            uint64_t t111 = (uintptr_t)t110;
            gm_tqs5p4n2tq19v t112;
            if (t111 < t105) {
            uint8_t t113 = GM_LD(uint8_t, ((uint8_t *)t104 + t111 * 1));
            gm_tqs5p4n2tq19v t114;
            GM_ST(uint8_t, ((uint8_t *)&t114), t113);
            ((uint8_t *)&t114)[1] = 1;
                t112 = t114;
            } else {
            gm_tqs5p4n2tq19v t115;
            memset(&t115, 0, sizeof t115);
                t112 = t115;
            }
            gm_tqs5p4n2tq19v t116 = t112;
            if (!((((uint8_t *)&t116))[1] != 0)) goto ie100;
            uint8_t t117 = GM_LD(uint8_t, ((uint8_t *)&t116));
            gm_l6 = t117;
            {
                uint8_t t120 = gm_l6;
                uint8_t t121 = ((t120 == ((uint8_t)120ULL)));
                if (!t121) goto ie119;
                {
                    gm_l5 = ((int64_t)16LL);
                }
                goto ix119;
ie119:;
                uint8_t t124 = gm_l6;
                uint8_t t125 = ((t124 == ((uint8_t)111ULL)));
                if (!t125) goto ie123;
                {
                    gm_l5 = ((int64_t)8LL);
                }
                goto ix123;
ie123:;
                uint8_t t128 = gm_l6;
                uint8_t t129 = ((t128 == ((uint8_t)98ULL)));
                if (!t129) goto ie127;
                {
                    gm_l5 = ((int64_t)2LL);
                }
                goto ix127;
ie127:;
ix127:;
ix123:;
ix119:;
            }
            goto ix100;
ie100:;
ix100:;
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        int64_t t132 = gm_l5;
        uint8_t t133 = (!(t132 == ((int64_t)10LL)));
        if (!t133) goto ie131;
        {
            void * t135 = gm_l0;
            void * t136 = gm_l0;
            uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)t136) + 16));
            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)2ULL), &r138);
            if (o139) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 302, 26);
            uint64_t t140 = r138;
            GM_ST(uint64_t, (((uint8_t *)t135) + 16), t140);
        }
        goto ix131;
ie131:;
ix131:;
    }
    {
        gm_l7 = ((int64_t)0LL);
    }
    {
        gm_l8 = ((int32_t)0LL);
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        void * t141 = gm_l0;
        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t141) + 16));
        gm_l10 = t142;
    }
    {
        for (;;) {
            void * t144 = gm_l0;
            gm_tqs5p4n2tq19v t145 = gm_f_peek_bkdvl65(t144);
            gm_tqs5p4n2tq19v t146 = t145;
            if (!((((uint8_t *)&t146))[1] != 0)) goto lb143;
            uint8_t t147 = GM_LD(uint8_t, ((uint8_t *)&t146));
            gm_l11 = t147;
            {
                uint8_t t151 = gm_l11;
                uint8_t t152 = ((t151 == ((uint8_t)95ULL)));
                if (!t152) goto ie150;
                {
                    uint8_t t155 = 0;
                    int32_t t156 = gm_l8;
                    uint8_t t157 = ((t156 == ((int32_t)0LL)));
                    if (!t157) {
                        uint8_t t158 = gm_l9;
                        t155 = t158;
                    } else t155 = 1;
                    if (!t155) goto ie154;
                    {
                        void * t160 = gm_l0;
                        gm_b16_8 t161;
                        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_s2a4d8jj74v6o));
                        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(48));
                        gm_tob1snuii5bme t162;
                        gm_f_error_96ci5lu((void *)&t162, t160, t161);
                        gm_tefknldmnh2si t163;
                        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t163 + 48, 0);
                        ((uint8_t *)&t163)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t163 + 8), t162);
                        gm_tefknldmnh2si t164 = t163;
                        uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
                        F.ret = t163;
                        goto gm_fret;
                    }
                    goto ix154;
ie154:;
ix154:;
                }
                {
                    gm_l9 = ((uint8_t)1);
                }
                {
                    void * t166 = gm_l0;
                    void * t167 = gm_l0;
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)t167) + 16));
                    uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                    if (o170) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 311, 13);
                    uint64_t t171 = r169;
                    GM_ST(uint64_t, (((uint8_t *)t166) + 16), t171);
                }
                {
                    goto lc143;
                }
                goto ix150;
ie150:;
ix150:;
            }
            {
                int64_t t172;
                {
                    uint8_t t175 = gm_l11;
                    gm_l12 = t175;
                }
                uint8_t t176 = 0;
                uint8_t t177 = gm_l12;
                uint8_t t178 = (((uint8_t)48ULL) <= t177);
                if (t178) {
                    uint8_t t179 = gm_l12;
                    uint8_t t180 = (t179 <= ((uint8_t)57ULL));
                    t176 = t180;
                } else t176 = 0;
                uint8_t t181 = t176;
                if (!t181) goto ie173;
                uint8_t t183 = gm_l11;
                uint8_t r184; bool o185 = __builtin_sub_overflow(t183, ((uint8_t)48ULL), &r184);
                if (o185) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 315, 17);
                uint8_t t186 = r184;
                int64_t t187 = (int64_t)t186;
                int64_t t188 = t187;
                t172 = t188;
                goto ix173;
ie173:;
                int64_t t189;
                int64_t t191 = gm_l5;
                uint8_t t192 = ((t191 == ((int64_t)16LL)));
                if (!t192) goto ie190;
                {
                    uint8_t t194 = gm_l11;
                    gm_l13 = t194;
                }
                uint8_t t195 = 0;
                uint8_t t196 = gm_l13;
                uint8_t t197 = (((uint8_t)97ULL) <= t196);
                if (t197) {
                    uint8_t t198 = gm_l13;
                    uint8_t t199 = (t198 <= ((uint8_t)102ULL));
                    t195 = t199;
                } else t195 = 0;
                uint8_t t200 = t195;
                if (!t200) goto ie190;
                uint8_t t202 = gm_l11;
                uint8_t r203; bool o204 = __builtin_sub_overflow(t202, ((uint8_t)97ULL), &r203);
                if (o204) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 317, 17);
                uint8_t t205 = r203;
                int64_t t206 = (int64_t)t205;
                int64_t r207; bool o208 = __builtin_add_overflow(t206, ((int64_t)10LL), &r207);
                if (o208) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 317, 13);
                int64_t t209 = r207;
                int64_t t210 = t209;
                t189 = t210;
                goto ix190;
ie190:;
                int64_t t211;
                int64_t t213 = gm_l5;
                uint8_t t214 = ((t213 == ((int64_t)16LL)));
                if (!t214) goto ie212;
                {
                    uint8_t t216 = gm_l11;
                    gm_l14 = t216;
                }
                uint8_t t217 = 0;
                uint8_t t218 = gm_l14;
                uint8_t t219 = (((uint8_t)65ULL) <= t218);
                if (t219) {
                    uint8_t t220 = gm_l14;
                    uint8_t t221 = (t220 <= ((uint8_t)70ULL));
                    t217 = t221;
                } else t217 = 0;
                uint8_t t222 = t217;
                if (!t222) goto ie212;
                uint8_t t224 = gm_l11;
                uint8_t r225; bool o226 = __builtin_sub_overflow(t224, ((uint8_t)65ULL), &r225);
                if (o226) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 319, 17);
                uint8_t t227 = r225;
                int64_t t228 = (int64_t)t227;
                int64_t r229; bool o230 = __builtin_add_overflow(t228, ((int64_t)10LL), &r229);
                if (o230) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 319, 13);
                int64_t t231 = r229;
                int64_t t232 = t231;
                t211 = t232;
                goto ix212;
ie212:;
                int64_t t234 = ((int64_t)99LL);
                t211 = t234;
ix212:;
                t189 = t211;
ix190:;
                t172 = t189;
ix173:;
                gm_l15 = t172;
            }
            {
                int64_t t236 = gm_l15;
                int64_t t237 = gm_l5;
                uint8_t t238 = (t236 >= t237);
                if (!t238) goto ie235;
                {
                    goto lb143;
                }
                goto ix235;
ie235:;
ix235:;
            }
            {
                int32_t t241 = gm_l8;
                uint8_t t242 = ((t241 == ((int32_t)1LL)));
                if (!t242) goto ie240;
                uint64_t t243 = gm_l10;
                uint64_t t244 = (uintptr_t)t243;
                void * t245 = gm_l0;
                uint64_t t246 = GM_LD(uintptr_t, (((uint8_t *)t245) + 0) + 8);
                if (t244 >= (uintptr_t)(t246)) gm_trap_index(t244, t246, gm_src3nt8pf0qk5gn, 324, 28);
                uint8_t t247 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t245) + 0)) + t244 * 1));
                uint8_t t248 = ((t247 == ((uint8_t)48ULL)));
                if (!t248) goto ie240;
                int64_t t249 = gm_l5;
                uint8_t t250 = ((t249 == ((int64_t)10LL)));
                if (!t250) goto ie240;
                {
                    void * t252 = gm_l0;
                    gm_b16_8 t253;
                    GM_ST(void *, (uint8_t *)&t253, (void *)(gm_s8lbhmq10rt7i));
                    GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(41));
                    gm_tob1snuii5bme t254;
                    gm_f_error_96ci5lu((void *)&t254, t252, t253);
                    gm_tefknldmnh2si t255;
                    GM_ST(uint8_t, (uint8_t *)&t255 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t255 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t255 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t255 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t255 + 48, 0);
                    ((uint8_t *)&t255)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t255 + 8), t254);
                    gm_tefknldmnh2si t256 = t255;
                    uint8_t t257 = ((((uint8_t *)&t256))[0] != 0);
                    F.ret = t255;
                    goto gm_fret;
                }
                goto ix240;
ie240:;
ix240:;
            }
            {
                {
                    int64_t t259 = gm_l7;
                    int64_t t260 = gm_l5;
                    int64_t r261; bool o262 = __builtin_mul_overflow(t259, t260, &r261);
                    gm_t913bb735pvt5 t263;
                    if (o262) {
                    gm_t913bb735pvt5 t264;
                    memset(&t264, 0, sizeof t264);
                        t263 = t264;
                    } else {
                    int64_t t265 = r261;
                    gm_t913bb735pvt5 t266;
                    GM_ST(uint8_t, (uint8_t *)&t266 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t266 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t266 + 12, 0);
                    GM_ST(int64_t, ((uint8_t *)&t266), t265);
                    ((uint8_t *)&t266)[8] = 1;
                        t263 = t266;
                    }
                    gm_l16 = t263;
                }
                {
                    int64_t t267 = gm_l15;
                    gm_l18 = t267;
                }
                gm_t913bb735pvt5 t268;
                if (!((((uint8_t *)&gm_l16))[8] != 0)) goto ie269;
                int64_t t270 = GM_LD(int64_t, ((uint8_t *)&gm_l16));
                gm_l17 = t270;
                int64_t t271 = gm_l17;
                int64_t t272 = gm_l18;
                int64_t r273; bool o274 = __builtin_add_overflow(t271, t272, &r273);
                gm_t913bb735pvt5 t275;
                if (o274) {
                gm_t913bb735pvt5 t276;
                memset(&t276, 0, sizeof t276);
                    t275 = t276;
                } else {
                int64_t t277 = r273;
                gm_t913bb735pvt5 t278;
                GM_ST(uint8_t, (uint8_t *)&t278 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t278 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t278 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t278), t277);
                ((uint8_t *)&t278)[8] = 1;
                    t275 = t278;
                }
                t268 = t275;
                goto ix269;
ie269:;
                gm_t913bb735pvt5 t279;
                memset(&t279, 0, sizeof t279);
                t268 = t279;
ix269:;
                gm_t913bb735pvt5 t280 = t268;
                gm_t913bb735pvt5 t281 = t280;
                int64_t t282;
                if (((((uint8_t *)&t281))[8] != 0)) {
                    int64_t t283 = GM_LD(int64_t, (((uint8_t *)&t281) + 0));
                    t282 = t283;
                } else {
                    void * t284 = gm_l0;
                    gm_b16_8 t285;
                    GM_ST(void *, (uint8_t *)&t285, (void *)(gm_s70iivik802q4));
                    GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(25));
                    gm_tob1snuii5bme t286;
                    gm_f_error_96ci5lu((void *)&t286, t284, t285);
                    gm_tefknldmnh2si t287;
                    GM_ST(uint8_t, (uint8_t *)&t287 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t287 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t287 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t287 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t287 + 48, 0);
                    ((uint8_t *)&t287)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t287 + 8), t286);
                    gm_tefknldmnh2si t288 = t287;
                    uint8_t t289 = ((((uint8_t *)&t288))[0] != 0);
                    F.ret = t287;
                    goto gm_fret;
                }
                gm_l7 = t282;
            }
            {
                int32_t t290 = gm_l8;
                int32_t r291; bool o292 = __builtin_add_overflow(t290, ((int32_t)1LL), &r291);
                if (o292) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 326, 9);
                int32_t t293 = r291;
                gm_l8 = t293;
            }
            {
                gm_l9 = ((uint8_t)0);
            }
            {
                void * t294 = gm_l0;
                void * t295 = gm_l0;
                uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)t295) + 16));
                uint64_t r297; bool o298 = __builtin_add_overflow(t296, ((uint64_t)1ULL), &r297);
                if (o298) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 328, 9);
                uint64_t t299 = r297;
                GM_ST(uint64_t, (((uint8_t *)t294) + 16), t299);
            }
lc143:;
        }
lb143:;
    }
    {
        uint8_t t301 = gm_l9;
        if (!t301) goto ie300;
        {
            void * t303 = gm_l0;
            gm_b16_8 t304;
            GM_ST(void *, (uint8_t *)&t304, (void *)(gm_s2a4d8jj74v6o));
            GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(48));
            gm_tob1snuii5bme t305;
            gm_f_error_96ci5lu((void *)&t305, t303, t304);
            gm_tefknldmnh2si t306;
            GM_ST(uint8_t, (uint8_t *)&t306 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t306 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t306 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t306 + 48, 0);
            ((uint8_t *)&t306)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t306 + 8), t305);
            gm_tefknldmnh2si t307 = t306;
            uint8_t t308 = ((((uint8_t *)&t307))[0] != 0);
            F.ret = t306;
            goto gm_fret;
        }
        goto ix300;
ie300:;
ix300:;
    }
    {
        int32_t t310 = gm_l8;
        uint8_t t311 = ((t310 == ((int32_t)0LL)));
        if (!t311) goto ie309;
        {
            void * t313 = gm_l0;
            gm_b16_8 t314;
            GM_ST(void *, (uint8_t *)&t314, (void *)(gm_s70iivik802q4));
            GM_ST(uintptr_t, (uint8_t *)&t314 + 8, (uintptr_t)(25));
            gm_tob1snuii5bme t315;
            gm_f_error_96ci5lu((void *)&t315, t313, t314);
            gm_tefknldmnh2si t316;
            GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t316 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t316 + 48, 0);
            ((uint8_t *)&t316)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t316 + 8), t315);
            gm_tefknldmnh2si t317 = t316;
            uint8_t t318 = ((((uint8_t *)&t317))[0] != 0);
            F.ret = t316;
            goto gm_fret;
        }
        goto ix309;
ie309:;
ix309:;
    }
    {
        int64_t t320 = gm_l5;
        uint8_t t321 = ((t320 == ((int64_t)10LL)));
        if (!t321) goto ie319;
        void * t322 = gm_l0;
        gm_tqs5p4n2tq19v t323 = gm_f_peek_bkdvl65(t322);
        gm_tqs5p4n2tq19v t324;
        GM_ST(uint8_t, ((uint8_t *)&t324), ((uint8_t)46ULL));
        ((uint8_t *)&t324)[1] = 1;
        gm_tqs5p4n2tq19v t325;
        GM_ST(uint8_t, ((uint8_t *)&t325), ((uint8_t)101ULL));
        ((uint8_t *)&t325)[1] = 1;
        gm_tqs5p4n2tq19v t326;
        GM_ST(uint8_t, ((uint8_t *)&t326), ((uint8_t)69ULL));
        ((uint8_t *)&t326)[1] = 1;
        gm_tqs5p4n2tq19v t327;
        GM_ST(uint8_t, ((uint8_t *)&t327), ((uint8_t)45ULL));
        ((uint8_t *)&t327)[1] = 1;
        gm_tqs5p4n2tq19v t328;
        GM_ST(uint8_t, ((uint8_t *)&t328), ((uint8_t)58ULL));
        ((uint8_t *)&t328)[1] = 1;
        gm_tcdtdmenq84v9 t329;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t329 + 0), t324);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t329 + 2), t325);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t329 + 4), t326);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t329 + 6), t327);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t329 + 8), t328);
        gm_tcdtdmenq84v9 t330 = t329;
        uint8_t t331 = 0;
        gm_tqs5p4n2tq19v t332 = t323;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t330) + 0, ((uint8_t *)&t332))) t331 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t330) + 2, ((uint8_t *)&t332))) t331 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t330) + 4, ((uint8_t *)&t332))) t331 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t330) + 6, ((uint8_t *)&t332))) t331 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t330) + 8, ((uint8_t *)&t332))) t331 = 1;
        uint8_t t333 = (uint8_t)(t331);
        if (!t333) goto ie319;
        {
            for (;;) {
                void * t336 = gm_l0;
                gm_tqs5p4n2tq19v t337 = gm_f_peek_bkdvl65(t336);
                gm_tqs5p4n2tq19v t338 = t337;
                if (!((((uint8_t *)&t338))[1] != 0)) goto lb335;
                uint8_t t339 = GM_LD(uint8_t, ((uint8_t *)&t338));
                gm_l19 = t339;
                {
                    uint8_t t343 = 0;
                    uint8_t t344 = 0;
                    uint8_t t345 = 0;
                    {
                        uint8_t t347 = gm_l19;
                        gm_l20 = t347;
                    }
                    uint8_t t348 = 0;
                    uint8_t t349 = gm_l20;
                    uint8_t t350 = (((uint8_t)97ULL) <= t349);
                    if (t350) {
                        uint8_t t351 = gm_l20;
                        uint8_t t352 = (t351 <= ((uint8_t)122ULL));
                        t348 = t352;
                    } else t348 = 0;
                    uint8_t t353 = t348;
                    if (!t353) {
                        {
                            uint8_t t355 = gm_l19;
                            gm_l21 = t355;
                        }
                        uint8_t t356 = 0;
                        uint8_t t357 = gm_l21;
                        uint8_t t358 = (((uint8_t)65ULL) <= t357);
                        if (t358) {
                            uint8_t t359 = gm_l21;
                            uint8_t t360 = (t359 <= ((uint8_t)90ULL));
                            t356 = t360;
                        } else t356 = 0;
                        uint8_t t361 = t356;
                        t345 = t361;
                    } else t345 = 1;
                    if (!t345) {
                        {
                            uint8_t t363 = gm_l19;
                            gm_l22 = t363;
                        }
                        uint8_t t364 = 0;
                        uint8_t t365 = gm_l22;
                        uint8_t t366 = (((uint8_t)48ULL) <= t365);
                        if (t366) {
                            uint8_t t367 = gm_l22;
                            uint8_t t368 = (t367 <= ((uint8_t)57ULL));
                            t364 = t368;
                        } else t364 = 0;
                        uint8_t t369 = t364;
                        t344 = t369;
                    } else t344 = 1;
                    if (!t344) {
                        uint8_t t370 = gm_l19;
                        gm_tb1msgo1fu1n3 t371;
                        GM_ST(uint8_t, ((uint8_t *)&t371 + 0), ((uint8_t)46ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t371 + 1), ((uint8_t)58ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t371 + 2), ((uint8_t)45ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t371 + 3), ((uint8_t)43ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t371 + 4), ((uint8_t)95ULL));
                        gm_tb1msgo1fu1n3 t372 = t371;
                        uint8_t t373 = 0;
                        uint8_t t374 = t370;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t372) + 0, ((uint8_t *)&t374))) t373 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t372) + 1, ((uint8_t *)&t374))) t373 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t372) + 2, ((uint8_t *)&t374))) t373 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t372) + 3, ((uint8_t *)&t374))) t373 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t372) + 4, ((uint8_t *)&t374))) t373 = 1;
                        uint8_t t375 = (uint8_t)(t373);
                        t343 = t375;
                    } else t343 = 1;
                    uint8_t t376 = (uint8_t)!t343;
                    if (!t376) goto ie342;
                    {
                        goto lb335;
                    }
                    goto ix342;
ie342:;
ix342:;
                }
                {
                    void * t378 = gm_l0;
                    void * t379 = gm_l0;
                    uint64_t t380 = GM_LD(uint64_t, (((uint8_t *)t379) + 16));
                    uint64_t r381; bool o382 = __builtin_add_overflow(t380, ((uint64_t)1ULL), &r381);
                    if (o382) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 336, 13);
                    uint64_t t383 = r381;
                    GM_ST(uint64_t, (((uint8_t *)t378) + 16), t383);
                }
lc335:;
            }
lb335:;
        }
        {
            void * t384 = gm_l0;
            uint64_t t385 = GM_LD(uintptr_t, (((uint8_t *)t384) + 0) + 8);
            void * t386 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t384) + 0)));
            uint64_t t387 = gm_l1;
            uint64_t t388 = (uintptr_t)t387;
            void * t389 = gm_l0;
            uint64_t t390 = GM_LD(uint64_t, (((uint8_t *)t389) + 16));
            uint64_t t391 = (uintptr_t)t390;
            if (t388 > t391 || t391 > (uintptr_t)t385) gm_trap_slice(t388, t391, t385, gm_src3nt8pf0qk5gn, 338, 28);
            gm_b16_8 t392;
            GM_ST(void *, (uint8_t *)&t392, (void *)(((uint8_t *)t386 + t388 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(t391 - t388));
            gm_tktr4rmbindgv t393;
            GM_ST(uint8_t, (uint8_t *)&t393 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t393 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t393 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t393 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t393 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t393 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t393 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t393 + 8), t392);
            gm_tefknldmnh2si t394;
            GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
            ((uint8_t *)&t394)[0] = 0;
            GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t394 + 8), t393);
            gm_tefknldmnh2si t395 = t394;
            uint8_t t396 = ((((uint8_t *)&t395))[0] != 0);
            F.ret = t394;
            goto gm_fret;
        }
        goto ix319;
ie319:;
ix319:;
    }
    {
        int64_t t397;
        uint8_t t399 = gm_l2;
        if (!t399) goto ie398;
        int64_t t401 = gm_l7;
        if (t401 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 340, 36);
        int64_t t402 = (int64_t)((uint64_t)0 - (uint64_t)t401);
        int64_t t403 = t402;
        t397 = t403;
        goto ix398;
ie398:;
        int64_t t405 = gm_l7;
        int64_t t406 = t405;
        t397 = t406;
ix398:;
        gm_tktr4rmbindgv t407;
        memset(&t407, 0, sizeof t407);
        GM_ST(uint8_t, ((uint8_t *)&t407 + 0), ((uint8_t)1ULL));
        GM_ST(int64_t, ((uint8_t *)&t407 + 8), t397);
        gm_tefknldmnh2si t408;
        GM_ST(uint8_t, (uint8_t *)&t408 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t408 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t408 + 4, 0);
        ((uint8_t *)&t408)[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t408 + 8), t407);
        gm_tefknldmnh2si t409 = t408;
        uint8_t t410 = ((((uint8_t *)&t409))[0] != 0);
        F.ret = t408;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tefknldmnh2si gm_f_number_4l95pjb_c(void * a0) { gm_tefknldmnh2si gm_r; gm_f_number_4l95pjb(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_string_80niubc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_string_80niubc F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* quote */
    uint8_t gm_l2; /* triple */
    gm_b16_8 gm_l3; /* kind */
    gm_b16_8 gm_l4; /* unclosed */
    uint8_t gm_l6; /* c */
    int32_t gm_l7; /* extra */
    uint8_t gm_l8; /* e */
    uint8_t gm_l9; /* broke */
    uint8_t gm_l10; /* k */
    GM_STACK_CHECK(&F, 3343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src3nt8pf0qk5gn, 345, 14);
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t5) + 0)) + t4 * 1));
        gm_l1 = t7;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 0) + 8);
        void * t10 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 0)));
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 16));
        uint64_t t13 = (uintptr_t)t12;
        if (t13 > (uintptr_t)t9 || (uintptr_t)t9 > (uintptr_t)t9) gm_trap_slice(t13, (uintptr_t)t9, t9, gm_src3nt8pf0qk5gn, 346, 15);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t10 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)t9 - t13));
        gm_b16_8 t15;
        uint8_t t17 = gm_l1;
        uint8_t t18 = ((t17 == ((uint8_t)34ULL)));
        if (!t18) goto ie16;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s8rm42n03sfss));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
        gm_b16_8 t21 = t20;
        t15 = t21;
        goto ix16;
ie16:;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_soakgb2h3g40i));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
        gm_b16_8 t24 = t23;
        t15 = t24;
ix16:;
        uint8_t t25 = gm_f_starts_with_6vvt01e(t14, t15);
        gm_l2 = t25;
    }
    {
        void * t26 = gm_l0;
        uint8_t *t27 = (((uint8_t *)t26) + 16);
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
        uint64_t t30;
        uint8_t t32 = gm_l2;
        if (!t32) goto ie31;
        uint64_t t34 = ((uint64_t)3ULL);
        t30 = t34;
        goto ix31;
ie31:;
        uint64_t t36 = ((uint64_t)1ULL);
        t30 = t36;
ix31:;
        uint64_t r37; bool o38 = __builtin_add_overflow(t29, t30, &r37);
        if (o38) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 347, 5);
        uint64_t t39 = r37;
        GM_ST(uint64_t, t27, t39);
    }
    {
        gm_b16_8 t40;
        uint8_t t42 = gm_l1;
        uint8_t t43 = ((t42 == ((uint8_t)34ULL)));
        if (!t43) goto ie41;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_scvuqmspcpb1m));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(5));
        gm_b16_8 t46 = t45;
        t40 = t46;
        goto ix41;
ie41:;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s4lo69slu4igt));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(7));
        gm_b16_8 t49 = t48;
        t40 = t49;
ix41:;
        gm_l3 = t40;
    }
    {
        gm_b16_8 t50;
        uint8_t t52 = gm_l2;
        if (!t52) goto ie51;
        gm_t6j6smj7qoctu t54;
        gm_b16_8 t55 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)t56) + 0) + 8);
        void * t58 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t56) + 0)));
        void * t59 = gm_l0;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 16));
        uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
        if (o62) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 350, 80);
        uint64_t t63 = r61;
        uint64_t t64 = (uintptr_t)t63;
        void * t65 = gm_l0;
        uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 16));
        uint64_t t67 = (uintptr_t)t66;
        if (t64 > t67 || t67 > (uintptr_t)t57) gm_trap_slice(t64, t67, t57, gm_src3nt8pf0qk5gn, 350, 73);
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t58 + t64 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t64));
        GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t68);
        gm_tabb710d9mq2e t69;
        gm_f_format_u10ruvh((void *)&t69, (void *)NULL, cx, NULL, &t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t70 = t69;
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_src3nt8pf0qk5gn, 350, 9);
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
        gm_b16_8 t72 = t71;
        t50 = t72;
        goto ix51;
ie51:;
        gm_t6j6smj7qoctu t74;
        gm_b16_8 t75 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t75);
        void * t76 = gm_l0;
        uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)t76) + 0) + 8);
        void * t78 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t76) + 0)));
        void * t79 = gm_l0;
        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 16));
        uint64_t r81; bool o82 = __builtin_sub_overflow(t80, ((uint64_t)1ULL), &r81);
        if (o82) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 352, 69);
        uint64_t t83 = r81;
        uint64_t t84 = (uintptr_t)t83;
        void * t85 = gm_l0;
        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 16));
        uint64_t t87 = (uintptr_t)t86;
        if (t84 > t87 || t87 > (uintptr_t)t77) gm_trap_slice(t84, t87, t77, gm_src3nt8pf0qk5gn, 352, 62);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t78 + t84 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87 - t84));
        GM_ST(gm_b16_8, (uint8_t *)&t74 + 16, t88);
        gm_tabb710d9mq2e t89;
        gm_f_format_0pvgsic((void *)&t89, (void *)NULL, cx, NULL, &t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t90 = t89;
        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_src3nt8pf0qk5gn, 352, 9);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
        gm_b16_8 t92 = t91;
        t50 = t92;
ix51:;
        gm_l4 = t50;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t94 = gm_l2;
        if (!t94) goto ie93;
        {
            void * t97 = gm_l0;
            gm_tqs5p4n2tq19v t98 = gm_f_peek_bkdvl65(t97);
            gm_tqs5p4n2tq19v t99;
            GM_ST(uint8_t, ((uint8_t *)&t99), ((uint8_t)13ULL));
            ((uint8_t *)&t99)[1] = 1;
            gm_tqs5p4n2tq19v t100 = t98;
            gm_tqs5p4n2tq19v t101 = t99;
            uint8_t t102 = (gm_eqb44cb3cf1n(((uint8_t *)&t100), ((uint8_t *)&t101)));
            if (!t102) goto ie96;
            void * t103 = gm_l0;
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)t103) + 0));
            gm_b16_8 t105 = t104;
            void * t106 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t105)));
            uint64_t t107 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t105) + 8);
            void * t108 = gm_l0;
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 16));
            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
            if (o111) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 357, 44);
            uint64_t t112 = r110;
            uint64_t t113 = (uintptr_t)t112;
            gm_tqs5p4n2tq19v t114;
            if (t113 < t107) {
            uint8_t t115 = GM_LD(uint8_t, ((uint8_t *)t106 + t113 * 1));
            gm_tqs5p4n2tq19v t116;
            GM_ST(uint8_t, ((uint8_t *)&t116), t115);
            ((uint8_t *)&t116)[1] = 1;
                t114 = t116;
            } else {
            gm_tqs5p4n2tq19v t117;
            memset(&t117, 0, sizeof t117);
                t114 = t117;
            }
            gm_tqs5p4n2tq19v t118;
            GM_ST(uint8_t, ((uint8_t *)&t118), ((uint8_t)10ULL));
            ((uint8_t *)&t118)[1] = 1;
            gm_tqs5p4n2tq19v t119 = t114;
            gm_tqs5p4n2tq19v t120 = t118;
            uint8_t t121 = (gm_eqb44cb3cf1n(((uint8_t *)&t119), ((uint8_t *)&t120)));
            if (!t121) goto ie96;
            {
                void * t123 = gm_l0;
                void * t124 = gm_l0;
                uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 16));
                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                if (o127) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 357, 65);
                uint64_t t128 = r126;
                GM_ST(uint64_t, (((uint8_t *)t123) + 16), t128);
            }
            goto ix96;
ie96:;
ix96:;
        }
        {
            void * t130 = gm_l0;
            gm_tqs5p4n2tq19v t131 = gm_f_peek_bkdvl65(t130);
            gm_tqs5p4n2tq19v t132;
            GM_ST(uint8_t, ((uint8_t *)&t132), ((uint8_t)10ULL));
            ((uint8_t *)&t132)[1] = 1;
            gm_tqs5p4n2tq19v t133 = t131;
            gm_tqs5p4n2tq19v t134 = t132;
            uint8_t t135 = (gm_eqb44cb3cf1n(((uint8_t *)&t133), ((uint8_t *)&t134)));
            if (!t135) goto ie129;
            {
                void * t137 = gm_l0;
                void * t138 = gm_l0;
                uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t138) + 16));
                uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                if (o141) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 359, 13);
                uint64_t t142 = r140;
                GM_ST(uint64_t, (((uint8_t *)t137) + 16), t142);
            }
            {
                void * t143 = gm_l0;
                void * t144 = gm_l0;
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)t144) + 24));
                uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                if (o147) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 360, 13);
                uint64_t t148 = r146;
                GM_ST(uint64_t, (((uint8_t *)t143) + 24), t148);
            }
            goto ix129;
ie129:;
ix129:;
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb149;
            {
                void * t152 = gm_l0;
                gm_tqs5p4n2tq19v t153 = gm_f_peek_bkdvl65(t152);
                gm_tqs5p4n2tq19v t154 = t153;
                uint8_t t155;
                if (((((uint8_t *)&t154))[1] != 0)) {
                    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t154) + 0));
                    t155 = t156;
                } else {
                    void * t157 = gm_l0;
                    gm_b16_8 t158 = gm_l4;
                    gm_b16_8 t159 = t158;
                    gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                    gm_tob1snuii5bme t161;
                    gm_f_error_96ci5lu((void *)&t161, t157, t160);
                    gm_tu615l4qlr8b2 t162;
                    GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                    ((uint8_t *)&t162)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t162 + 8), t161);
                    gm_tu615l4qlr8b2 t163 = t162;
                    uint8_t t164 = ((((uint8_t *)&t163))[0] != 0);
                    F.ret = t162;
                    goto gm_fret;
                }
                gm_l6 = t155;
            }
            {
                uint8_t t166 = gm_l6;
                uint8_t t167 = gm_l1;
                uint8_t t168 = ((t166 == t167));
                if (!t168) goto ie165;
                {
                    uint8_t t171 = gm_l2;
                    uint8_t t172 = (uint8_t)!t171;
                    if (!t172) goto ie170;
                    {
                        void * t174 = gm_l0;
                        void * t175 = gm_l0;
                        uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)t175) + 16));
                        uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                        if (o178) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 367, 17);
                        uint64_t t179 = r177;
                        GM_ST(uint64_t, (((uint8_t *)t174) + 16), t179);
                    }
                    {
                        goto lb149;
                    }
                    goto ix170;
ie170:;
ix170:;
                }
                {
                    void * t181 = gm_l0;
                    uint64_t t182 = GM_LD(uintptr_t, (((uint8_t *)t181) + 0) + 8);
                    void * t183 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t181) + 0)));
                    void * t184 = gm_l0;
                    uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)t184) + 16));
                    uint64_t t186 = (uintptr_t)t185;
                    if (t186 > (uintptr_t)t182 || (uintptr_t)t182 > (uintptr_t)t182) gm_trap_slice(t186, (uintptr_t)t182, t182, gm_src3nt8pf0qk5gn, 370, 16);
                    gm_b16_8 t187;
                    GM_ST(void *, (uint8_t *)&t187, (void *)(((uint8_t *)t183 + t186 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)((uintptr_t)t182 - t186));
                    gm_b16_8 t188;
                    uint8_t t190 = gm_l1;
                    uint8_t t191 = ((t190 == ((uint8_t)34ULL)));
                    if (!t191) goto ie189;
                    gm_b16_8 t193;
                    GM_ST(void *, (uint8_t *)&t193, (void *)(gm_s8rm42n03sfss));
                    GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(3));
                    gm_b16_8 t194 = t193;
                    t188 = t194;
                    goto ix189;
ie189:;
                    gm_b16_8 t196;
                    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_soakgb2h3g40i));
                    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(3));
                    gm_b16_8 t197 = t196;
                    t188 = t197;
ix189:;
                    uint8_t t198 = gm_f_starts_with_6vvt01e(t187, t188);
                    if (!t198) goto ie180;
                    {
                        void * t200 = gm_l0;
                        void * t201 = gm_l0;
                        uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)t201) + 16));
                        uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)3ULL), &r203);
                        if (o204) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 371, 17);
                        uint64_t t205 = r203;
                        GM_ST(uint64_t, (((uint8_t *)t200) + 16), t205);
                    }
                    {
                        gm_l7 = ((int32_t)0LL);
                    }
                    {
                        for (;;) {
                            int32_t t207 = gm_l7;
                            uint8_t t208 = (t207 < ((int32_t)2LL));
                            if (!t208) goto lb206;
                            void * t209 = gm_l0;
                            gm_tqs5p4n2tq19v t210 = gm_f_peek_bkdvl65(t209);
                            uint8_t t211 = gm_l1;
                            gm_tqs5p4n2tq19v t212;
                            GM_ST(uint8_t, ((uint8_t *)&t212), t211);
                            ((uint8_t *)&t212)[1] = 1;
                            gm_tqs5p4n2tq19v t213 = t210;
                            gm_tqs5p4n2tq19v t214 = t212;
                            uint8_t t215 = (gm_eqb44cb3cf1n(((uint8_t *)&t213), ((uint8_t *)&t214)));
                            if (!t215) goto lb206;
                            {
                                void * t218 = (void *)((uint8_t *)&F.l5);
                                uint8_t t219 = gm_l1;
                                gm_tftb1odc739t5 t220 = gm_f_push_t83rg2m(t218, t219);
                                gm_tftb1odc739t5 t221 = t220;
                                if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_src3nt8pf0qk5gn, 375, 21);
                                gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
                            }
                            {
                                void * t223 = gm_l0;
                                void * t224 = gm_l0;
                                uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)t224) + 16));
                                uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                                if (o227) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 376, 21);
                                uint64_t t228 = r226;
                                GM_ST(uint64_t, (((uint8_t *)t223) + 16), t228);
                            }
                            {
                                int32_t t229 = gm_l7;
                                int32_t r230; bool o231 = __builtin_add_overflow(t229, ((int32_t)1LL), &r230);
                                if (o231) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 377, 21);
                                int32_t t232 = r230;
                                gm_l7 = t232;
                            }
lc206:;
                        }
lb206:;
                    }
                    {
                        goto lb149;
                    }
                    goto ix180;
ie180:;
ix180:;
                }
                {
                    void * t233 = (void *)((uint8_t *)&F.l5);
                    uint8_t t234 = gm_l6;
                    gm_tftb1odc739t5 t235 = gm_f_push_t83rg2m(t233, t234);
                    gm_tftb1odc739t5 t236 = t235;
                    if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 1, gm_src3nt8pf0qk5gn, 381, 13);
                    gm_b0_1 t237 = GM_LD(gm_b0_1, (((uint8_t *)&t236) + 1));
                }
                {
                    void * t238 = gm_l0;
                    void * t239 = gm_l0;
                    uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)t239) + 16));
                    uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                    if (o242) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 382, 13);
                    uint64_t t243 = r241;
                    GM_ST(uint64_t, (((uint8_t *)t238) + 16), t243);
                }
                {
                    goto lc149;
                }
                goto ix165;
ie165:;
ix165:;
            }
            {
                uint8_t t245 = gm_l6;
                uint8_t t246 = ((t245 == ((uint8_t)10ULL)));
                if (!t246) goto ie244;
                {
                    uint8_t t249 = gm_l2;
                    uint8_t t250 = (uint8_t)!t249;
                    if (!t250) goto ie248;
                    {
                        void * t252 = gm_l0;
                        gm_b16_8 t253 = gm_l4;
                        gm_b16_8 t254 = t253;
                        gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                        gm_tob1snuii5bme t256;
                        gm_f_error_96ci5lu((void *)&t256, t252, t255);
                        gm_tu615l4qlr8b2 t257;
                        GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                        ((uint8_t *)&t257)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t257 + 8), t256);
                        gm_tu615l4qlr8b2 t258 = t257;
                        uint8_t t259 = ((((uint8_t *)&t258))[0] != 0);
                        F.ret = t257;
                        goto gm_fret;
                    }
                    goto ix248;
ie248:;
ix248:;
                }
                {
                    void * t260 = gm_l0;
                    void * t261 = gm_l0;
                    uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)t261) + 24));
                    uint64_t r263; bool o264 = __builtin_add_overflow(t262, ((uint64_t)1ULL), &r263);
                    if (o264) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 387, 13);
                    uint64_t t265 = r263;
                    GM_ST(uint64_t, (((uint8_t *)t260) + 24), t265);
                }
                goto ix244;
ie244:;
ix244:;
            }
            {
                void * t266 = gm_l0;
                void * t267 = gm_l0;
                uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)t267) + 16));
                uint64_t r269; bool o270 = __builtin_add_overflow(t268, ((uint64_t)1ULL), &r269);
                if (o270) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 389, 9);
                uint64_t t271 = r269;
                GM_ST(uint64_t, (((uint8_t *)t266) + 16), t271);
            }
            {
                uint8_t t273 = gm_l6;
                uint8_t t274 = ((t273 == ((uint8_t)92ULL)));
                if (!t274) goto ie272;
                uint8_t t275 = gm_l1;
                uint8_t t276 = ((t275 == ((uint8_t)34ULL)));
                if (!t276) goto ie272;
                {
                    void * t278 = gm_l0;
                    gm_tqs5p4n2tq19v t279 = gm_f_peek_bkdvl65(t278);
                    gm_tqs5p4n2tq19v t280 = t279;
                    uint8_t t281;
                    if (((((uint8_t *)&t280))[1] != 0)) {
                        uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&t280) + 0));
                        t281 = t282;
                    } else {
                        void * t283 = gm_l0;
                        gm_b16_8 t284 = gm_l4;
                        gm_b16_8 t285 = t284;
                        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                        gm_tob1snuii5bme t287;
                        gm_f_error_96ci5lu((void *)&t287, t283, t286);
                        gm_tu615l4qlr8b2 t288;
                        GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                        ((uint8_t *)&t288)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t288 + 8), t287);
                        gm_tu615l4qlr8b2 t289 = t288;
                        uint8_t t290 = ((((uint8_t *)&t289))[0] != 0);
                        F.ret = t288;
                        goto gm_fret;
                    }
                    gm_l8 = t281;
                }
                {
                    void * t291 = gm_l0;
                    void * t292 = gm_l0;
                    uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)t292) + 16));
                    uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                    if (o295) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 392, 13);
                    uint64_t t296 = r294;
                    GM_ST(uint64_t, (((uint8_t *)t291) + 16), t296);
                }
                {
                    uint8_t t297 = gm_l8;
                    uint8_t t298 = t297;
                    uint8_t t301 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t301 == ((uint8_t)98ULL))) goto mn300;
                    void * t302 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t303 = gm_f_push_t83rg2m(t302, ((uint8_t)8ULL));
                    gm_tftb1odc739t5 t304 = t303;
                    if (((((uint8_t *)&t304))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t304) + 1, gm_src3nt8pf0qk5gn, 394, 24);
                    gm_b0_1 t305 = GM_LD(gm_b0_1, (((uint8_t *)&t304) + 1));
                    goto me299;
mn300:;
                    uint8_t t307 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t307 == ((uint8_t)101ULL))) goto mn306;
                    void * t308 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t309 = gm_f_push_t83rg2m(t308, ((uint8_t)27ULL));
                    gm_tftb1odc739t5 t310 = t309;
                    if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_src3nt8pf0qk5gn, 395, 24);
                    gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
                    goto me299;
mn306:;
                    uint8_t t313 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t313 == ((uint8_t)102ULL))) goto mn312;
                    void * t314 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t315 = gm_f_push_t83rg2m(t314, ((uint8_t)12ULL));
                    gm_tftb1odc739t5 t316 = t315;
                    if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_src3nt8pf0qk5gn, 396, 24);
                    gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
                    goto me299;
mn312:;
                    uint8_t t319 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t319 == ((uint8_t)110ULL))) goto mn318;
                    void * t320 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t321 = gm_f_push_t83rg2m(t320, ((uint8_t)10ULL));
                    gm_tftb1odc739t5 t322 = t321;
                    if (((((uint8_t *)&t322))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t322) + 1, gm_src3nt8pf0qk5gn, 397, 24);
                    gm_b0_1 t323 = GM_LD(gm_b0_1, (((uint8_t *)&t322) + 1));
                    goto me299;
mn318:;
                    uint8_t t325 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t325 == ((uint8_t)114ULL))) goto mn324;
                    void * t326 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t327 = gm_f_push_t83rg2m(t326, ((uint8_t)13ULL));
                    gm_tftb1odc739t5 t328 = t327;
                    if (((((uint8_t *)&t328))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t328) + 1, gm_src3nt8pf0qk5gn, 398, 24);
                    gm_b0_1 t329 = GM_LD(gm_b0_1, (((uint8_t *)&t328) + 1));
                    goto me299;
mn324:;
                    uint8_t t331 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t331 == ((uint8_t)116ULL))) goto mn330;
                    void * t332 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t333 = gm_f_push_t83rg2m(t332, ((uint8_t)9ULL));
                    gm_tftb1odc739t5 t334 = t333;
                    if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 1, gm_src3nt8pf0qk5gn, 399, 24);
                    gm_b0_1 t335 = GM_LD(gm_b0_1, (((uint8_t *)&t334) + 1));
                    goto me299;
mn330:;
                    uint8_t t339 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t339 == ((uint8_t)34ULL))) goto pa338;
                    goto po337;
pa338:;
                    uint8_t t341 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t341 == ((uint8_t)92ULL))) goto pa340;
                    goto po337;
pa340:;
                    goto mn336;
po337:;
                    void * t342 = (void *)((uint8_t *)&F.l5);
                    uint8_t t343 = gm_l8;
                    gm_tftb1odc739t5 t344 = gm_f_push_t83rg2m(t342, t343);
                    gm_tftb1odc739t5 t345 = t344;
                    if (((((uint8_t *)&t345))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t345) + 1, gm_src3nt8pf0qk5gn, 400, 30);
                    gm_b0_1 t346 = GM_LD(gm_b0_1, (((uint8_t *)&t345) + 1));
                    goto me299;
mn336:;
                    uint8_t t348 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t348 == ((uint8_t)120ULL))) goto mn347;
                    void * t349 = gm_l0;
                    void * t350 = (void *)((uint8_t *)&F.l5);
                    gm_tr1k5nl69lhjo t351;
                    gm_f_hex_escape_eoc96dk((void *)&t351, (void *)NULL, cx, NULL, t349, t350, ((uint64_t)2ULL));
                    if (gm_unw.kind) {
                        goto gm_ll150;
                    }
                    gm_tr1k5nl69lhjo t352 = t351;
                    if (((((uint8_t *)&t352))[0] != 0)) {
                        gm_tob1snuii5bme t353 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t352) + 8));
                        gm_tu615l4qlr8b2 t354;
                        GM_ST(uint8_t, (uint8_t *)&t354 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t354 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t354 + 4, 0);
                        ((uint8_t *)&t354)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t354 + 8), t353);
                        F.ret = t354;
                        goto gm_fret;
                    }
                    gm_b0_1 t355 = GM_LD(gm_b0_1, (((uint8_t *)&t352) + 8));
                    goto me299;
mn347:;
                    uint8_t t357 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t357 == ((uint8_t)117ULL))) goto mn356;
                    void * t358 = gm_l0;
                    void * t359 = (void *)((uint8_t *)&F.l5);
                    gm_tr1k5nl69lhjo t360;
                    gm_f_hex_escape_eoc96dk((void *)&t360, (void *)NULL, cx, NULL, t358, t359, ((uint64_t)4ULL));
                    if (gm_unw.kind) {
                        goto gm_ll150;
                    }
                    gm_tr1k5nl69lhjo t361 = t360;
                    if (((((uint8_t *)&t361))[0] != 0)) {
                        gm_tob1snuii5bme t362 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t361) + 8));
                        gm_tu615l4qlr8b2 t363;
                        GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                        ((uint8_t *)&t363)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t363 + 8), t362);
                        F.ret = t363;
                        goto gm_fret;
                    }
                    gm_b0_1 t364 = GM_LD(gm_b0_1, (((uint8_t *)&t361) + 8));
                    goto me299;
mn356:;
                    uint8_t t366 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t366 == ((uint8_t)85ULL))) goto mn365;
                    void * t367 = gm_l0;
                    void * t368 = (void *)((uint8_t *)&F.l5);
                    gm_tr1k5nl69lhjo t369;
                    gm_f_hex_escape_eoc96dk((void *)&t369, (void *)NULL, cx, NULL, t367, t368, ((uint64_t)8ULL));
                    if (gm_unw.kind) {
                        goto gm_ll150;
                    }
                    gm_tr1k5nl69lhjo t370 = t369;
                    if (((((uint8_t *)&t370))[0] != 0)) {
                        gm_tob1snuii5bme t371 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t370) + 8));
                        gm_tu615l4qlr8b2 t372;
                        GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
                        ((uint8_t *)&t372)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t372 + 8), t371);
                        F.ret = t372;
                        goto gm_fret;
                    }
                    gm_b0_1 t373 = GM_LD(gm_b0_1, (((uint8_t *)&t370) + 8));
                    goto me299;
mn365:;
                    uint8_t t377 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t377 == ((uint8_t)32ULL))) goto pa376;
                    goto po375;
pa376:;
                    uint8_t t379 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t379 == ((uint8_t)9ULL))) goto pa378;
                    goto po375;
pa378:;
                    uint8_t t381 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t381 == ((uint8_t)13ULL))) goto pa380;
                    goto po375;
pa380:;
                    uint8_t t383 = GM_LD(uint8_t, ((uint8_t *)&t298));
                    if (!(t383 == ((uint8_t)10ULL))) goto pa382;
                    goto po375;
pa382:;
                    goto mn374;
po375:;
                    {
                        uint8_t t386 = gm_l2;
                        uint8_t t387 = (uint8_t)!t386;
                        if (!t387) goto ie385;
                        {
                            void * t389 = gm_l0;
                            gm_b16_8 t390 = GM_LD(gm_b16_8, gm_cn9bf8d1ktm);
                            gm_tob1snuii5bme t391;
                            gm_f_error_96ci5lu((void *)&t391, t389, t390);
                            gm_tu615l4qlr8b2 t392;
                            GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                            ((uint8_t *)&t392)[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t392 + 8), t391);
                            gm_tu615l4qlr8b2 t393 = t392;
                            uint8_t t394 = ((((uint8_t *)&t393))[0] != 0);
                            F.ret = t392;
                            goto gm_fret;
                        }
                        goto ix385;
ie385:;
ix385:;
                    }
                    {
                        void * t395 = gm_l0;
                        void * t396 = gm_l0;
                        uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)t396) + 16));
                        uint64_t r398; bool o399 = __builtin_sub_overflow(t397, ((uint64_t)1ULL), &r398);
                        if (o399) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 407, 21);
                        uint64_t t400 = r398;
                        GM_ST(uint64_t, (((uint8_t *)t395) + 16), t400);
                    }
                    {
                        gm_l9 = ((uint8_t)0);
                    }
                    {
                        for (;;) {
                            void * t402 = gm_l0;
                            gm_tqs5p4n2tq19v t403 = gm_f_peek_bkdvl65(t402);
                            gm_tqs5p4n2tq19v t404 = t403;
                            if (!((((uint8_t *)&t404))[1] != 0)) goto lb401;
                            uint8_t t405 = GM_LD(uint8_t, ((uint8_t *)&t404));
                            gm_l10 = t405;
                            {
                                uint8_t t409 = gm_l10;
                                uint8_t t410 = ((t409 == ((uint8_t)10ULL)));
                                if (!t410) goto ie408;
                                {
                                    void * t412 = gm_l0;
                                    void * t413 = gm_l0;
                                    uint64_t t414 = GM_LD(uint64_t, (((uint8_t *)t413) + 24));
                                    uint64_t r415; bool o416 = __builtin_add_overflow(t414, ((uint64_t)1ULL), &r415);
                                    if (o416) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 411, 29);
                                    uint64_t t417 = r415;
                                    GM_ST(uint64_t, (((uint8_t *)t412) + 24), t417);
                                }
                                {
                                    gm_l9 = ((uint8_t)1);
                                }
                                goto ix408;
ie408:;
                                uint8_t t419 = gm_l10;
                                gm_toh0ootodhjue t420;
                                GM_ST(uint8_t, ((uint8_t *)&t420 + 0), ((uint8_t)32ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t420 + 1), ((uint8_t)9ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t420 + 2), ((uint8_t)13ULL));
                                gm_toh0ootodhjue t421 = t420;
                                uint8_t t422 = 0;
                                uint8_t t423 = t419;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t421) + 0, ((uint8_t *)&t423))) t422 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t421) + 1, ((uint8_t *)&t423))) t422 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t421) + 2, ((uint8_t *)&t423))) t422 = 1;
                                uint8_t t424 = (uint8_t)(!t422);
                                if (!t424) goto ie418;
                                {
                                    goto lb401;
                                }
                                goto ix418;
ie418:;
ix418:;
ix408:;
                            }
                            {
                                void * t426 = gm_l0;
                                void * t427 = gm_l0;
                                uint64_t t428 = GM_LD(uint64_t, (((uint8_t *)t427) + 16));
                                uint64_t r429; bool o430 = __builtin_add_overflow(t428, ((uint64_t)1ULL), &r429);
                                if (o430) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 416, 25);
                                uint64_t t431 = r429;
                                GM_ST(uint64_t, (((uint8_t *)t426) + 16), t431);
                            }
lc401:;
                        }
lb401:;
                    }
                    {
                        uint8_t t433 = gm_l9;
                        uint8_t t434 = (uint8_t)!t433;
                        if (!t434) goto ie432;
                        {
                            void * t436 = gm_l0;
                            gm_b16_8 t437 = GM_LD(gm_b16_8, gm_cn9bf8d1ktm);
                            gm_tob1snuii5bme t438;
                            gm_f_error_96ci5lu((void *)&t438, t436, t437);
                            gm_tu615l4qlr8b2 t439;
                            GM_ST(uint8_t, (uint8_t *)&t439 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t439 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t439 + 4, 0);
                            ((uint8_t *)&t439)[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t439 + 8), t438);
                            gm_tu615l4qlr8b2 t440 = t439;
                            uint8_t t441 = ((((uint8_t *)&t440))[0] != 0);
                            F.ret = t439;
                            goto gm_fret;
                        }
                        goto ix432;
ie432:;
ix432:;
                    }
                    goto me299;
mn374:;
                    void * t443 = gm_l0;
                    gm_b16_8 t444 = GM_LD(gm_b16_8, gm_cn9bf8d1ktm);
                    gm_tob1snuii5bme t445;
                    gm_f_error_96ci5lu((void *)&t445, t443, t444);
                    gm_tu615l4qlr8b2 t446;
                    GM_ST(uint8_t, (uint8_t *)&t446 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t446 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t446 + 4, 0);
                    ((uint8_t *)&t446)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t446 + 8), t445);
                    gm_tu615l4qlr8b2 t447 = t446;
                    uint8_t t448 = ((((uint8_t *)&t447))[0] != 0);
                    F.ret = t446;
                    goto gm_fret;
                    goto me299;
mn442:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 393, 13);
me299:;
                }
                goto ix272;
ie272:;
                {
                    void * t450 = (void *)((uint8_t *)&F.l5);
                    uint8_t t451 = gm_l6;
                    gm_tftb1odc739t5 t452 = gm_f_push_t83rg2m(t450, t451);
                    gm_tftb1odc739t5 t453 = t452;
                    if (((((uint8_t *)&t453))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t453) + 1, gm_src3nt8pf0qk5gn, 423, 13);
                    gm_b0_1 t454 = GM_LD(gm_b0_1, (((uint8_t *)&t453) + 1));
                }
ix272:;
            }
lc149:;
        }
gm_ll150:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb149; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc149; }
        }
        goto gm_lad;
lb149:;
    }
    {
        gm_b16_8 t455 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t456 = t455;
        gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&t456));
        gm_tu615l4qlr8b2 t458;
        GM_ST(uint8_t, (uint8_t *)&t458 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t458 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t458 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t458 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t458 + 32, 0);
        ((uint8_t *)&t458)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t458 + 8), t457);
        gm_tu615l4qlr8b2 t459 = t458;
        uint8_t t460 = ((((uint8_t *)&t459))[0] != 0);
        F.ret = t458;
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
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_toml_gjtmf79 F;
    gm_b16_8 gm_l0; /* text */
    void * gm_l2; /* root */
    void * gm_l3; /* current */
    uint8_t gm_l4; /* c */
    uint64_t gm_l5; /* line */
    GM_STACK_CHECK(&F, 1388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&F.l1 + 33, 0, 7);
        gm_b16_8 t2 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&F.l1 + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&F.l1 + 24, ((uint64_t)1ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 32, ((uint8_t)0));
    }
    {
        void * t3 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        gm_l3 = t4;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb5;
            {
                void * t8 = (void *)((uint8_t *)&F.l1);
                gm_f_skip_space_and_newlines_qagb1bk(t8);
            }
            {
                void * t9 = (void *)((uint8_t *)&F.l1);
                gm_tqs5p4n2tq19v t10 = gm_f_peek_bkdvl65(t9);
                gm_tqs5p4n2tq19v t11 = t10;
                uint8_t t12;
                if (((((uint8_t *)&t11))[1] != 0)) {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
                    t12 = t13;
                } else {
                    goto lb5;
                }
                gm_l4 = t12;
            }
            {
                uint8_t t15 = gm_l4;
                uint8_t t16 = ((t15 == ((uint8_t)91ULL)));
                if (!t16) goto ie14;
                {
                    void * t18 = (void *)((uint8_t *)&F.l1);
                    void * t19 = gm_l2;
                    gm_trssmtuv8jr0v t20;
                    gm_f_table_header_j60mb6n((void *)&t20, (void *)NULL, cx, NULL, t18, t19);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    gm_trssmtuv8jr0v t21 = t20;
                    if (((((uint8_t *)&t21))[0] != 0)) {
                        gm_tob1snuii5bme t22 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t21) + 8));
                        gm_trssmtuv8jr0v t23;
                        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                        ((uint8_t *)&t23)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t23 + 8), t22);
                        F.ret = t23;
                        goto gm_fret;
                    }
                    void * t24 = GM_LD(void *, (((uint8_t *)&t21) + 8));
                    gm_l3 = t24;
                }
                goto ix14;
ie14:;
                {
                    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 24));
                    gm_l5 = t26;
                }
                {
                    void * t27 = (void *)((uint8_t *)&F.l1);
                    gm_t21m1eu5ta01n t28;
                    gm_f_key_path_7spb050((void *)&t28, (void *)NULL, cx, NULL, t27);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    gm_t21m1eu5ta01n t29 = t28;
                    if (((((uint8_t *)&t29))[0] != 0)) {
                        gm_tob1snuii5bme t30 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t29) + 8));
                        gm_trssmtuv8jr0v t31;
                        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                        ((uint8_t *)&t31)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t31 + 8), t30);
                        F.ret = t31;
                        goto gm_fret;
                    }
                    gm_trqfvmjtt4sij t32 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t29) + 8));
                    F.l6 = t32;
                }
                {
                    void * t33 = (void *)((uint8_t *)&F.l1);
                    gm_f_skip_space_voq272o(t33);
                }
                {
                    void * t35 = (void *)((uint8_t *)&F.l1);
                    gm_tqs5p4n2tq19v t36 = gm_f_peek_bkdvl65(t35);
                    gm_tqs5p4n2tq19v t37;
                    GM_ST(uint8_t, ((uint8_t *)&t37), ((uint8_t)61ULL));
                    ((uint8_t *)&t37)[1] = 1;
                    gm_tqs5p4n2tq19v t38 = t36;
                    gm_tqs5p4n2tq19v t39 = t37;
                    uint8_t t40 = (!gm_eqb44cb3cf1n(((uint8_t *)&t38), ((uint8_t *)&t39)));
                    if (!t40) goto ie34;
                    {
                        void * t42 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_spl83f693c752));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(31));
                        gm_tob1snuii5bme t44;
                        gm_f_error_96ci5lu((void *)&t44, t42, t43);
                        gm_trssmtuv8jr0v t45;
                        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                        ((uint8_t *)&t45)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t45 + 8), t44);
                        gm_trssmtuv8jr0v t46 = t45;
                        uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
                        F.ret = t45;
                        goto gm_fret;
                    }
                    goto ix34;
ie34:;
ix34:;
                }
                {
                    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 16));
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    if (o50) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 85, 13);
                    uint64_t t51 = r49;
                    GM_ST(uint64_t, (((uint8_t *)&F.l1) + 16), t51);
                }
                {
                    void * t52 = (void *)((uint8_t *)&F.l1);
                    gm_f_skip_space_voq272o(t52);
                }
                {
                    void * t53 = (void *)((uint8_t *)&F.l1);
                    gm_tefknldmnh2si t54;
                    gm_f_value_oq952o2((void *)&t54, (void *)NULL, cx, NULL, t53);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    gm_tefknldmnh2si t55 = t54;
                    if (((((uint8_t *)&t55))[0] != 0)) {
                        gm_tob1snuii5bme t56 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t55) + 8));
                        gm_trssmtuv8jr0v t57;
                        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                        ((uint8_t *)&t57)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t57 + 8), t56);
                        F.ret = t57;
                        goto gm_fret;
                    }
                    gm_tktr4rmbindgv t58 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)&t55) + 8));
                    F.l7 = t58;
                }
                {
                    void * t59 = gm_l3;
                    gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_b16_8 t61 = t60;
                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                    uint64_t t63 = gm_l5;
                    gm_tr1k5nl69lhjo t64;
                    gm_f_insert_path_cvuqfkc((void *)&t64, (void *)NULL, cx, NULL, t59, t62, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l7)), t63);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    gm_tr1k5nl69lhjo t65 = t64;
                    if (((((uint8_t *)&t65))[0] != 0)) {
                        gm_tob1snuii5bme t66 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t65) + 8));
                        gm_trssmtuv8jr0v t67;
                        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                        ((uint8_t *)&t67)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t67 + 8), t66);
                        F.ret = t67;
                        goto gm_fret;
                    }
                    gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 8));
                }
ix14:;
            }
            {
                void * t69 = (void *)((uint8_t *)&F.l1);
                gm_tr1k5nl69lhjo t70;
                gm_f_end_of_line_056kej4((void *)&t70, t69);
                gm_tr1k5nl69lhjo t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) {
                    gm_tob1snuii5bme t72 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t71) + 8));
                    gm_trssmtuv8jr0v t73;
                    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                    ((uint8_t *)&t73)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t73 + 8), t72);
                    F.ret = t73;
                    goto gm_fret;
                }
                gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 8));
            }
lc5:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
        }
        goto gm_lad;
lb5:;
    }
    {
        void * t75 = gm_l2;
        gm_trssmtuv8jr0v t76;
        GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t76 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t76 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t76 + 32, 0);
        ((uint8_t *)&t76)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t76 + 8), t75);
        gm_trssmtuv8jr0v t77 = t76;
        uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
        F.ret = t76;
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0);
__attribute__((aligned(4))) void gm_f_skip_space_and_newlines_qagb1bk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_space_and_newlines_qagb1bk F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 195u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_tqs5p4n2tq19v t4 = gm_f_peek_bkdvl65(t3);
            gm_tqs5p4n2tq19v t5 = t4;
            if (!((((uint8_t *)&t5))[1] != 0)) goto lb2;
            uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t5));
            gm_l1 = t6;
            {
                uint8_t t10 = gm_l1;
                gm_toh0ootodhjue t11;
                GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t11 + 1), ((uint8_t)9ULL));
                GM_ST(uint8_t, ((uint8_t *)&t11 + 2), ((uint8_t)13ULL));
                gm_toh0ootodhjue t12 = t11;
                uint8_t t13 = 0;
                uint8_t t14 = t10;
                if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 0, ((uint8_t *)&t14))) t13 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 1, ((uint8_t *)&t14))) t13 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 2, ((uint8_t *)&t14))) t13 = 1;
                uint8_t t15 = (uint8_t)(t13);
                if (!t15) goto ie9;
                {
                    void * t17 = gm_l0;
                    void * t18 = gm_l0;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 16));
                    uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                    if (o21) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 118, 13);
                    uint64_t t22 = r20;
                    GM_ST(uint64_t, (((uint8_t *)t17) + 16), t22);
                }
                goto ix9;
ie9:;
                uint8_t t24 = gm_l1;
                uint8_t t25 = ((t24 == ((uint8_t)10ULL)));
                if (!t25) goto ie23;
                {
                    void * t27 = gm_l0;
                    void * t28 = gm_l0;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 120, 13);
                    uint64_t t32 = r30;
                    GM_ST(uint64_t, (((uint8_t *)t27) + 16), t32);
                }
                {
                    void * t33 = gm_l0;
                    void * t34 = gm_l0;
                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 24));
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    if (o37) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 121, 13);
                    uint64_t t38 = r36;
                    GM_ST(uint64_t, (((uint8_t *)t33) + 24), t38);
                }
                goto ix23;
ie23:;
                uint8_t t40 = gm_l1;
                uint8_t t41 = ((t40 == ((uint8_t)35ULL)));
                if (!t41) goto ie39;
                {
                    for (;;) {
                        void * t44 = gm_l0;
                        gm_tqs5p4n2tq19v t45 = gm_f_peek_bkdvl65(t44);
                        gm_tqs5p4n2tq19v t46 = t45;
                        uint8_t t47;
                        if (((((uint8_t *)&t46))[1] != 0)) goto mn49;
                        t47 = ((uint8_t)0);
                        goto me48;
mn49:;
                        t47 = ((uint8_t)1);
                        goto me48;
mn50:;
                        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 123, 19);
me48:;
                        if (!t47) goto lb43;
                        void * t51 = gm_l0;
                        gm_tqs5p4n2tq19v t52 = gm_f_peek_bkdvl65(t51);
                        gm_tqs5p4n2tq19v t53;
                        GM_ST(uint8_t, ((uint8_t *)&t53), ((uint8_t)10ULL));
                        ((uint8_t *)&t53)[1] = 1;
                        gm_tqs5p4n2tq19v t54 = t52;
                        gm_tqs5p4n2tq19v t55 = t53;
                        uint8_t t56 = (!gm_eqb44cb3cf1n(((uint8_t *)&t54), ((uint8_t *)&t55)));
                        if (!t56) goto lb43;
                        {
                            void * t59 = gm_l0;
                            void * t60 = gm_l0;
                            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 16));
                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                            if (o63) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 123, 67);
                            uint64_t t64 = r62;
                            GM_ST(uint64_t, (((uint8_t *)t59) + 16), t64);
                        }
lc43:;
                    }
lb43:;
                }
                goto ix39;
ie39:;
                {
                    gm_b0_1 t66;
                    uint8_t t67 = 0;
                    F.ret = t66;
                    goto gm_fret;
                }
ix39:;
ix23:;
ix9:;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_skip_space_voq272o(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_space_voq272o F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_tqs5p4n2tq19v t4 = gm_f_peek_bkdvl65(t3);
            gm_tqs5p4n2tq19v t5;
            GM_ST(uint8_t, ((uint8_t *)&t5), ((uint8_t)32ULL));
            ((uint8_t *)&t5)[1] = 1;
            gm_tqs5p4n2tq19v t6;
            GM_ST(uint8_t, ((uint8_t *)&t6), ((uint8_t)9ULL));
            ((uint8_t *)&t6)[1] = 1;
            gm_t5r6k0fdcfsb3 t7;
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t7 + 0), t5);
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t7 + 2), t6);
            gm_t5r6k0fdcfsb3 t8 = t7;
            uint8_t t9 = 0;
            gm_tqs5p4n2tq19v t10 = t4;
            if (gm_eqb44cb3cf1n(((uint8_t *)&t8) + 0, ((uint8_t *)&t10))) t9 = 1;
            if (gm_eqb44cb3cf1n(((uint8_t *)&t8) + 2, ((uint8_t *)&t10))) t9 = 1;
            uint8_t t11 = (uint8_t)(t9);
            if (!t11) goto lb2;
            {
                void * t14 = gm_l0;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 111, 37);
                uint64_t t19 = r17;
                GM_ST(uint64_t, (((uint8_t *)t14) + 16), t19);
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_table_header_j60mb6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_table_header_j60mb6n F;
    void * gm_l0; /* r */
    void * gm_l1; /* root */
    uint8_t gm_l2; /* array */
    uint64_t gm_l3; /* line */
    gm_b16_8 gm_l5; /* names */
    void * gm_l6; /* t */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* name */
    uint8_t gm_l11; /* last */
    void * gm_l12; /* e */
    void * gm_l14; /* fresh */
    void * gm_l16; /* sub */
    void * gm_l18; /* sub */
    void * gm_l19; /* fresh */
    GM_STACK_CHECK(&F, 3036u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 150, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 16), t7);
    }
    {
        void * t8 = gm_l0;
        gm_tqs5p4n2tq19v t9 = gm_f_peek_bkdvl65(t8);
        gm_tqs5p4n2tq19v t10;
        GM_ST(uint8_t, ((uint8_t *)&t10), ((uint8_t)91ULL));
        ((uint8_t *)&t10)[1] = 1;
        gm_tqs5p4n2tq19v t11 = t9;
        gm_tqs5p4n2tq19v t12 = t10;
        uint8_t t13 = (gm_eqb44cb3cf1n(((uint8_t *)&t11), ((uint8_t *)&t12)));
        gm_l2 = t13;
    }
    {
        uint8_t t15 = gm_l2;
        if (!t15) goto ie14;
        {
            void * t17 = gm_l0;
            void * t18 = gm_l0;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 16));
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            if (o21) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 152, 16);
            uint64_t t22 = r20;
            GM_ST(uint64_t, (((uint8_t *)t17) + 16), t22);
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t23 = gm_l0;
        gm_f_skip_space_voq272o(t23);
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
        gm_l3 = t25;
    }
    {
        void * t26 = gm_l0;
        gm_t21m1eu5ta01n t27;
        gm_f_key_path_7spb050((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t21m1eu5ta01n t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) {
            gm_tob1snuii5bme t29 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t28) + 8));
            gm_trssmtuv8jr0v t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            ((uint8_t *)&t30)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t30 + 8), t29);
            F.ret = t30;
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t31 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t28) + 8));
        F.l4 = t31;
    }
    {
        void * t32 = gm_l0;
        gm_f_skip_space_voq272o(t32);
    }
    {
        void * t34 = gm_l0;
        gm_tqs5p4n2tq19v t35 = gm_f_peek_bkdvl65(t34);
        gm_tqs5p4n2tq19v t36;
        GM_ST(uint8_t, ((uint8_t *)&t36), ((uint8_t)93ULL));
        ((uint8_t *)&t36)[1] = 1;
        gm_tqs5p4n2tq19v t37 = t35;
        gm_tqs5p4n2tq19v t38 = t36;
        uint8_t t39 = (!gm_eqb44cb3cf1n(((uint8_t *)&t37), ((uint8_t *)&t38)));
        if (!t39) goto ie33;
        {
            void * t41 = gm_l0;
            gm_b16_8 t42;
            uint8_t t44 = gm_l2;
            if (!t44) goto ie43;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sdq87al6fhs9i));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(34));
            gm_b16_8 t47 = t46;
            t42 = t47;
            goto ix43;
ie43:;
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_std877jr2ed05));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(28));
            gm_b16_8 t50 = t49;
            t42 = t50;
ix43:;
            gm_tob1snuii5bme t51;
            gm_f_error_96ci5lu((void *)&t51, t41, t42);
            gm_trssmtuv8jr0v t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            ((uint8_t *)&t52)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t52 + 8), t51);
            gm_trssmtuv8jr0v t53 = t52;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t52;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t55 = gm_l0;
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 158, 5);
        uint64_t t60 = r58;
        GM_ST(uint64_t, (((uint8_t *)t55) + 16), t60);
    }
    {
        uint8_t t62 = gm_l2;
        if (!t62) goto ie61;
        {
            void * t65 = gm_l0;
            gm_tqs5p4n2tq19v t66 = gm_f_peek_bkdvl65(t65);
            gm_tqs5p4n2tq19v t67;
            GM_ST(uint8_t, ((uint8_t *)&t67), ((uint8_t)93ULL));
            ((uint8_t *)&t67)[1] = 1;
            gm_tqs5p4n2tq19v t68 = t66;
            gm_tqs5p4n2tq19v t69 = t67;
            uint8_t t70 = (!gm_eqb44cb3cf1n(((uint8_t *)&t68), ((uint8_t *)&t69)));
            if (!t70) goto ie64;
            {
                void * t72 = gm_l0;
                gm_b16_8 t73;
                GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sdq87al6fhs9i));
                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(34));
                gm_tob1snuii5bme t74;
                gm_f_error_96ci5lu((void *)&t74, t72, t73);
                gm_trssmtuv8jr0v t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                ((uint8_t *)&t75)[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t75 + 8), t74);
                gm_trssmtuv8jr0v t76 = t75;
                uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
                F.ret = t75;
                goto gm_fret;
            }
            goto ix64;
ie64:;
ix64:;
        }
        {
            void * t78 = gm_l0;
            void * t79 = gm_l0;
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 16));
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            if (o82) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 161, 9);
            uint64_t t83 = r81;
            GM_ST(uint64_t, (((uint8_t *)t78) + 16), t83);
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_l5 = t84;
    }
    {
        void * t85 = gm_l1;
        gm_l6 = t85;
    }
    {
        {
            gm_b16_8 t87 = gm_l5;
            gm_b16_8 t88 = t87;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            gm_l7 = t89;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t91 = gm_l8;
                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t93 = (t91 < t92);
                if (!t93) goto lb90;
                {
                    gm_tf1bjg81u0glv t96;
                    uint64_t t97 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t96 + 0, t97);
                    uint64_t t98 = gm_l8;
                    uint64_t t99 = (uintptr_t)t98;
                    uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t99 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t96 + 8, t101);
                    gm_tf1bjg81u0glv t102 = t96;
                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&t102) + 0));
                    gm_l9 = t105;
                    gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
                    gm_l10 = t106;
                    {
                        uint64_t t108 = gm_l9;
                        uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint64_t r110; bool o111 = __builtin_sub_overflow(t109, ((uint64_t)1ULL), &r110);
                        if (o111) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 166, 22);
                        uint64_t t112 = r110;
                        uint8_t t113 = ((t108 == t112));
                        gm_l11 = t113;
                    }
                    {
                        void * t115 = gm_l6;
                        gm_b16_8 t116 = gm_l10;
                        void * t117 = gm_f_get_9rme0gb(t115, t116);
                        void * t118 = t117;
                        if (!(GM_LD(void *, ((uint8_t *)&t118)) != 0)) goto ie114;
                        void * t119 = GM_LD(void *, ((uint8_t *)&t118));
                        gm_l12 = t119;
                        {
                            uint8_t t122 = gm_l11;
                            if (!t122) goto ie121;
                            uint8_t t123 = gm_l2;
                            if (!t123) goto ie121;
                            {
                                void * t126 = gm_l12;
                                uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t126) + 16) + 0));
                                if (t127 != ((uint8_t)3ULL)) goto ie125;
                                gm_trqfvmjtt4sij t128 = GM_LD(gm_trqfvmjtt4sij, ((((uint8_t *)t126) + 16) + 8));
                                F.l13 = t128;
                                {
                                    void * t130 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_ll94;
                                    }
                                    gm_l14 = t130;
                                }
                                {
                                    gm_trqfvmjtt4sij t131 = F.l13;
                                    F.l15 = t131;
                                }
                                {
                                    void * t132 = (void *)((uint8_t *)&F.l15);
                                    void * t133 = gm_l14;
                                    gm_tktr4rmbindgv t134;
                                    memset(&t134, 0, sizeof t134);
                                    GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)4ULL));
                                    GM_ST(void *, ((uint8_t *)&t134 + 8), t133);
                                    gm_tftb1odc739t5 t135 = gm_f_push_0pgacsj_2(t132, &t134);
                                    gm_tftb1odc739t5 t136 = t135;
                                    if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_src3nt8pf0qk5gn, 172, 17);
                                    gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
                                }
                                {
                                    void * t138 = gm_l12;
                                    gm_trqfvmjtt4sij t139 = F.l15;
                                    gm_tktr4rmbindgv t140;
                                    GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)3ULL));
                                    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t140 + 8), t139);
                                    GM_ST(gm_tktr4rmbindgv, (((uint8_t *)t138) + 16), t140);
                                }
                                {
                                    void * t141 = gm_l14;
                                    gm_trssmtuv8jr0v t142;
                                    GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t142 + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t142 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t142 + 32, 0);
                                    ((uint8_t *)&t142)[0] = 0;
                                    GM_ST(void *, ((uint8_t *)&t142 + 8), t141);
                                    gm_trssmtuv8jr0v t143 = t142;
                                    uint8_t t144 = ((((uint8_t *)&t143))[0] != 0);
                                    F.ret = t142;
                                    goto gm_fret;
                                }
                                goto ix125;
ie125:;
                                {
                                    void * t146 = gm_l0;
                                    gm_b16_8 t147;
                                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t148;
                                    gm_f_error_96ci5lu((void *)&t148, t146, t147);
                                    gm_trssmtuv8jr0v t149;
                                    GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                                    ((uint8_t *)&t149)[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t149 + 8), t148);
                                    gm_trssmtuv8jr0v t150 = t149;
                                    uint8_t t151 = ((((uint8_t *)&t150))[0] != 0);
                                    F.ret = t149;
                                    goto gm_fret;
                                }
ix125:;
                            }
                            goto ix121;
ie121:;
ix121:;
                        }
                        {
                            void * t152 = gm_l12;
                            gm_tktr4rmbindgv t153 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)t152) + 16));
                            gm_tktr4rmbindgv t154 = t153;
                            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t154) + 0));
                            switch (t160) { case ((uint8_t)0ULL): goto ma158; case ((uint8_t)1ULL): goto ma158; case ((uint8_t)2ULL): goto ma158; case ((uint8_t)3ULL): goto ma157; case ((uint8_t)4ULL): goto ma156; case ((uint8_t)5ULL): goto ma158; default: goto ma158; }
ma156:;
                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&t154) + 0));
                            if (t162 != ((uint8_t)4ULL)) goto mn161;
                            void * t163 = GM_LD(void *, (((uint8_t *)&t154) + 8));
                            gm_l16 = t163;
                            {
                                uint8_t t166 = gm_l11;
                                if (!t166) goto ie165;
                                uint8_t t167 = gm_l2;
                                uint8_t t168 = (uint8_t)!t167;
                                if (!t168) goto ie165;
                                void * t169 = gm_l16;
                                uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)t169) + 40));
                                if (!t170) goto ie165;
                                {
                                    void * t172 = gm_l0;
                                    gm_b16_8 t173;
                                    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t174;
                                    gm_f_error_96ci5lu((void *)&t174, t172, t173);
                                    gm_trssmtuv8jr0v t175;
                                    GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                                    ((uint8_t *)&t175)[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t175 + 8), t174);
                                    gm_trssmtuv8jr0v t176 = t175;
                                    uint8_t t177 = ((((uint8_t *)&t176))[0] != 0);
                                    F.ret = t175;
                                    goto gm_fret;
                                }
                                goto ix165;
ie165:;
ix165:;
                            }
                            {
                                void * t178 = gm_l16;
                                gm_l6 = t178;
                            }
                            goto me155;
mn161:;
ma157:;
                            uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&t154) + 0));
                            if (t180 != ((uint8_t)3ULL)) goto mn179;
                            gm_trqfvmjtt4sij t181 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t154) + 8));
                            F.l17 = t181;
                            uint64_t t182 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                            uint8_t t183 = (t182 > ((uint64_t)0ULL));
                            if (!t183) goto mn179;
                            {
                                uint8_t t186 = gm_l11;
                                if (!t186) goto ie185;
                                uint8_t t187 = gm_l2;
                                uint8_t t188 = (uint8_t)!t187;
                                if (!t188) goto ie185;
                                {
                                    void * t190 = gm_l0;
                                    gm_b16_8 t191;
                                    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t192;
                                    gm_f_error_96ci5lu((void *)&t192, t190, t191);
                                    gm_trssmtuv8jr0v t193;
                                    GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
                                    ((uint8_t *)&t193)[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t193 + 8), t192);
                                    gm_trssmtuv8jr0v t194 = t193;
                                    uint8_t t195 = ((((uint8_t *)&t194))[0] != 0);
                                    F.ret = t193;
                                    goto gm_fret;
                                }
                                goto ix185;
ie185:;
ix185:;
                            }
                            {
                                uint64_t t197 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                                uint64_t r198; bool o199 = __builtin_sub_overflow(t197, ((uint64_t)1ULL), &r198);
                                if (o199) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 183, 48);
                                uint64_t t200 = r198;
                                uint64_t t201 = (uintptr_t)t200;
                                uint64_t t202 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                                if (t201 >= (uintptr_t)(t202)) gm_trap_index(t201, t202, gm_src3nt8pf0qk5gn, 183, 36);
                                uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 0)) + t201 * 48) + 0));
                                if (t203 != ((uint8_t)4ULL)) goto ie196;
                                void * t204 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 0)) + t201 * 48) + 8));
                                gm_l18 = t204;
                                {
                                    void * t206 = gm_l18;
                                    gm_l6 = t206;
                                }
                                goto ix196;
ie196:;
                                {
                                    void * t208 = gm_l0;
                                    gm_b16_8 t209;
                                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t210;
                                    gm_f_error_96ci5lu((void *)&t210, t208, t209);
                                    gm_trssmtuv8jr0v t211;
                                    GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                                    ((uint8_t *)&t211)[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t211 + 8), t210);
                                    gm_trssmtuv8jr0v t212 = t211;
                                    uint8_t t213 = ((((uint8_t *)&t212))[0] != 0);
                                    F.ret = t211;
                                    goto gm_fret;
                                }
ix196:;
                            }
                            goto me155;
mn179:;
ma158:;
                            void * t215 = gm_l0;
                            gm_b16_8 t216;
                            GM_ST(void *, (uint8_t *)&t216, (void *)(gm_skiffh4urhl7e));
                            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(13));
                            gm_tob1snuii5bme t217;
                            gm_f_error_96ci5lu((void *)&t217, t215, t216);
                            gm_trssmtuv8jr0v t218;
                            GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
                            ((uint8_t *)&t218)[0] = 1;
                            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t218 + 8), t217);
                            gm_trssmtuv8jr0v t219 = t218;
                            uint8_t t220 = ((((uint8_t *)&t219))[0] != 0);
                            F.ret = t218;
                            goto gm_fret;
                            goto me155;
mn214:;
ma159:;
                            gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 176, 13);
me155:;
                        }
                        goto ix114;
ie114:;
                        {
                            void * t222 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll94;
                            }
                            gm_l19 = t222;
                        }
                        {
                            uint8_t t224 = gm_l11;
                            if (!t224) goto ie223;
                            uint8_t t225 = gm_l2;
                            if (!t225) goto ie223;
                            {
                                gm_f_list_0202sdh_2((void *)&F.l20, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll94;
                                }
                            }
                            {
                                void * t227 = (void *)((uint8_t *)&F.l20);
                                void * t228 = gm_l19;
                                gm_tktr4rmbindgv t229;
                                memset(&t229, 0, sizeof t229);
                                GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t229 + 8), t228);
                                gm_tftb1odc739t5 t230 = gm_f_push_0pgacsj_2(t227, &t229);
                                gm_tftb1odc739t5 t231 = t230;
                                if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 1, gm_src3nt8pf0qk5gn, 192, 17);
                                gm_b0_1 t232 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 1));
                            }
                            {
                                void * t233 = gm_l6;
                                void * t234 = (void *)(((uint8_t *)t233) + 0);
                                gm_trc3jdv2g5qre t235;
                                gm_b16_8 t236 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t235 + 0, t236);
                                gm_trqfvmjtt4sij t237 = F.l20;
                                gm_tktr4rmbindgv t238;
                                GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)3ULL));
                                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t238 + 8), t237);
                                GM_ST(gm_tktr4rmbindgv, (uint8_t *)&t235 + 16, t238);
                                uint64_t t239 = gm_l3;
                                GM_ST(uint64_t, (uint8_t *)&t235 + 64, t239);
                                gm_tftb1odc739t5 t240 = gm_f_push_2pp1ri6(t234, &t235);
                                gm_tftb1odc739t5 t241 = t240;
                                if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_src3nt8pf0qk5gn, 193, 17);
                                gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
                            }
                            goto ix223;
ie223:;
                            {
                                void * t244 = gm_l6;
                                void * t245 = (void *)(((uint8_t *)t244) + 0);
                                gm_trc3jdv2g5qre t246;
                                gm_b16_8 t247 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t246 + 0, t247);
                                void * t248 = gm_l19;
                                gm_tktr4rmbindgv t249;
                                memset(&t249, 0, sizeof t249);
                                GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t249 + 8), t248);
                                GM_ST(gm_tktr4rmbindgv, (uint8_t *)&t246 + 16, t249);
                                uint64_t t250 = gm_l3;
                                GM_ST(uint64_t, (uint8_t *)&t246 + 64, t250);
                                gm_tftb1odc739t5 t251 = gm_f_push_2pp1ri6(t245, &t246);
                                gm_tftb1odc739t5 t252 = t251;
                                if (((((uint8_t *)&t252))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t252) + 1, gm_src3nt8pf0qk5gn, 195, 17);
                                gm_b0_1 t253 = GM_LD(gm_b0_1, (((uint8_t *)&t252) + 1));
                            }
ix223:;
                        }
                        {
                            void * t254 = gm_l19;
                            gm_l6 = t254;
                        }
ix114:;
                    }
                    goto me103;
mn104:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 165, 5);
me103:;
                }
lc90:;
                {
                    uint64_t t256 = gm_l8;
                    uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                    uint64_t t259 = r257;
                    gm_l8 = t259;
                }
            }
gm_ll94:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb90; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc90; }
            }
            goto gm_lad;
lb90:;
        }
    }
    {
        void * t260 = gm_l6;
        GM_ST(uint8_t, (((uint8_t *)t260) + 40), ((uint8_t)1));
    }
    {
        void * t261 = gm_l6;
        gm_trssmtuv8jr0v t262;
        GM_ST(uint8_t, (uint8_t *)&t262 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t262 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t262 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t262 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t262 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t262 + 32, 0);
        ((uint8_t *)&t262)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t262 + 8), t261);
        gm_trssmtuv8jr0v t263 = t262;
        uint8_t t264 = ((((uint8_t *)&t263))[0] != 0);
        F.ret = t262;
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
__attribute__((aligned(4))) void gm_f_value_oq952o2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_value_oq952o2 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    gm_b16_8 gm_l2; /* rest */
    GM_STACK_CHECK(&F, 2398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t2) + 32), ((uint8_t)0));
    }
    {
        void * t3 = gm_l0;
        gm_tqs5p4n2tq19v t4 = gm_f_peek_bkdvl65(t3);
        gm_tqs5p4n2tq19v t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[1] != 0)) {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t10;
            gm_f_error_96ci5lu((void *)&t10, t8, t9);
            gm_tefknldmnh2si t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 48, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t11 + 8), t10);
            gm_tefknldmnh2si t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t14 = gm_l1;
        uint8_t t15 = t14;
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t20 == ((uint8_t)34ULL))) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t22 == ((uint8_t)39ULL))) goto pa21;
        goto po18;
pa21:;
        goto mn17;
po18:;
        void * t23 = gm_l0;
        gm_tu615l4qlr8b2 t24;
        gm_f_parse_string_80niubc((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tu615l4qlr8b2 t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) {
            gm_tob1snuii5bme t26 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t25) + 8));
            gm_tefknldmnh2si t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t27 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t27 + 48, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t27 + 8), t26);
            F.ret = t27;
            goto gm_fret;
        }
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_tktr4rmbindgv t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
        gm_tefknldmnh2si t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        ((uint8_t *)&t30)[0] = 0;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t30 + 8), t29);
        gm_tefknldmnh2si t31 = t30;
        uint8_t t32 = ((((uint8_t *)&t31))[0] != 0);
        F.ret = t30;
        goto gm_fret;
        goto me16;
mn17:;
        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t34 == ((uint8_t)91ULL))) goto mn33;
        void * t35 = gm_l0;
        gm_f_array_canpt3e((void *)&(*(gm_tefknldmnh2si *)gm_out), (void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me16;
mn33:;
        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t37 == ((uint8_t)123ULL))) goto mn36;
        void * t38 = gm_l0;
        gm_f_inline_table_cvvpgel((void *)&(*(gm_tefknldmnh2si *)gm_out), (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me16;
mn36:;
        uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t40 == ((uint8_t)116ULL))) goto mn39;
        {
            void * t43 = gm_l0;
            uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 0) + 8);
            void * t45 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 0)));
            void * t46 = gm_l0;
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 16));
            uint64_t t48 = (uintptr_t)t47;
            if (t48 > (uintptr_t)t44 || (uintptr_t)t44 > (uintptr_t)t44) gm_trap_slice(t48, (uintptr_t)t44, t44, gm_src3nt8pf0qk5gn, 258, 20);
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t45 + t48 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)((uintptr_t)t44 - t48));
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(4));
            uint8_t t51 = gm_f_starts_with_6vvt01e(t49, t50);
            uint8_t t52 = (uint8_t)!t51;
            if (!t52) goto ie42;
            {
                void * t54 = gm_l0;
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_shlilv0hq3bqu));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(32));
                gm_tob1snuii5bme t56;
                gm_f_error_96ci5lu((void *)&t56, t54, t55);
                gm_tefknldmnh2si t57;
                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 48, 0);
                ((uint8_t *)&t57)[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t57 + 8), t56);
                gm_tefknldmnh2si t58 = t57;
                uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
                F.ret = t57;
                goto gm_fret;
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            void * t60 = gm_l0;
            void * t61 = gm_l0;
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 16));
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)4ULL), &r63);
            if (o64) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 259, 13);
            uint64_t t65 = r63;
            GM_ST(uint64_t, (((uint8_t *)t60) + 16), t65);
        }
        {
            void * t66 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t66) + 32), ((uint8_t)1));
        }
        {
            gm_tktr4rmbindgv t67;
            memset(&t67, 0, sizeof t67);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
            GM_ST(uint8_t, ((uint8_t *)&t67 + 8), ((uint8_t)1));
            gm_tefknldmnh2si t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
            ((uint8_t *)&t68)[0] = 0;
            GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t68 + 8), t67);
            gm_tefknldmnh2si t69 = t68;
            uint8_t t70 = ((((uint8_t *)&t69))[0] != 0);
            F.ret = t68;
            goto gm_fret;
        }
        goto me16;
mn39:;
        uint8_t t72 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t72 == ((uint8_t)102ULL))) goto mn71;
        {
            void * t75 = gm_l0;
            uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)t75) + 0) + 8);
            void * t77 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t75) + 0)));
            void * t78 = gm_l0;
            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 16));
            uint64_t t80 = (uintptr_t)t79;
            if (t80 > (uintptr_t)t76 || (uintptr_t)t76 > (uintptr_t)t76) gm_trap_slice(t80, (uintptr_t)t76, t76, gm_src3nt8pf0qk5gn, 264, 20);
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t77 + t80 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)((uintptr_t)t76 - t80));
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(5));
            uint8_t t83 = gm_f_starts_with_6vvt01e(t81, t82);
            uint8_t t84 = (uint8_t)!t83;
            if (!t84) goto ie74;
            {
                void * t86 = gm_l0;
                gm_b16_8 t87;
                GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sne7l93pr2tmb));
                GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(33));
                gm_tob1snuii5bme t88;
                gm_f_error_96ci5lu((void *)&t88, t86, t87);
                gm_tefknldmnh2si t89;
                GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t89 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t89 + 48, 0);
                ((uint8_t *)&t89)[0] = 1;
                GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t89 + 8), t88);
                gm_tefknldmnh2si t90 = t89;
                uint8_t t91 = ((((uint8_t *)&t90))[0] != 0);
                F.ret = t89;
                goto gm_fret;
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            void * t92 = gm_l0;
            void * t93 = gm_l0;
            uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)t93) + 16));
            uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)5ULL), &r95);
            if (o96) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 265, 13);
            uint64_t t97 = r95;
            GM_ST(uint64_t, (((uint8_t *)t92) + 16), t97);
        }
        {
            void * t98 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t98) + 32), ((uint8_t)1));
        }
        {
            gm_tktr4rmbindgv t99;
            memset(&t99, 0, sizeof t99);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)2ULL));
            GM_ST(uint8_t, ((uint8_t *)&t99 + 8), ((uint8_t)0));
            gm_tefknldmnh2si t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
            ((uint8_t *)&t100)[0] = 0;
            GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t100 + 8), t99);
            gm_tefknldmnh2si t101 = t100;
            uint8_t t102 = ((((uint8_t *)&t101))[0] != 0);
            F.ret = t100;
            goto gm_fret;
        }
        goto me16;
mn71:;
        uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t106 == ((uint8_t)43ULL))) goto pa105;
        goto po104;
pa105:;
        uint8_t t108 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t108 == ((uint8_t)45ULL))) goto pa107;
        goto po104;
pa107:;
        uint8_t t110 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if ((t110 < ((uint8_t)48ULL)) || (t110 > ((uint8_t)57ULL))) goto pa109;
        goto po104;
pa109:;
        goto mn103;
po104:;
        void * t111 = gm_l0;
        gm_f_number_4l95pjb((void *)&(*(gm_tefknldmnh2si *)gm_out), t111);
        goto gm_fdone;
        goto me16;
mn103:;
        uint8_t t115 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t115 == ((uint8_t)105ULL))) goto pa114;
        goto po113;
pa114:;
        uint8_t t117 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t117 == ((uint8_t)110ULL))) goto pa116;
        goto po113;
pa116:;
        goto mn112;
po113:;
        {
            void * t119 = gm_l0;
            uint64_t t120 = GM_LD(uintptr_t, (((uint8_t *)t119) + 0) + 8);
            void * t121 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t119) + 0)));
            void * t122 = gm_l0;
            uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)t122) + 16));
            uint64_t t124 = (uintptr_t)t123;
            if (t124 > (uintptr_t)t120 || (uintptr_t)t120 > (uintptr_t)t120) gm_trap_slice(t124, (uintptr_t)t120, t120, gm_src3nt8pf0qk5gn, 271, 21);
            gm_b16_8 t125;
            GM_ST(void *, (uint8_t *)&t125, (void *)(((uint8_t *)t121 + t124 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)((uintptr_t)t120 - t124));
            gm_l2 = t125;
        }
        {
            uint8_t t127 = 0;
            gm_b16_8 t128 = gm_l2;
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(3));
            uint8_t t130 = gm_f_starts_with_6vvt01e(t128, t129);
            if (!t130) {
                gm_b16_8 t131 = gm_l2;
                gm_b16_8 t132;
                GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sfa86erpod86s));
                GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(3));
                uint8_t t133 = gm_f_starts_with_6vvt01e(t131, t132);
                t127 = t133;
            } else t127 = 1;
            if (!t127) goto ie126;
            {
                void * t135 = gm_l0;
                void * t136 = gm_l0;
                uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)t136) + 16));
                uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)3ULL), &r138);
                if (o139) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 273, 17);
                uint64_t t140 = r138;
                GM_ST(uint64_t, (((uint8_t *)t135) + 16), t140);
            }
            {
                void * t141 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t141) + 32), ((uint8_t)1));
            }
            {
                uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t143 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t144 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t145 = (uintptr_t)((uint64_t)3ULL);
                if (t144 > t145 || t145 > (uintptr_t)t142) gm_trap_slice(t144, t145, t142, gm_src3nt8pf0qk5gn, 275, 36);
                gm_b16_8 t146;
                GM_ST(void *, (uint8_t *)&t146, (void *)(((uint8_t *)t143 + t144 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(t145 - t144));
                gm_tktr4rmbindgv t147;
                GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t147 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t147 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t147 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t147 + 8), t146);
                gm_tefknldmnh2si t148;
                GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                ((uint8_t *)&t148)[0] = 0;
                GM_ST(gm_tktr4rmbindgv, ((uint8_t *)&t148 + 8), t147);
                gm_tefknldmnh2si t149 = t148;
                uint8_t t150 = ((((uint8_t *)&t149))[0] != 0);
                F.ret = t148;
                goto gm_fret;
            }
            goto ix126;
ie126:;
ix126:;
        }
        {
            void * t151 = gm_l0;
            gm_b16_8 t152 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t153;
            gm_f_error_96ci5lu((void *)&t153, t151, t152);
            gm_tefknldmnh2si t154;
            GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t154 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t154 + 48, 0);
            ((uint8_t *)&t154)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t154 + 8), t153);
            gm_tefknldmnh2si t155 = t154;
            uint8_t t156 = ((((uint8_t *)&t155))[0] != 0);
            F.ret = t154;
            goto gm_fret;
        }
        goto me16;
mn112:;
        uint8_t t158 = GM_LD(uint8_t, ((uint8_t *)&t15));
        if (!(t158 == ((uint8_t)61ULL))) goto mn157;
        void * t159 = gm_l0;
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sv5o4caljvbmj));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(27));
        gm_tob1snuii5bme t161;
        gm_f_error_96ci5lu((void *)&t161, t159, t160);
        gm_tefknldmnh2si t162;
        GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t162 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t162 + 48, 0);
        ((uint8_t *)&t162)[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t162 + 8), t161);
        gm_tefknldmnh2si t163 = t162;
        uint8_t t164 = ((((uint8_t *)&t163))[0] != 0);
        F.ret = t162;
        goto gm_fret;
        goto me16;
mn157:;
        void * t166 = gm_l0;
        gm_b16_8 t167 = GM_LD(gm_b16_8, gm_cal7njs7es4);
        gm_tob1snuii5bme t168;
        gm_f_error_96ci5lu((void *)&t168, t166, t167);
        gm_tefknldmnh2si t169;
        GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t169 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t169 + 48, 0);
        ((uint8_t *)&t169)[0] = 1;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t169 + 8), t168);
        gm_tefknldmnh2si t170 = t169;
        uint8_t t171 = ((((uint8_t *)&t170))[0] != 0);
        F.ret = t169;
        goto gm_fret;
        goto me16;
mn165:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 253, 5);
me16:;
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
