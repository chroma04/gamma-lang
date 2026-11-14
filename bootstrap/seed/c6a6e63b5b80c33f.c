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
typedef struct gm_csp8136jfauorf { unsigned __int128 f0; _Bool f1; _Bool f2; } gm_csp8136jfauorf;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t f2; uint8_t pe[14]; } gm_t18m13lmi7mp6;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csp8136jfauorf) == 32 && _Alignof(gm_csp8136jfauorf) == 16 && offsetof(gm_csp8136jfauorf, f0) == 0 && offsetof(gm_csp8136jfauorf, f1) == 16 && offsetof(gm_csp8136jfauorf, f2) == 17, "layout of `Arith`");
_Static_assert(sizeof(gm_t18m13lmi7mp6) == 32 && _Alignof(gm_t18m13lmi7mp6) == 16 && offsetof(gm_t18m13lmi7mp6, f0) == 0 && offsetof(gm_t18m13lmi7mp6, f1) == 16 && offsetof(gm_t18m13lmi7mp6, f2) == 17, "layout of `Arith`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint32_t v1; } f1; } gm_tcula66ipo514;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tcula66ipo514) == 32 && _Alignof(gm_tcula66ipo514) == 8 && offsetof(gm_tcula66ipo514, f0) == 0 && offsetof(gm_tcula66ipo514, f1) == 8, "layout of `Flow!u32`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
struct gm_fr_checked_overflow_ma1bm1g {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_div_zero_trap_ap1saqd {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_finish_int_jmjdqd4 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_arith_is3k5b1 {
    gm_frame h;
    gm_t18m13lmi7mp6 ret;
};
struct gm_fr_int_binop_qrbogd9 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_cmp_kb3sm6u {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_int_hi_qa5fgoo {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_int_lo_i8jbm6e {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_int_pow_wrapped_5mjnoh4 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_int_shl_b8ne661 {
    gm_frame h;
    gm_t18m13lmi7mp6 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_num_mask_do3naah {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_overflow_result_vqlhh7t {
    gm_frame h;
    gm_tdlro53ljvkl0 ret;
};
struct gm_fr_overflow_trap_j2f8u18 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_pow_7je2uvi {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_sat_bits_ug4cjjm {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_shift_amount_aalipud {
    gm_frame h;
    gm_tcula66ipo514 ret;
};
struct gm_fr_shift_check_a7uaho4 {
    gm_frame h;
    gm_tcula66ipo514 ret;
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
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_finish_int_jmjdqd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, const void * a3, gm_t88rcucg1b7uk a4, gm_ttovhfee8mf28 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_format_bl27tn4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jp4udcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nacqkaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_int_arith_is3k5b1(void *gm_out, gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4);
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_arith_is3k5b1_c(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4);
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __int128 gm_f_int_hi_qa5fgoo(uint32_t a0);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_int_pow_wrapped_5mjnoh4(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_int_shl_b8ne661(void *gm_out, uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3);
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_shl_b8ne661_c(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_overflow_result_vqlhh7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, unsigned __int128 a5, uint8_t a6, gm_b16_8 a7);
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_sat_bits_ug4cjjm(uint8_t a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_shift_amount_aalipud(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint32_t a5);
__attribute__((aligned(4))) void gm_f_shift_check_a7uaho4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, uint8_t a3, unsigned __int128 a4, uint32_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_wr_uint_rs9ado5(void * a0, gm_b16_8 a1, unsigned __int128 a2);
extern const uint8_t gm_s0k8sea1jhldg[21];
extern const uint8_t gm_s2hnapin8228t[21];
extern const uint8_t gm_s5cmg64f6av8k[70];
extern const uint8_t gm_s9idfla83gb22[59];
extern const uint8_t gm_s9r2a03bnhj06[58];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src9pvgnpibl2nm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcqod65l394oc8[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_div_zero_trap_ap1saqd F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slf2kl43ogurh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 88) + 0) == ((uint8_t)3ULL)));
    F.ret = t2;
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_mask_do3naah F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)128ULL));
    if (!t4) goto ie2;
    unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    unsigned __int128 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    unsigned __int128 t10 = (unsigned __int128)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 128) gm_trap_shift((uint64_t)n11, "u128", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 r12 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n11);
    bool o13 = (unsigned __int128)(r12 >> (int)n11) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    if (o13) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 t14 = r12;
    unsigned __int128 r15; bool o16 = __builtin_sub_overflow(t14, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 8, 73);
    unsigned __int128 t17 = r15;
    unsigned __int128 t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_overflow_trap_j2f8u18 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* sym */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_eruojoe((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcqod65l394oc8, 23, 108);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t7);
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
    GM_STACK_CHECK(&F, 129u);
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_checked_overflow_ma1bm1g F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* sym */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_fast_23em7sv(t3);
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t6);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t7 = gm_l0;
        gm_tl874t2atnlh3 t8 = gm_l1;
        gm_b16_8 t9 = gm_l2;
        gm_t76123gfet73g t10;
        gm_f_overflow_trap_j2f8u18((void *)&t10, (void *)NULL, cx, NULL, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t10);
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
extern __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_finish_int_jmjdqd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, const void * a3, gm_t88rcucg1b7uk a4, gm_ttovhfee8mf28 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7, gm_b16_8 a8) {
    struct gm_fr_finish_int_jmjdqd4 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t18m13lmi7mp6 gm_l3; /* r */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* sym */
    gm_t88rcucg1b7uk gm_l7; /* res_ty */
    gm_b16_8 gm_l8; /* dst */
    unsigned __int128 gm_l9; /* low */
    gm_t51b2en4qp02s gm_l10; /* pb */
    uint64_t gm_l11; /* n */
    GM_STACK_CHECK(&F, 609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    {
        {
            uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)a3) + 0));
            unsigned __int128 t6 = gm_f_num_ext_348kmbu(t3, t4, t5);
            gm_l9 = t6;
        }
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t9 != ((uint8_t)3ULL)) goto mn8;
            {
                uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a3) + 16));
                if (!t12) goto ie11;
                {
                    gm_b16_8 t14 = gm_l8;
                    gm_f_fill_abmf5g5(t14, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t15;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t15);
                    goto gm_fdone;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
            }
            {
                void * t16 = gm_l0;
                gm_t88rcucg1b7uk t17 = gm_l4;
                uint64_t t18 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t19 = gm_f_us_cuv01fp(t18);
                gm_l11 = t19;
            }
            {
                void * t20 = gm_l0;
                void * t21 = ((uint8_t *)&gm_l10);
                uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t23 = gm_l11;
                uint64_t t24 = (uintptr_t)t23;
                if (t22 > t24 || t24 > (uintptr_t)16) gm_trap_slice(t22, t24, 16, gm_srcqod65l394oc8, 149, 24);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + t22 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
                unsigned __int128 t26 = gm_l9;
                gm_f_wr_uint_rs9ado5(t20, t25, t26);
            }
            {
                void * t27 = gm_l0;
                gm_t88rcucg1b7uk t28 = gm_l7;
                gm_b16_8 t29 = gm_l8;
                void * t30 = ((uint8_t *)&gm_l10);
                uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t32 = gm_l11;
                uint64_t t33 = (uintptr_t)t32;
                if (t31 > t33 || t33 > (uintptr_t)16) gm_trap_slice(t31, t33, 16, gm_srcqod65l394oc8, 150, 38);
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t30 + t31 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t31));
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                gm_b0_1 t36 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t27, t28, t29, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me7;
mn8:;
            {
                uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)a3) + 16));
                uint8_t t41 = (uint8_t)!t40;
                if (!t41) goto ie39;
                {
                    void * t43 = gm_l0;
                    gm_b16_8 t44 = gm_l8;
                    unsigned __int128 t45 = gm_l9;
                    gm_f_wr_uint_rs9ado5(t43, t44, t45);
                }
                {
                    gm_b0_1 t46;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t46);
                    goto gm_fdone;
                }
                goto ix39;
ie39:;
ix39:;
            }
            {
                void * t47 = gm_l0;
                gm_b16_8 t48 = gm_l8;
                void * t49 = gm_l0;
                gm_tl874t2atnlh3 t50 = gm_l1;
                gm_t9qkh0rsooc8d t51 = gm_l2;
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                unsigned __int128 t54 = gm_l9;
                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)a3) + 17));
                gm_b16_8 t56 = gm_l6;
                gm_tdlro53ljvkl0 t57;
                gm_f_overflow_result_vqlhh7t((void *)&t57, (void *)NULL, cx, NULL, t49, t50, t51, t52, t53, t54, t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t57))[0] != 0)) {
                    gm_tcfa6l7reoosn t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t58 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t57) + 16)));
                    F.ret = t58;
                    goto gm_fret;
                }
                unsigned __int128 t59 = GM_LD(unsigned __int128, (((uint8_t *)&t57) + 16));
                gm_f_wr_uint_rs9ado5(t47, t48, t59);
            }
            goto me7;
mn37:;
            gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 141, 5);
me7:;
        }
    }
    gm_b0_1 t60;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t60);
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
__attribute__((aligned(4))) void gm_f_int_arith_is3k5b1(void *gm_out, gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_arith_is3k5b1 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    uint8_t gm_l1; /* signed */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* a */
    unsigned __int128 gm_l4; /* c */
    unsigned __int128 gm_l5; /* mask */
    unsigned __int128 gm_l6; /* r */
    uint8_t gm_l7; /* over */
    unsigned __int128 gm_l8; /* p */
    unsigned __int128 gm_l9; /* p */
    uint8_t gm_l10; /* over */
    __int128 gm_l11; /* x */
    __int128 gm_l12; /* z */
    __int128 gm_l13; /* lo */
    __int128 gm_l14; /* hi */
    __int128 gm_l15; /* r */
    uint8_t gm_l16; /* over */
    __int128 gm_l17; /* r */
    uint8_t gm_l18; /* over */
    __int128 gm_l19; /* p */
    __int128 gm_l20; /* o */
    __int128 gm_l21; /* p */
    __int128 gm_l22; /* p */
    uint8_t gm_l23; /* over */
    GM_STACK_CHECK_SHALLOW(&F, 2502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = gm_l1;
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            uint32_t t6 = gm_l2;
            unsigned __int128 t7 = gm_f_num_mask_do3naah(t6);
            gm_l5 = t7;
        }
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t13) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; default: goto ma11; }
ma9:;
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t15 != ((uint8_t)0ULL)) goto mn14;
            {
                unsigned __int128 t17 = gm_l3;
                unsigned __int128 t18 = gm_l4;
                unsigned __int128 r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
                unsigned __int128 t21 = r19;
                gm_l6 = t21;
            }
            {
                uint8_t t22;
                uint32_t t24 = gm_l2;
                uint8_t t25 = (t24 >= ((uint32_t)128ULL));
                if (!t25) goto ie23;
                unsigned __int128 t27 = gm_l6;
                unsigned __int128 t28 = gm_l3;
                uint8_t t29 = (t27 < t28);
                uint8_t t30 = t29;
                t22 = t30;
                goto ix23;
ie23:;
                unsigned __int128 t32 = gm_l6;
                unsigned __int128 t33 = gm_l5;
                uint8_t t34 = (t32 > t33);
                uint8_t t35 = t34;
                t22 = t35;
ix23:;
                gm_l7 = t22;
            }
            {
                memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                unsigned __int128 t36 = gm_l6;
                unsigned __int128 t37 = gm_l5;
                unsigned __int128 t38 = (unsigned __int128)(t36 & t37);
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t38);
                uint8_t t39 = gm_l7;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t39);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)0));
                goto gm_fdone;
            }
            goto me8;
mn14:;
ma10:;
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t41 != ((uint8_t)1ULL)) goto mn40;
            memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
            unsigned __int128 t42 = gm_l3;
            unsigned __int128 t43 = gm_l4;
            unsigned __int128 r44; bool o45 = __builtin_sub_overflow(t42, t43, &r44);
            unsigned __int128 t46 = r44;
            unsigned __int128 t47 = gm_l5;
            unsigned __int128 t48 = (unsigned __int128)(t46 & t47);
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t48);
            unsigned __int128 t49 = gm_l3;
            unsigned __int128 t50 = gm_l4;
            uint8_t t51 = (t49 < t50);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t51);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)1));
            goto gm_fdone;
            goto me8;
mn40:;
ma11:;
            {
                uint32_t t55 = gm_l2;
                uint8_t t56 = (t55 <= ((uint32_t)64ULL));
                if (!t56) goto ie54;
                {
                    unsigned __int128 t58 = gm_l3;
                    unsigned __int128 t59 = gm_l4;
                    unsigned __int128 r60; bool o61 = __builtin_mul_overflow(t58, t59, &r60);
                    if (o61) gm_trap("integer overflow in `*`", gm_srcqod65l394oc8, 82, 26);
                    unsigned __int128 t62 = r60;
                    gm_l8 = t62;
                }
                {
                    memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                    unsigned __int128 t63 = gm_l8;
                    unsigned __int128 t64 = gm_l5;
                    unsigned __int128 t65 = (unsigned __int128)(t63 & t64);
                    GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t65);
                    unsigned __int128 t66 = gm_l8;
                    unsigned __int128 t67 = gm_l5;
                    uint8_t t68 = (t66 > t67);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t68);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix54;
ie54:;
ix54:;
            }
            {
                unsigned __int128 t69 = gm_l3;
                unsigned __int128 t70 = gm_l4;
                unsigned __int128 r71; bool o72 = __builtin_mul_overflow(t69, t70, &r71);
                unsigned __int128 t73 = r71;
                gm_l9 = t73;
            }
            {
                uint8_t t74 = 0;
                unsigned __int128 t75 = gm_l3;
                uint8_t t76 = (!(t75 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (t76) {
                    uint8_t t77 = 0;
                    unsigned __int128 t78 = gm_l9;
                    unsigned __int128 t79 = gm_l3;
                    if (t79 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 86, 41);
                    unsigned __int128 t80 = (unsigned __int128)(t78 / t79);
                    unsigned __int128 t81 = gm_l4;
                    uint8_t t82 = (!(t80 == t81));
                    if (!t82) {
                        unsigned __int128 t83 = gm_l9;
                        unsigned __int128 t84 = gm_l5;
                        uint8_t t85 = (t83 > t84);
                        t77 = t85;
                    } else t77 = 1;
                    t74 = t77;
                } else t74 = 0;
                gm_l10 = t74;
            }
            {
                memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                unsigned __int128 t86 = gm_l9;
                unsigned __int128 t87 = gm_l5;
                unsigned __int128 t88 = (unsigned __int128)(t86 & t87);
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t88);
                uint8_t t89 = gm_l10;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t89);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)0));
                goto gm_fdone;
            }
            goto me8;
mn52:;
ma12:;
            gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 73, 9);
me8:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        unsigned __int128 t90 = gm_l3;
        __int128 t91;
        memset(&t91, 0, sizeof t91); memcpy(&t91, ((uint8_t *)&t90), sizeof t91 < 16 ? sizeof t91 : 16);
        gm_l11 = t91;
    }
    {
        unsigned __int128 t92 = gm_l4;
        __int128 t93;
        memset(&t93, 0, sizeof t93); memcpy(&t93, ((uint8_t *)&t92), sizeof t93 < 16 ? sizeof t93 : 16);
        gm_l12 = t93;
    }
    {
        uint32_t t94 = gm_l2;
        __int128 t95 = gm_f_int_lo_i8jbm6e(t94);
        gm_l13 = t95;
    }
    {
        uint32_t t96 = gm_l2;
        __int128 t97 = gm_f_int_hi_qa5fgoo(t96);
        gm_l14 = t97;
    }
    {
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t103) { case ((uint8_t)0ULL): goto ma99; case ((uint8_t)1ULL): goto ma100; case ((uint8_t)2ULL): goto ma101; case ((uint8_t)3ULL): goto ma101; case ((uint8_t)4ULL): goto ma101; case ((uint8_t)5ULL): goto ma101; case ((uint8_t)6ULL): goto ma101; case ((uint8_t)7ULL): goto ma101; case ((uint8_t)8ULL): goto ma101; case ((uint8_t)9ULL): goto ma101; case ((uint8_t)10ULL): goto ma101; case ((uint8_t)11ULL): goto ma101; case ((uint8_t)12ULL): goto ma101; case ((uint8_t)13ULL): goto ma101; default: goto ma101; }
ma99:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t105 != ((uint8_t)0ULL)) goto mn104;
        {
            __int128 t107 = gm_l11;
            __int128 t108 = gm_l12;
            __int128 r109; bool o110 = __builtin_add_overflow(t107, t108, &r109);
            __int128 t111 = r109;
            gm_l15 = t111;
        }
        {
            uint8_t t112;
            uint32_t t114 = gm_l2;
            uint8_t t115 = (t114 >= ((uint32_t)128ULL));
            if (!t115) goto ie113;
            __int128 t117 = gm_l11;
            __int128 t118 = gm_l15;
            __int128 t119 = (__int128)(t117 ^ t118);
            __int128 t120 = gm_l12;
            __int128 t121 = gm_l15;
            __int128 t122 = (__int128)(t120 ^ t121);
            __int128 t123 = (__int128)(t119 & t122);
            uint8_t t124 = (t123 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            uint8_t t125 = t124;
            t112 = t125;
            goto ix113;
ie113:;
            uint8_t t127 = 0;
            __int128 t128 = gm_l15;
            __int128 t129 = gm_l13;
            uint8_t t130 = (t128 < t129);
            if (!t130) {
                __int128 t131 = gm_l15;
                __int128 t132 = gm_l14;
                uint8_t t133 = (t131 > t132);
                t127 = t133;
            } else t127 = 1;
            uint8_t t134 = t127;
            t112 = t134;
ix113:;
            gm_l16 = t112;
        }
        {
            memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
            uint32_t t135 = gm_l2;
            __int128 t136 = gm_l15;
            unsigned __int128 t137;
            memset(&t137, 0, sizeof t137); memcpy(&t137, ((uint8_t *)&t136), sizeof t137 < 16 ? sizeof t137 : 16);
            unsigned __int128 t138 = gm_f_num_ext_348kmbu(((uint8_t)1), t135, t137);
            uint8_t t139 = gm_l16;
            __int128 t140 = gm_l12;
            uint8_t t141 = (t140 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t138);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t139);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, t141);
            goto gm_fdone;
        }
        goto me98;
mn104:;
ma100:;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t143 != ((uint8_t)1ULL)) goto mn142;
        {
            __int128 t145 = gm_l11;
            __int128 t146 = gm_l12;
            __int128 r147; bool o148 = __builtin_sub_overflow(t145, t146, &r147);
            __int128 t149 = r147;
            gm_l17 = t149;
        }
        {
            uint8_t t150;
            uint32_t t152 = gm_l2;
            uint8_t t153 = (t152 >= ((uint32_t)128ULL));
            if (!t153) goto ie151;
            __int128 t155 = gm_l11;
            __int128 t156 = gm_l12;
            __int128 t157 = (__int128)(t155 ^ t156);
            __int128 t158 = gm_l11;
            __int128 t159 = gm_l17;
            __int128 t160 = (__int128)(t158 ^ t159);
            __int128 t161 = (__int128)(t157 & t160);
            uint8_t t162 = (t161 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            uint8_t t163 = t162;
            t150 = t163;
            goto ix151;
ie151:;
            uint8_t t165 = 0;
            __int128 t166 = gm_l17;
            __int128 t167 = gm_l13;
            uint8_t t168 = (t166 < t167);
            if (!t168) {
                __int128 t169 = gm_l17;
                __int128 t170 = gm_l14;
                uint8_t t171 = (t169 > t170);
                t165 = t171;
            } else t165 = 1;
            uint8_t t172 = t165;
            t150 = t172;
ix151:;
            gm_l18 = t150;
        }
        {
            memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
            uint32_t t173 = gm_l2;
            __int128 t174 = gm_l17;
            unsigned __int128 t175;
            memset(&t175, 0, sizeof t175); memcpy(&t175, ((uint8_t *)&t174), sizeof t175 < 16 ? sizeof t175 : 16);
            unsigned __int128 t176 = gm_f_num_ext_348kmbu(((uint8_t)1), t173, t175);
            uint8_t t177 = gm_l18;
            __int128 t178 = gm_l12;
            uint8_t t179 = (t178 > ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t176);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t177);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, t179);
            goto gm_fdone;
        }
        goto me98;
mn142:;
ma101:;
        {
            uint32_t t183 = gm_l2;
            uint8_t t184 = (t183 <= ((uint32_t)63ULL));
            if (!t184) goto ie182;
            {
                __int128 t186 = gm_l11;
                __int128 t187 = gm_l12;
                __int128 r188; bool o189 = __builtin_mul_overflow(t186, t187, &r188);
                if (o189) gm_trap("integer overflow in `*`", gm_srcqod65l394oc8, 108, 22);
                __int128 t190 = r188;
                gm_l19 = t190;
            }
            {
                memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                uint32_t t191 = gm_l2;
                __int128 t192 = gm_l19;
                unsigned __int128 t193;
                memset(&t193, 0, sizeof t193); memcpy(&t193, ((uint8_t *)&t192), sizeof t193 < 16 ? sizeof t193 : 16);
                unsigned __int128 t194 = gm_f_num_ext_348kmbu(((uint8_t)1), t191, t193);
                uint8_t t195 = 0;
                __int128 t196 = gm_l19;
                __int128 t197 = gm_l13;
                uint8_t t198 = (t196 < t197);
                if (!t198) {
                    __int128 t199 = gm_l19;
                    __int128 t200 = gm_l14;
                    uint8_t t201 = (t199 > t200);
                    t195 = t201;
                } else t195 = 1;
                __int128 t202 = gm_l19;
                uint8_t t203 = (t202 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t194);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t195);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, t203);
                goto gm_fdone;
            }
            goto ix182;
ie182:;
ix182:;
        }
        {
            uint8_t t205 = 0;
            __int128 t206 = gm_l11;
            uint8_t t207 = ((t206 == ((__int128)(((unsigned __int128)0u << 64) | 0u))));
            if (!t207) {
                __int128 t208 = gm_l12;
                uint8_t t209 = ((t208 == ((__int128)(((unsigned __int128)0u << 64) | 0u))));
                t205 = t209;
            } else t205 = 1;
            if (!t205) goto ie204;
            {
                memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix204;
ie204:;
ix204:;
        }
        {
            uint8_t t212 = 0;
            __int128 t213 = gm_l11;
            uint8_t t214 = ((t213 == ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 18446744073709551615u))));
            if (!t214) {
                __int128 t215 = gm_l12;
                uint8_t t216 = ((t215 == ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 18446744073709551615u))));
                t212 = t216;
            } else t212 = 1;
            if (!t212) goto ie211;
            {
                __int128 t218;
                __int128 t220 = gm_l11;
                uint8_t t221 = ((t220 == ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 18446744073709551615u))));
                if (!t221) goto ie219;
                __int128 t223 = gm_l12;
                __int128 t224 = t223;
                t218 = t224;
                goto ix219;
ie219:;
                __int128 t226 = gm_l11;
                __int128 t227 = t226;
                t218 = t227;
ix219:;
                gm_l20 = t218;
            }
            {
                __int128 t229 = gm_l20;
                __int128 t230 = gm_f_int_lo_i8jbm6e(((uint32_t)128ULL));
                uint8_t t231 = ((t229 == t230));
                if (!t231) goto ie228;
                {
                    memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                    __int128 t233 = gm_l20;
                    unsigned __int128 t234;
                    memset(&t234, 0, sizeof t234); memcpy(&t234, ((uint8_t *)&t233), sizeof t234 < 16 ? sizeof t234 : 16);
                    GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t234);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, ((uint8_t)1));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix228;
ie228:;
ix228:;
            }
            {
                __int128 t235 = gm_l20;
                if (t235 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 115, 22);
                __int128 t236 = (__int128)((unsigned __int128)0 - (unsigned __int128)t235);
                gm_l21 = t236;
            }
            {
                memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
                uint32_t t237 = gm_l2;
                __int128 t238 = gm_l21;
                unsigned __int128 t239;
                memset(&t239, 0, sizeof t239); memcpy(&t239, ((uint8_t *)&t238), sizeof t239 < 16 ? sizeof t239 : 16);
                unsigned __int128 t240 = gm_f_num_ext_348kmbu(((uint8_t)1), t237, t239);
                uint8_t t241 = 0;
                __int128 t242 = gm_l21;
                __int128 t243 = gm_l13;
                uint8_t t244 = (t242 < t243);
                if (!t244) {
                    __int128 t245 = gm_l21;
                    __int128 t246 = gm_l14;
                    uint8_t t247 = (t245 > t246);
                    t241 = t247;
                } else t241 = 1;
                __int128 t248 = gm_l21;
                uint8_t t249 = (t248 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t240);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t241);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, t249);
                goto gm_fdone;
            }
            goto ix211;
ie211:;
ix211:;
        }
        {
            __int128 t250 = gm_l11;
            __int128 t251 = gm_l12;
            __int128 r252; bool o253 = __builtin_mul_overflow(t250, t251, &r252);
            __int128 t254 = r252;
            gm_l22 = t254;
        }
        {
            uint8_t t255 = 0;
            uint8_t t256 = 0;
            __int128 t257 = gm_l22;
            __int128 t258 = gm_l11;
            if (t258 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 119, 21);
            if (t257 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t258 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcqod65l394oc8, 119, 21);
            __int128 t259 = (__int128)(t257 / t258);
            __int128 t260 = gm_l12;
            uint8_t t261 = (!(t259 == t260));
            if (!t261) {
                __int128 t262 = gm_l22;
                __int128 t263 = gm_l13;
                uint8_t t264 = (t262 < t263);
                t256 = t264;
            } else t256 = 1;
            if (!t256) {
                __int128 t265 = gm_l22;
                __int128 t266 = gm_l14;
                uint8_t t267 = (t265 > t266);
                t255 = t267;
            } else t255 = 1;
            gm_l23 = t255;
        }
        {
            memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
            uint32_t t268 = gm_l2;
            __int128 t269 = gm_l22;
            unsigned __int128 t270;
            memset(&t270, 0, sizeof t270); memcpy(&t270, ((uint8_t *)&t269), sizeof t270 < 16 ? sizeof t270 : 16);
            unsigned __int128 t271 = gm_f_num_ext_348kmbu(((uint8_t)1), t268, t270);
            uint8_t t272 = gm_l23;
            __int128 t273 = gm_l11;
            uint8_t t274 = (t273 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            __int128 t275 = gm_l12;
            uint8_t t276 = (t275 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            uint8_t t277 = (!(t274 == t276));
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t271);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t272);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, t277);
            goto gm_fdone;
        }
        goto me98;
mn180:;
ma102:;
        gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 95, 5);
me98:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_arith_is3k5b1_c(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint32_t a2, unsigned __int128 a3, unsigned __int128 a4) { gm_t18m13lmi7mp6 gm_r; gm_f_int_arith_is3k5b1(&gm_r, a0, a1, a2, a3, a4); return GM_CONV(gm_csp8136jfauorf, gm_r); }
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_int_binop_qrbogd9 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* rt */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* dst */
    gm_ttovhfee8mf28 gm_l9; /* info */
    unsigned __int128 gm_l10; /* a */
    gm_t9qkh0rsooc8d gm_l11; /* fl */
    gm_t9qkh0rsooc8d gm_l12; /* fl */
    gm_t9qkh0rsooc8d gm_l13; /* fl */
    unsigned __int128 gm_l14; /* c */
    gm_t9qkh0rsooc8d gm_l15; /* fl */
    uint32_t gm_l16; /* n */
    uint32_t gm_l17; /* n */
    unsigned __int128 gm_l18; /* c */
    uint8_t gm_l19; /* is_div */
    __int128 gm_l20; /* p */
    __int128 gm_l21; /* q */
    GM_STACK_CHECK(&F, 2202u);
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
    gm_l8 = a8;
    {
        {
            void * t3 = gm_l0;
            gm_t88rcucg1b7uk t4 = gm_l3;
            gm_tg5lql6itufk7 t5 = gm_f_int_info_ulcevdv(t3, GM_CONV(gm_csguankko4lmhd, t4));
            gm_ttovhfee8mf28 t6;
            if (((((uint8_t *)&t5))[8] != 0)) {
                gm_ttovhfee8mf28 t7 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t5) + 0));
                t6 = t7;
            } else {
                void * t8 = gm_l0;
                gm_tl874t2atnlh3 t9 = gm_l1;
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s5cmg64f6av8k));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(69));
                gm_t76123gfet73g t11;
                gm_f_trap_qh84l9d((void *)&t11, (void *)NULL, cx, NULL, t8, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t11);
                goto gm_fdone;
            }
            gm_l9 = t6;
        }
        {
            void * t12 = gm_l0;
            gm_ttovhfee8mf28 t13 = gm_l9;
            gm_b16_8 t14 = gm_l6;
            unsigned __int128 t15 = gm_f_rd_int_iuioodt(t12, t13, t14);
            gm_l10 = t15;
        }
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t26) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma24; default: goto ma24; }
ma17:;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t30 != ((uint8_t)0ULL)) goto pa29;
            gm_t9qkh0rsooc8d t31 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
            gm_l11 = t31;
            goto po28;
pa29:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t33 != ((uint8_t)1ULL)) goto pa32;
            gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
            gm_l11 = t34;
            goto po28;
pa32:;
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t36 != ((uint8_t)2ULL)) goto pa35;
            gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
            gm_l11 = t37;
            goto po28;
pa35:;
            goto mn27;
po28:;
            {
                void * t39 = gm_l0;
                gm_ttovhfee8mf28 t40 = gm_l9;
                gm_b16_8 t41 = gm_l7;
                unsigned __int128 t42 = gm_f_rd_int_iuioodt(t39, t40, t41);
                gm_l14 = t42;
            }
            {
                void * t43 = gm_l0;
                gm_tl874t2atnlh3 t44 = gm_l1;
                gm_t9qkh0rsooc8d t45 = gm_l11;
                gm_ts6oqppjh0ba3 t46 = gm_l2;
                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                unsigned __int128 t49 = gm_l10;
                unsigned __int128 t50 = gm_l14;
                gm_t18m13lmi7mp6 t51;
                gm_f_int_arith_is3k5b1((void *)&t51, t46, t47, t48, t49, t50);
                gm_t88rcucg1b7uk t52 = gm_l3;
                gm_ttovhfee8mf28 t53 = gm_l9;
                gm_ts6oqppjh0ba3 t54 = gm_l2;
                gm_b16_8 t55 = gm_f_op_sym_6olbepb(t54);
                gm_t88rcucg1b7uk t56 = gm_l5;
                gm_b16_8 t57 = gm_l8;
                gm_f_finish_int_jmjdqd4((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t43, t44, t45, &t51, t52, t53, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me16;
mn27:;
ma18:;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t59 != ((uint8_t)3ULL)) goto mn58;
            gm_t9qkh0rsooc8d t60 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
            gm_l15 = t60;
            {
                void * t62 = gm_l0;
                gm_tl874t2atnlh3 t63 = gm_l1;
                gm_t88rcucg1b7uk t64 = gm_l3;
                gm_t88rcucg1b7uk t65 = gm_l4;
                gm_b16_8 t66 = gm_l7;
                uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                gm_tcula66ipo514 t68;
                gm_f_shift_amount_aalipud((void *)&t68, (void *)NULL, cx, NULL, t62, t63, t64, t65, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t68))[0] != 0)) {
                    gm_tcfa6l7reoosn t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                    ((uint8_t *)&t69)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t69 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t68) + 8)));
                    F.ret = t69;
                    goto gm_fret;
                }
                uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)&t68) + 8));
                gm_l16 = t70;
            }
            {
                void * t71 = gm_l0;
                gm_tl874t2atnlh3 t72 = gm_l1;
                gm_t9qkh0rsooc8d t73 = gm_l15;
                uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                unsigned __int128 t76 = gm_l10;
                uint32_t t77 = gm_l16;
                gm_t18m13lmi7mp6 t78;
                gm_f_int_shl_b8ne661((void *)&t78, t74, t75, t76, t77);
                gm_t88rcucg1b7uk t79 = gm_l3;
                gm_ttovhfee8mf28 t80 = gm_l9;
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_si62ejtp0vbct));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(2));
                gm_t88rcucg1b7uk t82 = gm_l5;
                gm_b16_8 t83 = gm_l8;
                gm_f_finish_int_jmjdqd4((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t71, t72, t73, &t78, t79, t80, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me16;
mn58:;
ma19:;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t85 != ((uint8_t)6ULL)) goto mn84;
            {
                void * t87 = gm_l0;
                gm_tl874t2atnlh3 t88 = gm_l1;
                gm_t88rcucg1b7uk t89 = gm_l3;
                gm_t88rcucg1b7uk t90 = gm_l4;
                gm_b16_8 t91 = gm_l7;
                uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                gm_tcula66ipo514 t93;
                gm_f_shift_amount_aalipud((void *)&t93, (void *)NULL, cx, NULL, t87, t88, t89, t90, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t93))[0] != 0)) {
                    gm_tcfa6l7reoosn t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                    ((uint8_t *)&t94)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t94 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t93) + 8)));
                    F.ret = t94;
                    goto gm_fret;
                }
                uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&t93) + 8));
                gm_l17 = t95;
            }
            {
                uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                if (!t97) goto ie96;
                {
                    void * t99 = gm_l0;
                    gm_b16_8 t100 = gm_l8;
                    unsigned __int128 t101 = gm_l10;
                    __int128 t102;
                    memset(&t102, 0, sizeof t102); memcpy(&t102, ((uint8_t *)&t101), sizeof t102 < 16 ? sizeof t102 : 16);
                    uint32_t t103 = gm_l17;
                    unsigned __int128 n104 = (unsigned __int128)t103;
                    if (n104 >= 128) gm_trap_shift((uint64_t)n104, "i128", gm_srcqod65l394oc8, 187, 34);
                    __int128 t105 = (__int128)(t102 >> (int)n104);
                    unsigned __int128 t106;
                    memset(&t106, 0, sizeof t106); memcpy(&t106, ((uint8_t *)&t105), sizeof t106 < 16 ? sizeof t106 : 16);
                    gm_f_wr_uint_rs9ado5(t99, t100, t106);
                }
                goto ix96;
ie96:;
                {
                    void * t108 = gm_l0;
                    gm_b16_8 t109 = gm_l8;
                    unsigned __int128 t110 = gm_l10;
                    uint32_t t111 = gm_l17;
                    unsigned __int128 t112 = (unsigned __int128)t111;
                    unsigned __int128 n113 = (unsigned __int128)t112;
                    if (n113 >= 128) gm_trap_shift((uint64_t)n113, "u128", gm_srcqod65l394oc8, 189, 33);
                    unsigned __int128 t114 = (unsigned __int128)(t110 >> (int)n113);
                    gm_f_wr_uint_rs9ado5(t108, t109, t114);
                }
ix96:;
            }
            goto me16;
mn84:;
ma20:;
            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t118 != ((uint8_t)4ULL)) goto pa117;
            goto po116;
pa117:;
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t120 != ((uint8_t)5ULL)) goto pa119;
            goto po116;
pa119:;
            goto mn115;
po116:;
            {
                void * t122 = gm_l0;
                gm_ttovhfee8mf28 t123 = gm_l9;
                gm_b16_8 t124 = gm_l7;
                unsigned __int128 t125 = gm_f_rd_int_iuioodt(t122, t123, t124);
                gm_l18 = t125;
            }
            {
                unsigned __int128 t127 = gm_l18;
                uint8_t t128 = ((t127 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t128) goto ie126;
                {
                    void * t130 = gm_l0;
                    gm_tl874t2atnlh3 t131 = gm_l1;
                    gm_t76123gfet73g t132;
                    gm_f_div_zero_trap_ap1saqd((void *)&t132, (void *)NULL, cx, NULL, t130, t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t132);
                    goto gm_fdone;
                }
                goto ix126;
ie126:;
ix126:;
            }
            {
                uint8_t t133 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)4ULL)));
                gm_l19 = t133;
            }
            {
                uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                uint8_t t136 = (uint8_t)!t135;
                if (!t136) goto ie134;
                {
                    void * t138 = gm_l0;
                    gm_b16_8 t139 = gm_l8;
                    unsigned __int128 t140;
                    uint8_t t142 = gm_l19;
                    if (!t142) goto ie141;
                    unsigned __int128 t144 = gm_l10;
                    unsigned __int128 t145 = gm_l18;
                    if (t145 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 197, 45);
                    unsigned __int128 t146 = (unsigned __int128)(t144 / t145);
                    unsigned __int128 t147 = t146;
                    t140 = t147;
                    goto ix141;
ie141:;
                    unsigned __int128 t149 = gm_l10;
                    unsigned __int128 t150 = gm_l18;
                    if (t150 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 197, 60);
                    unsigned __int128 t151 = (unsigned __int128)(t149 % t150);
                    unsigned __int128 t152 = t151;
                    t140 = t152;
ix141:;
                    gm_f_wr_uint_rs9ado5(t138, t139, t140);
                }
                {
                    gm_b0_1 t153;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t153);
                    goto gm_fdone;
                }
                goto ix134;
ie134:;
ix134:;
            }
            {
                unsigned __int128 t154 = gm_l10;
                __int128 t155;
                memset(&t155, 0, sizeof t155); memcpy(&t155, ((uint8_t *)&t154), sizeof t155 < 16 ? sizeof t155 : 16);
                gm_l20 = t155;
            }
            {
                unsigned __int128 t156 = gm_l18;
                __int128 t157;
                memset(&t157, 0, sizeof t157); memcpy(&t157, ((uint8_t *)&t156), sizeof t157 < 16 ? sizeof t157 : 16);
                gm_l21 = t157;
            }
            {
                __int128 t159 = gm_l21;
                uint8_t t160 = ((t159 == ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 18446744073709551615u))));
                if (!t160) goto ie158;
                {
                    __int128 t163 = gm_l20;
                    uint32_t t164 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                    __int128 t165 = gm_f_int_lo_i8jbm6e(t164);
                    uint8_t t166 = ((t163 == t165));
                    if (!t166) goto ie162;
                    {
                        void * t168 = gm_l0;
                        gm_tl874t2atnlh3 t169 = gm_l1;
                        gm_ts6oqppjh0ba3 t170 = gm_l2;
                        gm_b16_8 t171 = gm_f_op_sym_6olbepb(t170);
                        gm_t76123gfet73g t172;
                        gm_f_overflow_trap_j2f8u18((void *)&t172, (void *)NULL, cx, NULL, t168, t169, t171);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t172);
                        goto gm_fdone;
                    }
                    goto ix162;
ie162:;
ix162:;
                }
                {
                    void * t173 = gm_l0;
                    gm_b16_8 t174 = gm_l8;
                    unsigned __int128 t175;
                    uint8_t t177 = gm_l19;
                    if (!t177) goto ie176;
                    __int128 t179 = gm_l20;
                    if (t179 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 204, 46);
                    __int128 t180 = (__int128)((unsigned __int128)0 - (unsigned __int128)t179);
                    unsigned __int128 t181;
                    memset(&t181, 0, sizeof t181); memcpy(&t181, ((uint8_t *)&t180), sizeof t181 < 16 ? sizeof t181 : 16);
                    unsigned __int128 t182 = t181;
                    t175 = t182;
                    goto ix176;
ie176:;
                    unsigned __int128 t184 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                    t175 = t184;
ix176:;
                    gm_f_wr_uint_rs9ado5(t173, t174, t175);
                }
                {
                    gm_b0_1 t185;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t185);
                    goto gm_fdone;
                }
                goto ix158;
ie158:;
ix158:;
            }
            {
                void * t186 = gm_l0;
                gm_b16_8 t187 = gm_l8;
                __int128 t188;
                uint8_t t190 = gm_l19;
                if (!t190) goto ie189;
                __int128 t192 = gm_l20;
                __int128 t193 = gm_l21;
                if (t193 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 207, 42);
                if (t192 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t193 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcqod65l394oc8, 207, 42);
                __int128 t194 = (__int128)(t192 / t193);
                __int128 t195 = t194;
                t188 = t195;
                goto ix189;
ie189:;
                __int128 t197 = gm_l20;
                __int128 t198 = gm_l21;
                if (t198 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 207, 57);
                if (t197 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t198 == (__int128)-1) gm_trap("integer overflow in `%`", gm_srcqod65l394oc8, 207, 57);
                __int128 t199 = (__int128)(t197 % t198);
                __int128 t200 = t199;
                t188 = t200;
ix189:;
                unsigned __int128 t201;
                memset(&t201, 0, sizeof t201); memcpy(&t201, ((uint8_t *)&t188), sizeof t201 < 16 ? sizeof t201 : 16);
                gm_f_wr_uint_rs9ado5(t186, t187, t201);
            }
            goto me16;
mn115:;
ma21:;
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t203 != ((uint8_t)7ULL)) goto mn202;
            void * t204 = gm_l0;
            gm_b16_8 t205 = gm_l8;
            unsigned __int128 t206 = gm_l10;
            void * t207 = gm_l0;
            gm_ttovhfee8mf28 t208 = gm_l9;
            gm_b16_8 t209 = gm_l7;
            unsigned __int128 t210 = gm_f_rd_int_iuioodt(t207, t208, t209);
            unsigned __int128 t211 = (unsigned __int128)(t206 & t210);
            gm_f_wr_uint_rs9ado5(t204, t205, t211);
            goto me16;
mn202:;
ma22:;
            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t213 != ((uint8_t)8ULL)) goto mn212;
            void * t214 = gm_l0;
            gm_b16_8 t215 = gm_l8;
            unsigned __int128 t216 = gm_l10;
            void * t217 = gm_l0;
            gm_ttovhfee8mf28 t218 = gm_l9;
            gm_b16_8 t219 = gm_l7;
            unsigned __int128 t220 = gm_f_rd_int_iuioodt(t217, t218, t219);
            unsigned __int128 t221 = (unsigned __int128)(t216 | t220);
            gm_f_wr_uint_rs9ado5(t214, t215, t221);
            goto me16;
mn212:;
ma23:;
            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t223 != ((uint8_t)9ULL)) goto mn222;
            void * t224 = gm_l0;
            gm_b16_8 t225 = gm_l8;
            unsigned __int128 t226 = gm_l10;
            void * t227 = gm_l0;
            gm_ttovhfee8mf28 t228 = gm_l9;
            gm_b16_8 t229 = gm_l7;
            unsigned __int128 t230 = gm_f_rd_int_iuioodt(t227, t228, t229);
            unsigned __int128 t231 = (unsigned __int128)(t226 ^ t230);
            gm_f_wr_uint_rs9ado5(t224, t225, t231);
            goto me16;
mn222:;
ma24:;
            void * t233 = gm_l0;
            gm_tl874t2atnlh3 t234 = gm_l1;
            gm_b16_8 t235;
            GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s9idfla83gb22));
            GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(58));
            gm_t76123gfet73g t236;
            gm_f_trap_qh84l9d((void *)&t236, (void *)NULL, cx, NULL, t233, t234, t235);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t236);
            goto gm_fdone;
            goto me16;
mn232:;
ma25:;
            gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 175, 5);
me16:;
        }
    }
    gm_b0_1 t237;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t237);
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
inline __attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_cmp_kb3sm6u F;
    uint8_t gm_l0; /* signed */
    unsigned __int128 gm_l1; /* x */
    unsigned __int128 gm_l2; /* y */
    __int128 gm_l3; /* p */
    __int128 gm_l4; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l0;
        if (!t3) goto ie2;
        {
            unsigned __int128 t5 = gm_l1;
            __int128 t6;
            memset(&t6, 0, sizeof t6); memcpy(&t6, ((uint8_t *)&t5), sizeof t6 < 16 ? sizeof t6 : 16);
            gm_l3 = t6;
        }
        {
            unsigned __int128 t7 = gm_l2;
            __int128 t8;
            memset(&t8, 0, sizeof t8); memcpy(&t8, ((uint8_t *)&t7), sizeof t8 < 16 ? sizeof t8 : 16);
            gm_l4 = t8;
        }
        {
            int32_t t9;
            __int128 t11 = gm_l3;
            __int128 t12 = gm_l4;
            uint8_t t13 = (t11 < t12);
            if (!t13) goto ie10;
            int32_t t15 = ((int32_t)-1LL);
            t9 = t15;
            goto ix10;
ie10:;
            int32_t t16;
            __int128 t18 = gm_l3;
            __int128 t19 = gm_l4;
            uint8_t t20 = (t18 > t19);
            if (!t20) goto ie17;
            int32_t t22 = ((int32_t)1LL);
            t16 = t22;
            goto ix17;
ie17:;
            int32_t t24 = ((int32_t)0LL);
            t16 = t24;
ix17:;
            t9 = t16;
ix10:;
            uint8_t t25 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        int32_t t26;
        unsigned __int128 t28 = gm_l1;
        unsigned __int128 t29 = gm_l2;
        uint8_t t30 = (t28 < t29);
        if (!t30) goto ie27;
        int32_t t32 = ((int32_t)-1LL);
        t26 = t32;
        goto ix27;
ie27:;
        int32_t t33;
        unsigned __int128 t35 = gm_l1;
        unsigned __int128 t36 = gm_l2;
        uint8_t t37 = (t35 > t36);
        if (!t37) goto ie34;
        int32_t t39 = ((int32_t)1LL);
        t33 = t39;
        goto ix34;
ie34:;
        int32_t t41 = ((int32_t)0LL);
        t33 = t41;
ix34:;
        t26 = t33;
ix27:;
        uint8_t t42 = 0;
        F.ret = t26;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) __int128 gm_f_int_hi_qa5fgoo(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_hi_qa5fgoo F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((uint32_t)128ULL));
        if (!t4) goto ie2;
        {
            unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            unsigned __int128 n7 = (unsigned __int128)((uint64_t)1ULL);
            if (n7 >= 128) gm_trap_shift((uint64_t)n7, "u128", gm_srcqod65l394oc8, 12, 30);
            unsigned __int128 t8 = (unsigned __int128)(t6 >> (int)n7);
            __int128 t9;
            memset(&t9, 0, sizeof t9); memcpy(&t9, ((uint8_t *)&t8), sizeof t9 < 16 ? sizeof t9 : 16);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t11 = gm_l0;
        uint32_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint32_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 13, 25);
        uint32_t t14 = r12;
        unsigned __int128 n15 = (unsigned __int128)t14;
        if (n15 >= 128) gm_trap_shift((uint64_t)n15, "i128", gm_srcqod65l394oc8, 13, 13);
        __int128 r16 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n15);
        bool o17 = (__int128)(r16 >> (int)n15) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o17) gm_trap("integer overflow in `<<`", gm_srcqod65l394oc8, 13, 13);
        __int128 t18 = r16;
        __int128 r19; bool o20 = __builtin_sub_overflow(t18, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 13, 12);
        __int128 t21 = r19;
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_lo_i8jbm6e F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((uint32_t)128ULL));
        if (!t4) goto ie2;
        {
            unsigned __int128 n6 = (unsigned __int128)((uint64_t)127ULL);
            if (n6 >= 128) gm_trap_shift((uint64_t)n6, "u128", gm_srcqod65l394oc8, 6, 30);
            unsigned __int128 r7 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n6);
            bool o8 = (unsigned __int128)(r7 >> (int)n6) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o8) gm_trap("integer overflow in `<<`", gm_srcqod65l394oc8, 6, 30);
            unsigned __int128 t9 = r7;
            __int128 t10;
            memset(&t10, 0, sizeof t10); memcpy(&t10, ((uint8_t *)&t9), sizeof t10 < 16 ? sizeof t10 : 16);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t12 = gm_l0;
        uint32_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint32_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 7, 26);
        uint32_t t15 = r13;
        unsigned __int128 n16 = (unsigned __int128)t15;
        if (n16 >= 128) gm_trap_shift((uint64_t)n16, "i128", gm_srcqod65l394oc8, 7, 14);
        __int128 r17 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n16);
        bool o18 = (__int128)(r17 >> (int)n16) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o18) gm_trap("integer overflow in `<<`", gm_srcqod65l394oc8, 7, 14);
        __int128 t19 = r17;
        if (t19 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 7, 12);
        __int128 t20 = (__int128)((unsigned __int128)0 - (unsigned __int128)t19);
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_int_pow_wrapped_5mjnoh4(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_pow_wrapped_5mjnoh4 F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    unsigned __int128 gm_l2; /* base */
    uint32_t gm_l3; /* n */
    gm_ts6oqppjh0ba3 gm_l4; /* mulop */
    unsigned __int128 gm_l5; /* result */
    unsigned __int128 gm_l6; /* b */
    uint32_t gm_l7; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t9qkh0rsooc8d t2;
        GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
        gm_ts6oqppjh0ba3 t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t3 + 1), t2);
        gm_l4 = t3;
    }
    {
        gm_l5 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    }
    {
        unsigned __int128 t4 = gm_l2;
        gm_l6 = t4;
    }
    {
        uint32_t t5 = gm_l3;
        gm_l7 = t5;
    }
    {
        for (;;) {
            uint32_t t7 = gm_l7;
            uint8_t t8 = (t7 > ((uint32_t)0ULL));
            if (!t8) goto lb6;
            {
                uint32_t t12 = gm_l7;
                uint32_t t13 = (uint32_t)(t12 & ((uint32_t)1ULL));
                uint8_t t14 = ((t13 == ((uint32_t)1ULL)));
                if (!t14) goto ie11;
                {
                    gm_ts6oqppjh0ba3 t16 = gm_l4;
                    uint8_t t17 = gm_l0;
                    uint32_t t18 = gm_l1;
                    unsigned __int128 t19 = gm_l5;
                    unsigned __int128 t20 = gm_l6;
                    gm_t18m13lmi7mp6 t21;
                    gm_f_int_arith_is3k5b1((void *)&t21, t16, t17, t18, t19, t20);
                    unsigned __int128 t22 = GM_LD(unsigned __int128, (((uint8_t *)&t21) + 0));
                    gm_l5 = t22;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                gm_ts6oqppjh0ba3 t23 = gm_l4;
                uint8_t t24 = gm_l0;
                uint32_t t25 = gm_l1;
                unsigned __int128 t26 = gm_l6;
                unsigned __int128 t27 = gm_l6;
                gm_t18m13lmi7mp6 t28;
                gm_f_int_arith_is3k5b1((void *)&t28, t23, t24, t25, t26, t27);
                unsigned __int128 t29 = GM_LD(unsigned __int128, (((uint8_t *)&t28) + 0));
                gm_l6 = t29;
            }
            {
                uint32_t t30 = gm_l7;
                unsigned __int128 n31 = (unsigned __int128)((uint64_t)1ULL);
                if (n31 >= 32) gm_trap_shift((uint64_t)n31, "u32", gm_srcqod65l394oc8, 290, 9);
                uint32_t t32 = (uint32_t)(t30 >> (int)n31);
                gm_l7 = t32;
            }
lc6:;
        }
lb6:;
    }
    {
        uint8_t t33 = gm_l0;
        uint32_t t34 = gm_l1;
        unsigned __int128 t35 = gm_l5;
        unsigned __int128 t36 = gm_f_num_ext_348kmbu(t33, t34, t35);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_shl_b8ne661(void *gm_out, uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_shl_b8ne661 F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    unsigned __int128 gm_l2; /* a */
    uint32_t gm_l3; /* n */
    unsigned __int128 gm_l4; /* k */
    unsigned __int128 gm_l5; /* low */
    __int128 gm_l6; /* v */
    __int128 gm_l7; /* w */
    __int128 gm_l8; /* wrapped */
    GM_STACK_CHECK_SHALLOW(&F, 619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        unsigned __int128 t3 = (unsigned __int128)t2;
        gm_l4 = t3;
    }
    {
        uint8_t t5 = gm_l0;
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie4;
        {
            unsigned __int128 t8 = gm_l2;
            unsigned __int128 t9 = gm_l4;
            unsigned __int128 n10 = (unsigned __int128)t9;
            if (n10 >= 128) gm_trap_shift((uint64_t)n10, "u128", gm_srcqod65l394oc8, 129, 17);
            unsigned __int128 r11 = (unsigned __int128)((unsigned __int128)t8 << (int)n10);
            bool o12 = (unsigned __int128)(r11 >> (int)n10) != t8;
            unsigned __int128 t13 = r11;
            uint32_t t14 = gm_l1;
            unsigned __int128 t15 = gm_f_num_mask_do3naah(t14);
            unsigned __int128 t16 = (unsigned __int128)(t13 & t15);
            gm_l5 = t16;
        }
        {
            memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
            unsigned __int128 t17 = gm_l5;
            unsigned __int128 t18 = gm_l5;
            unsigned __int128 t19 = gm_l4;
            unsigned __int128 n20 = (unsigned __int128)t19;
            if (n20 >= 128) gm_trap_shift((uint64_t)n20, "u128", gm_srcqod65l394oc8, 130, 39);
            unsigned __int128 t21 = (unsigned __int128)(t18 >> (int)n20);
            unsigned __int128 t22 = gm_l2;
            uint8_t t23 = (!(t21 == t22));
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t17);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t23);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, ((uint8_t)0));
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        unsigned __int128 t24 = gm_l2;
        __int128 t25;
        memset(&t25, 0, sizeof t25); memcpy(&t25, ((uint8_t *)&t24), sizeof t25 < 16 ? sizeof t25 : 16);
        gm_l6 = t25;
    }
    {
        __int128 t26 = gm_l6;
        unsigned __int128 t27 = gm_l4;
        unsigned __int128 n28 = (unsigned __int128)t27;
        if (n28 >= 128) gm_trap_shift((uint64_t)n28, "i128", gm_srcqod65l394oc8, 133, 10);
        __int128 r29 = (__int128)((unsigned __int128)t26 << (int)n28);
        bool o30 = (__int128)(r29 >> (int)n28) != t26;
        __int128 t31 = r29;
        gm_l7 = t31;
    }
    {
        __int128 t32;
        uint32_t t34 = gm_l1;
        uint8_t t35 = (t34 >= ((uint32_t)128ULL));
        if (!t35) goto ie33;
        __int128 t37 = gm_l7;
        __int128 t38 = t37;
        t32 = t38;
        goto ix33;
ie33:;
        __int128 t40 = gm_l7;
        uint32_t t41 = gm_l1;
        uint32_t r42; bool o43 = __builtin_sub_overflow(((uint32_t)128ULL), t41, &r42);
        if (o43) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 134, 52);
        uint32_t t44 = r42;
        unsigned __int128 n45 = (unsigned __int128)t44;
        if (n45 >= 128) gm_trap_shift((uint64_t)n45, "i128", gm_srcqod65l394oc8, 134, 45);
        __int128 r46 = (__int128)((unsigned __int128)t40 << (int)n45);
        bool o47 = (__int128)(r46 >> (int)n45) != t40;
        __int128 t48 = r46;
        uint32_t t49 = gm_l1;
        uint32_t r50; bool o51 = __builtin_sub_overflow(((uint32_t)128ULL), t49, &r50);
        if (o51) gm_trap("integer overflow in `-`", gm_srcqod65l394oc8, 134, 69);
        uint32_t t52 = r50;
        unsigned __int128 n53 = (unsigned __int128)t52;
        if (n53 >= 128) gm_trap_shift((uint64_t)n53, "i128", gm_srcqod65l394oc8, 134, 44);
        __int128 t54 = (__int128)(t48 >> (int)n53);
        __int128 t55 = t54;
        t32 = t55;
ix33:;
        gm_l8 = t32;
    }
    {
        memset((uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 18, 0, 14);
        __int128 t56 = gm_l8;
        unsigned __int128 t57;
        memset(&t57, 0, sizeof t57); memcpy(&t57, ((uint8_t *)&t56), sizeof t57 < 16 ? sizeof t57 : 16);
        __int128 t58 = gm_l8;
        unsigned __int128 t59 = gm_l4;
        unsigned __int128 n60 = (unsigned __int128)t59;
        if (n60 >= 128) gm_trap_shift((uint64_t)n60, "i128", gm_srcqod65l394oc8, 135, 50);
        __int128 t61 = (__int128)(t58 >> (int)n60);
        __int128 t62 = gm_l6;
        uint8_t t63 = (!(t61 == t62));
        __int128 t64 = gm_l6;
        uint8_t t65 = (t64 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 0, t57);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 16, t63);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t18m13lmi7mp6 *)gm_out) + 17, t65);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csp8136jfauorf gm_f_int_shl_b8ne661_c(uint8_t a0, uint32_t a1, unsigned __int128 a2, uint32_t a3) { gm_t18m13lmi7mp6 gm_r; gm_f_int_shl_b8ne661(&gm_r, a0, a1, a2, a3); return GM_CONV(gm_csp8136jfauorf, gm_r); }
inline __attribute__((aligned(4))) void gm_f_overflow_result_vqlhh7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, unsigned __int128 a5, uint8_t a6, gm_b16_8 a7) {
    struct gm_fr_overflow_result_vqlhh7t F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    uint8_t gm_l3; /* signed */
    uint32_t gm_l4; /* bits */
    unsigned __int128 gm_l5; /* low */
    uint8_t gm_l6; /* below */
    gm_b16_8 gm_l7; /* sym */
    GM_STACK_CHECK(&F, 216u);
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
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t7) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma5; default: goto ma5; }
ma3:;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t9 != ((uint8_t)2ULL)) goto mn8;
        uint8_t t10 = gm_l3;
        uint32_t t11 = gm_l4;
        uint8_t t12 = gm_l6;
        unsigned __int128 t13 = gm_f_sat_bits_ug4cjjm(t10, t11, t12);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
        ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 0;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16, t13);
        goto gm_fdone;
        goto me2;
mn8:;
ma4:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        void * t16 = gm_l0;
        gm_tl874t2atnlh3 t17 = gm_l1;
        gm_b16_8 t18 = gm_l7;
        gm_tcfa6l7reoosn t19;
        gm_f_checked_overflow_ma1bm1g((void *)&t19, (void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_tdlro53ljvkl0 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 40, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t20 + 16), (*(gm_t76123gfet73g *)(((uint8_t *)&t19) + 8)));
            F.ret = t20;
            goto gm_fret;
        }
        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
        goto me2;
mn14:;
ma5:;
        goto me2;
mn22:;
ma6:;
        gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 39, 5);
me2:;
    }
    {
        unsigned __int128 t24 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
        ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 0;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16, t24);
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
extern __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_pow_7je2uvi F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* res_ty */
    gm_t88rcucg1b7uk gm_l5; /* ety */
    gm_b16_8 gm_l6; /* bb */
    gm_b16_8 gm_l7; /* nb */
    gm_b16_8 gm_l8; /* dst */
    gm_ttovhfee8mf28 gm_l9; /* info */
    uint8_t gm_l10; /* signed */
    uint32_t gm_l11; /* bits */
    unsigned __int128 gm_l12; /* base */
    gm_ttovhfee8mf28 gm_l13; /* einfo */
    unsigned __int128 gm_l14; /* nraw */
    uint32_t gm_l15; /* n */
    gm_t86h6hhm6d76n gm_l16; /* bval */
    gm_ts6oqppjh0ba3 gm_l17; /* mulop */
    unsigned __int128 gm_l18; /* acc */
    uint8_t gm_l19; /* over */
    uint32_t gm_l20; /* <tmp> */
    uint32_t gm_l21; /* <tmp> */
    gm_t18m13lmi7mp6 gm_l22; /* r */
    unsigned __int128 gm_l23; /* v */
    gm_t51b2en4qp02s gm_l24; /* pb */
    uint64_t gm_l25; /* sz */
    GM_STACK_CHECK(&F, 1529u);
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
    gm_l8 = a8;
    {
        {
            void * t3 = gm_l0;
            gm_t88rcucg1b7uk t4 = gm_l3;
            gm_tg5lql6itufk7 t5 = gm_f_int_info_ulcevdv(t3, GM_CONV(gm_csguankko4lmhd, t4));
            gm_ttovhfee8mf28 t6;
            if (((((uint8_t *)&t5))[8] != 0)) {
                gm_ttovhfee8mf28 t7 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t5) + 0));
                t6 = t7;
            } else {
                void * t8 = gm_l0;
                gm_tl874t2atnlh3 t9 = gm_l1;
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s2hnapin8228t));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(20));
                gm_t76123gfet73g t11;
                gm_f_trap_qh84l9d((void *)&t11, (void *)NULL, cx, NULL, t8, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t11);
                goto gm_fdone;
            }
            gm_l9 = t6;
        }
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            gm_l10 = t12;
        }
        {
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
            gm_l11 = t13;
        }
        {
            void * t14 = gm_l0;
            gm_t88rcucg1b7uk t15 = gm_l3;
            gm_b16_8 t16 = gm_l6;
            unsigned __int128 t17 = gm_f_get_int_pg7p4gc(t14, GM_CONV(gm_csguankko4lmhd, t15), t16);
            gm_l12 = t17;
        }
        {
            void * t18 = gm_l0;
            gm_t88rcucg1b7uk t19 = gm_l5;
            gm_tg5lql6itufk7 t20 = gm_f_int_info_ulcevdv(t18, GM_CONV(gm_csguankko4lmhd, t19));
            gm_ttovhfee8mf28 t21;
            if (((((uint8_t *)&t20))[8] != 0)) {
                gm_ttovhfee8mf28 t22 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t20) + 0));
                t21 = t22;
            } else {
                void * t23 = gm_l0;
                gm_tl874t2atnlh3 t24 = gm_l1;
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s0k8sea1jhldg));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(20));
                gm_t76123gfet73g t26;
                gm_f_trap_qh84l9d((void *)&t26, (void *)NULL, cx, NULL, t23, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t26);
                goto gm_fdone;
            }
            gm_l13 = t21;
        }
        {
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l5;
            gm_b16_8 t29 = gm_l7;
            unsigned __int128 t30 = gm_f_get_int_pg7p4gc(t27, GM_CONV(gm_csguankko4lmhd, t28), t29);
            gm_l14 = t30;
        }
        {
            gm_l15 = ((uint32_t)0ULL);
        }
        {
            uint8_t t32 = 0;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            if (t33) {
                unsigned __int128 t34 = gm_l14;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)127ULL);
                if (n35 >= 128) gm_trap_shift((uint64_t)n35, "u128", gm_srcqod65l394oc8, 236, 31);
                unsigned __int128 t36 = (unsigned __int128)(t34 >> (int)n35);
                uint8_t t37 = ((t36 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                t32 = t37;
            } else t32 = 0;
            uint8_t t38 = (uint8_t)!t32;
            if (!t38) goto ie31;
            {
                unsigned __int128 t40 = gm_l14;
                uint32_t t41 = (uint32_t)~((uint32_t)0ULL);
                unsigned __int128 t42 = (unsigned __int128)t41;
                unsigned __int128 t43 = (t40 < t42 ? t40 : t42);
                uint32_t t44 = (uint32_t)t43;
                gm_l15 = t44;
            }
            goto ix31;
ie31:;
ix31:;
        }
        {
            uint8_t t45 = gm_l10;
            unsigned __int128 t46 = gm_l12;
            gm_f_num_val_i3upeur((void *)&gm_l16, t45, t46);
        }
        {
            gm_t9qkh0rsooc8d t47;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 1), t47);
            gm_l17 = t48;
        }
        {
            gm_l18 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        }
        {
            gm_l19 = ((uint8_t)0);
        }
        {
            uint32_t t50 = gm_l15;
            uint8_t t51 = ((t50 == ((uint32_t)0ULL)));
            if (!t51) goto ie49;
            {
                gm_l18 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            }
            goto ix49;
ie49:;
            unsigned __int128 t54 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l16) + 16));
            uint8_t t55 = ((t54 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
            if (!t55) goto ie53;
            {
                gm_l18 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            goto ix53;
ie53:;
            unsigned __int128 t58 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l16) + 16));
            uint8_t t59 = ((t58 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
            if (!t59) goto ie57;
            {
                unsigned __int128 t61;
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                if (!t63) goto ie62;
                uint32_t t64 = gm_l15;
                if (((uint32_t)2ULL) == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 247, 31);
                uint32_t t65 = (uint32_t)(t64 % ((uint32_t)2ULL));
                uint8_t t66 = ((t65 == ((uint32_t)1ULL)));
                if (!t66) goto ie62;
                unsigned __int128 r68; bool o69 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r68);
                unsigned __int128 t70 = r68;
                unsigned __int128 t71 = t70;
                t61 = t71;
                goto ix62;
ie62:;
                unsigned __int128 t73 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                t61 = t73;
ix62:;
                gm_l18 = t61;
            }
            goto ix57;
ie57:;
            {
                {
                    gm_l21 = ((uint32_t)0ULL);
                }
                {
                    uint32_t t76 = gm_l15;
                    gm_l20 = t76;
                }
                {
                    for (;;) {
                        uint32_t t78 = gm_l21;
                        uint32_t t79 = gm_l20;
                        uint8_t t80 = (t78 < t79);
                        if (!t80) goto lb77;
                        {
                            uint32_t t83 = gm_l21;
                            {
                                gm_ts6oqppjh0ba3 t87 = gm_l17;
                                uint8_t t88 = gm_l10;
                                uint32_t t89 = gm_l11;
                                unsigned __int128 t90 = gm_l18;
                                unsigned __int128 t91 = gm_l12;
                                gm_f_int_arith_is3k5b1((void *)&gm_l22, t87, t88, t89, t90, t91);
                            }
                            {
                                unsigned __int128 t92 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l22) + 0));
                                gm_l18 = t92;
                            }
                            {
                                uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 16));
                                if (!t94) goto ie93;
                                {
                                    gm_l19 = ((uint8_t)1);
                                }
                                {
                                    goto lb77;
                                }
                                goto ix93;
ie93:;
ix93:;
                            }
                            goto me84;
mn85:;
                            gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 249, 9);
me84:;
                        }
lc77:;
                        {
                            uint32_t t97 = gm_l21;
                            uint32_t r98; bool o99 = __builtin_add_overflow(t97, ((uint32_t)1ULL), &r98);
                            uint32_t t100 = r98;
                            gm_l21 = t100;
                        }
                    }
lb77:;
                }
            }
ix57:;
ix53:;
ix49:;
        }
        {
            uint8_t t102 = gm_l19;
            uint8_t t103 = (uint8_t)!t102;
            if (!t103) goto ie101;
            {
                uint8_t t105 = gm_l10;
                uint32_t t106 = gm_l11;
                unsigned __int128 t107 = gm_l18;
                unsigned __int128 t108 = gm_f_num_ext_348kmbu(t105, t106, t107);
                gm_l23 = t108;
            }
            {
                uint8_t t110 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)3ULL)));
                if (!t110) goto ie109;
                {
                }
                {
                    void * t112 = gm_l0;
                    gm_t88rcucg1b7uk t113 = gm_l3;
                    uint64_t t114 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t112, t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t115 = gm_f_us_cuv01fp(t114);
                    gm_l25 = t115;
                }
                {
                    void * t116 = gm_l0;
                    void * t117 = ((uint8_t *)&gm_l24);
                    uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t119 = gm_l25;
                    uint64_t t120 = (uintptr_t)t119;
                    if (t118 > t120 || t120 > (uintptr_t)16) gm_trap_slice(t118, t120, 16, gm_srcqod65l394oc8, 263, 24);
                    gm_b16_8 t121;
                    GM_ST(void *, (uint8_t *)&t121, (void *)(((uint8_t *)t117 + t118 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(t120 - t118));
                    unsigned __int128 t122 = gm_l23;
                    gm_f_put_uint_cddin99(t116, t121, t122);
                }
                {
                    void * t123 = gm_l0;
                    gm_t88rcucg1b7uk t124 = gm_l4;
                    gm_b16_8 t125 = gm_l8;
                    void * t126 = ((uint8_t *)&gm_l24);
                    uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t128 = gm_l25;
                    uint64_t t129 = (uintptr_t)t128;
                    if (t127 > t129 || t129 > (uintptr_t)16) gm_trap_slice(t127, t129, 16, gm_srcqod65l394oc8, 264, 38);
                    gm_b16_8 t130;
                    GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t126 + t127 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(t129 - t127));
                    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
                    gm_b0_1 t132 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t123, t124, t125, t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix109;
ie109:;
                {
                    void * t134 = gm_l0;
                    gm_b16_8 t135 = gm_l8;
                    unsigned __int128 t136 = gm_l23;
                    gm_f_put_uint_cddin99(t134, t135, t136);
                }
ix109:;
            }
            {
                gm_b0_1 t137;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t137);
                goto gm_fdone;
            }
            goto ix101;
ie101:;
ix101:;
        }
        {
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t144) { case ((uint8_t)0ULL): goto ma142; case ((uint8_t)1ULL): goto ma141; case ((uint8_t)2ULL): goto ma140; case ((uint8_t)3ULL): goto ma139; default: goto ma143; }
ma139:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t146 != ((uint8_t)3ULL)) goto mn145;
            gm_b16_8 t147 = gm_l8;
            gm_f_fill_abmf5g5(t147, ((uint8_t)0ULL));
            goto me138;
mn145:;
ma140:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t149 != ((uint8_t)2ULL)) goto mn148;
            void * t150 = gm_l0;
            gm_b16_8 t151 = gm_l8;
            uint8_t t152 = gm_l10;
            uint32_t t153 = gm_l11;
            uint8_t t154 = 0;
            uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            if (t155) {
                uint32_t t156 = gm_l15;
                if (((uint32_t)2ULL) == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 272, 69);
                uint32_t t157 = (uint32_t)(t156 % ((uint32_t)2ULL));
                uint8_t t158 = ((t157 == ((uint32_t)1ULL)));
                t154 = t158;
            } else t154 = 0;
            unsigned __int128 t159 = gm_f_sat_bits_ug4cjjm(t152, t153, t154);
            gm_f_put_uint_cddin99(t150, t151, t159);
            goto me138;
mn148:;
ma141:;
            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t161 != ((uint8_t)1ULL)) goto mn160;
            void * t162 = gm_l0;
            gm_b16_8 t163 = gm_l8;
            uint8_t t164 = gm_l10;
            uint32_t t165 = gm_l11;
            unsigned __int128 t166 = gm_l12;
            uint32_t t167 = gm_l15;
            unsigned __int128 t168 = gm_f_int_pow_wrapped_5mjnoh4(t164, t165, t166, t167);
            gm_f_put_uint_cddin99(t162, t163, t168);
            goto me138;
mn160:;
ma142:;
            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t170 != ((uint8_t)0ULL)) goto mn169;
            {
                void * t173 = gm_l0;
                uint8_t t174 = gm_f_fast_23em7sv(t173);
                uint8_t t175 = (uint8_t)!t174;
                if (!t175) goto ie172;
                {
                    void * t177 = gm_l0;
                    gm_tl874t2atnlh3 t178 = gm_l1;
                    gm_tommf4j9upomc t179;
                    memset((uint8_t *)&t179 + 20, 0, 4);
                    void * t180 = gm_l0;
                    gm_t88rcucg1b7uk t181 = gm_l3;
                    gm_b16_8 t182 = gm_l6;
                    gm_b16_8 t183 = gm_f_int_text_vrjnuu2((void *)NULL, cx, NULL, t180, t181, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t179 + 0, t183);
                    uint32_t t184 = gm_l15;
                    GM_ST(uint32_t, (uint8_t *)&t179 + 16, t184);
                    gm_tabb710d9mq2e t185;
                    gm_f_format_bl27tn4((void *)&t185, (void *)NULL, cx, NULL, &t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_srcqod65l394oc8, 275, 52);
                    gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
                    gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                    gm_t76123gfet73g t188;
                    gm_f_trap_qh84l9d((void *)&t188, (void *)NULL, cx, NULL, t177, t178, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t188);
                    goto gm_fdone;
                }
                goto ix172;
ie172:;
ix172:;
            }
            {
                void * t189 = gm_l0;
                gm_b16_8 t190 = gm_l8;
                uint8_t t191 = gm_l10;
                uint32_t t192 = gm_l11;
                unsigned __int128 t193 = gm_l12;
                uint32_t t194 = gm_l15;
                unsigned __int128 t195 = gm_f_int_pow_wrapped_5mjnoh4(t191, t192, t193, t194);
                gm_f_put_uint_cddin99(t189, t190, t195);
            }
            goto me138;
mn169:;
ma143:;
            gm_trap("no match arm matched the scrutinee", gm_srcqod65l394oc8, 270, 5);
me138:;
        }
    }
    gm_b0_1 t196;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t196);
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_sat_bits_ug4cjjm(uint8_t a0, uint32_t a1, uint8_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sat_bits_ug4cjjm F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    uint8_t gm_l2; /* below */
    GM_STACK_CHECK_SHALLOW(&F, 215u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l0;
        if (!t3) goto ie2;
        {
            __int128 t5;
            uint8_t t7 = gm_l2;
            if (!t7) goto ie6;
            uint32_t t9 = gm_l1;
            __int128 t10 = gm_f_int_lo_i8jbm6e(t9);
            __int128 t11 = t10;
            t5 = t11;
            goto ix6;
ie6:;
            uint32_t t13 = gm_l1;
            __int128 t14 = gm_f_int_hi_qa5fgoo(t13);
            __int128 t15 = t14;
            t5 = t15;
ix6:;
            unsigned __int128 t16;
            memset(&t16, 0, sizeof t16); memcpy(&t16, ((uint8_t *)&t5), sizeof t16 < 16 ? sizeof t16 : 16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        unsigned __int128 t18;
        uint8_t t20 = gm_l2;
        if (!t20) goto ie19;
        unsigned __int128 t22 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        t18 = t22;
        goto ix19;
ie19:;
        uint32_t t24 = gm_l1;
        unsigned __int128 t25 = gm_f_num_mask_do3naah(t24);
        unsigned __int128 t26 = t25;
        t18 = t26;
ix19:;
        uint8_t t27 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_shift_amount_aalipud(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint32_t a5) {
    struct gm_fr_shift_amount_aalipud F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* lt */
    gm_t88rcucg1b7uk gm_l3; /* rt */
    gm_b16_8 gm_l4; /* y */
    uint32_t gm_l5; /* bits */
    gm_ttovhfee8mf28 gm_l6; /* rinfo */
    unsigned __int128 gm_l7; /* v */
    uint8_t gm_l8; /* neg */
    GM_STACK_CHECK(&F, 378u);
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
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_tg5lql6itufk7 t4 = gm_f_int_info_ulcevdv(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_ttovhfee8mf28 t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            gm_ttovhfee8mf28 t6 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            void * t7 = gm_l0;
            gm_tl874t2atnlh3 t8 = gm_l1;
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s9r2a03bnhj06));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(57));
            gm_t76123gfet73g t10;
            gm_f_trap_qh84l9d((void *)&t10, (void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcula66ipo514 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        gm_l6 = t5;
    }
    {
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l3;
        gm_b16_8 t13 = gm_l4;
        unsigned __int128 t14 = gm_f_get_int_pg7p4gc(t11, GM_CONV(gm_csguankko4lmhd, t12), t13);
        gm_l7 = t14;
    }
    {
        uint8_t t15 = 0;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (t16) {
            unsigned __int128 t17 = gm_l7;
            unsigned __int128 n18 = (unsigned __int128)((uint64_t)127ULL);
            if (n18 >= 128) gm_trap_shift((uint64_t)n18, "u128", gm_srcqod65l394oc8, 167, 30);
            unsigned __int128 t19 = (unsigned __int128)(t17 >> (int)n18);
            uint8_t t20 = ((t19 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
            t15 = t20;
        } else t15 = 0;
        gm_l8 = t15;
    }
    {
        void * t21 = gm_l0;
        gm_tl874t2atnlh3 t22 = gm_l1;
        gm_t88rcucg1b7uk t23 = gm_l2;
        uint8_t t24 = gm_l8;
        unsigned __int128 t25;
        uint8_t t27 = gm_l8;
        if (!t27) goto ie26;
        unsigned __int128 t29 = gm_l7;
        unsigned __int128 r30; bool o31 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t29, &r30);
        unsigned __int128 t32 = r30;
        unsigned __int128 t33 = t32;
        t25 = t33;
        goto ix26;
ie26:;
        unsigned __int128 t35 = gm_l7;
        unsigned __int128 t36 = t35;
        t25 = t36;
ix26:;
        uint32_t t37 = gm_l5;
        gm_f_shift_check_a7uaho4((void *)&(*(gm_tcula66ipo514 *)gm_out), (void *)NULL, cx, NULL, t21, t22, t23, t24, t25, t37);
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
inline __attribute__((aligned(4))) void gm_f_shift_check_a7uaho4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, uint8_t a3, unsigned __int128 a4, uint32_t a5) {
    struct gm_fr_shift_check_a7uaho4 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* lt */
    uint8_t gm_l3; /* neg */
    unsigned __int128 gm_l4; /* mag */
    uint32_t gm_l5; /* bits */
    gm_b16_8 gm_l6; /* text */
    GM_STACK_CHECK(&F, 595u);
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
        uint8_t t3 = gm_l3;
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        unsigned __int128 t5 = gm_l4;
        uint32_t t6 = gm_l5;
        unsigned __int128 t7 = (unsigned __int128)t6;
        uint8_t t8 = (t5 < t7);
        if (!t8) goto ie2;
        {
            unsigned __int128 t10 = gm_l4;
            uint32_t t11 = (uint32_t)t10;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcula66ipo514 *)gm_out))[0] = 0;
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        uint8_t t14 = gm_f_fast_23em7sv(t13);
        if (!t14) goto ie12;
        {
            uint8_t t17 = gm_l3;
            if (!t17) goto ie16;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcula66ipo514 *)gm_out))[0] = 0;
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 8, ((uint32_t)0ULL));
                goto gm_fdone;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            unsigned __int128 t19 = gm_l4;
            uint64_t t20 = (uint64_t)~((uint64_t)0ULL);
            unsigned __int128 t21 = (unsigned __int128)t20;
            unsigned __int128 t22 = (t19 < t21 ? t19 : t21);
            uint64_t t23 = (uint64_t)t22;
            uint32_t t24 = gm_l5;
            uint64_t t25 = (uint64_t)t24;
            if (t25 == 0) gm_trap("division by zero", gm_srcqod65l394oc8, 54, 17);
            uint64_t t26 = (uint64_t)(t23 % t25);
            uint32_t t27 = (uint32_t)t26;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcula66ipo514 *)gm_out))[0] = 0;
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t28;
        uint8_t t30 = gm_l3;
        if (!t30) goto ie29;
        unsigned __int128 t32 = gm_l4;
        gm_tsn2omssg6cpf t33;
        GM_ST(unsigned __int128, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_nacqkaa((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcqod65l394oc8, 56, 22);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_b16_8 t36 = t35;
        t28 = t36;
        goto ix29;
ie29:;
        unsigned __int128 t38 = gm_l4;
        gm_tsn2omssg6cpf t39;
        GM_ST(unsigned __int128, ((uint8_t *)&t39 + 0), t38);
        gm_tabb710d9mq2e t40;
        gm_f_format_ra7pont((void *)&t40, (void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcqod65l394oc8, 56, 55);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_b16_8 t42 = t41;
        t28 = t42;
ix29:;
        gm_l6 = t28;
    }
    {
        void * t43 = gm_l0;
        gm_tl874t2atnlh3 t44 = gm_l1;
        gm_t6j6smj7qoctu t45;
        gm_b16_8 t46 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t46);
        void * t47 = gm_l0;
        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 8));
        gm_t88rcucg1b7uk t49 = gm_l2;
        gm_b16_8 t50 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t50);
        gm_tabb710d9mq2e t51;
        gm_f_format_jp4udcs((void *)&t51, (void *)NULL, cx, NULL, &t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srcqod65l394oc8, 57, 26);
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
        gm_t76123gfet73g t54;
        gm_f_trap_qh84l9d((void *)&t54, (void *)NULL, cx, NULL, t43, t44, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcula66ipo514 *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcula66ipo514 *)gm_out) + 8, t54);
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
