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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binary_6rg6s7q {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_binop_nrsq3mp {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_bytes_equal_cksjlgj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cmp_op_mflafbf {
    gm_frame h;
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_contains_nugmipn {
    gm_frame h;
    gm_t2jeg3bg7sfqa l6; /* s */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_equal_e5vf82h {
    gm_frame h;
    gm_t2jeg3bg7sfqa l20; /* sa */
    gm_t2jeg3bg7sfqa l21; /* sb */
    gm_tpautr4br7h3l l39; /* def */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_invalid_tag_1gif354 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_invalid_tag_text_f3l5hi7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_cmp_r2i4hv9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_less_nbfe87l {
    gm_frame h;
    gm_t2jeg3bg7sfqa l14; /* sa */
    gm_t2jeg3bg7sfqa l15; /* sb */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_op_sym_6olbepb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_seq_of_ifahtjs {
    gm_frame h;
    gm_tpekhfs32op0f l10; /* e */
    gm_tmc91gv7n9c0i ret;
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
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unary_sorkbs7 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_unop_qamfooj {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_binary_6rg6s7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_bytes_equal_cksjlgj(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_aib5qn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b946o7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bvot0a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dmukbli(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gd3p27d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kfcf8tk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mq7e94u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_vno937p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vqmfrgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_less_nbfe87l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unary_sorkbs7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, void * a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s7ah1q5ah26rj[12];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa2pnk1jujp5p[23];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_sqp26euqk4rkd[16];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcj10g653a00lt[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_cmp_r2i4hv9 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t6 != ((uint8_t)10ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t8 != ((uint8_t)11ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)12ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)13ULL)) goto pa11;
    goto po4;
pa11:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 26, 40);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
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
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 16));
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = t2;
    uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t5 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 176));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    if (t6 > t10 || t10 > (uintptr_t)t4) gm_trap_slice(t6, t10, t4, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + t6 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t6));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t1, t11);
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)0));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)0));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_8jbgndp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l2;
    gm_b16_8 t2 = t1;
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = t6;
    uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
    if (t3 >= (uintptr_t)(t8)) gm_trap_index(t3, t8, gm_srcnqtpfma6qe4a, 174, 81);
    uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t7)) + t3 * 8));
    uint64_t t10 = gm_f_us_cuv01fp(t9);
    uint64_t t11 = (uintptr_t)t10;
    gm_b16_8 t12 = t2;
    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&t12) + 8);
    if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcnqtpfma6qe4a, 174, 76);
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t12)) + t11 * 1));
    uint8_t t15 = (!(t14 == ((uint8_t)0ULL)));
    F.ret = t15;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28 = t27;
        void * t29;
        if ((GM_LD(void *, ((uint8_t *)&t28)) != 0)) {
            void * t30 = GM_LD(void *, (((uint8_t *)&t28) + 0));
            t29 = t30;
        } else {
            uint8_t t31 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t29;
    }
    {
        uint64_t t32 = gm_l1;
        void * t33 = gm_l5;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
        uint64_t r35; bool o36 = __builtin_sub_overflow(t32, t34, &r35);
        if (o36) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t37 = r35;
        gm_l7 = t37;
    }
    {
        uint64_t t38 = gm_l7;
        void * t39 = gm_l5;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
        unsigned __int128 n41 = (unsigned __int128)t40;
        if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t42 = (uint64_t)(t38 >> (int)n41);
        gm_l8 = t42;
    }
    {
        void * t43 = gm_l6;
        void * t44 = t43;
        uint64_t t45 = gm_l8;
        uint64_t t46 = gm_f_us_cuv01fp(t45);
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = (void *)((uint8_t *)t44 + t47 * 24);
        gm_l9 = t48;
    }
    {
        uint64_t t49 = gm_l7;
        uint64_t t50 = gm_l8;
        void * t51 = gm_l5;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
        unsigned __int128 n53 = (unsigned __int128)t52;
        if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r54 = (uint64_t)((uint64_t)t50 << (int)n53);
        bool o55 = (uint64_t)(r54 >> (int)n53) != t50;
        if (o55) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t56 = r54;
        uint64_t r57; bool o58 = __builtin_sub_overflow(t49, t56, &r57);
        if (o58) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t59 = r57;
        gm_l10 = t59;
    }
    {
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        uint8_t t63 = 0;
        void * t64 = gm_l9;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t64) + 17));
        uint8_t t66 = (uint8_t)!t65;
        if (!t66) {
            uint64_t t67 = gm_l2;
            void * t68 = gm_l9;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
            uint8_t t70 = (t67 > t69);
            t63 = t70;
        } else t63 = 1;
        if (!t63) {
            uint64_t t71 = gm_l10;
            void * t72 = gm_l9;
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 8));
            uint64_t t74 = gm_l2;
            uint64_t r75; bool o76 = __builtin_sub_overflow(t73, t74, &r75);
            if (o76) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t77 = r75;
            uint8_t t78 = (t71 > t77);
            t62 = t78;
        } else t62 = 1;
        if (!t62) {
            uint8_t t79 = 0;
            uint8_t t80 = gm_l3;
            if (t80) {
                void * t81 = gm_l9;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t81) + 18));
                uint8_t t83 = (uint8_t)!t82;
                t79 = t83;
            } else t79 = 0;
            t61 = t79;
        } else t61 = 1;
        if (!t61) goto ie60;
        {
            uint8_t t85 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        void * t86 = gm_l9;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = t87;
        uint64_t t89 = gm_l10;
        uint64_t t90 = gm_f_us_cuv01fp(t89);
        uint64_t t91 = (uintptr_t)t90;
        void * t92 = (void *)((uint8_t *)t88 + t91 * 1);
        void * t93 = t92;
        uint8_t t94 = 0;
        F.ret = t93;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_binary_6rg6s7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_binary_6rg6s7q F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    void * gm_l3; /* l */
    void * gm_l4; /* r */
    void * gm_l5; /* e */
    gm_b16_8 gm_l6; /* dst */
    uint64_t gm_l7; /* ln */
    uint64_t gm_l8; /* rn */
    gm_t51b2en4qp02s gm_l9; /* xb */
    gm_t51b2en4qp02s gm_l10; /* yb */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* x */
    gm_b16_8 gm_l13; /* y */
    GM_STACK_CHECK(&F, 1568u);
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
    {
        {
            void * t3 = gm_l3;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
            uint64_t t5 = gm_f_us_cuv01fp(t4);
            gm_l7 = t5;
        }
        {
            void * t6 = gm_l4;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            uint64_t t8 = gm_f_us_cuv01fp(t7);
            gm_l8 = t8;
        }
        {
            uint64_t t10 = gm_l7;
            uint8_t t11 = (t10 <= ((uint64_t)16ULL));
            if (!t11) goto ie9;
            uint64_t t12 = gm_l8;
            uint8_t t13 = (t12 <= ((uint64_t)16ULL));
            if (!t13) goto ie9;
            {
            }
            {
            }
            {
                void * t15 = gm_l0;
                gm_tbl8il92l97ut t16 = gm_l1;
                void * t17 = gm_l3;
                void * t18 = ((uint8_t *)&gm_l9);
                uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t20 = gm_l7;
                uint64_t t21 = (uintptr_t)t20;
                if (t19 > t21 || t21 > (uintptr_t)16) gm_trap_slice(t19, t21, 16, gm_srcj10g653a00lt, 91, 24);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t18 + t19 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21 - t19));
                gm_tcfa6l7reoosn t23;
                gm_f_ev_25jfdar((void *)&t23, (void *)NULL, cx, NULL, t15, t16, t17, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t24 = t23;
                if (((((uint8_t *)&t24))[0] != 0)) {
                    gm_t76123gfet73g t25 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t24) + 8));
                    gm_tcfa6l7reoosn t26;
                    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                    ((uint8_t *)&t26)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t26 + 8), t25);
                    F.ret = t26;
                    goto gm_fret;
                }
                gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 8));
            }
            {
                void * t28 = gm_l0;
                gm_tbl8il92l97ut t29 = gm_l1;
                void * t30 = gm_l4;
                void * t31 = ((uint8_t *)&gm_l10);
                uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t33 = gm_l8;
                uint64_t t34 = (uintptr_t)t33;
                if (t32 > t34 || t34 > (uintptr_t)16) gm_trap_slice(t32, t34, 16, gm_srcj10g653a00lt, 92, 24);
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t31 + t32 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34 - t32));
                gm_tcfa6l7reoosn t36;
                gm_f_ev_25jfdar((void *)&t36, (void *)NULL, cx, NULL, t28, t29, t30, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t37 = t36;
                if (((((uint8_t *)&t37))[0] != 0)) {
                    gm_t76123gfet73g t38 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t37) + 8));
                    gm_tcfa6l7reoosn t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    ((uint8_t *)&t39)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t39 + 8), t38);
                    F.ret = t39;
                    goto gm_fret;
                }
                gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
            }
            {
                void * t41 = gm_l0;
                void * t42 = gm_l5;
                gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 112));
                gm_ts6oqppjh0ba3 t44 = gm_l2;
                void * t45 = gm_l3;
                gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 96));
                void * t47 = gm_l4;
                gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 96));
                void * t49 = gm_l5;
                gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t49) + 96));
                void * t51 = ((uint8_t *)&gm_l9);
                uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t53 = gm_l7;
                uint64_t t54 = (uintptr_t)t53;
                if (t52 > t54 || t54 > (uintptr_t)16) gm_trap_slice(t52, t54, 16, gm_srcj10g653a00lt, 93, 54);
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t51 + t52 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54 - t52));
                void * t56 = ((uint8_t *)&gm_l10);
                uint64_t t57 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t58 = gm_l8;
                uint64_t t59 = (uintptr_t)t58;
                if (t57 > t59 || t59 > (uintptr_t)16) gm_trap_slice(t57, t59, 16, gm_srcj10g653a00lt, 93, 65);
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t56 + t57 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(t59 - t57));
                gm_b16_8 t61 = gm_l6;
                gm_f_binop_nrsq3mp((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t41, t43, t44, t46, t48, t50, t55, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            void * t62 = gm_l0;
            gm_t2lqu8jsj511d t63 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t62));
            gm_l11 = t63;
        }
        {
            void * t64 = gm_l0;
            void * t65 = gm_l3;
            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 104));
            gm_b16_8 t67 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t64, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t67;
        }
        {
            void * t68 = gm_l0;
            gm_tbl8il92l97ut t69 = gm_l1;
            void * t70 = gm_l3;
            gm_b16_8 t71 = gm_l12;
            gm_tcfa6l7reoosn t72;
            gm_f_ev_25jfdar((void *)&t72, (void *)NULL, cx, NULL, t68, t69, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t73 = t72;
            if (((((uint8_t *)&t73))[0] != 0)) {
                gm_t76123gfet73g t74 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t73) + 8));
                gm_tcfa6l7reoosn t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                ((uint8_t *)&t75)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t75 + 8), t74);
                F.ret = t75;
                goto gm_fret;
            }
            gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 8));
        }
        {
            void * t77 = gm_l0;
            void * t78 = gm_l4;
            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 104));
            gm_b16_8 t80 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t77, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t80;
        }
        {
            void * t81 = gm_l0;
            gm_tbl8il92l97ut t82 = gm_l1;
            void * t83 = gm_l4;
            gm_b16_8 t84 = gm_l13;
            gm_tcfa6l7reoosn t85;
            gm_f_ev_25jfdar((void *)&t85, (void *)NULL, cx, NULL, t81, t82, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t86 = t85;
            if (((((uint8_t *)&t86))[0] != 0)) {
                gm_t76123gfet73g t87 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t86) + 8));
                gm_tcfa6l7reoosn t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                ((uint8_t *)&t88)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t88 + 8), t87);
                F.ret = t88;
                goto gm_fret;
            }
            gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 8));
        }
        {
            void * t90 = gm_l0;
            void * t91 = gm_l5;
            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t91) + 112));
            gm_ts6oqppjh0ba3 t93 = gm_l2;
            void * t94 = gm_l3;
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 96));
            void * t96 = gm_l4;
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + 96));
            void * t98 = gm_l5;
            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t98) + 96));
            gm_b16_8 t100 = gm_l12;
            gm_b16_8 t101 = gm_l13;
            gm_b16_8 t102 = gm_l6;
            gm_tcfa6l7reoosn t103;
            gm_f_binop_nrsq3mp((void *)&t103, (void *)NULL, cx, NULL, t90, t92, t93, t95, t97, t99, t100, t101, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t104 = t103;
            if (((((uint8_t *)&t104))[0] != 0)) {
                gm_t76123gfet73g t105 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t104) + 8));
                gm_tcfa6l7reoosn t106;
                GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                ((uint8_t *)&t106)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t106 + 8), t105);
                F.ret = t106;
                goto gm_fret;
            }
            gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 8));
        }
        {
            void * t108 = gm_l0;
            gm_t2lqu8jsj511d t109 = gm_l11;
            gm_f_release_jn5kqdi(t108, GM_CONV(gm_csc7lc6jlkqkuq, t109));
        }
    }
    gm_b0_1 t110;
    gm_tcfa6l7reoosn t111;
    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t111 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t111 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t111 + 24, 0);
    ((uint8_t *)&t111)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t111 + 8), t110);
    gm_tcfa6l7reoosn t112 = t111;
    F.ret = t112;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_binop_nrsq3mp F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* rt */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* dst */
    uint8_t gm_l9; /* v */
    double gm_l10; /* p */
    double gm_l11; /* q */
    double gm_l12; /* r */
    uint64_t gm_l13; /* n */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    uint64_t gm_l15; /* esz */
    uint8_t gm_l16; /* scalar_r */
    gm_t88rcucg1b7uk gm_l17; /* ety */
    uint8_t gm_l18; /* is_mask */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* xs */
    gm_b16_8 gm_l23; /* ys */
    gm_b16_8 gm_l24; /* ds */
    gm_t88rcucg1b7uk gm_l25; /* u */
    GM_STACK_CHECK(&F, 3199u);
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
            gm_ts6oqppjh0ba3 t4 = gm_l2;
            uint8_t t5 = gm_f_is_cmp_r2i4hv9(t4);
            if (!t5) goto ie3;
            {
                void * t7 = gm_l0;
                gm_tl874t2atnlh3 t8 = gm_l1;
                gm_ts6oqppjh0ba3 t9 = gm_l2;
                gm_t88rcucg1b7uk t10 = gm_l3;
                gm_b16_8 t11 = gm_l6;
                gm_b16_8 t12 = gm_l7;
                gm_t1ec8br9ddkav t13;
                gm_f_cmp_op_mflafbf((void *)&t13, (void *)NULL, cx, NULL, t7, t8, t9, t10, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t14 = t13;
                if (((((uint8_t *)&t14))[0] != 0)) {
                    gm_t76123gfet73g t15 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t14) + 8));
                    gm_tcfa6l7reoosn t16;
                    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
                    ((uint8_t *)&t16)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t16 + 8), t15);
                    F.ret = t16;
                    goto gm_fret;
                }
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t14) + 8));
                gm_l9 = t17;
            }
            {
                uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcj10g653a00lt, 36, 9);
                uint8_t *t20 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t18 * 1);
                uint8_t t21;
                uint8_t t23 = gm_l9;
                if (!t23) goto ie22;
                uint8_t t25 = ((uint8_t)1ULL);
                t21 = t25;
                goto ix22;
ie22:;
                uint8_t t27 = ((uint8_t)0ULL);
                t21 = t27;
ix22:;
                GM_ST(uint8_t, t20, t21);
            }
            {
                gm_b0_1 t28;
                gm_tcfa6l7reoosn t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t29 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                ((uint8_t *)&t29)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), t28);
                gm_tcfa6l7reoosn t30 = t29;
                uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
                F.ret = t29;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t32 = gm_l0;
            gm_t88rcucg1b7uk t33 = gm_l3;
            gm_t53at80pfdus3 t34;
            gm_f_kind_htv6093((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
            gm_t53at80pfdus3 t35 = t34;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            switch (t43) { case ((uint8_t)0ULL): goto ma37; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma37; case ((uint8_t)3ULL): goto ma38; case ((uint8_t)4ULL): goto ma41; case ((uint8_t)5ULL): goto ma41; case ((uint8_t)6ULL): goto ma41; case ((uint8_t)7ULL): goto ma41; case ((uint8_t)8ULL): goto ma41; case ((uint8_t)9ULL): goto ma41; case ((uint8_t)10ULL): goto ma41; case ((uint8_t)11ULL): goto ma39; case ((uint8_t)12ULL): goto ma41; case ((uint8_t)13ULL): goto ma41; case ((uint8_t)14ULL): goto ma41; case ((uint8_t)15ULL): goto ma41; case ((uint8_t)16ULL): goto ma41; case ((uint8_t)17ULL): goto ma40; case ((uint8_t)18ULL): goto ma41; case ((uint8_t)19ULL): goto ma41; case ((uint8_t)20ULL): goto ma41; case ((uint8_t)21ULL): goto ma41; case ((uint8_t)22ULL): goto ma41; case ((uint8_t)23ULL): goto ma41; case ((uint8_t)24ULL): goto ma41; case ((uint8_t)25ULL): goto ma41; case ((uint8_t)26ULL): goto ma41; default: goto ma41; }
ma37:;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t47 != ((uint8_t)0ULL)) goto pa46;
            goto po45;
pa46:;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t49 != ((uint8_t)1ULL)) goto pa48;
            goto po45;
pa48:;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t51 != ((uint8_t)2ULL)) goto pa50;
            goto po45;
pa50:;
            goto mn44;
po45:;
            void * t52 = gm_l0;
            gm_tl874t2atnlh3 t53 = gm_l1;
            gm_ts6oqppjh0ba3 t54 = gm_l2;
            gm_t88rcucg1b7uk t55 = gm_l3;
            gm_t88rcucg1b7uk t56 = gm_l4;
            gm_t88rcucg1b7uk t57 = gm_l5;
            gm_b16_8 t58 = gm_l6;
            gm_b16_8 t59 = t58;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            gm_b16_8 t61 = gm_l7;
            gm_b16_8 t62 = t61;
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_b16_8 t64 = gm_l8;
            gm_f_int_binop_qrbogd9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t52, t53, t54, t55, t56, t57, t60, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me36;
mn44:;
ma38:;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t66 != ((uint8_t)3ULL)) goto mn65;
            {
                void * t68 = gm_l0;
                gm_t88rcucg1b7uk t69 = gm_l3;
                gm_b16_8 t70 = gm_l6;
                gm_b16_8 t71 = t70;
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                double t73 = gm_f_get_float_grmhlm4(t68, GM_CONV(gm_csguankko4lmhd, t69), t72);
                gm_l10 = t73;
            }
            {
                void * t74 = gm_l0;
                gm_t88rcucg1b7uk t75 = gm_l3;
                gm_b16_8 t76 = gm_l7;
                gm_b16_8 t77 = t76;
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                double t79 = gm_f_get_float_grmhlm4(t74, GM_CONV(gm_csguankko4lmhd, t75), t78);
                gm_l11 = t79;
            }
            {
                double t80;
                uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t88) { case ((uint8_t)0ULL): goto ma82; case ((uint8_t)1ULL): goto ma83; case ((uint8_t)2ULL): goto ma84; case ((uint8_t)3ULL): goto ma86; case ((uint8_t)4ULL): goto ma85; case ((uint8_t)5ULL): goto ma86; case ((uint8_t)6ULL): goto ma86; case ((uint8_t)7ULL): goto ma86; case ((uint8_t)8ULL): goto ma86; case ((uint8_t)9ULL): goto ma86; case ((uint8_t)10ULL): goto ma86; case ((uint8_t)11ULL): goto ma86; case ((uint8_t)12ULL): goto ma86; case ((uint8_t)13ULL): goto ma86; default: goto ma86; }
ma82:;
                uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t90 != ((uint8_t)0ULL)) goto mn89;
                double t91 = gm_l10;
                double t92 = gm_l11;
                double t93 = (t91 + t92);
                t80 = t93;
                goto me81;
mn89:;
ma83:;
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t95 != ((uint8_t)1ULL)) goto mn94;
                double t96 = gm_l10;
                double t97 = gm_l11;
                double t98 = (t96 - t97);
                t80 = t98;
                goto me81;
mn94:;
ma84:;
                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t100 != ((uint8_t)2ULL)) goto mn99;
                double t101 = gm_l10;
                double t102 = gm_l11;
                double t103 = (t101 * t102);
                t80 = t103;
                goto me81;
mn99:;
ma85:;
                uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t105 != ((uint8_t)4ULL)) goto mn104;
                double t106 = gm_l10;
                double t107 = gm_l11;
                double t108 = (t106 / t107);
                t80 = t108;
                goto me81;
mn104:;
ma86:;
                void * t110 = gm_l0;
                gm_tl874t2atnlh3 t111 = gm_l1;
                gm_ts6oqppjh0ba3 t112 = gm_l2;
                gm_b16_8 t113 = gm_f_op_sym_6olbepb(t112);
                gm_tnpifv20irnph t114;
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
                gm_tabb710d9mq2e t115;
                gm_f_format_aib5qn5((void *)&t115, (void *)NULL, cx, NULL, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t116 = t115;
                if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srcj10g653a00lt, 49, 43);
                gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                gm_b16_8 t118 = t117;
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                gm_t76123gfet73g t120;
                gm_f_trap_qh84l9d((void *)&t120, (void *)NULL, cx, NULL, t110, t111, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t121;
                GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
                ((uint8_t *)&t121)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t121 + 8), t120);
                gm_tcfa6l7reoosn t122 = t121;
                uint8_t t123 = ((((uint8_t *)&t122))[0] != 0);
                F.ret = t121;
                goto gm_fret;
                goto me81;
mn109:;
ma87:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 44, 18);
me81:;
                gm_l12 = t80;
            }
            {
                void * t124 = gm_l0;
                gm_t88rcucg1b7uk t125 = gm_l3;
                double t126 = gm_l12;
                gm_b16_8 t127 = gm_l8;
                gm_f_put_float_fiad72d(t124, GM_CONV(gm_csguankko4lmhd, t125), t126, t127);
            }
            goto me36;
mn65:;
ma39:;
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t129 != ((uint8_t)11ULL)) goto mn128;
            uint64_t t130 = GM_LD(uint64_t, ((((uint8_t *)&t35) + 8) + 0));
            gm_l13 = t130;
            gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 8));
            gm_l14 = t131;
            {
                void * t133 = gm_l0;
                gm_t88rcucg1b7uk t134 = gm_l14;
                uint64_t t135 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t133, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t136 = gm_f_us_cuv01fp(t135);
                gm_l15 = t136;
            }
            {
                void * t137 = gm_l0;
                gm_t88rcucg1b7uk t138 = gm_l4;
                gm_t53at80pfdus3 t139;
                gm_f_kind_htv6093((void *)&t139, t137, GM_CONV(gm_csguankko4lmhd, t138));
                gm_t53at80pfdus3 t140 = t139;
                uint8_t t141;
                uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t140) + 0));
                if (t144 != ((uint8_t)11ULL)) goto mn143;
                t141 = ((uint8_t)0);
                goto me142;
mn143:;
                t141 = ((uint8_t)1);
                goto me142;
mn145:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 56, 25);
me142:;
                gm_l16 = t141;
            }
            {
                gm_t88rcucg1b7uk t146;
                uint8_t t148 = gm_l16;
                if (!t148) goto ie147;
                gm_t88rcucg1b7uk t150 = gm_l4;
                gm_t88rcucg1b7uk t151 = t150;
                t146 = t151;
                goto ix147;
ie147:;
                gm_t88rcucg1b7uk t153 = gm_l14;
                gm_t88rcucg1b7uk t154 = t153;
                t146 = t154;
ix147:;
                gm_l17 = t146;
            }
            {
                void * t155 = gm_l0;
                gm_t88rcucg1b7uk t156 = gm_l14;
                gm_t53at80pfdus3 t157;
                gm_f_kind_htv6093((void *)&t157, t155, GM_CONV(gm_csguankko4lmhd, t156));
                gm_t53at80pfdus3 t158 = t157;
                uint8_t t159 = ((GM_LD(uint8_t, ((uint8_t *)&t158) + 0) == ((uint8_t)4ULL)));
                gm_l18 = t159;
            }
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t161 = gm_l13;
                    uint64_t t162 = gm_f_us_cuv01fp(t161);
                    gm_l19 = t162;
                }
                {
                    for (;;) {
                        uint64_t t164 = gm_l20;
                        uint64_t t165 = gm_l19;
                        uint8_t t166 = (t164 < t165);
                        if (!t166) goto lb163;
                        {
                            uint64_t t169 = gm_l20;
                            uint64_t t170 = t169;
                            uint64_t t173 = GM_LD(uint64_t, ((uint8_t *)&t170));
                            gm_l21 = t173;
                            {
                                uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                void * t176 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                                uint64_t t177 = gm_l21;
                                uint64_t t178 = gm_l15;
                                uint64_t r179; bool o180 = __builtin_mul_overflow(t177, t178, &r179);
                                if (o180) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 60, 25);
                                uint64_t t181 = r179;
                                uint64_t t182 = (uintptr_t)t181;
                                uint64_t t183 = gm_l21;
                                uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                if (o185) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 60, 35);
                                uint64_t t186 = r184;
                                uint64_t t187 = gm_l15;
                                uint64_t r188; bool o189 = __builtin_mul_overflow(t186, t187, &r188);
                                if (o189) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 60, 34);
                                uint64_t t190 = r188;
                                uint64_t t191 = (uintptr_t)t190;
                                if (t182 > t191 || t191 > (uintptr_t)t175) gm_trap_slice(t182, t191, t175, gm_srcj10g653a00lt, 60, 23);
                                gm_b16_8 t192;
                                GM_ST(void *, (uint8_t *)&t192, (void *)(((uint8_t *)t176 + t182 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(t191 - t182));
                                gm_l22 = t192;
                            }
                            {
                                gm_b16_8 t193;
                                uint8_t t195 = gm_l16;
                                if (!t195) goto ie194;
                                gm_b16_8 t197 = gm_l7;
                                gm_b16_8 t198 = t197;
                                t193 = t198;
                                goto ix194;
ie194:;
                                uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                void * t201 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                uint64_t t202 = gm_l21;
                                uint64_t t203 = gm_l15;
                                uint64_t r204; bool o205 = __builtin_mul_overflow(t202, t203, &r204);
                                if (o205) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 61, 50);
                                uint64_t t206 = r204;
                                uint64_t t207 = (uintptr_t)t206;
                                uint64_t t208 = gm_l21;
                                uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                                if (o210) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 61, 60);
                                uint64_t t211 = r209;
                                uint64_t t212 = gm_l15;
                                uint64_t r213; bool o214 = __builtin_mul_overflow(t211, t212, &r213);
                                if (o214) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 61, 59);
                                uint64_t t215 = r213;
                                uint64_t t216 = (uintptr_t)t215;
                                if (t207 > t216 || t216 > (uintptr_t)t200) gm_trap_slice(t207, t216, t200, gm_srcj10g653a00lt, 61, 48);
                                gm_b16_8 t217;
                                GM_ST(void *, (uint8_t *)&t217, (void *)(((uint8_t *)t201 + t207 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(t216 - t207));
                                gm_b16_8 t218 = t217;
                                t193 = t218;
ix194:;
                                gm_l23 = t193;
                            }
                            {
                                uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                void * t220 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)));
                                uint64_t t221 = gm_l21;
                                uint64_t t222 = gm_l15;
                                uint64_t r223; bool o224 = __builtin_mul_overflow(t221, t222, &r223);
                                if (o224) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 62, 27);
                                uint64_t t225 = r223;
                                uint64_t t226 = (uintptr_t)t225;
                                uint64_t t227 = gm_l21;
                                uint64_t r228; bool o229 = __builtin_add_overflow(t227, ((uint64_t)1ULL), &r228);
                                if (o229) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 62, 37);
                                uint64_t t230 = r228;
                                uint64_t t231 = gm_l15;
                                uint64_t r232; bool o233 = __builtin_mul_overflow(t230, t231, &r232);
                                if (o233) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 62, 36);
                                uint64_t t234 = r232;
                                uint64_t t235 = (uintptr_t)t234;
                                if (t226 > t235 || t235 > (uintptr_t)t219) gm_trap_slice(t226, t235, t219, gm_srcj10g653a00lt, 62, 23);
                                gm_b16_8 t236;
                                GM_ST(void *, (uint8_t *)&t236, (void *)(((uint8_t *)t220 + t226 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(t235 - t226));
                                gm_l24 = t236;
                            }
                            {
                                uint8_t t238 = gm_l18;
                                if (!t238) goto ie237;
                                {
                                    uint64_t t240 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                    if (t240 >= (uintptr_t)(t241)) gm_trap_index(t240, t241, gm_srcj10g653a00lt, 65, 21);
                                    uint8_t t242;
                                    uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    switch (t248) { case ((uint8_t)0ULL): goto ma246; case ((uint8_t)1ULL): goto ma246; case ((uint8_t)2ULL): goto ma246; case ((uint8_t)3ULL): goto ma246; case ((uint8_t)4ULL): goto ma246; case ((uint8_t)5ULL): goto ma246; case ((uint8_t)6ULL): goto ma246; case ((uint8_t)7ULL): goto ma244; case ((uint8_t)8ULL): goto ma245; case ((uint8_t)9ULL): goto ma246; case ((uint8_t)10ULL): goto ma246; case ((uint8_t)11ULL): goto ma246; case ((uint8_t)12ULL): goto ma246; case ((uint8_t)13ULL): goto ma246; default: goto ma246; }
ma244:;
                                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t250 != ((uint8_t)7ULL)) goto mn249;
                                    uint64_t t251 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    if (t251 >= (uintptr_t)(t252)) gm_trap_index(t251, t252, gm_srcj10g653a00lt, 66, 36);
                                    uint8_t t253 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t251 * 1));
                                    uint64_t t254 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t255 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    if (t254 >= (uintptr_t)(t255)) gm_trap_index(t254, t255, gm_srcj10g653a00lt, 66, 44);
                                    uint8_t t256 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t254 * 1));
                                    uint8_t t257 = (uint8_t)(t253 & t256);
                                    t242 = t257;
                                    goto me243;
mn249:;
ma245:;
                                    uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t259 != ((uint8_t)8ULL)) goto mn258;
                                    uint64_t t260 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t261 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    if (t260 >= (uintptr_t)(t261)) gm_trap_index(t260, t261, gm_srcj10g653a00lt, 67, 35);
                                    uint8_t t262 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t260 * 1));
                                    uint64_t t263 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    if (t263 >= (uintptr_t)(t264)) gm_trap_index(t263, t264, gm_srcj10g653a00lt, 67, 43);
                                    uint8_t t265 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t263 * 1));
                                    uint8_t t266 = (uint8_t)(t262 | t265);
                                    t242 = t266;
                                    goto me243;
mn258:;
ma246:;
                                    uint64_t t268 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    if (t268 >= (uintptr_t)(t269)) gm_trap_index(t268, t269, gm_srcj10g653a00lt, 68, 30);
                                    uint8_t t270 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t268 * 1));
                                    uint64_t t271 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t272 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    if (t271 >= (uintptr_t)(t272)) gm_trap_index(t271, t272, gm_srcj10g653a00lt, 68, 38);
                                    uint8_t t273 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t271 * 1));
                                    uint8_t t274 = (uint8_t)(t270 ^ t273);
                                    t242 = t274;
                                    goto me243;
mn267:;
ma247:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 65, 29);
me243:;
                                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t240 * 1), t242);
                                }
                                goto ix237;
ie237:;
                                {
                                    void * t276 = gm_l0;
                                    gm_tl874t2atnlh3 t277 = gm_l1;
                                    gm_ts6oqppjh0ba3 t278 = gm_l2;
                                    gm_t88rcucg1b7uk t279 = gm_l14;
                                    gm_t88rcucg1b7uk t280 = gm_l17;
                                    gm_t88rcucg1b7uk t281 = gm_l14;
                                    gm_b16_8 t282 = gm_l22;
                                    gm_b16_8 t283 = gm_l23;
                                    gm_b16_8 t284 = gm_l24;
                                    gm_tcfa6l7reoosn t285;
                                    gm_f_binop_nrsq3mp((void *)&t285, (void *)NULL, cx, NULL, t276, t277, t278, t279, t280, t281, t282, t283, t284);
                                    if (gm_unw.kind) {
                                        goto gm_ll167;
                                    }
                                    gm_tcfa6l7reoosn t286 = t285;
                                    if (((((uint8_t *)&t286))[0] != 0)) {
                                        gm_t76123gfet73g t287 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t286) + 8));
                                        gm_tcfa6l7reoosn t288;
                                        GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                                        ((uint8_t *)&t288)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t288 + 8), t287);
                                        F.ret = t288;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t289 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 8));
                                }
ix237:;
                            }
                            goto me171;
mn172:;
                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 59, 13);
me171:;
                        }
lc163:;
                        {
                            uint64_t t291 = gm_l20;
                            uint64_t r292; bool o293 = __builtin_add_overflow(t291, ((uint64_t)1ULL), &r292);
                            uint64_t t294 = r292;
                            gm_l20 = t294;
                        }
                    }
gm_ll167:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb163; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc163; }
                    }
                    goto gm_lad;
lb163:;
                }
            }
            goto me36;
mn128:;
ma40:;
            uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t296 != ((uint8_t)17ULL)) goto mn295;
            {
                void * t298 = gm_l0;
                gm_t88rcucg1b7uk t299 = gm_l3;
                gm_tkn42u7na3t81 t300 = gm_f_bits_struct_ty_22ue20s(t298, GM_CONV(gm_csguankko4lmhd, t299));
                gm_tkn42u7na3t81 t301 = t300;
                gm_t88rcucg1b7uk t302;
                if (((((uint8_t *)&t301))[4] != 0)) {
                    gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t301) + 0));
                    t302 = t303;
                } else {
                    void * t304 = gm_l0;
                    gm_tl874t2atnlh3 t305 = gm_l1;
                    gm_t6j6smj7qoctu t306;
                    gm_ts6oqppjh0ba3 t307 = gm_l2;
                    gm_b16_8 t308 = gm_f_op_sym_6olbepb(t307);
                    GM_ST(gm_b16_8, (uint8_t *)&t306 + 0, t308);
                    void * t309 = gm_l0;
                    void * t310 = GM_LD(void *, (((uint8_t *)t309) + 8));
                    gm_t88rcucg1b7uk t311 = gm_l3;
                    gm_b16_8 t312 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t310, t311);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t306 + 16, t312);
                    gm_tabb710d9mq2e t313;
                    gm_f_format_bvot0a7((void *)&t313, (void *)NULL, cx, NULL, &t306);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t314 = t313;
                    if (((((uint8_t *)&t314))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t314) + 8, gm_srcj10g653a00lt, 76, 65);
                    gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&t314) + 8));
                    gm_b16_8 t316 = t315;
                    gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
                    gm_t76123gfet73g t318;
                    gm_f_trap_qh84l9d((void *)&t318, (void *)NULL, cx, NULL, t304, t305, t317);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t319;
                    GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
                    ((uint8_t *)&t319)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t319 + 8), t318);
                    gm_tcfa6l7reoosn t320 = t319;
                    uint8_t t321 = ((((uint8_t *)&t320))[0] != 0);
                    F.ret = t319;
                    goto gm_fret;
                }
                gm_l25 = t302;
            }
            {
                void * t322 = gm_l0;
                gm_tl874t2atnlh3 t323 = gm_l1;
                gm_ts6oqppjh0ba3 t324 = gm_l2;
                gm_t88rcucg1b7uk t325 = gm_l25;
                gm_t88rcucg1b7uk t326 = gm_l25;
                gm_t88rcucg1b7uk t327 = gm_l25;
                gm_b16_8 t328 = gm_l6;
                gm_b16_8 t329 = t328;
                gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
                gm_b16_8 t331 = gm_l7;
                gm_b16_8 t332 = t331;
                gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
                gm_b16_8 t334 = gm_l8;
                gm_f_int_binop_qrbogd9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t322, t323, t324, t325, t326, t327, t330, t333, t334);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me36;
mn295:;
ma41:;
            void * t336 = gm_l0;
            gm_tl874t2atnlh3 t337 = gm_l1;
            gm_t6j6smj7qoctu t338;
            gm_ts6oqppjh0ba3 t339 = gm_l2;
            gm_b16_8 t340 = gm_f_op_sym_6olbepb(t339);
            GM_ST(gm_b16_8, (uint8_t *)&t338 + 0, t340);
            void * t341 = gm_l0;
            void * t342 = GM_LD(void *, (((uint8_t *)t341) + 8));
            gm_t88rcucg1b7uk t343 = gm_l3;
            gm_b16_8 t344 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t342, t343);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t338 + 16, t344);
            gm_tabb710d9mq2e t345;
            gm_f_format_bvot0a7((void *)&t345, (void *)NULL, cx, NULL, &t338);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t346 = t345;
            if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 8, gm_srcj10g653a00lt, 79, 35);
            gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&t346) + 8));
            gm_b16_8 t348 = t347;
            gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
            gm_t76123gfet73g t350;
            gm_f_trap_qh84l9d((void *)&t350, (void *)NULL, cx, NULL, t336, t337, t349);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t351;
            GM_ST(uint8_t, (uint8_t *)&t351 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t351 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t351 + 4, 0);
            ((uint8_t *)&t351)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t351 + 8), t350);
            gm_tcfa6l7reoosn t352 = t351;
            uint8_t t353 = ((((uint8_t *)&t352))[0] != 0);
            F.ret = t351;
            goto gm_fret;
            goto me36;
mn335:;
ma42:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 39, 5);
me36:;
        }
    }
    gm_b0_1 t354;
    gm_tcfa6l7reoosn t355;
    GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t355 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t355 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t355 + 24, 0);
    ((uint8_t *)&t355)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t355 + 8), t354);
    gm_tcfa6l7reoosn t356 = t355;
    F.ret = t356;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bytes_equal_cksjlgj(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bytes_equal_cksjlgj F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = (!(t3 == t4));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l2 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l3;
                uint64_t t12 = gm_l2;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l3;
                    uint64_t t17 = t16;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l4 = t20;
                    {
                        uint64_t t23 = gm_l4;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcj10g653a00lt, 383, 12);
                        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t24 * 1));
                        uint64_t t27 = gm_l4;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srcj10g653a00lt, 383, 24);
                        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t28 * 1));
                        uint8_t t31 = (!(t26 == t30));
                        if (!t31) goto ie22;
                        {
                            uint8_t t33 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 382, 5);
me18:;
                }
lc10:;
                {
                    uint64_t t35 = gm_l3;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l3 = t38;
                }
            }
lb10:;
        }
    }
    {
        uint8_t t39 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_cmp_op_mflafbf F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* y */
    gm_ttovhfee8mf28 gm_l6; /* info */
    unsigned __int128 gm_l7; /* a */
    unsigned __int128 gm_l8; /* c */
    __int128 gm_l9; /* p */
    __int128 gm_l10; /* q */
    GM_STACK_CHECK(&F, 1801u);
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
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l3;
        gm_tg5lql6itufk7 t5 = gm_f_int_info_ulcevdv(t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tg5lql6itufk7 t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        gm_ttovhfee8mf28 t7 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t6));
        gm_l6 = t7;
        {
            void * t9 = gm_l0;
            gm_ttovhfee8mf28 t10 = gm_l6;
            gm_b16_8 t11 = gm_l4;
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            unsigned __int128 t14 = gm_f_rd_int_iuioodt(t9, t10, t13);
            gm_l7 = t14;
        }
        {
            void * t15 = gm_l0;
            gm_ttovhfee8mf28 t16 = gm_l6;
            gm_b16_8 t17 = gm_l5;
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            unsigned __int128 t20 = gm_f_rd_int_iuioodt(t15, t16, t19);
            gm_l8 = t20;
        }
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (!t22) goto ie21;
            {
                unsigned __int128 t24 = gm_l7;
                unsigned __int128 t25 = t24;
                __int128 t26;
                memset(&t26, 0, sizeof t26); memcpy(&t26, ((uint8_t *)&t25), sizeof t26 < 16 ? sizeof t26 : 16);
                gm_l9 = t26;
            }
            {
                unsigned __int128 t27 = gm_l8;
                unsigned __int128 t28 = t27;
                __int128 t29;
                memset(&t29, 0, sizeof t29); memcpy(&t29, ((uint8_t *)&t28), sizeof t29 < 16 ? sizeof t29 : 16);
                gm_l10 = t29;
            }
            {
                gm_t1ec8br9ddkav t30;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t37) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; case ((uint8_t)7ULL): goto ma35; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma35; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma33; case ((uint8_t)12ULL): goto ma34; case ((uint8_t)13ULL): goto ma35; default: goto ma35; }
ma32:;
                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t39 != ((uint8_t)10ULL)) goto mn38;
                __int128 t40 = gm_l9;
                __int128 t41 = gm_l10;
                uint8_t t42 = (t40 < t41);
                gm_t1ec8br9ddkav t43;
                GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t43 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t43 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t43 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t43 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t43 + 24, 0);
                ((uint8_t *)&t43)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t43 + 8), t42);
                t30 = t43;
                goto me31;
mn38:;
ma33:;
                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t45 != ((uint8_t)11ULL)) goto mn44;
                __int128 t46 = gm_l9;
                __int128 t47 = gm_l10;
                uint8_t t48 = (t46 <= t47);
                gm_t1ec8br9ddkav t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 24, 0);
                ((uint8_t *)&t49)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t49 + 8), t48);
                t30 = t49;
                goto me31;
mn44:;
ma34:;
                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t51 != ((uint8_t)12ULL)) goto mn50;
                __int128 t52 = gm_l9;
                __int128 t53 = gm_l10;
                uint8_t t54 = (t52 > t53);
                gm_t1ec8br9ddkav t55;
                GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t55 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t55 + 24, 0);
                ((uint8_t *)&t55)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t55 + 8), t54);
                t30 = t55;
                goto me31;
mn50:;
ma35:;
                __int128 t57 = gm_l9;
                __int128 t58 = gm_l10;
                uint8_t t59 = (t57 >= t58);
                gm_t1ec8br9ddkav t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t60 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t60 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
                ((uint8_t *)&t60)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t60 + 8), t59);
                t30 = t60;
                goto me31;
mn56:;
ma36:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 184, 20);
me31:;
                gm_t1ec8br9ddkav t61 = t30;
                uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
                F.ret = t30;
                goto gm_fret;
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            gm_t1ec8br9ddkav t63;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t70) { case ((uint8_t)0ULL): goto ma68; case ((uint8_t)1ULL): goto ma68; case ((uint8_t)2ULL): goto ma68; case ((uint8_t)3ULL): goto ma68; case ((uint8_t)4ULL): goto ma68; case ((uint8_t)5ULL): goto ma68; case ((uint8_t)6ULL): goto ma68; case ((uint8_t)7ULL): goto ma68; case ((uint8_t)8ULL): goto ma68; case ((uint8_t)9ULL): goto ma68; case ((uint8_t)10ULL): goto ma65; case ((uint8_t)11ULL): goto ma66; case ((uint8_t)12ULL): goto ma67; case ((uint8_t)13ULL): goto ma68; default: goto ma68; }
ma65:;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t72 != ((uint8_t)10ULL)) goto mn71;
            unsigned __int128 t73 = gm_l7;
            unsigned __int128 t74 = gm_l8;
            uint8_t t75 = (t73 < t74);
            gm_t1ec8br9ddkav t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t76 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 24, 0);
            ((uint8_t *)&t76)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t76 + 8), t75);
            t63 = t76;
            goto me64;
mn71:;
ma66:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t78 != ((uint8_t)11ULL)) goto mn77;
            unsigned __int128 t79 = gm_l7;
            unsigned __int128 t80 = gm_l8;
            uint8_t t81 = (t79 <= t80);
            gm_t1ec8br9ddkav t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t82 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 24, 0);
            ((uint8_t *)&t82)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t82 + 8), t81);
            t63 = t82;
            goto me64;
mn77:;
ma67:;
            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t84 != ((uint8_t)12ULL)) goto mn83;
            unsigned __int128 t85 = gm_l7;
            unsigned __int128 t86 = gm_l8;
            uint8_t t87 = (t85 > t86);
            gm_t1ec8br9ddkav t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t88 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t88 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t88 + 24, 0);
            ((uint8_t *)&t88)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t88 + 8), t87);
            t63 = t88;
            goto me64;
mn83:;
ma68:;
            unsigned __int128 t90 = gm_l7;
            unsigned __int128 t91 = gm_l8;
            uint8_t t92 = (t90 >= t91);
            gm_t1ec8br9ddkav t93;
            GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t93 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t93 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t93 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t93 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t93 + 24, 0);
            ((uint8_t *)&t93)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t93 + 8), t92);
            t63 = t93;
            goto me64;
mn89:;
ma69:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 191, 16);
me64:;
            gm_t1ec8br9ddkav t94 = t63;
            uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
            F.ret = t63;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t102) { case ((uint8_t)0ULL): goto ma100; case ((uint8_t)1ULL): goto ma100; case ((uint8_t)2ULL): goto ma100; case ((uint8_t)3ULL): goto ma100; case ((uint8_t)4ULL): goto ma100; case ((uint8_t)5ULL): goto ma100; case ((uint8_t)6ULL): goto ma100; case ((uint8_t)7ULL): goto ma100; case ((uint8_t)8ULL): goto ma100; case ((uint8_t)9ULL): goto ma100; case ((uint8_t)10ULL): goto ma97; case ((uint8_t)11ULL): goto ma99; case ((uint8_t)12ULL): goto ma98; case ((uint8_t)13ULL): goto ma100; default: goto ma100; }
ma97:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t104 != ((uint8_t)10ULL)) goto mn103;
        void * t105 = gm_l0;
        gm_tl874t2atnlh3 t106 = gm_l1;
        gm_t88rcucg1b7uk t107 = gm_l3;
        gm_b16_8 t108 = gm_l4;
        gm_b16_8 t109 = gm_l5;
        gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t105, t106, t107, t108, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me96;
mn103:;
ma98:;
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t111 != ((uint8_t)12ULL)) goto mn110;
        void * t112 = gm_l0;
        gm_tl874t2atnlh3 t113 = gm_l1;
        gm_t88rcucg1b7uk t114 = gm_l3;
        gm_b16_8 t115 = gm_l5;
        gm_b16_8 t116 = gm_l4;
        gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t112, t113, t114, t115, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me96;
mn110:;
ma99:;
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t118 != ((uint8_t)11ULL)) goto mn117;
        {
            void * t121 = gm_l0;
            gm_tl874t2atnlh3 t122 = gm_l1;
            gm_t88rcucg1b7uk t123 = gm_l3;
            gm_b16_8 t124 = gm_l4;
            gm_b16_8 t125 = gm_l5;
            gm_t1ec8br9ddkav t126;
            gm_f_less_nbfe87l((void *)&t126, (void *)NULL, cx, NULL, t121, t122, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t127 = t126;
            if (((((uint8_t *)&t127))[0] != 0)) {
                gm_t76123gfet73g t128 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t127) + 8));
                gm_t1ec8br9ddkav t129;
                GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                ((uint8_t *)&t129)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t129 + 8), t128);
                F.ret = t129;
                goto gm_fret;
            }
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t127) + 8));
            if (!t130) goto ie120;
            {
                gm_t1ec8br9ddkav t132;
                GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t132 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t132 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t132 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t132 + 24, 0);
                ((uint8_t *)&t132)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t132 + 8), ((uint8_t)1));
                gm_t1ec8br9ddkav t133 = t132;
                uint8_t t134 = ((((uint8_t *)&t133))[0] != 0);
                F.ret = t132;
                goto gm_fret;
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            void * t135 = gm_l0;
            gm_tl874t2atnlh3 t136 = gm_l1;
            gm_t88rcucg1b7uk t137 = gm_l3;
            gm_b16_8 t138 = gm_l4;
            gm_b16_8 t139 = gm_l5;
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t135, t136, t137, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me96;
mn117:;
ma100:;
        {
            void * t143 = gm_l0;
            gm_tl874t2atnlh3 t144 = gm_l1;
            gm_t88rcucg1b7uk t145 = gm_l3;
            gm_b16_8 t146 = gm_l5;
            gm_b16_8 t147 = gm_l4;
            gm_t1ec8br9ddkav t148;
            gm_f_less_nbfe87l((void *)&t148, (void *)NULL, cx, NULL, t143, t144, t145, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t149 = t148;
            if (((((uint8_t *)&t149))[0] != 0)) {
                gm_t76123gfet73g t150 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t149) + 8));
                gm_t1ec8br9ddkav t151;
                GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                ((uint8_t *)&t151)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t151 + 8), t150);
                F.ret = t151;
                goto gm_fret;
            }
            uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&t149) + 8));
            if (!t152) goto ie142;
            {
                gm_t1ec8br9ddkav t154;
                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t154 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t154 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t154 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t154 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t154 + 24, 0);
                ((uint8_t *)&t154)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t154 + 8), ((uint8_t)1));
                gm_t1ec8br9ddkav t155 = t154;
                uint8_t t156 = ((((uint8_t *)&t155))[0] != 0);
                F.ret = t154;
                goto gm_fret;
            }
            goto ix142;
ie142:;
ix142:;
        }
        {
            void * t157 = gm_l0;
            gm_tl874t2atnlh3 t158 = gm_l1;
            gm_t88rcucg1b7uk t159 = gm_l3;
            gm_b16_8 t160 = gm_l5;
            gm_b16_8 t161 = gm_l4;
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t157, t158, t159, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me96;
mn140:;
ma101:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 198, 5);
me96:;
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
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_contains_nugmipn F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* _elem_ty */
    gm_b16_8 gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* cty */
    gm_b16_8 gm_l5; /* c */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* elems */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* a */
    gm_b16_8 gm_l15; /* other */
    uint8_t gm_l16; /* incl */
    gm_tfbnciphf3261 gm_l17; /* lf */
    gm_tfbnciphf3261 gm_l18; /* hf */
    gm_b16_8 gm_l19; /* lo */
    gm_b16_8 gm_l20; /* hi */
    uint8_t gm_l21; /* ge_lo */
    uint8_t gm_l22; /* below */
    GM_STACK_CHECK(&F, 3223u);
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
        gm_t88rcucg1b7uk t3 = gm_l4;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t16 != ((uint8_t)11ULL)) goto pa15;
        goto po14;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t18 != ((uint8_t)10ULL)) goto pa17;
        goto po14;
pa17:;
        goto mn13;
po14:;
        {
            void * t20 = gm_l0;
            gm_tl874t2atnlh3 t21 = gm_l1;
            gm_t88rcucg1b7uk t22 = gm_l4;
            gm_b16_8 t23 = gm_l5;
            gm_tmc91gv7n9c0i t24;
            gm_f_seq_of_ifahtjs((void *)&t24, (void *)NULL, cx, NULL, t20, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmc91gv7n9c0i t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) {
                gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t25) + 8));
                gm_t1ec8br9ddkav t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), t26);
                F.ret = t27;
                goto gm_fret;
            }
            gm_t2jeg3bg7sfqa t28 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t25) + 8));
            F.l6 = t28;
        }
        {
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
            uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
            if (!t31) goto ie29;
            {
                gm_t1ec8br9ddkav t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 24, 0);
                ((uint8_t *)&t33)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t33 + 8), ((uint8_t)0));
                gm_t1ec8br9ddkav t34 = t33;
                uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
                F.ret = t33;
                goto gm_fret;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                if (t38 == 0) gm_trap("division by zero", gm_srcj10g653a00lt, 399, 25);
                uint64_t t39 = (uint64_t)(t37 / t38);
                gm_l7 = t39;
            }
            {
                for (;;) {
                    uint64_t t41 = gm_l8;
                    uint64_t t42 = gm_l7;
                    uint8_t t43 = (t41 < t42);
                    if (!t43) goto lb40;
                    {
                        uint64_t t46 = gm_l8;
                        uint64_t t47 = t46;
                        uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&t47));
                        gm_l9 = t50;
                        {
                            void * t53 = gm_l0;
                            gm_tl874t2atnlh3 t54 = gm_l1;
                            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 0));
                            uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                            void * t57 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 24)));
                            uint64_t t58 = gm_l9;
                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                            uint64_t r60; bool o61 = __builtin_mul_overflow(t58, t59, &r60);
                            if (o61) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 400, 53);
                            uint64_t t62 = r60;
                            uint64_t t63 = (uintptr_t)t62;
                            uint64_t t64 = gm_l9;
                            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                            if (o66) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 400, 65);
                            uint64_t t67 = r65;
                            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                            uint64_t r69; bool o70 = __builtin_mul_overflow(t67, t68, &r69);
                            if (o70) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 400, 64);
                            uint64_t t71 = r69;
                            uint64_t t72 = (uintptr_t)t71;
                            if (t63 > t72 || t72 > (uintptr_t)t56) gm_trap_slice(t63, t72, t56, gm_srcj10g653a00lt, 400, 46);
                            gm_b16_8 t73;
                            GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t57 + t63 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72 - t63));
                            gm_b16_8 t74 = gm_l3;
                            gm_t1ec8br9ddkav t75;
                            gm_f_equal_e5vf82h((void *)&t75, (void *)NULL, cx, NULL, t53, t54, t55, t73, t74);
                            if (gm_unw.kind) {
                                goto gm_ll44;
                            }
                            gm_t1ec8br9ddkav t76 = t75;
                            if (((((uint8_t *)&t76))[0] != 0)) {
                                gm_t76123gfet73g t77 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t76) + 8));
                                gm_t1ec8br9ddkav t78;
                                GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                                ((uint8_t *)&t78)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t78 + 8), t77);
                                F.ret = t78;
                                goto gm_fret;
                            }
                            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t76) + 8));
                            if (!t79) goto ie52;
                            {
                                gm_t1ec8br9ddkav t81;
                                GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t81 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t81 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t81 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t81 + 24, 0);
                                ((uint8_t *)&t81)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t81 + 8), ((uint8_t)1));
                                gm_t1ec8br9ddkav t82 = t81;
                                uint8_t t83 = ((((uint8_t *)&t82))[0] != 0);
                                F.ret = t81;
                                goto gm_fret;
                            }
                            goto ix52;
ie52:;
ix52:;
                        }
                        goto me48;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 399, 13);
me48:;
                    }
lc40:;
                    {
                        uint64_t t85 = gm_l8;
                        uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                        uint64_t t88 = r86;
                        gm_l8 = t88;
                    }
                }
gm_ll44:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
                }
                goto gm_lad;
lb40:;
            }
        }
        {
            gm_t1ec8br9ddkav t89;
            GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t89 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 24, 0);
            ((uint8_t *)&t89)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t89 + 8), ((uint8_t)0));
            gm_t1ec8br9ddkav t90 = t89;
            uint8_t t91 = ((((uint8_t *)&t90))[0] != 0);
            F.ret = t89;
            goto gm_fret;
        }
        goto me6;
mn13:;
ma8:;
        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t93 != ((uint8_t)12ULL)) goto mn92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l10 = t94;
        {
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                gm_l11 = t97;
            }
            {
                for (;;) {
                    uint64_t t99 = gm_l12;
                    uint64_t t100 = gm_l11;
                    uint8_t t101 = (t99 < t100);
                    if (!t101) goto lb98;
                    {
                        uint64_t t104 = gm_l12;
                        uint64_t t105 = t104;
                        uint64_t t108 = GM_LD(uint64_t, ((uint8_t *)&t105));
                        gm_l13 = t108;
                        {
                            void * t111 = gm_l0;
                            gm_tl874t2atnlh3 t112 = gm_l1;
                            uint64_t t113 = gm_l13;
                            uint64_t t114 = (uintptr_t)t113;
                            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_srcj10g653a00lt, 406, 38);
                            gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t114 * 4));
                            void * t117 = gm_l0;
                            gm_t88rcucg1b7uk t118 = gm_l4;
                            gm_b16_8 t119 = gm_l5;
                            uint64_t t120 = gm_l13;
                            uint32_t t121 = (uint32_t)t120;
                            gm_b16_8 t122 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t117, t118, t119, t121);
                            if (gm_unw.kind) {
                                goto gm_ll102;
                            }
                            gm_b16_8 t123 = gm_l3;
                            gm_t1ec8br9ddkav t124;
                            gm_f_equal_e5vf82h((void *)&t124, (void *)NULL, cx, NULL, t111, t112, t116, t122, t123);
                            if (gm_unw.kind) {
                                goto gm_ll102;
                            }
                            gm_t1ec8br9ddkav t125 = t124;
                            if (((((uint8_t *)&t125))[0] != 0)) {
                                gm_t76123gfet73g t126 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t125) + 8));
                                gm_t1ec8br9ddkav t127;
                                GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                                ((uint8_t *)&t127)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t127 + 8), t126);
                                F.ret = t127;
                                goto gm_fret;
                            }
                            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t125) + 8));
                            if (!t128) goto ie110;
                            {
                                gm_t1ec8br9ddkav t130;
                                GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t130 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t130 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t130 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t130 + 24, 0);
                                ((uint8_t *)&t130)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t130 + 8), ((uint8_t)1));
                                gm_t1ec8br9ddkav t131 = t130;
                                uint8_t t132 = ((((uint8_t *)&t131))[0] != 0);
                                F.ret = t130;
                                goto gm_fret;
                            }
                            goto ix110;
ie110:;
ix110:;
                        }
                        goto me106;
mn107:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 405, 13);
me106:;
                    }
lc98:;
                    {
                        uint64_t t134 = gm_l12;
                        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                        uint64_t t137 = r135;
                        gm_l12 = t137;
                    }
                }
gm_ll102:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb98; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc98; }
                }
                goto gm_lad;
lb98:;
            }
        }
        {
            gm_t1ec8br9ddkav t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t138 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t138 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t138 + 24, 0);
            ((uint8_t *)&t138)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t138 + 8), ((uint8_t)0));
            gm_t1ec8br9ddkav t139 = t138;
            uint8_t t140 = ((((uint8_t *)&t139))[0] != 0);
            F.ret = t138;
            goto gm_fret;
        }
        goto me6;
mn92:;
ma9:;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t142 != ((uint8_t)17ULL)) goto mn141;
        {
            void * t144 = gm_l0;
            gm_t88rcucg1b7uk t145 = gm_l4;
            void * t146 = gm_f_adt_of_8k5vqud(t144, GM_CONV(gm_csguankko4lmhd, t145));
            void * t147 = t146;
            void * t148;
            if ((GM_LD(void *, ((uint8_t *)&t147)) != 0)) {
                void * t149 = GM_LD(void *, (((uint8_t *)&t147) + 0));
                t148 = t149;
            } else {
                void * t150 = gm_l0;
                gm_tl874t2atnlh3 t151 = gm_l1;
                gm_b16_8 t152;
                GM_ST(void *, (uint8_t *)&t152, (void *)(gm_sa2pnk1jujp5p));
                GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(22));
                gm_t76123gfet73g t153;
                gm_f_trap_qh84l9d((void *)&t153, (void *)NULL, cx, NULL, t150, t151, t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t154;
                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                ((uint8_t *)&t154)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t154 + 8), t153);
                gm_t1ec8br9ddkav t155 = t154;
                uint8_t t156 = ((((uint8_t *)&t155))[0] != 0);
                F.ret = t154;
                goto gm_fret;
            }
            gm_l14 = t148;
        }
        {
            void * t157 = gm_l14;
            uint8_t t158;
            if (!((GM_LD(uintptr_t, (((uint8_t *)t157) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)t157) + 0)), gm_seue7mq07v0oc, 5) == 0)) goto mn160;
            t158 = ((uint8_t)0);
            goto me159;
mn160:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)t157) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)t157) + 0)), gm_s43cq4krigs2b, 9) == 0)) goto mn161;
            t158 = ((uint8_t)1);
            goto me159;
mn161:;
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)t157) + 0));
            gm_l15 = t163;
            void * t164 = gm_l0;
            gm_tl874t2atnlh3 t165 = gm_l1;
            gm_b16_8 t166 = gm_l15;
            gm_tnpifv20irnph t167;
            GM_ST(gm_b16_8, ((uint8_t *)&t167 + 0), t166);
            gm_tabb710d9mq2e t168;
            gm_f_format_kfcf8tk((void *)&t168, (void *)NULL, cx, NULL, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t169 = t168;
            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 8, gm_srcj10g653a00lt, 415, 47);
            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t169) + 8));
            gm_b16_8 t171 = t170;
            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
            gm_t76123gfet73g t173;
            gm_f_trap_qh84l9d((void *)&t173, (void *)NULL, cx, NULL, t164, t165, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t174;
            GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
            ((uint8_t *)&t174)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t174 + 8), t173);
            gm_t1ec8br9ddkav t175 = t174;
            uint8_t t176 = ((((uint8_t *)&t175))[0] != 0);
            F.ret = t174;
            goto gm_fret;
            goto me159;
mn162:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 412, 21);
me159:;
            gm_l16 = t158;
        }
        {
            void * t177 = gm_l0;
            gm_t88rcucg1b7uk t178 = gm_l4;
            gm_tfbnciphf3261 t179 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t177, t178, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t179;
        }
        {
            void * t180 = gm_l0;
            gm_t88rcucg1b7uk t181 = gm_l4;
            gm_tfbnciphf3261 t182 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t180, t181, ((uint32_t)1ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t182;
        }
        {
            gm_b16_8 t183 = gm_l5;
            gm_b16_8 t184 = t183;
            uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&t184) + 8);
            void * t186 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t184)));
            uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint64_t t188 = gm_f_us_cuv01fp(t187);
            uint64_t t189 = (uintptr_t)t188;
            uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint64_t t191 = gm_f_us_cuv01fp(t190);
            void * t192 = gm_l0;
            gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            uint64_t t194 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t192, t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t195 = gm_f_us_cuv01fp(t194);
            uint64_t r196; bool o197 = __builtin_add_overflow(t191, t195, &r196);
            if (o197) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 419, 33);
            uint64_t t198 = r196;
            uint64_t t199 = (uintptr_t)t198;
            if (t189 > t199 || t199 > (uintptr_t)t185) gm_trap_slice(t189, t199, t185, gm_srcj10g653a00lt, 419, 19);
            gm_b16_8 t200;
            GM_ST(void *, (uint8_t *)&t200, (void *)(((uint8_t *)t186 + t189 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(t199 - t189));
            gm_l19 = t200;
        }
        {
            gm_b16_8 t201 = gm_l5;
            gm_b16_8 t202 = t201;
            uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&t202) + 8);
            void * t204 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t202)));
            uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint64_t t206 = gm_f_us_cuv01fp(t205);
            uint64_t t207 = (uintptr_t)t206;
            uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint64_t t209 = gm_f_us_cuv01fp(t208);
            void * t210 = gm_l0;
            gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 0));
            uint64_t t212 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t213 = gm_f_us_cuv01fp(t212);
            uint64_t r214; bool o215 = __builtin_add_overflow(t209, t213, &r214);
            if (o215) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 420, 33);
            uint64_t t216 = r214;
            uint64_t t217 = (uintptr_t)t216;
            if (t207 > t217 || t217 > (uintptr_t)t203) gm_trap_slice(t207, t217, t203, gm_srcj10g653a00lt, 420, 19);
            gm_b16_8 t218;
            GM_ST(void *, (uint8_t *)&t218, (void *)(((uint8_t *)t204 + t207 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(t217 - t207));
            gm_l20 = t218;
        }
        {
            void * t219 = gm_l0;
            gm_tl874t2atnlh3 t220 = gm_l1;
            gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_b16_8 t222 = gm_l3;
            gm_b16_8 t223 = gm_l19;
            gm_t1ec8br9ddkav t224;
            gm_f_less_nbfe87l((void *)&t224, (void *)NULL, cx, NULL, t219, t220, t221, t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t225 = t224;
            if (((((uint8_t *)&t225))[0] != 0)) {
                gm_t76123gfet73g t226 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t225) + 8));
                gm_t1ec8br9ddkav t227;
                GM_ST(uint8_t, (uint8_t *)&t227 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t227 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                ((uint8_t *)&t227)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t227 + 8), t226);
                F.ret = t227;
                goto gm_fret;
            }
            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&t225) + 8));
            uint8_t t229 = (uint8_t)!t228;
            gm_l21 = t229;
        }
        {
            uint8_t t230;
            uint8_t t232 = gm_l16;
            if (!t232) goto ie231;
            void * t234 = gm_l0;
            gm_tl874t2atnlh3 t235 = gm_l1;
            gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_b16_8 t237 = gm_l20;
            gm_b16_8 t238 = gm_l3;
            gm_t1ec8br9ddkav t239;
            gm_f_less_nbfe87l((void *)&t239, (void *)NULL, cx, NULL, t234, t235, t236, t237, t238);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t240 = t239;
            if (((((uint8_t *)&t240))[0] != 0)) {
                gm_t76123gfet73g t241 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t240) + 8));
                gm_t1ec8br9ddkav t242;
                GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                ((uint8_t *)&t242)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t242 + 8), t241);
                F.ret = t242;
                goto gm_fret;
            }
            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&t240) + 8));
            uint8_t t244 = (uint8_t)!t243;
            uint8_t t245 = t244;
            t230 = t245;
            goto ix231;
ie231:;
            void * t247 = gm_l0;
            gm_tl874t2atnlh3 t248 = gm_l1;
            gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_b16_8 t250 = gm_l3;
            gm_b16_8 t251 = gm_l20;
            gm_t1ec8br9ddkav t252;
            gm_f_less_nbfe87l((void *)&t252, (void *)NULL, cx, NULL, t247, t248, t249, t250, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t253 = t252;
            if (((((uint8_t *)&t253))[0] != 0)) {
                gm_t76123gfet73g t254 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t253) + 8));
                gm_t1ec8br9ddkav t255;
                GM_ST(uint8_t, (uint8_t *)&t255 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t255 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t255 + 4, 0);
                ((uint8_t *)&t255)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t255 + 8), t254);
                F.ret = t255;
                goto gm_fret;
            }
            uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&t253) + 8));
            uint8_t t257 = t256;
            t230 = t257;
ix231:;
            gm_l22 = t230;
        }
        {
            uint8_t t258 = 0;
            uint8_t t259 = gm_l21;
            if (t259) {
                uint8_t t260 = gm_l22;
                t258 = t260;
            } else t258 = 0;
            gm_t1ec8br9ddkav t261;
            GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t261 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t261 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t261 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 24, 0);
            ((uint8_t *)&t261)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t261 + 8), t258);
            gm_t1ec8br9ddkav t262 = t261;
            uint8_t t263 = ((((uint8_t *)&t262))[0] != 0);
            F.ret = t261;
            goto gm_fret;
        }
        goto me6;
mn141:;
ma10:;
        void * t265 = gm_l0;
        gm_tl874t2atnlh3 t266 = gm_l1;
        void * t267 = gm_l0;
        void * t268 = GM_LD(void *, (((uint8_t *)t267) + 8));
        gm_t88rcucg1b7uk t269 = gm_l4;
        gm_b16_8 t270 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t268, t269);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t271;
        GM_ST(gm_b16_8, ((uint8_t *)&t271 + 0), t270);
        gm_tabb710d9mq2e t272;
        gm_f_format_kfcf8tk((void *)&t272, (void *)NULL, cx, NULL, t271);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t273 = t272;
        if (((((uint8_t *)&t273))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t273) + 8, gm_srcj10g653a00lt, 425, 35);
        gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)&t273) + 8));
        gm_b16_8 t275 = t274;
        gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
        gm_t76123gfet73g t277;
        gm_f_trap_qh84l9d((void *)&t277, (void *)NULL, cx, NULL, t265, t266, t276);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1ec8br9ddkav t278;
        GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
        ((uint8_t *)&t278)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t278 + 8), t277);
        gm_t1ec8br9ddkav t279 = t278;
        uint8_t t280 = ((((uint8_t *)&t279))[0] != 0);
        F.ret = t278;
        goto gm_fret;
        goto me6;
mn264:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 395, 5);
me6:;
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
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_equal_e5vf82h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* x */
    gm_b16_8 gm_l4; /* y */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* elems */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    uint64_t gm_l12; /* esz */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    gm_t2lqu8jsj511d gm_l17; /* pa */
    gm_t2lqu8jsj511d gm_l18; /* pb */
    uint8_t gm_l19; /* is_float */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* inner */
    uint8_t gm_l26; /* pa */
    uint8_t gm_l27; /* pb */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    uint8_t gm_l30; /* ea */
    gm_t88rcucg1b7uk gm_l31; /* u */
    gm_tcmkompbl09il gm_l32; /* u */
    uint32_t gm_l33; /* va */
    uint32_t gm_l34; /* vb */
    gm_t88rcucg1b7uk gm_l35; /* pt */
    uint64_t gm_l36; /* off */
    uint64_t gm_l37; /* n */
    void * gm_l38; /* a */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_tfbnciphf3261 gm_l43; /* fl */
    uint64_t gm_l44; /* off */
    uint64_t gm_l45; /* n */
    GM_STACK_CHECK(&F, 7798u);
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
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma15; case ((uint8_t)14ULL): goto ma16; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma18; case ((uint8_t)21ULL): goto ma18; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma18; case ((uint8_t)24ULL): goto ma18; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma18; default: goto ma18; }
ma7:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t24 != ((uint8_t)0ULL)) goto pa23;
        goto po22;
pa23:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t26 != ((uint8_t)1ULL)) goto pa25;
        goto po22;
pa25:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t28 != ((uint8_t)2ULL)) goto pa27;
        goto po22;
pa27:;
        goto mn21;
po22:;
        {
            void * t30 = gm_l0;
            gm_t88rcucg1b7uk t31 = gm_l2;
            gm_tg5lql6itufk7 t32 = gm_f_int_info_ulcevdv(t30, GM_CONV(gm_csguankko4lmhd, t31));
            gm_tg5lql6itufk7 t33 = t32;
            gm_ttovhfee8mf28 t34;
            if (((((uint8_t *)&t33))[8] != 0)) {
                gm_ttovhfee8mf28 t35 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t33) + 0));
                t34 = t35;
            } else {
                gm_t1ec8br9ddkav t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t36 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
                ((uint8_t *)&t36)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t36 + 8), ((uint8_t)0));
                gm_t1ec8br9ddkav t37 = t36;
                uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
                F.ret = t36;
                goto gm_fret;
            }
            gm_l5 = t34;
        }
        {
            void * t39 = gm_l0;
            gm_ttovhfee8mf28 t40 = gm_l5;
            gm_b16_8 t41 = gm_l3;
            gm_b16_8 t42 = t41;
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            unsigned __int128 t44 = gm_f_rd_int_iuioodt(t39, t40, t43);
            void * t45 = gm_l0;
            gm_ttovhfee8mf28 t46 = gm_l5;
            gm_b16_8 t47 = gm_l4;
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            unsigned __int128 t50 = gm_f_rd_int_iuioodt(t45, t46, t49);
            uint8_t t51 = ((t44 == t50));
            gm_t1ec8br9ddkav t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t52 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
            ((uint8_t *)&t52)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t52 + 8), t51);
            gm_t1ec8br9ddkav t53 = t52;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t52;
            goto gm_fret;
        }
        goto me6;
mn21:;
ma8:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t56 != ((uint8_t)3ULL)) goto mn55;
        void * t57 = gm_l0;
        gm_t88rcucg1b7uk t58 = gm_l2;
        gm_b16_8 t59 = gm_l3;
        gm_b16_8 t60 = t59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        double t62 = gm_f_get_float_grmhlm4(t57, GM_CONV(gm_csguankko4lmhd, t58), t61);
        void * t63 = gm_l0;
        gm_t88rcucg1b7uk t64 = gm_l2;
        gm_b16_8 t65 = gm_l4;
        gm_b16_8 t66 = t65;
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        double t68 = gm_f_get_float_grmhlm4(t63, GM_CONV(gm_csguankko4lmhd, t64), t67);
        uint8_t t69 = ((t62 == t68));
        gm_t1ec8br9ddkav t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t70 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
        ((uint8_t *)&t70)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t70 + 8), t69);
        gm_t1ec8br9ddkav t71 = t70;
        uint8_t t72 = ((((uint8_t *)&t71))[0] != 0);
        F.ret = t70;
        goto gm_fret;
        goto me6;
mn55:;
ma9:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t74 != ((uint8_t)4ULL)) goto mn73;
        uint64_t t75 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcj10g653a00lt, 295, 26);
        uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t75 * 1));
        uint8_t t78 = (!(t77 == ((uint8_t)0ULL)));
        uint64_t t79 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srcj10g653a00lt, 295, 45);
        uint8_t t81 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t79 * 1));
        uint8_t t82 = (!(t81 == ((uint8_t)0ULL)));
        uint8_t t83 = ((t78 == t82));
        gm_t1ec8br9ddkav t84;
        GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t84 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t84 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t84 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
        ((uint8_t *)&t84)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t84 + 8), t83);
        gm_t1ec8br9ddkav t85 = t84;
        uint8_t t86 = ((((uint8_t *)&t85))[0] != 0);
        F.ret = t84;
        goto gm_fret;
        goto me6;
mn73:;
ma10:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t90 != ((uint8_t)8ULL)) goto pa89;
        goto po88;
pa89:;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t92 != ((uint8_t)9ULL)) goto pa91;
        goto po88;
pa91:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t94 != ((uint8_t)7ULL)) goto pa93;
        goto po88;
pa93:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t96 != ((uint8_t)16ULL)) goto pa95;
        goto po88;
pa95:;
        goto mn87;
po88:;
        void * t97 = gm_l0;
        gm_b16_8 t98 = gm_l3;
        gm_b16_8 t99 = t98;
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        uint64_t t101 = gm_f_ptr_of_b77b4c9(t97, t100);
        void * t102 = gm_l0;
        gm_b16_8 t103 = gm_l4;
        gm_b16_8 t104 = t103;
        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
        uint64_t t106 = gm_f_ptr_of_b77b4c9(t102, t105);
        uint8_t t107 = ((t101 == t106));
        gm_t1ec8br9ddkav t108;
        GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t108 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t108 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t108 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t108 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t108 + 24, 0);
        ((uint8_t *)&t108)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t108 + 8), t107);
        gm_t1ec8br9ddkav t109 = t108;
        uint8_t t110 = ((((uint8_t *)&t109))[0] != 0);
        F.ret = t108;
        goto gm_fret;
        goto me6;
mn87:;
ma11:;
        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t112 != ((uint8_t)5ULL)) goto mn111;
        gm_t1ec8br9ddkav t113;
        GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t113 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t113 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t113 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t113 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t113 + 24, 0);
        ((uint8_t *)&t113)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t113 + 8), ((uint8_t)1));
        gm_t1ec8br9ddkav t114 = t113;
        uint8_t t115 = ((((uint8_t *)&t114))[0] != 0);
        F.ret = t113;
        goto gm_fret;
        goto me6;
mn111:;
ma12:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t117 != ((uint8_t)12ULL)) goto mn116;
        gm_b16_8 t118 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l6 = t118;
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                gm_l7 = t121;
            }
            {
                for (;;) {
                    uint64_t t123 = gm_l8;
                    uint64_t t124 = gm_l7;
                    uint8_t t125 = (t123 < t124);
                    if (!t125) goto lb122;
                    {
                        uint64_t t128 = gm_l8;
                        uint64_t t129 = t128;
                        uint64_t t132 = GM_LD(uint64_t, ((uint8_t *)&t129));
                        gm_l9 = t132;
                        {
                            void * t135 = gm_l0;
                            gm_tl874t2atnlh3 t136 = gm_l1;
                            uint64_t t137 = gm_l9;
                            uint64_t t138 = (uintptr_t)t137;
                            uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t138 >= (uintptr_t)(t139)) gm_trap_index(t138, t139, gm_srcj10g653a00lt, 300, 42);
                            gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t138 * 4));
                            void * t141 = gm_l0;
                            gm_t88rcucg1b7uk t142 = gm_l2;
                            gm_b16_8 t143 = gm_l3;
                            uint64_t t144 = gm_l9;
                            uint32_t t145 = (uint32_t)t144;
                            gm_b16_8 t146 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t141, t142, t143, t145);
                            if (gm_unw.kind) {
                                goto gm_ll126;
                            }
                            void * t147 = gm_l0;
                            gm_t88rcucg1b7uk t148 = gm_l2;
                            gm_b16_8 t149 = gm_l4;
                            uint64_t t150 = gm_l9;
                            uint32_t t151 = (uint32_t)t150;
                            gm_b16_8 t152 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t147, t148, t149, t151);
                            if (gm_unw.kind) {
                                goto gm_ll126;
                            }
                            gm_t1ec8br9ddkav t153;
                            gm_f_equal_e5vf82h((void *)&t153, (void *)NULL, cx, NULL, t135, t136, t140, t146, t152);
                            if (gm_unw.kind) {
                                goto gm_ll126;
                            }
                            gm_t1ec8br9ddkav t154 = t153;
                            if (((((uint8_t *)&t154))[0] != 0)) {
                                gm_t76123gfet73g t155 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t154) + 8));
                                gm_t1ec8br9ddkav t156;
                                GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
                                ((uint8_t *)&t156)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t156 + 8), t155);
                                F.ret = t156;
                                goto gm_fret;
                            }
                            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&t154) + 8));
                            uint8_t t158 = (uint8_t)!t157;
                            if (!t158) goto ie134;
                            {
                                gm_t1ec8br9ddkav t160;
                                GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t160 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t160 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t160 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t160 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t160 + 24, 0);
                                ((uint8_t *)&t160)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t160 + 8), ((uint8_t)0));
                                gm_t1ec8br9ddkav t161 = t160;
                                uint8_t t162 = ((((uint8_t *)&t161))[0] != 0);
                                F.ret = t160;
                                goto gm_fret;
                            }
                            goto ix134;
ie134:;
ix134:;
                        }
                        goto me130;
mn131:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 299, 13);
me130:;
                    }
lc122:;
                    {
                        uint64_t t164 = gm_l8;
                        uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                        uint64_t t167 = r165;
                        gm_l8 = t167;
                    }
                }
gm_ll126:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb122; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc122; }
                }
                goto gm_lad;
lb122:;
            }
        }
        {
            gm_t1ec8br9ddkav t168;
            GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t168 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t168 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t168 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t168 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t168 + 24, 0);
            ((uint8_t *)&t168)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t168 + 8), ((uint8_t)1));
            gm_t1ec8br9ddkav t169 = t168;
            uint8_t t170 = ((((uint8_t *)&t169))[0] != 0);
            F.ret = t168;
            goto gm_fret;
        }
        goto me6;
mn116:;
ma13:;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t172 != ((uint8_t)11ULL)) goto mn171;
        uint64_t t173 = GM_LD(uint64_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l10 = t173;
        gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
        gm_l11 = t174;
        {
            void * t176 = gm_l0;
            gm_t88rcucg1b7uk t177 = gm_l11;
            uint64_t t178 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t176, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t179 = gm_f_us_cuv01fp(t178);
            gm_l12 = t179;
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t181 = gm_l10;
                uint64_t t182 = gm_f_us_cuv01fp(t181);
                gm_l13 = t182;
            }
            {
                for (;;) {
                    uint64_t t184 = gm_l14;
                    uint64_t t185 = gm_l13;
                    uint8_t t186 = (t184 < t185);
                    if (!t186) goto lb183;
                    {
                        uint64_t t189 = gm_l14;
                        uint64_t t190 = t189;
                        uint64_t t193 = GM_LD(uint64_t, ((uint8_t *)&t190));
                        gm_l15 = t193;
                        {
                            void * t196 = gm_l0;
                            gm_tl874t2atnlh3 t197 = gm_l1;
                            gm_t88rcucg1b7uk t198 = gm_l11;
                            uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            void * t200 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                            uint64_t t201 = gm_l15;
                            uint64_t t202 = gm_l12;
                            uint64_t r203; bool o204 = __builtin_mul_overflow(t201, t202, &r203);
                            if (o204) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 50);
                            uint64_t t205 = r203;
                            uint64_t t206 = (uintptr_t)t205;
                            uint64_t t207 = gm_l15;
                            uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                            if (o209) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 307, 60);
                            uint64_t t210 = r208;
                            uint64_t t211 = gm_l12;
                            uint64_t r212; bool o213 = __builtin_mul_overflow(t210, t211, &r212);
                            if (o213) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 59);
                            uint64_t t214 = r212;
                            uint64_t t215 = (uintptr_t)t214;
                            if (t206 > t215 || t215 > (uintptr_t)t199) gm_trap_slice(t206, t215, t199, gm_srcj10g653a00lt, 307, 48);
                            gm_b16_8 t216;
                            GM_ST(void *, (uint8_t *)&t216, (void *)(((uint8_t *)t200 + t206 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(t215 - t206));
                            uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            void * t218 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                            uint64_t t219 = gm_l15;
                            uint64_t t220 = gm_l12;
                            uint64_t r221; bool o222 = __builtin_mul_overflow(t219, t220, &r221);
                            if (o222) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 77);
                            uint64_t t223 = r221;
                            uint64_t t224 = (uintptr_t)t223;
                            uint64_t t225 = gm_l15;
                            uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                            if (o227) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 307, 87);
                            uint64_t t228 = r226;
                            uint64_t t229 = gm_l12;
                            uint64_t r230; bool o231 = __builtin_mul_overflow(t228, t229, &r230);
                            if (o231) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 86);
                            uint64_t t232 = r230;
                            uint64_t t233 = (uintptr_t)t232;
                            if (t224 > t233 || t233 > (uintptr_t)t217) gm_trap_slice(t224, t233, t217, gm_srcj10g653a00lt, 307, 75);
                            gm_b16_8 t234;
                            GM_ST(void *, (uint8_t *)&t234, (void *)(((uint8_t *)t218 + t224 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(t233 - t224));
                            gm_t1ec8br9ddkav t235;
                            gm_f_equal_e5vf82h((void *)&t235, (void *)NULL, cx, NULL, t196, t197, t198, t216, t234);
                            if (gm_unw.kind) {
                                goto gm_ll187;
                            }
                            gm_t1ec8br9ddkav t236 = t235;
                            if (((((uint8_t *)&t236))[0] != 0)) {
                                gm_t76123gfet73g t237 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t236) + 8));
                                gm_t1ec8br9ddkav t238;
                                GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                                ((uint8_t *)&t238)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t238 + 8), t237);
                                F.ret = t238;
                                goto gm_fret;
                            }
                            uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&t236) + 8));
                            uint8_t t240 = (uint8_t)!t239;
                            if (!t240) goto ie195;
                            {
                                gm_t1ec8br9ddkav t242;
                                GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t242 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t242 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t242 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t242 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t242 + 24, 0);
                                ((uint8_t *)&t242)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t242 + 8), ((uint8_t)0));
                                gm_t1ec8br9ddkav t243 = t242;
                                uint8_t t244 = ((((uint8_t *)&t243))[0] != 0);
                                F.ret = t242;
                                goto gm_fret;
                            }
                            goto ix195;
ie195:;
ix195:;
                        }
                        goto me191;
mn192:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 306, 13);
me191:;
                    }
lc183:;
                    {
                        uint64_t t246 = gm_l14;
                        uint64_t r247; bool o248 = __builtin_add_overflow(t246, ((uint64_t)1ULL), &r247);
                        uint64_t t249 = r247;
                        gm_l14 = t249;
                    }
                }
gm_ll187:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb183; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc183; }
                }
                goto gm_lad;
lb183:;
            }
        }
        {
            gm_t1ec8br9ddkav t250;
            GM_ST(uint8_t, (uint8_t *)&t250 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t250 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t250 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t250 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t250 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t250 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t250 + 24, 0);
            ((uint8_t *)&t250)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t250 + 8), ((uint8_t)1));
            gm_t1ec8br9ddkav t251 = t250;
            uint8_t t252 = ((((uint8_t *)&t251))[0] != 0);
            F.ret = t250;
            goto gm_fret;
        }
        goto me6;
mn171:;
ma14:;
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t254 != ((uint8_t)10ULL)) goto mn253;
        gm_t88rcucg1b7uk t255 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l16 = t255;
        {
            void * t257 = gm_l0;
            gm_b16_8 t258 = gm_l3;
            gm_b16_8 t259 = t258;
            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
            gm_t2lqu8jsj511d t261 = gm_f_slice_parts_1se5hu4(t257, t260);
            gm_l17 = t261;
        }
        {
            void * t262 = gm_l0;
            gm_b16_8 t263 = gm_l4;
            gm_b16_8 t264 = t263;
            gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
            gm_t2lqu8jsj511d t266 = gm_f_slice_parts_1se5hu4(t262, t265);
            gm_l18 = t266;
        }
        {
            uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint8_t t270 = (!(t268 == t269));
            if (!t270) goto ie267;
            {
                gm_t1ec8br9ddkav t272;
                GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t272 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t272 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t272 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t272 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t272 + 24, 0);
                ((uint8_t *)&t272)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t272 + 8), ((uint8_t)0));
                gm_t1ec8br9ddkav t273 = t272;
                uint8_t t274 = ((((uint8_t *)&t273))[0] != 0);
                F.ret = t272;
                goto gm_fret;
            }
            goto ix267;
ie267:;
ix267:;
        }
        {
            void * t275 = gm_l0;
            gm_t88rcucg1b7uk t276 = gm_l16;
            gm_t53at80pfdus3 t277;
            gm_f_kind_htv6093((void *)&t277, t275, GM_CONV(gm_csguankko4lmhd, t276));
            gm_t53at80pfdus3 t278 = t277;
            uint8_t t279;
            uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&t278) + 0));
            if (t282 != ((uint8_t)3ULL)) goto mn281;
            t279 = ((uint8_t)1);
            goto me280;
mn281:;
            t279 = ((uint8_t)0);
            goto me280;
mn283:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 315, 25);
me280:;
            gm_l19 = t279;
        }
        {
            uint8_t t285 = 0;
            uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 0));
            uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t288 = ((t286 == t287));
            if (!t288) {
                uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t290 = ((t289 == ((uint64_t)0ULL)));
                t285 = t290;
            } else t285 = 1;
            if (!t285) goto ie284;
            uint8_t t291 = 0;
            uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint8_t t293 = ((t292 == ((uint64_t)0ULL)));
            if (!t293) {
                uint8_t t294 = gm_l19;
                uint8_t t295 = (uint8_t)!t294;
                t291 = t295;
            } else t291 = 1;
            if (!t291) goto ie284;
            {
                gm_t1ec8br9ddkav t297;
                GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t297 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t297 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t297 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t297 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t297 + 24, 0);
                ((uint8_t *)&t297)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t297 + 8), ((uint8_t)1));
                gm_t1ec8br9ddkav t298 = t297;
                uint8_t t299 = ((((uint8_t *)&t298))[0] != 0);
                F.ret = t297;
                goto gm_fret;
            }
            goto ix284;
ie284:;
ix284:;
        }
        {
            void * t300 = gm_l0;
            gm_tl874t2atnlh3 t301 = gm_l1;
            gm_t88rcucg1b7uk t302 = gm_l2;
            gm_b16_8 t303 = gm_l3;
            gm_tmc91gv7n9c0i t304;
            gm_f_seq_of_ifahtjs((void *)&t304, (void *)NULL, cx, NULL, t300, t301, t302, t303);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmc91gv7n9c0i t305 = t304;
            if (((((uint8_t *)&t305))[0] != 0)) {
                gm_t76123gfet73g t306 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t305) + 8));
                gm_t1ec8br9ddkav t307;
                GM_ST(uint8_t, (uint8_t *)&t307 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t307 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t307 + 4, 0);
                ((uint8_t *)&t307)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t307 + 8), t306);
                F.ret = t307;
                goto gm_fret;
            }
            gm_t2jeg3bg7sfqa t308 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t305) + 8));
            F.l20 = t308;
        }
        {
            void * t309 = gm_l0;
            gm_tl874t2atnlh3 t310 = gm_l1;
            gm_t88rcucg1b7uk t311 = gm_l2;
            gm_b16_8 t312 = gm_l4;
            gm_tmc91gv7n9c0i t313;
            gm_f_seq_of_ifahtjs((void *)&t313, (void *)NULL, cx, NULL, t309, t310, t311, t312);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmc91gv7n9c0i t314 = t313;
            if (((((uint8_t *)&t314))[0] != 0)) {
                gm_t76123gfet73g t315 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t314) + 8));
                gm_t1ec8br9ddkav t316;
                GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
                ((uint8_t *)&t316)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t316 + 8), t315);
                F.ret = t316;
                goto gm_fret;
            }
            gm_t2jeg3bg7sfqa t317 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t314) + 8));
            F.l21 = t317;
        }
        {
            uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
            uint8_t t320 = ((t319 == ((uint64_t)1ULL)));
            if (!t320) goto ie318;
            void * t321 = gm_l0;
            gm_t88rcucg1b7uk t322 = gm_l16;
            gm_t53at80pfdus3 t323;
            gm_f_kind_htv6093((void *)&t323, t321, GM_CONV(gm_csguankko4lmhd, t322));
            gm_t53at80pfdus3 t324 = t323;
            uint8_t t325;
            uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&t324) + 0));
            if (t328 != ((uint8_t)0ULL)) goto mn327;
            t325 = ((uint8_t)1);
            goto me326;
mn327:;
            t325 = ((uint8_t)0);
            goto me326;
mn329:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 320, 32);
me326:;
            if (!t325) goto ie318;
            {
                uint8_t t331 = 0;
                uint64_t t332 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 24) + 8);
                uint64_t t333 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 24) + 8);
                uint8_t t334 = ((t332 == t333));
                if (t334) {
                    gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 24));
                    gm_b16_8 t336 = t335;
                    gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&t336));
                    gm_b16_8 t338 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 24));
                    gm_b16_8 t339 = t338;
                    gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                    uint8_t t341 = gm_f_bytes_equal_cksjlgj(t337, t340);
                    t331 = t341;
                } else t331 = 0;
                gm_t1ec8br9ddkav t342;
                GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t342 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t342 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t342 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t342 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t342 + 24, 0);
                ((uint8_t *)&t342)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t342 + 8), t331);
                gm_t1ec8br9ddkav t343 = t342;
                uint8_t t344 = ((((uint8_t *)&t343))[0] != 0);
                F.ret = t342;
                goto gm_fret;
            }
            goto ix318;
ie318:;
ix318:;
        }
        {
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                uint64_t t346 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint64_t t347 = gm_f_us_cuv01fp(t346);
                gm_l22 = t347;
            }
            {
                for (;;) {
                    uint64_t t349 = gm_l23;
                    uint64_t t350 = gm_l22;
                    uint8_t t351 = (t349 < t350);
                    if (!t351) goto lb348;
                    {
                        uint64_t t354 = gm_l23;
                        uint64_t t355 = t354;
                        uint64_t t358 = GM_LD(uint64_t, ((uint8_t *)&t355));
                        gm_l24 = t358;
                        {
                            void * t361 = gm_l0;
                            gm_tl874t2atnlh3 t362 = gm_l1;
                            gm_t88rcucg1b7uk t363 = gm_l16;
                            uint64_t t364 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 24) + 8);
                            void * t365 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l20) + 24)));
                            uint64_t t366 = gm_l24;
                            uint64_t t367 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r368; bool o369 = __builtin_mul_overflow(t366, t367, &r368);
                            if (o369) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 56);
                            uint64_t t370 = r368;
                            uint64_t t371 = (uintptr_t)t370;
                            uint64_t t372 = gm_l24;
                            uint64_t r373; bool o374 = __builtin_add_overflow(t372, ((uint64_t)1ULL), &r373);
                            if (o374) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 322, 69);
                            uint64_t t375 = r373;
                            uint64_t t376 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r377; bool o378 = __builtin_mul_overflow(t375, t376, &r377);
                            if (o378) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 68);
                            uint64_t t379 = r377;
                            uint64_t t380 = (uintptr_t)t379;
                            if (t371 > t380 || t380 > (uintptr_t)t364) gm_trap_slice(t371, t380, t364, gm_srcj10g653a00lt, 322, 48);
                            gm_b16_8 t381;
                            GM_ST(void *, (uint8_t *)&t381, (void *)(((uint8_t *)t365 + t371 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(t380 - t371));
                            uint64_t t382 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 24) + 8);
                            void * t383 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 24)));
                            uint64_t t384 = gm_l24;
                            uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r386; bool o387 = __builtin_mul_overflow(t384, t385, &r386);
                            if (o387) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 95);
                            uint64_t t388 = r386;
                            uint64_t t389 = (uintptr_t)t388;
                            uint64_t t390 = gm_l24;
                            uint64_t r391; bool o392 = __builtin_add_overflow(t390, ((uint64_t)1ULL), &r391);
                            if (o392) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 322, 108);
                            uint64_t t393 = r391;
                            uint64_t t394 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r395; bool o396 = __builtin_mul_overflow(t393, t394, &r395);
                            if (o396) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 107);
                            uint64_t t397 = r395;
                            uint64_t t398 = (uintptr_t)t397;
                            if (t389 > t398 || t398 > (uintptr_t)t382) gm_trap_slice(t389, t398, t382, gm_srcj10g653a00lt, 322, 87);
                            gm_b16_8 t399;
                            GM_ST(void *, (uint8_t *)&t399, (void *)(((uint8_t *)t383 + t389 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t399 + 8, (uintptr_t)(t398 - t389));
                            gm_t1ec8br9ddkav t400;
                            gm_f_equal_e5vf82h((void *)&t400, (void *)NULL, cx, NULL, t361, t362, t363, t381, t399);
                            if (gm_unw.kind) {
                                goto gm_ll352;
                            }
                            gm_t1ec8br9ddkav t401 = t400;
                            if (((((uint8_t *)&t401))[0] != 0)) {
                                gm_t76123gfet73g t402 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t401) + 8));
                                gm_t1ec8br9ddkav t403;
                                GM_ST(uint8_t, (uint8_t *)&t403 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t403 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t403 + 4, 0);
                                ((uint8_t *)&t403)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t403 + 8), t402);
                                F.ret = t403;
                                goto gm_fret;
                            }
                            uint8_t t404 = GM_LD(uint8_t, (((uint8_t *)&t401) + 8));
                            uint8_t t405 = (uint8_t)!t404;
                            if (!t405) goto ie360;
                            {
                                gm_t1ec8br9ddkav t407;
                                GM_ST(uint8_t, (uint8_t *)&t407 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t407 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t407 + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&t407 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t407 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t407 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t407 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t407 + 24, 0);
                                ((uint8_t *)&t407)[0] = 0;
                                GM_ST(uint8_t, ((uint8_t *)&t407 + 8), ((uint8_t)0));
                                gm_t1ec8br9ddkav t408 = t407;
                                uint8_t t409 = ((((uint8_t *)&t408))[0] != 0);
                                F.ret = t407;
                                goto gm_fret;
                            }
                            goto ix360;
ie360:;
ix360:;
                        }
                        goto me356;
mn357:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 321, 13);
me356:;
                    }
lc348:;
                    {
                        uint64_t t411 = gm_l23;
                        uint64_t r412; bool o413 = __builtin_add_overflow(t411, ((uint64_t)1ULL), &r412);
                        uint64_t t414 = r412;
                        gm_l23 = t414;
                    }
                }
gm_ll352:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb348; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc348; }
                }
                goto gm_lad;
lb348:;
            }
        }
        {
            gm_t1ec8br9ddkav t415;
            GM_ST(uint8_t, (uint8_t *)&t415 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t415 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t415 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t415 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t415 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t415 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t415 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t415 + 24, 0);
            ((uint8_t *)&t415)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t415 + 8), ((uint8_t)1));
            gm_t1ec8br9ddkav t416 = t415;
            uint8_t t417 = ((((uint8_t *)&t416))[0] != 0);
            F.ret = t415;
            goto gm_fret;
        }
        goto me6;
mn253:;
ma15:;
        uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t419 != ((uint8_t)13ULL)) goto mn418;
        gm_t88rcucg1b7uk t420 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l25 = t420;
        {
            void * t423 = gm_l0;
            void * t424 = GM_LD(void *, (((uint8_t *)t423) + 8));
            gm_t88rcucg1b7uk t425 = gm_l25;
            uint8_t t426 = gm_f_nullable_pointer_7qfmn6o(t424, GM_CONV(gm_csguankko4lmhd, t425));
            if (!t426) goto ie422;
            {
                void * t428 = gm_l0;
                gm_b16_8 t429 = gm_l3;
                gm_b16_8 t430 = t429;
                gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)&t430));
                uint64_t t432 = gm_f_ptr_of_b77b4c9(t428, t431);
                void * t433 = gm_l0;
                gm_b16_8 t434 = gm_l4;
                gm_b16_8 t435 = t434;
                gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&t435));
                uint64_t t437 = gm_f_ptr_of_b77b4c9(t433, t436);
                uint8_t t438 = ((t432 == t437));
                gm_t1ec8br9ddkav t439;
                GM_ST(uint8_t, (uint8_t *)&t439 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t439 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t439 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t439 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t439 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t439 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t439 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t439 + 24, 0);
                ((uint8_t *)&t439)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t439 + 8), t438);
                gm_t1ec8br9ddkav t440 = t439;
                uint8_t t441 = ((((uint8_t *)&t440))[0] != 0);
                F.ret = t439;
                goto gm_fret;
            }
            goto ix422;
ie422:;
ix422:;
        }
        {
            void * t442 = gm_l0;
            gm_t88rcucg1b7uk t443 = gm_l2;
            gm_b16_8 t444 = gm_l3;
            gm_b16_8 t445 = t444;
            gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&t445));
            uint8_t t447 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t442, t443, t446);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t447;
        }
        {
            void * t448 = gm_l0;
            gm_t88rcucg1b7uk t449 = gm_l2;
            gm_b16_8 t450 = gm_l4;
            gm_b16_8 t451 = t450;
            gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&t451));
            uint8_t t453 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t448, t449, t452);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t453;
        }
        {
            uint8_t t455 = gm_l26;
            uint8_t t456 = (uint8_t)!t455;
            if (!t456) goto ie454;
            uint8_t t457 = gm_l27;
            uint8_t t458 = (uint8_t)!t457;
            if (!t458) goto ie454;
            {
                gm_t1ec8br9ddkav t460;
                GM_ST(uint8_t, (uint8_t *)&t460 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t460 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t460 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t460 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t460 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t460 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t460 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t460 + 24, 0);
                ((uint8_t *)&t460)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t460 + 8), ((uint8_t)1));
                gm_t1ec8br9ddkav t461 = t460;
                uint8_t t462 = ((((uint8_t *)&t461))[0] != 0);
                F.ret = t460;
                goto gm_fret;
            }
            goto ix454;
ie454:;
ix454:;
        }
        {
            uint8_t t464 = gm_l26;
            if (!t464) goto ie463;
            uint8_t t465 = gm_l27;
            if (!t465) goto ie463;
            {
                void * t467 = gm_l0;
                gm_tl874t2atnlh3 t468 = gm_l1;
                gm_t88rcucg1b7uk t469 = gm_l25;
                void * t470 = gm_l0;
                gm_t88rcucg1b7uk t471 = gm_l2;
                gm_b16_8 t472 = gm_l3;
                gm_b16_8 t473 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t470, t471, t472);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t474 = gm_l0;
                gm_t88rcucg1b7uk t475 = gm_l2;
                gm_b16_8 t476 = gm_l4;
                gm_b16_8 t477 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t474, t475, t476);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t467, t468, t469, t473, t477);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix463;
ie463:;
ix463:;
        }
        {
            gm_t1ec8br9ddkav t478;
            GM_ST(uint8_t, (uint8_t *)&t478 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t478 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t478 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t478 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t478 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t478 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t478 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t478 + 24, 0);
            ((uint8_t *)&t478)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t478 + 8), ((uint8_t)0));
            gm_t1ec8br9ddkav t479 = t478;
            uint8_t t480 = ((((uint8_t *)&t479))[0] != 0);
            F.ret = t478;
            goto gm_fret;
        }
        goto me6;
mn418:;
ma16:;
        uint8_t t482 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t482 != ((uint8_t)14ULL)) goto mn481;
        gm_t88rcucg1b7uk t483 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
        gm_l28 = t483;
        gm_t88rcucg1b7uk t484 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l29 = t484;
        {
            void * t486 = gm_l0;
            gm_t88rcucg1b7uk t487 = gm_l2;
            gm_b16_8 t488 = gm_l3;
            gm_b16_8 t489 = t488;
            gm_b16_8 t490 = GM_LD(gm_b16_8, ((uint8_t *)&t489));
            uint8_t t491 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t486, t487, t490);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t491;
        }
        {
            uint8_t t493 = gm_l30;
            void * t494 = gm_l0;
            gm_t88rcucg1b7uk t495 = gm_l2;
            gm_b16_8 t496 = gm_l4;
            gm_b16_8 t497 = t496;
            gm_b16_8 t498 = GM_LD(gm_b16_8, ((uint8_t *)&t497));
            uint8_t t499 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t494, t495, t498);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t500 = (!(t493 == t499));
            if (!t500) goto ie492;
            {
                gm_t1ec8br9ddkav t502;
                GM_ST(uint8_t, (uint8_t *)&t502 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t502 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t502 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t502 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t502 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t502 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t502 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t502 + 24, 0);
                ((uint8_t *)&t502)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t502 + 8), ((uint8_t)0));
                gm_t1ec8br9ddkav t503 = t502;
                uint8_t t504 = ((((uint8_t *)&t503))[0] != 0);
                F.ret = t502;
                goto gm_fret;
            }
            goto ix492;
ie492:;
ix492:;
        }
        {
            void * t505 = gm_l0;
            gm_tl874t2atnlh3 t506 = gm_l1;
            gm_t88rcucg1b7uk t507;
            uint8_t t509 = gm_l30;
            if (!t509) goto ie508;
            gm_t88rcucg1b7uk t511 = gm_l28;
            gm_t88rcucg1b7uk t512 = t511;
            t507 = t512;
            goto ix508;
ie508:;
            gm_t88rcucg1b7uk t514 = gm_l29;
            gm_t88rcucg1b7uk t515 = t514;
            t507 = t515;
ix508:;
            void * t516 = gm_l0;
            gm_t88rcucg1b7uk t517 = gm_l2;
            gm_b16_8 t518 = gm_l3;
            gm_b16_8 t519 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t516, t517, t518);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t520 = gm_l0;
            gm_t88rcucg1b7uk t521 = gm_l2;
            gm_b16_8 t522 = gm_l4;
            gm_b16_8 t523 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t520, t521, t522);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t505, t506, t507, t519, t523);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me6;
mn481:;
ma17:;
        uint8_t t525 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t525 != ((uint8_t)17ULL)) goto mn524;
        {
            void * t528 = gm_l0;
            gm_t88rcucg1b7uk t529 = gm_l2;
            gm_tkn42u7na3t81 t530 = gm_f_bits_struct_ty_22ue20s(t528, GM_CONV(gm_csguankko4lmhd, t529));
            gm_tkn42u7na3t81 t531 = t530;
            if (!((((uint8_t *)&t531))[4] != 0)) goto ie527;
            gm_t88rcucg1b7uk t532 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t531));
            gm_l31 = t532;
            {
                void * t534 = gm_l0;
                gm_tl874t2atnlh3 t535 = gm_l1;
                gm_t88rcucg1b7uk t536 = gm_l31;
                gm_b16_8 t537 = gm_l3;
                gm_b16_8 t538 = gm_l4;
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t534, t535, t536, t537, t538);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix527;
ie527:;
ix527:;
        }
        {
            void * t540 = gm_l0;
            gm_t88rcucg1b7uk t541 = gm_l2;
            gm_t57euh612ligr t542;
            gm_f_union_def_h8mvf4t((void *)&t542, t540, GM_CONV(gm_csguankko4lmhd, t541));
            gm_t57euh612ligr t543 = t542;
            if (!((((uint8_t *)&t543))[24] != 0)) goto ie539;
            gm_tcmkompbl09il t544 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t543));
            gm_l32 = t544;
            {
                void * t546 = gm_l0;
                gm_t88rcucg1b7uk t547 = gm_l2;
                gm_b16_8 t548 = gm_l3;
                gm_b16_8 t549 = t548;
                gm_b16_8 t550 = GM_LD(gm_b16_8, ((uint8_t *)&t549));
                gm_t9805qtonbsag t551 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t546, t547, t550);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9805qtonbsag t552 = t551;
                uint32_t t553;
                if (((((uint8_t *)&t552))[4] != 0)) {
                    uint32_t t554 = GM_LD(uint32_t, (((uint8_t *)&t552) + 0));
                    t553 = t554;
                } else {
                    void * t555 = gm_l0;
                    gm_tl874t2atnlh3 t556 = gm_l1;
                    gm_t88rcucg1b7uk t557 = gm_l2;
                    gm_b16_8 t558 = gm_l3;
                    gm_b16_8 t559 = t558;
                    gm_b16_8 t560 = GM_LD(gm_b16_8, ((uint8_t *)&t559));
                    gm_t76123gfet73g t561;
                    gm_f_invalid_tag_1gif354((void *)&t561, (void *)NULL, cx, NULL, t555, t556, t557, t560);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t1ec8br9ddkav t562;
                    GM_ST(uint8_t, (uint8_t *)&t562 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t562 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t562 + 4, 0);
                    ((uint8_t *)&t562)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t562 + 8), t561);
                    gm_t1ec8br9ddkav t563 = t562;
                    uint8_t t564 = ((((uint8_t *)&t563))[0] != 0);
                    F.ret = t562;
                    goto gm_fret;
                }
                gm_l33 = t553;
            }
            {
                void * t565 = gm_l0;
                gm_t88rcucg1b7uk t566 = gm_l2;
                gm_b16_8 t567 = gm_l4;
                gm_b16_8 t568 = t567;
                gm_b16_8 t569 = GM_LD(gm_b16_8, ((uint8_t *)&t568));
                gm_t9805qtonbsag t570 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t565, t566, t569);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9805qtonbsag t571 = t570;
                uint32_t t572;
                if (((((uint8_t *)&t571))[4] != 0)) {
                    uint32_t t573 = GM_LD(uint32_t, (((uint8_t *)&t571) + 0));
                    t572 = t573;
                } else {
                    void * t574 = gm_l0;
                    gm_tl874t2atnlh3 t575 = gm_l1;
                    gm_t88rcucg1b7uk t576 = gm_l2;
                    gm_b16_8 t577 = gm_l4;
                    gm_b16_8 t578 = t577;
                    gm_b16_8 t579 = GM_LD(gm_b16_8, ((uint8_t *)&t578));
                    gm_t76123gfet73g t580;
                    gm_f_invalid_tag_1gif354((void *)&t580, (void *)NULL, cx, NULL, t574, t575, t576, t579);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t1ec8br9ddkav t581;
                    GM_ST(uint8_t, (uint8_t *)&t581 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t581 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t581 + 4, 0);
                    ((uint8_t *)&t581)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t581 + 8), t580);
                    gm_t1ec8br9ddkav t582 = t581;
                    uint8_t t583 = ((((uint8_t *)&t582))[0] != 0);
                    F.ret = t581;
                    goto gm_fret;
                }
                gm_l34 = t572;
            }
            {
                uint32_t t585 = gm_l33;
                uint32_t t586 = gm_l34;
                uint8_t t587 = (!(t585 == t586));
                if (!t587) goto ie584;
                {
                    gm_t1ec8br9ddkav t589;
                    GM_ST(uint8_t, (uint8_t *)&t589 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t589 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t589 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t589 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t589 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t589 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t589 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t589 + 24, 0);
                    ((uint8_t *)&t589)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t589 + 8), ((uint8_t)0));
                    gm_t1ec8br9ddkav t590 = t589;
                    uint8_t t591 = ((((uint8_t *)&t590))[0] != 0);
                    F.ret = t589;
                    goto gm_fret;
                }
                goto ix584;
ie584:;
ix584:;
            }
            {
                gm_b16_8 t592 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                gm_b16_8 t593 = t592;
                uint32_t t594 = gm_l33;
                uint64_t t595 = (uint64_t)t594;
                uint64_t t596 = gm_f_us_cuv01fp(t595);
                uint64_t t597 = (uintptr_t)t596;
                gm_b16_8 t598 = t593;
                uint64_t t599 = GM_LD(uintptr_t, ((uint8_t *)&t598) + 8);
                if (t597 >= (uintptr_t)(t599)) gm_trap_index(t597, t599, gm_srcj10g653a00lt, 345, 23);
                gm_tkn42u7na3t81 t600 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t598)) + t597 * 72) + 16));
                gm_tkn42u7na3t81 t601 = t600;
                gm_t88rcucg1b7uk t602;
                if (((((uint8_t *)&t601))[4] != 0)) {
                    gm_t88rcucg1b7uk t603 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t601) + 0));
                    t602 = t603;
                } else {
                    gm_t1ec8br9ddkav t604;
                    GM_ST(uint8_t, (uint8_t *)&t604 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t604 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t604 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t604 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t604 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t604 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t604 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t604 + 24, 0);
                    ((uint8_t *)&t604)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t604 + 8), ((uint8_t)1));
                    gm_t1ec8br9ddkav t605 = t604;
                    uint8_t t606 = ((((uint8_t *)&t605))[0] != 0);
                    F.ret = t604;
                    goto gm_fret;
                }
                gm_l35 = t602;
            }
            {
                void * t607 = gm_l0;
                gm_t88rcucg1b7uk t608 = gm_l2;
                uint64_t t609 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t607, t608);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t610 = gm_f_us_cuv01fp(t609);
                gm_l36 = t610;
            }
            {
                void * t611 = gm_l0;
                gm_t88rcucg1b7uk t612 = gm_l35;
                uint64_t t613 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t611, t612);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t614 = gm_f_us_cuv01fp(t613);
                gm_l37 = t614;
            }
            {
                void * t615 = gm_l0;
                gm_tl874t2atnlh3 t616 = gm_l1;
                gm_t88rcucg1b7uk t617 = gm_l35;
                uint64_t t618 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t619 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t620 = gm_l36;
                uint64_t t621 = (uintptr_t)t620;
                uint64_t t622 = gm_l36;
                uint64_t t623 = gm_l37;
                uint64_t r624; bool o625 = __builtin_add_overflow(t622, t623, &r624);
                if (o625) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 348, 49);
                uint64_t t626 = r624;
                uint64_t t627 = (uintptr_t)t626;
                if (t621 > t627 || t627 > (uintptr_t)t618) gm_trap_slice(t621, t627, t618, gm_srcj10g653a00lt, 348, 42);
                gm_b16_8 t628;
                GM_ST(void *, (uint8_t *)&t628, (void *)(((uint8_t *)t619 + t621 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t628 + 8, (uintptr_t)(t627 - t621));
                uint64_t t629 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t630 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t631 = gm_l36;
                uint64_t t632 = (uintptr_t)t631;
                uint64_t t633 = gm_l36;
                uint64_t t634 = gm_l37;
                uint64_t r635; bool o636 = __builtin_add_overflow(t633, t634, &r635);
                if (o636) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 348, 66);
                uint64_t t637 = r635;
                uint64_t t638 = (uintptr_t)t637;
                if (t632 > t638 || t638 > (uintptr_t)t629) gm_trap_slice(t632, t638, t629, gm_srcj10g653a00lt, 348, 59);
                gm_b16_8 t639;
                GM_ST(void *, (uint8_t *)&t639, (void *)(((uint8_t *)t630 + t632 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t639 + 8, (uintptr_t)(t638 - t632));
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t615, t616, t617, t628, t639);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix539;
ie539:;
ix539:;
        }
        {
            void * t640 = gm_l0;
            gm_t88rcucg1b7uk t641 = gm_l2;
            void * t642 = gm_f_adt_of_8k5vqud(t640, GM_CONV(gm_csguankko4lmhd, t641));
            void * t643 = t642;
            void * t644;
            if ((GM_LD(void *, ((uint8_t *)&t643)) != 0)) {
                void * t645 = GM_LD(void *, (((uint8_t *)&t643) + 0));
                t644 = t645;
            } else {
                void * t646 = gm_l0;
                gm_tl874t2atnlh3 t647 = gm_l1;
                void * t648 = gm_l0;
                void * t649 = GM_LD(void *, (((uint8_t *)t648) + 8));
                gm_t88rcucg1b7uk t650 = gm_l2;
                gm_b16_8 t651 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t649, t650);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t652;
                GM_ST(gm_b16_8, ((uint8_t *)&t652 + 0), t651);
                gm_tabb710d9mq2e t653;
                gm_f_format_h8e7ps1((void *)&t653, (void *)NULL, cx, NULL, t652);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t654 = t653;
                if (((((uint8_t *)&t654))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t654) + 8, gm_srcj10g653a00lt, 350, 56);
                gm_b16_8 t655 = GM_LD(gm_b16_8, (((uint8_t *)&t654) + 8));
                gm_b16_8 t656 = t655;
                gm_b16_8 t657 = GM_LD(gm_b16_8, ((uint8_t *)&t656));
                gm_t76123gfet73g t658;
                gm_f_trap_qh84l9d((void *)&t658, (void *)NULL, cx, NULL, t646, t647, t657);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t659;
                GM_ST(uint8_t, (uint8_t *)&t659 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t659 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t659 + 4, 0);
                ((uint8_t *)&t659)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t659 + 8), t658);
                gm_t1ec8br9ddkav t660 = t659;
                uint8_t t661 = ((((uint8_t *)&t660))[0] != 0);
                F.ret = t659;
                goto gm_fret;
            }
            gm_l38 = t644;
        }
        {
            void * t663 = gm_l38;
            uint8_t t664 = GM_LD(uint8_t, ((((uint8_t *)t663) + 24) + 0));
            if (t664 != ((uint8_t)0ULL)) goto ie662;
            gm_tpautr4br7h3l t665 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t663) + 24) + 8));
            F.l39 = t665;
            {
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t668 = GM_LD(uintptr_t, (((uint8_t *)&F.l39) + 0) + 8);
                    gm_l40 = t668;
                }
                {
                    for (;;) {
                        uint64_t t670 = gm_l41;
                        uint64_t t671 = gm_l40;
                        uint8_t t672 = (t670 < t671);
                        if (!t672) goto lb669;
                        {
                            uint64_t t675 = gm_l41;
                            uint64_t t676 = t675;
                            uint64_t t679 = GM_LD(uint64_t, ((uint8_t *)&t676));
                            gm_l42 = t679;
                            {
                                void * t681 = gm_l0;
                                gm_t88rcucg1b7uk t682 = gm_l2;
                                uint64_t t683 = gm_l42;
                                uint32_t t684 = (uint32_t)t683;
                                gm_tfbnciphf3261 t685 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t681, t682, t684);
                                if (gm_unw.kind) {
                                    goto gm_ll673;
                                }
                                gm_l43 = t685;
                            }
                            {
                                uint64_t t686 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                uint64_t t687 = gm_f_us_cuv01fp(t686);
                                gm_l44 = t687;
                            }
                            {
                                void * t688 = gm_l0;
                                gm_t88rcucg1b7uk t689 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 0));
                                uint64_t t690 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t688, t689);
                                if (gm_unw.kind) {
                                    goto gm_ll673;
                                }
                                uint64_t t691 = gm_f_us_cuv01fp(t690);
                                gm_l45 = t691;
                            }
                            {
                                void * t693 = gm_l0;
                                gm_tl874t2atnlh3 t694 = gm_l1;
                                gm_t88rcucg1b7uk t695 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 0));
                                uint64_t t696 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t697 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t698 = gm_l44;
                                uint64_t t699 = (uintptr_t)t698;
                                uint64_t t700 = gm_l44;
                                uint64_t t701 = gm_l45;
                                uint64_t r702; bool o703 = __builtin_add_overflow(t700, t701, &r702);
                                if (o703) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 356, 56);
                                uint64_t t704 = r702;
                                uint64_t t705 = (uintptr_t)t704;
                                if (t699 > t705 || t705 > (uintptr_t)t696) gm_trap_slice(t699, t705, t696, gm_srcj10g653a00lt, 356, 49);
                                gm_b16_8 t706;
                                GM_ST(void *, (uint8_t *)&t706, (void *)(((uint8_t *)t697 + t699 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t706 + 8, (uintptr_t)(t705 - t699));
                                uint64_t t707 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                void * t708 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                                uint64_t t709 = gm_l44;
                                uint64_t t710 = (uintptr_t)t709;
                                uint64_t t711 = gm_l44;
                                uint64_t t712 = gm_l45;
                                uint64_t r713; bool o714 = __builtin_add_overflow(t711, t712, &r713);
                                if (o714) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 356, 73);
                                uint64_t t715 = r713;
                                uint64_t t716 = (uintptr_t)t715;
                                if (t710 > t716 || t716 > (uintptr_t)t707) gm_trap_slice(t710, t716, t707, gm_srcj10g653a00lt, 356, 66);
                                gm_b16_8 t717;
                                GM_ST(void *, (uint8_t *)&t717, (void *)(((uint8_t *)t708 + t710 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t717 + 8, (uintptr_t)(t716 - t710));
                                gm_t1ec8br9ddkav t718;
                                gm_f_equal_e5vf82h((void *)&t718, (void *)NULL, cx, NULL, t693, t694, t695, t706, t717);
                                if (gm_unw.kind) {
                                    goto gm_ll673;
                                }
                                gm_t1ec8br9ddkav t719 = t718;
                                if (((((uint8_t *)&t719))[0] != 0)) {
                                    gm_t76123gfet73g t720 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t719) + 8));
                                    gm_t1ec8br9ddkav t721;
                                    GM_ST(uint8_t, (uint8_t *)&t721 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t721 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t721 + 4, 0);
                                    ((uint8_t *)&t721)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t721 + 8), t720);
                                    F.ret = t721;
                                    goto gm_fret;
                                }
                                uint8_t t722 = GM_LD(uint8_t, (((uint8_t *)&t719) + 8));
                                uint8_t t723 = (uint8_t)!t722;
                                if (!t723) goto ie692;
                                {
                                    gm_t1ec8br9ddkav t725;
                                    GM_ST(uint8_t, (uint8_t *)&t725 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t725 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t725 + 4, 0);
                                    GM_ST(uint8_t, (uint8_t *)&t725 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t725 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t725 + 12, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t725 + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t725 + 24, 0);
                                    ((uint8_t *)&t725)[0] = 0;
                                    GM_ST(uint8_t, ((uint8_t *)&t725 + 8), ((uint8_t)0));
                                    gm_t1ec8br9ddkav t726 = t725;
                                    uint8_t t727 = ((((uint8_t *)&t726))[0] != 0);
                                    F.ret = t725;
                                    goto gm_fret;
                                }
                                goto ix692;
ie692:;
ix692:;
                            }
                            goto me677;
mn678:;
                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 352, 13);
me677:;
                        }
lc669:;
                        {
                            uint64_t t729 = gm_l41;
                            uint64_t r730; bool o731 = __builtin_add_overflow(t729, ((uint64_t)1ULL), &r730);
                            uint64_t t732 = r730;
                            gm_l41 = t732;
                        }
                    }
gm_ll673:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb669; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc669; }
                    }
                    goto gm_lad;
lb669:;
                }
            }
            {
                gm_t1ec8br9ddkav t733;
                GM_ST(uint8_t, (uint8_t *)&t733 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t733 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t733 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t733 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t733 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t733 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t733 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t733 + 24, 0);
                ((uint8_t *)&t733)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t733 + 8), ((uint8_t)1));
                gm_t1ec8br9ddkav t734 = t733;
                uint8_t t735 = ((((uint8_t *)&t734))[0] != 0);
                F.ret = t733;
                goto gm_fret;
            }
            goto ix662;
ie662:;
            {
                void * t737 = gm_l0;
                gm_tl874t2atnlh3 t738 = gm_l1;
                void * t739 = gm_l0;
                void * t740 = GM_LD(void *, (((uint8_t *)t739) + 8));
                gm_t88rcucg1b7uk t741 = gm_l2;
                gm_b16_8 t742 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t740, t741);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t743;
                GM_ST(gm_b16_8, ((uint8_t *)&t743 + 0), t742);
                gm_tabb710d9mq2e t744;
                gm_f_format_dmukbli((void *)&t744, (void *)NULL, cx, NULL, t743);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t745 = t744;
                if (((((uint8_t *)&t745))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t745) + 8, gm_srcj10g653a00lt, 351, 62);
                gm_b16_8 t746 = GM_LD(gm_b16_8, (((uint8_t *)&t745) + 8));
                gm_b16_8 t747 = t746;
                gm_b16_8 t748 = GM_LD(gm_b16_8, ((uint8_t *)&t747));
                gm_t76123gfet73g t749;
                gm_f_trap_qh84l9d((void *)&t749, (void *)NULL, cx, NULL, t737, t738, t748);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t750;
                GM_ST(uint8_t, (uint8_t *)&t750 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t750 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t750 + 4, 0);
                ((uint8_t *)&t750)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t750 + 8), t749);
                gm_t1ec8br9ddkav t751 = t750;
                uint8_t t752 = ((((uint8_t *)&t751))[0] != 0);
                F.ret = t750;
                goto gm_fret;
            }
ix662:;
        }
        goto me6;
mn524:;
ma18:;
        void * t754 = gm_l0;
        gm_tl874t2atnlh3 t755 = gm_l1;
        void * t756 = gm_l0;
        void * t757 = GM_LD(void *, (((uint8_t *)t756) + 8));
        gm_t88rcucg1b7uk t758 = gm_l2;
        gm_b16_8 t759 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t757, t758);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t760;
        GM_ST(gm_b16_8, ((uint8_t *)&t760 + 0), t759);
        gm_tabb710d9mq2e t761;
        gm_f_format_dmukbli((void *)&t761, (void *)NULL, cx, NULL, t760);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t762 = t761;
        if (((((uint8_t *)&t762))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t762) + 8, gm_srcj10g653a00lt, 360, 35);
        gm_b16_8 t763 = GM_LD(gm_b16_8, (((uint8_t *)&t762) + 8));
        gm_b16_8 t764 = t763;
        gm_b16_8 t765 = GM_LD(gm_b16_8, ((uint8_t *)&t764));
        gm_t76123gfet73g t766;
        gm_f_trap_qh84l9d((void *)&t766, (void *)NULL, cx, NULL, t754, t755, t765);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1ec8br9ddkav t767;
        GM_ST(uint8_t, (uint8_t *)&t767 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t767 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t767 + 4, 0);
        ((uint8_t *)&t767)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t767 + 8), t766);
        gm_t1ec8br9ddkav t768 = t767;
        uint8_t t769 = ((((uint8_t *)&t768))[0] != 0);
        F.ret = t767;
        goto gm_fret;
        goto me6;
mn753:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 289, 5);
me6:;
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
inline __attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_invalid_tag_1gif354 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    gm_tcmkompbl09il gm_l4; /* u */
    GM_STACK_CHECK(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t57euh612ligr t4;
        gm_f_union_def_h8mvf4t((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t57euh612ligr t5 = t4;
        gm_tcmkompbl09il t6;
        if (((((uint8_t *)&t5))[24] != 0)) {
            gm_tcmkompbl09il t7 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            gm_tl874t2atnlh3 t9 = gm_l1;
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s7ah1q5ah26rj));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(11));
            gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l4 = t6;
    }
    {
        void * t11 = gm_l0;
        gm_tl874t2atnlh3 t12 = gm_l1;
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l2;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 0));
        gm_b16_8 t16 = gm_l3;
        gm_b16_8 t17 = gm_f_invalid_tag_text_f3l5hi7((void *)NULL, cx, NULL, t13, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t11, t12, t17);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_invalid_tag_text_f3l5hi7 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* tag */
    gm_b16_8 gm_l3; /* b */
    unsigned __int128 gm_l4; /* raw */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* text */
    GM_STACK_CHECK(&F, 754u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_b16_8 t4 = gm_l3;
        gm_b16_8 t5 = t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)));
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l2;
        uint64_t t11 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t12 = gm_f_us_cuv01fp(t11);
        uint64_t t13 = (uintptr_t)t12;
        if (t8 > t13 || t13 > (uintptr_t)t6) gm_trap_slice(t8, t13, t6, gm_srcj10g653a00lt, 373, 27);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t7 + t8 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13 - t8));
        unsigned __int128 t15 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t14);
        gm_l4 = t15;
    }
    {
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_tg5lql6itufk7 t18 = gm_f_int_info_ulcevdv(t16, GM_CONV(gm_csguankko4lmhd, t17));
        gm_tg5lql6itufk7 t19 = t18;
        gm_ttovhfee8mf28 t20;
        if (((((uint8_t *)&t19))[8] != 0)) {
            gm_ttovhfee8mf28 t21 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t19) + 0));
            t20 = t21;
        } else {
            gm_ttovhfee8mf28 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t22 + 4), ((uint32_t)8ULL));
            t20 = t22;
        }
        gm_l5 = t20;
    }
    {
        gm_b16_8 t23;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (!t25) goto ie24;
        unsigned __int128 t27 = gm_l4;
        unsigned __int128 t28 = t27;
        __int128 t29;
        memset(&t29, 0, sizeof t29); memcpy(&t29, ((uint8_t *)&t28), sizeof t29 < 16 ? sizeof t29 : 16);
        gm_tpl3q6e8s9qo1 t30;
        GM_ST(__int128, ((uint8_t *)&t30 + 0), t29);
        gm_tabb710d9mq2e t31;
        gm_f_format_9qvncot((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcj10g653a00lt, 375, 30);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_b16_8 t34 = t33;
        t23 = t34;
        goto ix24;
ie24:;
        unsigned __int128 t36 = gm_l4;
        gm_tsn2omssg6cpf t37;
        GM_ST(unsigned __int128, ((uint8_t *)&t37 + 0), t36);
        gm_tabb710d9mq2e t38;
        gm_f_format_ra7pont((void *)&t38, (void *)NULL, cx, NULL, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t39 = t38;
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcj10g653a00lt, 375, 73);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
        gm_b16_8 t41 = t40;
        t23 = t41;
ix24:;
        gm_l6 = t23;
    }
    {
        gm_t6j6smj7qoctu t42;
        gm_b16_8 t43 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 8));
        gm_t88rcucg1b7uk t46 = gm_l1;
        gm_b16_8 t47 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t47);
        gm_tabb710d9mq2e t48;
        gm_f_format_mq7e94u((void *)&t48, (void *)NULL, cx, NULL, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_srcj10g653a00lt, 376, 12);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_b16_8 t51 = t50;
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        uint8_t t53 = 0;
        F.ret = t52;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_less_nbfe87l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_less_nbfe87l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* x */
    gm_b16_8 gm_l4; /* y */
    gm_ttovhfee8mf28 gm_l5; /* info */
    unsigned __int128 gm_l6; /* a */
    unsigned __int128 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l12; /* fa */
    gm_b16_8 gm_l13; /* fb */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l20; /* xs */
    gm_b16_8 gm_l21; /* ys */
    gm_tcmkompbl09il gm_l22; /* u */
    gm_ttovhfee8mf28 gm_l23; /* info */
    uint64_t gm_l24; /* n */
    unsigned __int128 gm_l25; /* a */
    unsigned __int128 gm_l26; /* c */
    gm_t88rcucg1b7uk gm_l27; /* u */
    GM_STACK_CHECK(&F, 4447u);
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
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma7:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t20 != ((uint8_t)0ULL)) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t22 != ((uint8_t)1ULL)) goto pa21;
        goto po18;
pa21:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t24 != ((uint8_t)2ULL)) goto pa23;
        goto po18;
pa23:;
        goto mn17;
po18:;
        {
            void * t26 = gm_l0;
            gm_t88rcucg1b7uk t27 = gm_l2;
            gm_tg5lql6itufk7 t28 = gm_f_int_info_ulcevdv(t26, GM_CONV(gm_csguankko4lmhd, t27));
            gm_tg5lql6itufk7 t29 = t28;
            gm_ttovhfee8mf28 t30;
            if (((((uint8_t *)&t29))[8] != 0)) {
                gm_ttovhfee8mf28 t31 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t29) + 0));
                t30 = t31;
            } else {
                gm_t1ec8br9ddkav t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t32 + 8), ((uint8_t)0));
                gm_t1ec8br9ddkav t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            gm_l5 = t30;
        }
        {
            void * t35 = gm_l0;
            gm_ttovhfee8mf28 t36 = gm_l5;
            gm_b16_8 t37 = gm_l3;
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            unsigned __int128 t40 = gm_f_rd_int_iuioodt(t35, t36, t39);
            gm_l6 = t40;
        }
        {
            void * t41 = gm_l0;
            gm_ttovhfee8mf28 t42 = gm_l5;
            gm_b16_8 t43 = gm_l4;
            gm_b16_8 t44 = t43;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            unsigned __int128 t46 = gm_f_rd_int_iuioodt(t41, t42, t45);
            gm_l7 = t46;
        }
        {
            gm_t1ec8br9ddkav t47;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (!t49) goto ie48;
            unsigned __int128 t51 = gm_l6;
            unsigned __int128 t52 = t51;
            __int128 t53;
            memset(&t53, 0, sizeof t53); memcpy(&t53, ((uint8_t *)&t52), sizeof t53 < 16 ? sizeof t53 : 16);
            unsigned __int128 t54 = gm_l7;
            unsigned __int128 t55 = t54;
            __int128 t56;
            memset(&t56, 0, sizeof t56); memcpy(&t56, ((uint8_t *)&t55), sizeof t56 < 16 ? sizeof t56 : 16);
            uint8_t t57 = (t53 < t56);
            uint8_t t58 = t57;
            gm_t1ec8br9ddkav t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t59 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 24, 0);
            ((uint8_t *)&t59)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 8), t58);
            t47 = t59;
            goto ix48;
ie48:;
            unsigned __int128 t61 = gm_l6;
            unsigned __int128 t62 = gm_l7;
            uint8_t t63 = (t61 < t62);
            uint8_t t64 = t63;
            gm_t1ec8br9ddkav t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t65 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t65 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t65 + 24, 0);
            ((uint8_t *)&t65)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 8), t64);
            t47 = t65;
ix48:;
            gm_t1ec8br9ddkav t66 = t47;
            uint8_t t67 = ((((uint8_t *)&t66))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        goto me6;
mn17:;
ma8:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t69 != ((uint8_t)3ULL)) goto mn68;
        void * t70 = gm_l0;
        gm_t88rcucg1b7uk t71 = gm_l2;
        gm_b16_8 t72 = gm_l3;
        gm_b16_8 t73 = t72;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        double t75 = gm_f_get_float_grmhlm4(t70, GM_CONV(gm_csguankko4lmhd, t71), t74);
        void * t76 = gm_l0;
        gm_t88rcucg1b7uk t77 = gm_l2;
        gm_b16_8 t78 = gm_l4;
        gm_b16_8 t79 = t78;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
        double t81 = gm_f_get_float_grmhlm4(t76, GM_CONV(gm_csguankko4lmhd, t77), t80);
        uint8_t t82 = (t75 < t81);
        gm_t1ec8br9ddkav t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t83 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 24, 0);
        ((uint8_t *)&t83)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t83 + 8), t82);
        gm_t1ec8br9ddkav t84 = t83;
        uint8_t t85 = ((((uint8_t *)&t84))[0] != 0);
        F.ret = t83;
        goto gm_fret;
        goto me6;
mn68:;
ma9:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t87 != ((uint8_t)4ULL)) goto mn86;
        uint64_t t88 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcj10g653a00lt, 246, 25);
        uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t88 * 1));
        uint64_t t91 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_srcj10g653a00lt, 246, 32);
        uint8_t t93 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t91 * 1));
        uint8_t t94 = (t90 < t93);
        gm_t1ec8br9ddkav t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t95 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t95 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t95 + 24, 0);
        ((uint8_t *)&t95)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t95 + 8), t94);
        gm_t1ec8br9ddkav t96 = t95;
        uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
        F.ret = t95;
        goto gm_fret;
        goto me6;
mn86:;
ma10:;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t101 != ((uint8_t)8ULL)) goto pa100;
        goto po99;
pa100:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t103 != ((uint8_t)9ULL)) goto pa102;
        goto po99;
pa102:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t105 != ((uint8_t)7ULL)) goto pa104;
        goto po99;
pa104:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t107 != ((uint8_t)16ULL)) goto pa106;
        goto po99;
pa106:;
        goto mn98;
po99:;
        void * t108 = gm_l0;
        gm_b16_8 t109 = gm_l3;
        gm_b16_8 t110 = t109;
        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
        uint64_t t112 = gm_f_ptr_of_b77b4c9(t108, t111);
        void * t113 = gm_l0;
        gm_b16_8 t114 = gm_l4;
        gm_b16_8 t115 = t114;
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
        uint64_t t117 = gm_f_ptr_of_b77b4c9(t113, t116);
        uint8_t t118 = (t112 < t117);
        gm_t1ec8br9ddkav t119;
        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t119 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t119 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t119 + 24, 0);
        ((uint8_t *)&t119)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t119 + 8), t118);
        gm_t1ec8br9ddkav t120 = t119;
        uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
        F.ret = t119;
        goto gm_fret;
        goto me6;
mn98:;
ma11:;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t123 != ((uint8_t)12ULL)) goto mn122;
        gm_b16_8 t124 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l8 = t124;
        {
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                gm_l9 = t127;
            }
            {
                for (;;) {
                    uint64_t t129 = gm_l10;
                    uint64_t t130 = gm_l9;
                    uint8_t t131 = (t129 < t130);
                    if (!t131) goto lb128;
                    {
                        uint64_t t134 = gm_l10;
                        uint64_t t135 = t134;
                        uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                        gm_l11 = t138;
                        {
                            void * t140 = gm_l0;
                            gm_t88rcucg1b7uk t141 = gm_l2;
                            gm_b16_8 t142 = gm_l3;
                            uint64_t t143 = gm_l11;
                            uint32_t t144 = (uint32_t)t143;
                            gm_b16_8 t145 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t140, t141, t142, t144);
                            if (gm_unw.kind) {
                                goto gm_ll132;
                            }
                            gm_l12 = t145;
                        }
                        {
                            void * t146 = gm_l0;
                            gm_t88rcucg1b7uk t147 = gm_l2;
                            gm_b16_8 t148 = gm_l4;
                            uint64_t t149 = gm_l11;
                            uint32_t t150 = (uint32_t)t149;
                            gm_b16_8 t151 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t146, t147, t148, t150);
                            if (gm_unw.kind) {
                                goto gm_ll132;
                            }
                            gm_l13 = t151;
                        }
                        {
                            void * t153 = gm_l0;
                            gm_tl874t2atnlh3 t154 = gm_l1;
                            uint64_t t155 = gm_l11;
                            uint64_t t156 = (uintptr_t)t155;
                            uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            if (t156 >= (uintptr_t)(t157)) gm_trap_index(t156, t157, gm_srcj10g653a00lt, 252, 42);
                            gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t156 * 4));
                            gm_b16_8 t159 = gm_l12;
                            gm_b16_8 t160 = gm_l13;
                            gm_t1ec8br9ddkav t161;
                            gm_f_equal_e5vf82h((void *)&t161, (void *)NULL, cx, NULL, t153, t154, t158, t159, t160);
                            if (gm_unw.kind) {
                                goto gm_ll132;
                            }
                            gm_t1ec8br9ddkav t162 = t161;
                            if (((((uint8_t *)&t162))[0] != 0)) {
                                gm_t76123gfet73g t163 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t162) + 8));
                                gm_t1ec8br9ddkav t164;
                                GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
                                ((uint8_t *)&t164)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t164 + 8), t163);
                                F.ret = t164;
                                goto gm_fret;
                            }
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&t162) + 8));
                            uint8_t t166 = (uint8_t)!t165;
                            if (!t166) goto ie152;
                            {
                                void * t168 = gm_l0;
                                gm_tl874t2atnlh3 t169 = gm_l1;
                                uint64_t t170 = gm_l11;
                                uint64_t t171 = (uintptr_t)t170;
                                uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_srcj10g653a00lt, 252, 82);
                                gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t171 * 4));
                                gm_b16_8 t174 = gm_l12;
                                gm_b16_8 t175 = gm_l13;
                                gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t168, t169, t173, t174, t175);
                                if (gm_unw.kind) {
                                    goto gm_ll132;
                                }
                                goto gm_fdone;
                            }
                            goto ix152;
ie152:;
ix152:;
                        }
                        goto me136;
mn137:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 249, 13);
me136:;
                    }
lc128:;
                    {
                        uint64_t t177 = gm_l10;
                        uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
                        uint64_t t180 = r178;
                        gm_l10 = t180;
                    }
                }
gm_ll132:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb128; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc128; }
                }
                goto gm_lad;
lb128:;
            }
        }
        {
            gm_t1ec8br9ddkav t181;
            GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t181 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t181 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t181 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t181 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t181 + 24, 0);
            ((uint8_t *)&t181)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t181 + 8), ((uint8_t)0));
            gm_t1ec8br9ddkav t182 = t181;
            uint8_t t183 = ((((uint8_t *)&t182))[0] != 0);
            F.ret = t181;
            goto gm_fret;
        }
        goto me6;
mn122:;
ma12:;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t187 != ((uint8_t)11ULL)) goto pa186;
        goto po185;
pa186:;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t189 != ((uint8_t)10ULL)) goto pa188;
        goto po185;
pa188:;
        goto mn184;
po185:;
        {
            void * t191 = gm_l0;
            gm_tl874t2atnlh3 t192 = gm_l1;
            gm_t88rcucg1b7uk t193 = gm_l2;
            gm_b16_8 t194 = gm_l3;
            gm_tmc91gv7n9c0i t195;
            gm_f_seq_of_ifahtjs((void *)&t195, (void *)NULL, cx, NULL, t191, t192, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmc91gv7n9c0i t196 = t195;
            if (((((uint8_t *)&t196))[0] != 0)) {
                gm_t76123gfet73g t197 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t196) + 8));
                gm_t1ec8br9ddkav t198;
                GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                ((uint8_t *)&t198)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t198 + 8), t197);
                F.ret = t198;
                goto gm_fret;
            }
            gm_t2jeg3bg7sfqa t199 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t196) + 8));
            F.l14 = t199;
        }
        {
            void * t200 = gm_l0;
            gm_tl874t2atnlh3 t201 = gm_l1;
            gm_t88rcucg1b7uk t202 = gm_l2;
            gm_b16_8 t203 = gm_l4;
            gm_tmc91gv7n9c0i t204;
            gm_f_seq_of_ifahtjs((void *)&t204, (void *)NULL, cx, NULL, t200, t201, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmc91gv7n9c0i t205 = t204;
            if (((((uint8_t *)&t205))[0] != 0)) {
                gm_t76123gfet73g t206 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t205) + 8));
                gm_t1ec8br9ddkav t207;
                GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
                ((uint8_t *)&t207)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t207 + 8), t206);
                F.ret = t207;
                goto gm_fret;
            }
            gm_t2jeg3bg7sfqa t208 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t205) + 8));
            F.l15 = t208;
        }
        {
            uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 16));
            uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
            uint64_t t211 = (t209 < t210 ? t209 : t210);
            gm_l16 = t211;
        }
        {
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                uint64_t t213 = gm_l16;
                uint64_t t214 = gm_f_us_cuv01fp(t213);
                gm_l17 = t214;
            }
            {
                for (;;) {
                    uint64_t t216 = gm_l18;
                    uint64_t t217 = gm_l17;
                    uint8_t t218 = (t216 < t217);
                    if (!t218) goto lb215;
                    {
                        uint64_t t221 = gm_l18;
                        uint64_t t222 = t221;
                        uint64_t t225 = GM_LD(uint64_t, ((uint8_t *)&t222));
                        gm_l19 = t225;
                        {
                            uint64_t t227 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 24) + 8);
                            void * t228 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 24)));
                            uint64_t t229 = gm_l19;
                            uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r231; bool o232 = __builtin_mul_overflow(t229, t230, &r231);
                            if (o232) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 261, 31);
                            uint64_t t233 = r231;
                            uint64_t t234 = (uintptr_t)t233;
                            uint64_t t235 = gm_l19;
                            uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                            if (o237) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 261, 44);
                            uint64_t t238 = r236;
                            uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r240; bool o241 = __builtin_mul_overflow(t238, t239, &r240);
                            if (o241) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 261, 43);
                            uint64_t t242 = r240;
                            uint64_t t243 = (uintptr_t)t242;
                            if (t234 > t243 || t243 > (uintptr_t)t227) gm_trap_slice(t234, t243, t227, gm_srcj10g653a00lt, 261, 23);
                            gm_b16_8 t244;
                            GM_ST(void *, (uint8_t *)&t244, (void *)(((uint8_t *)t228 + t234 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(t243 - t234));
                            gm_l20 = t244;
                        }
                        {
                            uint64_t t245 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 24) + 8);
                            void * t246 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 24)));
                            uint64_t t247 = gm_l19;
                            uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r249; bool o250 = __builtin_mul_overflow(t247, t248, &r249);
                            if (o250) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 262, 31);
                            uint64_t t251 = r249;
                            uint64_t t252 = (uintptr_t)t251;
                            uint64_t t253 = gm_l19;
                            uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                            if (o255) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 262, 44);
                            uint64_t t256 = r254;
                            uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r258; bool o259 = __builtin_mul_overflow(t256, t257, &r258);
                            if (o259) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 262, 43);
                            uint64_t t260 = r258;
                            uint64_t t261 = (uintptr_t)t260;
                            if (t252 > t261 || t261 > (uintptr_t)t245) gm_trap_slice(t252, t261, t245, gm_srcj10g653a00lt, 262, 23);
                            gm_b16_8 t262;
                            GM_ST(void *, (uint8_t *)&t262, (void *)(((uint8_t *)t246 + t252 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(t261 - t252));
                            gm_l21 = t262;
                        }
                        {
                            void * t264 = gm_l0;
                            gm_tl874t2atnlh3 t265 = gm_l1;
                            gm_t88rcucg1b7uk t266 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 0));
                            gm_b16_8 t267 = gm_l20;
                            gm_b16_8 t268 = gm_l21;
                            gm_t1ec8br9ddkav t269;
                            gm_f_equal_e5vf82h((void *)&t269, (void *)NULL, cx, NULL, t264, t265, t266, t267, t268);
                            if (gm_unw.kind) {
                                goto gm_ll219;
                            }
                            gm_t1ec8br9ddkav t270 = t269;
                            if (((((uint8_t *)&t270))[0] != 0)) {
                                gm_t76123gfet73g t271 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t270) + 8));
                                gm_t1ec8br9ddkav t272;
                                GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
                                ((uint8_t *)&t272)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t272 + 8), t271);
                                F.ret = t272;
                                goto gm_fret;
                            }
                            uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&t270) + 8));
                            uint8_t t274 = (uint8_t)!t273;
                            if (!t274) goto ie263;
                            {
                                void * t276 = gm_l0;
                                gm_tl874t2atnlh3 t277 = gm_l1;
                                gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 0));
                                gm_b16_8 t279 = gm_l20;
                                gm_b16_8 t280 = gm_l21;
                                gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t276, t277, t278, t279, t280);
                                if (gm_unw.kind) {
                                    goto gm_ll219;
                                }
                                goto gm_fdone;
                            }
                            goto ix263;
ie263:;
ix263:;
                        }
                        goto me223;
mn224:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 260, 13);
me223:;
                    }
lc215:;
                    {
                        uint64_t t282 = gm_l18;
                        uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                        uint64_t t285 = r283;
                        gm_l18 = t285;
                    }
                }
gm_ll219:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb215; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc215; }
                }
                goto gm_lad;
lb215:;
            }
        }
        {
            uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 16));
            uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
            uint8_t t288 = (t286 < t287);
            gm_t1ec8br9ddkav t289;
            GM_ST(uint8_t, (uint8_t *)&t289 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t289 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t289 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t289 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t289 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t289 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t289 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t289 + 24, 0);
            ((uint8_t *)&t289)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t289 + 8), t288);
            gm_t1ec8br9ddkav t290 = t289;
            uint8_t t291 = ((((uint8_t *)&t290))[0] != 0);
            F.ret = t289;
            goto gm_fret;
        }
        goto me6;
mn184:;
ma13:;
        uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t293 != ((uint8_t)17ULL)) goto mn292;
        {
            void * t296 = gm_l0;
            gm_t88rcucg1b7uk t297 = gm_l2;
            gm_t57euh612ligr t298;
            gm_f_union_def_h8mvf4t((void *)&t298, t296, GM_CONV(gm_csguankko4lmhd, t297));
            gm_t57euh612ligr t299 = t298;
            if (!((((uint8_t *)&t299))[24] != 0)) goto ie295;
            gm_tcmkompbl09il t300 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t299));
            gm_l22 = t300;
            {
                void * t302 = gm_l0;
                gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                gm_tg5lql6itufk7 t304 = gm_f_int_info_ulcevdv(t302, GM_CONV(gm_csguankko4lmhd, t303));
                gm_tg5lql6itufk7 t305 = t304;
                gm_ttovhfee8mf28 t306;
                if (((((uint8_t *)&t305))[8] != 0)) {
                    gm_ttovhfee8mf28 t307 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t305) + 0));
                    t306 = t307;
                } else {
                    gm_t1ec8br9ddkav t308;
                    GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t308 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t308 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t308 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t308 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t308 + 24, 0);
                    ((uint8_t *)&t308)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t308 + 8), ((uint8_t)0));
                    gm_t1ec8br9ddkav t309 = t308;
                    uint8_t t310 = ((((uint8_t *)&t309))[0] != 0);
                    F.ret = t308;
                    goto gm_fret;
                }
                gm_l23 = t306;
            }
            {
                void * t311 = gm_l0;
                gm_t88rcucg1b7uk t312 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                uint64_t t313 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t311, t312);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t314 = gm_f_us_cuv01fp(t313);
                gm_l24 = t314;
            }
            {
                void * t315 = gm_l0;
                gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                uint64_t t317 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t318 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t319 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t320 = gm_l24;
                uint64_t t321 = (uintptr_t)t320;
                if (t319 > t321 || t321 > (uintptr_t)t317) gm_trap_slice(t319, t321, t317, gm_srcj10g653a00lt, 271, 39);
                gm_b16_8 t322;
                GM_ST(void *, (uint8_t *)&t322, (void *)(((uint8_t *)t318 + t319 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(t321 - t319));
                gm_b16_8 t323 = t322;
                gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                unsigned __int128 t325 = gm_f_get_int_pg7p4gc(t315, GM_CONV(gm_csguankko4lmhd, t316), t324);
                gm_l25 = t325;
            }
            {
                void * t326 = gm_l0;
                gm_t88rcucg1b7uk t327 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t329 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t330 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t331 = gm_l24;
                uint64_t t332 = (uintptr_t)t331;
                if (t330 > t332 || t332 > (uintptr_t)t328) gm_trap_slice(t330, t332, t328, gm_srcj10g653a00lt, 272, 39);
                gm_b16_8 t333;
                GM_ST(void *, (uint8_t *)&t333, (void *)(((uint8_t *)t329 + t330 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(t332 - t330));
                gm_b16_8 t334 = t333;
                gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)&t334));
                unsigned __int128 t336 = gm_f_get_int_pg7p4gc(t326, GM_CONV(gm_csguankko4lmhd, t327), t335);
                gm_l26 = t336;
            }
            {
                gm_t1ec8br9ddkav t337;
                uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                if (!t339) goto ie338;
                unsigned __int128 t341 = gm_l25;
                unsigned __int128 t342 = t341;
                __int128 t343;
                memset(&t343, 0, sizeof t343); memcpy(&t343, ((uint8_t *)&t342), sizeof t343 < 16 ? sizeof t343 : 16);
                unsigned __int128 t344 = gm_l26;
                unsigned __int128 t345 = t344;
                __int128 t346;
                memset(&t346, 0, sizeof t346); memcpy(&t346, ((uint8_t *)&t345), sizeof t346 < 16 ? sizeof t346 : 16);
                uint8_t t347 = (t343 < t346);
                uint8_t t348 = t347;
                gm_t1ec8br9ddkav t349;
                GM_ST(uint8_t, (uint8_t *)&t349 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t349 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t349 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t349 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t349 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t349 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t349 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t349 + 24, 0);
                ((uint8_t *)&t349)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t349 + 8), t348);
                t337 = t349;
                goto ix338;
ie338:;
                unsigned __int128 t351 = gm_l25;
                unsigned __int128 t352 = gm_l26;
                uint8_t t353 = (t351 < t352);
                uint8_t t354 = t353;
                gm_t1ec8br9ddkav t355;
                GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t355 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t355 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t355 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t355 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t355 + 24, 0);
                ((uint8_t *)&t355)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t355 + 8), t354);
                t337 = t355;
ix338:;
                gm_t1ec8br9ddkav t356 = t337;
                uint8_t t357 = ((((uint8_t *)&t356))[0] != 0);
                F.ret = t337;
                goto gm_fret;
            }
            goto ix295;
ie295:;
ix295:;
        }
        {
            void * t359 = gm_l0;
            gm_t88rcucg1b7uk t360 = gm_l2;
            gm_tkn42u7na3t81 t361 = gm_f_bits_struct_ty_22ue20s(t359, GM_CONV(gm_csguankko4lmhd, t360));
            gm_tkn42u7na3t81 t362 = t361;
            if (!((((uint8_t *)&t362))[4] != 0)) goto ie358;
            gm_t88rcucg1b7uk t363 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t362));
            gm_l27 = t363;
            {
                void * t365 = gm_l0;
                gm_tl874t2atnlh3 t366 = gm_l1;
                gm_t88rcucg1b7uk t367 = gm_l27;
                gm_b16_8 t368 = gm_l3;
                gm_b16_8 t369 = gm_l4;
                gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t365, t366, t367, t368, t369);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix358;
ie358:;
ix358:;
        }
        {
            void * t370 = gm_l0;
            gm_tl874t2atnlh3 t371 = gm_l1;
            void * t372 = gm_l0;
            void * t373 = GM_LD(void *, (((uint8_t *)t372) + 8));
            gm_t88rcucg1b7uk t374 = gm_l2;
            gm_b16_8 t375 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t373, t374);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t376;
            GM_ST(gm_b16_8, ((uint8_t *)&t376 + 0), t375);
            gm_tabb710d9mq2e t377;
            gm_f_format_vqmfrgb((void *)&t377, (void *)NULL, cx, NULL, t376);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t378 = t377;
            if (((((uint8_t *)&t378))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t378) + 8, gm_srcj10g653a00lt, 276, 34);
            gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)&t378) + 8));
            gm_b16_8 t380 = t379;
            gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
            gm_t76123gfet73g t382;
            gm_f_trap_qh84l9d((void *)&t382, (void *)NULL, cx, NULL, t370, t371, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t383;
            GM_ST(uint8_t, (uint8_t *)&t383 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t383 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t383 + 4, 0);
            ((uint8_t *)&t383)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t383 + 8), t382);
            gm_t1ec8br9ddkav t384 = t383;
            uint8_t t385 = ((((uint8_t *)&t384))[0] != 0);
            F.ret = t383;
            goto gm_fret;
        }
        goto me6;
mn292:;
ma14:;
        void * t387 = gm_l0;
        gm_tl874t2atnlh3 t388 = gm_l1;
        void * t389 = gm_l0;
        void * t390 = GM_LD(void *, (((uint8_t *)t389) + 8));
        gm_t88rcucg1b7uk t391 = gm_l2;
        gm_b16_8 t392 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t390, t391);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t393;
        GM_ST(gm_b16_8, ((uint8_t *)&t393 + 0), t392);
        gm_tabb710d9mq2e t394;
        gm_f_format_vqmfrgb((void *)&t394, (void *)NULL, cx, NULL, t393);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t395 = t394;
        if (((((uint8_t *)&t395))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t395) + 8, gm_srcj10g653a00lt, 278, 35);
        gm_b16_8 t396 = GM_LD(gm_b16_8, (((uint8_t *)&t395) + 8));
        gm_b16_8 t397 = t396;
        gm_b16_8 t398 = GM_LD(gm_b16_8, ((uint8_t *)&t397));
        gm_t76123gfet73g t399;
        gm_f_trap_qh84l9d((void *)&t399, (void *)NULL, cx, NULL, t387, t388, t398);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1ec8br9ddkav t400;
        GM_ST(uint8_t, (uint8_t *)&t400 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t400 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t400 + 4, 0);
        ((uint8_t *)&t400)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t400 + 8), t399);
        gm_t1ec8br9ddkav t401 = t400;
        uint8_t t402 = ((((uint8_t *)&t401))[0] != 0);
        F.ret = t400;
        goto gm_fret;
        goto me6;
mn386:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 238, 5);
me6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_sym_6olbepb F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
    t1 = t21;
    goto me2;
mn19:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t23 != ((uint8_t)1ULL)) goto mn22;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
    t1 = t24;
    goto me2;
mn22:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
    t1 = t27;
    goto me2;
mn25:;
ma6:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)3ULL)) goto mn28;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(2));
    t1 = t30;
    goto me2;
mn28:;
ma7:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)4ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(1));
    t1 = t33;
    goto me2;
mn31:;
ma8:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)5ULL)) goto mn34;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
    t1 = t36;
    goto me2;
mn34:;
ma9:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)6ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
    t1 = t39;
    goto me2;
mn37:;
ma10:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)7ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
    t1 = t42;
    goto me2;
mn40:;
ma11:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)8ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
    t1 = t45;
    goto me2;
mn43:;
ma12:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)9ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
    t1 = t48;
    goto me2;
mn46:;
ma13:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)10ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
    t1 = t51;
    goto me2;
mn49:;
ma14:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t53 != ((uint8_t)11ULL)) goto mn52;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s4g3cajt15dmi));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
    t1 = t54;
    goto me2;
mn52:;
ma15:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)12ULL)) goto mn55;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sm5be4659ulci));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
    t1 = t57;
    goto me2;
mn55:;
ma16:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t59 != ((uint8_t)13ULL)) goto mn58;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s02ru83e3fffc));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
    t1 = t60;
    goto me2;
mn58:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 9, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_seq_of_ifahtjs F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint64_t gm_l4; /* n */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint64_t gm_l7; /* esz */
    gm_t2lqu8jsj511d gm_l8; /* sp */
    uint64_t gm_l9; /* total */
    gm_b16_8 gm_l11; /* data */
    GM_STACK_CHECK(&F, 1756u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t13 != ((uint8_t)11ULL)) goto mn12;
        uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l4 = t14;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
        gm_l5 = t15;
        gm_t2jeg3bg7sfqa t16;
        memset((uint8_t *)&t16 + 4, 0, 4);
        gm_t88rcucg1b7uk t17 = gm_l5;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t16 + 0, t17);
        void * t18 = gm_l0;
        gm_t88rcucg1b7uk t19 = gm_l5;
        uint64_t t20 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t21 = gm_f_us_cuv01fp(t20);
        GM_ST(uint64_t, (uint8_t *)&t16 + 8, t21);
        uint64_t t22 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t16 + 16, t22);
        gm_b16_8 t23 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 24, t23);
        gm_tmc91gv7n9c0i t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        ((uint8_t *)&t24)[0] = 0;
        GM_ST(gm_t2jeg3bg7sfqa, ((uint8_t *)&t24 + 8), t16);
        gm_tmc91gv7n9c0i t25 = t24;
        uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
        F.ret = t24;
        goto gm_fret;
        goto me6;
mn12:;
ma8:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t28 != ((uint8_t)10ULL)) goto mn27;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l6 = t29;
        {
            void * t31 = gm_l0;
            gm_t88rcucg1b7uk t32 = gm_l6;
            uint64_t t33 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t33;
        }
        {
            void * t34 = gm_l0;
            gm_b16_8 t35 = gm_l3;
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_t2lqu8jsj511d t38 = gm_f_slice_parts_1se5hu4(t34, t37);
            gm_l8 = t38;
        }
        {
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint64_t t40 = gm_l7;
            uint64_t r41; bool o42 = __builtin_mul_overflow(t39, t40, &r41);
            uint64_t t43 = r41;
            gm_l9 = t43;
        }
        {
            uint64_t t45 = gm_l7;
            uint8_t t46 = (!(t45 == ((uint64_t)0ULL)));
            if (!t46) goto ie44;
            uint64_t t47 = gm_l9;
            uint64_t t48 = gm_l7;
            if (t48 == 0) gm_trap("division by zero", gm_srcj10g653a00lt, 227, 33);
            uint64_t t49 = (uint64_t)(t47 / t48);
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t51 = (!(t49 == t50));
            if (!t51) goto ie44;
            {
                void * t53 = gm_l0;
                gm_tl874t2atnlh3 t54 = gm_l1;
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sqp26euqk4rkd));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(15));
                gm_t76123gfet73g t56;
                gm_f_trap_qh84l9d((void *)&t56, (void *)NULL, cx, NULL, t53, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tmc91gv7n9c0i t57;
                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 40, 0);
                ((uint8_t *)&t57)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t57 + 8), t56);
                gm_tmc91gv7n9c0i t58 = t57;
                uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
                F.ret = t57;
                goto gm_fret;
            }
            goto ix44;
ie44:;
ix44:;
        }
        {
            uint64_t t61 = gm_l9;
            uint8_t t62 = ((t61 == ((uint64_t)0ULL)));
            if (!t62) goto ie60;
            {
                gm_t2jeg3bg7sfqa t64;
                memset((uint8_t *)&t64 + 4, 0, 4);
                gm_t88rcucg1b7uk t65 = gm_l6;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t64 + 0, t65);
                uint64_t t66 = gm_l7;
                uint64_t t67 = gm_f_us_cuv01fp(t66);
                GM_ST(uint64_t, (uint8_t *)&t64 + 8, t67);
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                GM_ST(uint64_t, (uint8_t *)&t64 + 16, t68);
                gm_b16_8 t69 = gm_f_empty_slice_ijtp9lo();
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 24, t69);
                gm_tmc91gv7n9c0i t70;
                GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                ((uint8_t *)&t70)[0] = 0;
                GM_ST(gm_t2jeg3bg7sfqa, ((uint8_t *)&t70 + 8), t64);
                gm_tmc91gv7n9c0i t71 = t70;
                uint8_t t72 = ((((uint8_t *)&t71))[0] != 0);
                F.ret = t70;
                goto gm_fret;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            void * t73 = gm_l0;
            void * t74 = (void *)(((uint8_t *)t73) + 184);
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 0));
            uint64_t t76 = gm_l9;
            gm_t934k1j7u70ct t77;
            gm_f_read_u6r4vns((void *)&t77, t74, t75, t76, ((uint64_t)1ULL));
            gm_t934k1j7u70ct t78 = t77;
            gm_b16_8 t79;
            if (!((((uint8_t *)&t78))[0] != 0)) {
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
                t79 = t80;
            } else {
                gm_tpekhfs32op0f t81 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t78) + 8));
                F.l10 = t81;
                void * t82 = gm_l0;
                gm_tl874t2atnlh3 t83 = gm_l1;
                gm_t76123gfet73g t84;
                gm_f_mem_trap_ecogko0((void *)&t84, (void *)NULL, cx, NULL, t82, t83, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l10)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tmc91gv7n9c0i t85;
                GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 40, 0);
                ((uint8_t *)&t85)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t85 + 8), t84);
                gm_tmc91gv7n9c0i t86 = t85;
                uint8_t t87 = ((((uint8_t *)&t86))[0] != 0);
                F.ret = t85;
                goto gm_fret;
            }
            gm_l11 = t79;
        }
        {
            gm_t2jeg3bg7sfqa t88;
            memset((uint8_t *)&t88 + 4, 0, 4);
            gm_t88rcucg1b7uk t89 = gm_l6;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t88 + 0, t89);
            uint64_t t90 = gm_l7;
            uint64_t t91 = gm_f_us_cuv01fp(t90);
            GM_ST(uint64_t, (uint8_t *)&t88 + 8, t91);
            uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            GM_ST(uint64_t, (uint8_t *)&t88 + 16, t92);
            gm_b16_8 t93 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t88 + 24, t93);
            gm_tmc91gv7n9c0i t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            ((uint8_t *)&t94)[0] = 0;
            GM_ST(gm_t2jeg3bg7sfqa, ((uint8_t *)&t94 + 8), t88);
            gm_tmc91gv7n9c0i t95 = t94;
            uint8_t t96 = ((((uint8_t *)&t95))[0] != 0);
            F.ret = t94;
            goto gm_fret;
        }
        goto me6;
mn27:;
ma9:;
        void * t98 = gm_l0;
        gm_tl874t2atnlh3 t99 = gm_l1;
        void * t100 = gm_l0;
        void * t101 = GM_LD(void *, (((uint8_t *)t100) + 8));
        gm_t88rcucg1b7uk t102 = gm_l2;
        gm_b16_8 t103 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t104;
        GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
        gm_tabb710d9mq2e t105;
        gm_f_format_b946o7j((void *)&t105, (void *)NULL, cx, NULL, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t106 = t105;
        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_srcj10g653a00lt, 232, 35);
        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
        gm_b16_8 t108 = t107;
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
        gm_t76123gfet73g t110;
        gm_f_trap_qh84l9d((void *)&t110, (void *)NULL, cx, NULL, t98, t99, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmc91gv7n9c0i t111;
        GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t111 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t111 + 40, 0);
        ((uint8_t *)&t111)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t111 + 8), t110);
        gm_tmc91gv7n9c0i t112 = t111;
        uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
        F.ret = t111;
        goto gm_fret;
        goto me6;
mn97:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 221, 5);
me6:;
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
__attribute__((aligned(4))) void gm_f_unary_sorkbs7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, void * a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_unary_sorkbs7 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    void * gm_l3; /* x */
    void * gm_l4; /* e */
    gm_b16_8 gm_l5; /* dst */
    uint64_t gm_l6; /* n */
    gm_t51b2en4qp02s gm_l7; /* xb */
    gm_t2lqu8jsj511d gm_l8; /* mk */
    gm_b16_8 gm_l9; /* v */
    GM_STACK_CHECK(&F, 1028u);
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
        {
            void * t3 = gm_l3;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
            uint64_t t5 = gm_f_us_cuv01fp(t4);
            gm_l6 = t5;
        }
        {
            uint64_t t7 = gm_l6;
            uint8_t t8 = (t7 <= ((uint64_t)16ULL));
            if (!t8) goto ie6;
            {
            }
            {
                void * t10 = gm_l0;
                gm_tbl8il92l97ut t11 = gm_l1;
                void * t12 = gm_l3;
                void * t13 = ((uint8_t *)&gm_l7);
                uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t15 = gm_l6;
                uint64_t t16 = (uintptr_t)t15;
                if (t14 > t16 || t16 > (uintptr_t)16) gm_trap_slice(t14, t16, 16, gm_srcj10g653a00lt, 162, 24);
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + t14 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16 - t14));
                gm_tcfa6l7reoosn t18;
                gm_f_ev_25jfdar((void *)&t18, (void *)NULL, cx, NULL, t10, t11, t12, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t19 = t18;
                if (((((uint8_t *)&t19))[0] != 0)) {
                    gm_t76123gfet73g t20 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t19) + 8));
                    gm_tcfa6l7reoosn t21;
                    GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                    ((uint8_t *)&t21)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t21 + 8), t20);
                    F.ret = t21;
                    goto gm_fret;
                }
                gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
            }
            {
                void * t23 = gm_l0;
                void * t24 = gm_l4;
                gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 112));
                gm_t9qkh0rsooc8d t26 = gm_l2;
                void * t27 = gm_l3;
                gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 96));
                void * t29 = ((uint8_t *)&gm_l7);
                uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t31 = gm_l6;
                uint64_t t32 = (uintptr_t)t31;
                if (t30 > t32 || t32 > (uintptr_t)16) gm_trap_slice(t30, t32, 16, gm_srcj10g653a00lt, 163, 41);
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t29 + t30 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t30));
                gm_b16_8 t34 = t33;
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                gm_b16_8 t36 = gm_l5;
                gm_f_unop_qamfooj((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t23, t25, t26, t28, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            void * t37 = gm_l0;
            gm_t2lqu8jsj511d t38 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t37));
            gm_l8 = t38;
        }
        {
            void * t39 = gm_l0;
            void * t40 = gm_l3;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 104));
            gm_b16_8 t42 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t39, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t42;
        }
        {
            void * t43 = gm_l0;
            gm_tbl8il92l97ut t44 = gm_l1;
            void * t45 = gm_l3;
            gm_b16_8 t46 = gm_l9;
            gm_tcfa6l7reoosn t47;
            gm_f_ev_25jfdar((void *)&t47, (void *)NULL, cx, NULL, t43, t44, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t48 = t47;
            if (((((uint8_t *)&t48))[0] != 0)) {
                gm_t76123gfet73g t49 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t48) + 8));
                gm_tcfa6l7reoosn t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                ((uint8_t *)&t50)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t50 + 8), t49);
                F.ret = t50;
                goto gm_fret;
            }
            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 8));
        }
        {
            void * t52 = gm_l0;
            void * t53 = gm_l4;
            gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 112));
            gm_t9qkh0rsooc8d t55 = gm_l2;
            void * t56 = gm_l3;
            gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 96));
            gm_b16_8 t58 = gm_l9;
            gm_b16_8 t59 = t58;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            gm_b16_8 t61 = gm_l5;
            gm_tcfa6l7reoosn t62;
            gm_f_unop_qamfooj((void *)&t62, (void *)NULL, cx, NULL, t52, t54, t55, t57, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t63 = t62;
            if (((((uint8_t *)&t63))[0] != 0)) {
                gm_t76123gfet73g t64 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t63) + 8));
                gm_tcfa6l7reoosn t65;
                GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                ((uint8_t *)&t65)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t65 + 8), t64);
                F.ret = t65;
                goto gm_fret;
            }
            gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 8));
        }
        {
            void * t67 = gm_l0;
            gm_t2lqu8jsj511d t68 = gm_l8;
            gm_f_release_jn5kqdi(t67, GM_CONV(gm_csc7lc6jlkqkuq, t68));
        }
    }
    gm_b0_1 t69;
    gm_tcfa6l7reoosn t70;
    GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t70 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t70 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
    ((uint8_t *)&t70)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t70 + 8), t69);
    gm_tcfa6l7reoosn t71 = t70;
    F.ret = t71;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_unop_qamfooj F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* dst */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_ttovhfee8mf28 gm_l12; /* info */
    unsigned __int128 gm_l13; /* v */
    __int128 gm_l14; /* x */
    uint64_t gm_l15; /* n */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* esz */
    uint8_t gm_l18; /* is_mask */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* lane */
    gm_b16_8 gm_l23; /* ds */
    gm_t88rcucg1b7uk gm_l24; /* u */
    gm_ttovhfee8mf28 gm_l25; /* info */
    GM_STACK_CHECK(&F, 3627u);
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
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t8) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; default: goto ma7; }
ma4:;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t10 != ((uint8_t)2ULL)) goto mn9;
            {
                uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcj10g653a00lt, 107, 19);
                uint8_t *t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 1);
                uint8_t t15;
                uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcj10g653a00lt, 107, 31);
                uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 1));
                uint8_t t20 = ((t19 == ((uint8_t)0ULL)));
                if (!t20) goto ie16;
                uint8_t t22 = ((uint8_t)1ULL);
                t15 = t22;
                goto ix16;
ie16:;
                uint8_t t24 = ((uint8_t)0ULL);
                t15 = t24;
ix16:;
                GM_ST(uint8_t, t14, t15);
            }
            goto me3;
mn9:;
ma5:;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t26 != ((uint8_t)0ULL)) goto mn25;
            {
                void * t29 = gm_l0;
                gm_t88rcucg1b7uk t30 = gm_l3;
                gm_t53at80pfdus3 t31;
                gm_f_kind_htv6093((void *)&t31, t29, GM_CONV(gm_csguankko4lmhd, t30));
                gm_t53at80pfdus3 t32 = t31;
                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t32) + 0));
                if (t33 != ((uint8_t)3ULL)) goto ie28;
                {
                    void * t35 = gm_l0;
                    gm_t88rcucg1b7uk t36 = gm_l3;
                    void * t37 = gm_l0;
                    gm_t88rcucg1b7uk t38 = gm_l3;
                    gm_b16_8 t39 = gm_l4;
                    double t40 = gm_f_get_float_grmhlm4(t37, GM_CONV(gm_csguankko4lmhd, t38), t39);
                    double t41 = -t40;
                    gm_b16_8 t42 = gm_l5;
                    gm_f_put_float_fiad72d(t35, GM_CONV(gm_csguankko4lmhd, t36), t41, t42);
                }
                {
                    gm_b0_1 t43;
                    gm_tcfa6l7reoosn t44;
                    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t44 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t44 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
                    ((uint8_t *)&t44)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t44 + 8), t43);
                    gm_tcfa6l7reoosn t45 = t44;
                    uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
                    F.ret = t44;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t48 = gm_l0;
                gm_t88rcucg1b7uk t49 = gm_l3;
                gm_t53at80pfdus3 t50;
                gm_f_kind_htv6093((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
                gm_t53at80pfdus3 t51 = t50;
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t51) + 0));
                if (t52 != ((uint8_t)11ULL)) goto ie47;
                uint64_t t53 = GM_LD(uint64_t, ((((uint8_t *)&t51) + 8) + 0));
                gm_l6 = t53;
                gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t51) + 8) + 8));
                gm_l7 = t54;
                {
                    void * t56 = gm_l0;
                    gm_t88rcucg1b7uk t57 = gm_l7;
                    uint64_t t58 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t56, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t59 = gm_f_us_cuv01fp(t58);
                    gm_l8 = t59;
                }
                {
                    {
                        gm_l10 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t61 = gm_l6;
                        uint64_t t62 = gm_f_us_cuv01fp(t61);
                        gm_l9 = t62;
                    }
                    {
                        for (;;) {
                            uint64_t t64 = gm_l10;
                            uint64_t t65 = gm_l9;
                            uint8_t t66 = (t64 < t65);
                            if (!t66) goto lb63;
                            {
                                uint64_t t69 = gm_l10;
                                uint64_t t70 = t69;
                                uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t70));
                                gm_l11 = t73;
                                {
                                    void * t75 = gm_l0;
                                    gm_tl874t2atnlh3 t76 = gm_l1;
                                    gm_t9qkh0rsooc8d t77 = gm_l2;
                                    gm_t88rcucg1b7uk t78 = gm_l7;
                                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                    void * t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                                    uint64_t t81 = gm_l11;
                                    uint64_t t82 = gm_l8;
                                    uint64_t r83; bool o84 = __builtin_mul_overflow(t81, t82, &r83);
                                    if (o84) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 66);
                                    uint64_t t85 = r83;
                                    uint64_t t86 = (uintptr_t)t85;
                                    uint64_t t87 = gm_l11;
                                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                                    if (o89) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 115, 76);
                                    uint64_t t90 = r88;
                                    uint64_t t91 = gm_l8;
                                    uint64_t r92; bool o93 = __builtin_mul_overflow(t90, t91, &r92);
                                    if (o93) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 75);
                                    uint64_t t94 = r92;
                                    uint64_t t95 = (uintptr_t)t94;
                                    if (t86 > t95 || t95 > (uintptr_t)t79) gm_trap_slice(t86, t95, t79, gm_srcj10g653a00lt, 115, 64);
                                    gm_b16_8 t96;
                                    GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t80 + t86 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(t95 - t86));
                                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    void * t98 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                                    uint64_t t99 = gm_l11;
                                    uint64_t t100 = gm_l8;
                                    uint64_t r101; bool o102 = __builtin_mul_overflow(t99, t100, &r101);
                                    if (o102) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 95);
                                    uint64_t t103 = r101;
                                    uint64_t t104 = (uintptr_t)t103;
                                    uint64_t t105 = gm_l11;
                                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                                    if (o107) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 115, 105);
                                    uint64_t t108 = r106;
                                    uint64_t t109 = gm_l8;
                                    uint64_t r110; bool o111 = __builtin_mul_overflow(t108, t109, &r110);
                                    if (o111) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 104);
                                    uint64_t t112 = r110;
                                    uint64_t t113 = (uintptr_t)t112;
                                    if (t104 > t113 || t113 > (uintptr_t)t97) gm_trap_slice(t104, t113, t97, gm_srcj10g653a00lt, 115, 91);
                                    gm_b16_8 t114;
                                    GM_ST(void *, (uint8_t *)&t114, (void *)(((uint8_t *)t98 + t104 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(t113 - t104));
                                    gm_tcfa6l7reoosn t115;
                                    gm_f_unop_qamfooj((void *)&t115, (void *)NULL, cx, NULL, t75, t76, t77, t78, t96, t114);
                                    if (gm_unw.kind) {
                                        goto gm_ll67;
                                    }
                                    gm_tcfa6l7reoosn t116 = t115;
                                    if (((((uint8_t *)&t116))[0] != 0)) {
                                        gm_t76123gfet73g t117 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t116) + 8));
                                        gm_tcfa6l7reoosn t118;
                                        GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                                        ((uint8_t *)&t118)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t118 + 8), t117);
                                        F.ret = t118;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 8));
                                }
                                goto me71;
mn72:;
                                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 115, 17);
me71:;
                            }
lc63:;
                            {
                                uint64_t t121 = gm_l10;
                                uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                                uint64_t t124 = r122;
                                gm_l10 = t124;
                            }
                        }
gm_ll67:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb63; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc63; }
                        }
                        goto gm_lad;
lb63:;
                    }
                }
                {
                    gm_b0_1 t125;
                    gm_tcfa6l7reoosn t126;
                    GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t126 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t126 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t126 + 24, 0);
                    ((uint8_t *)&t126)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t126 + 8), t125);
                    gm_tcfa6l7reoosn t127 = t126;
                    uint8_t t128 = ((((uint8_t *)&t127))[0] != 0);
                    F.ret = t126;
                    goto gm_fret;
                }
                goto ix47;
ie47:;
ix47:;
            }
            {
                void * t129 = gm_l0;
                gm_t88rcucg1b7uk t130 = gm_l3;
                gm_tg5lql6itufk7 t131 = gm_f_int_info_ulcevdv(t129, GM_CONV(gm_csguankko4lmhd, t130));
                gm_tg5lql6itufk7 t132 = t131;
                gm_ttovhfee8mf28 t133;
                if (((((uint8_t *)&t132))[8] != 0)) {
                    gm_ttovhfee8mf28 t134 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t132) + 0));
                    t133 = t134;
                } else {
                    void * t135 = gm_l0;
                    gm_tl874t2atnlh3 t136 = gm_l1;
                    void * t137 = gm_l0;
                    void * t138 = GM_LD(void *, (((uint8_t *)t137) + 8));
                    gm_t88rcucg1b7uk t139 = gm_l3;
                    gm_b16_8 t140 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t138, t139);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t141;
                    GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t140);
                    gm_tabb710d9mq2e t142;
                    gm_f_format_vno937p((void *)&t142, (void *)NULL, cx, NULL, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t143 = t142;
                    if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srcj10g653a00lt, 118, 61);
                    gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                    gm_b16_8 t145 = t144;
                    gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
                    gm_t76123gfet73g t147;
                    gm_f_trap_qh84l9d((void *)&t147, (void *)NULL, cx, NULL, t135, t136, t146);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t148;
                    GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                    ((uint8_t *)&t148)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t148 + 8), t147);
                    gm_tcfa6l7reoosn t149 = t148;
                    uint8_t t150 = ((((uint8_t *)&t149))[0] != 0);
                    F.ret = t148;
                    goto gm_fret;
                }
                gm_l12 = t133;
            }
            {
                void * t151 = gm_l0;
                gm_t88rcucg1b7uk t152 = gm_l3;
                gm_b16_8 t153 = gm_l4;
                unsigned __int128 t154 = gm_f_get_int_pg7p4gc(t151, GM_CONV(gm_csguankko4lmhd, t152), t153);
                gm_l13 = t154;
            }
            {
                uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                if (!t156) goto ie155;
                {
                    unsigned __int128 t158 = gm_l13;
                    unsigned __int128 t159 = t158;
                    __int128 t160;
                    memset(&t160, 0, sizeof t160); memcpy(&t160, ((uint8_t *)&t159), sizeof t160 < 16 ? sizeof t160 : 16);
                    gm_l14 = t160;
                }
                {
                    __int128 t162 = gm_l14;
                    uint32_t t163 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                    __int128 t164 = gm_f_int_lo_i8jbm6e(t163);
                    uint8_t t165 = ((t162 == t164));
                    if (!t165) goto ie161;
                    {
                        void * t167 = gm_l0;
                        gm_tl874t2atnlh3 t168 = gm_l1;
                        gm_b16_8 t169;
                        GM_ST(void *, (uint8_t *)&t169, (void *)(gm_skolmnfc0ccbd));
                        GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(1));
                        gm_tcfa6l7reoosn t170;
                        gm_f_checked_overflow_ma1bm1g((void *)&t170, (void *)NULL, cx, NULL, t167, t168, t169);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t171 = t170;
                        if (((((uint8_t *)&t171))[0] != 0)) {
                            gm_t76123gfet73g t172 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t171) + 8));
                            gm_tcfa6l7reoosn t173;
                            GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
                            ((uint8_t *)&t173)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t173 + 8), t172);
                            F.ret = t173;
                            goto gm_fret;
                        }
                        gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 8));
                    }
                    {
                        void * t175 = gm_l0;
                        gm_b16_8 t176 = gm_l5;
                        unsigned __int128 t177 = gm_l13;
                        gm_f_put_uint_cddin99(t175, t176, t177);
                    }
                    {
                        gm_b0_1 t178;
                        gm_tcfa6l7reoosn t179;
                        GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t179 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t179 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t179 + 24, 0);
                        ((uint8_t *)&t179)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t179 + 8), t178);
                        gm_tcfa6l7reoosn t180 = t179;
                        uint8_t t181 = ((((uint8_t *)&t180))[0] != 0);
                        F.ret = t179;
                        goto gm_fret;
                    }
                    goto ix161;
ie161:;
ix161:;
                }
                {
                    void * t182 = gm_l0;
                    gm_b16_8 t183 = gm_l5;
                    __int128 t184 = gm_l14;
                    if (t184 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcj10g653a00lt, 127, 34);
                    __int128 t185 = (__int128)((unsigned __int128)0 - (unsigned __int128)t184);
                    __int128 t186 = t185;
                    unsigned __int128 t187;
                    memset(&t187, 0, sizeof t187); memcpy(&t187, ((uint8_t *)&t186), sizeof t187 < 16 ? sizeof t187 : 16);
                    gm_f_put_uint_cddin99(t182, t183, t187);
                }
                goto ix155;
ie155:;
                unsigned __int128 t189 = gm_l13;
                uint8_t t190 = ((t189 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t190) goto ie188;
                {
                    gm_b16_8 t192 = gm_l5;
                    gm_f_fill_abmf5g5(t192, ((uint8_t)0ULL));
                }
                goto ix188;
ie188:;
                {
                    void * t194 = gm_l0;
                    gm_tl874t2atnlh3 t195 = gm_l1;
                    gm_b16_8 t196;
                    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_skolmnfc0ccbd));
                    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(1));
                    gm_tcfa6l7reoosn t197;
                    gm_f_checked_overflow_ma1bm1g((void *)&t197, (void *)NULL, cx, NULL, t194, t195, t196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t198 = t197;
                    if (((((uint8_t *)&t198))[0] != 0)) {
                        gm_t76123gfet73g t199 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t198) + 8));
                        gm_tcfa6l7reoosn t200;
                        GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                        ((uint8_t *)&t200)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t200 + 8), t199);
                        F.ret = t200;
                        goto gm_fret;
                    }
                    gm_b0_1 t201 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 8));
                }
                {
                    void * t202 = gm_l0;
                    gm_b16_8 t203 = gm_l5;
                    uint32_t t204 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                    unsigned __int128 t205 = gm_l13;
                    unsigned __int128 r206; bool o207 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t205, &r206);
                    unsigned __int128 t208 = r206;
                    unsigned __int128 t209 = gm_f_num_ext_348kmbu(((uint8_t)0), t204, t208);
                    gm_f_put_uint_cddin99(t202, t203, t209);
                }
ix188:;
ix155:;
            }
            goto me3;
mn25:;
ma6:;
            uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t211 != ((uint8_t)1ULL)) goto mn210;
            {
                void * t214 = gm_l0;
                gm_t88rcucg1b7uk t215 = gm_l3;
                gm_t53at80pfdus3 t216;
                gm_f_kind_htv6093((void *)&t216, t214, GM_CONV(gm_csguankko4lmhd, t215));
                gm_t53at80pfdus3 t217 = t216;
                uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&t217) + 0));
                if (t218 != ((uint8_t)11ULL)) goto ie213;
                uint64_t t219 = GM_LD(uint64_t, ((((uint8_t *)&t217) + 8) + 0));
                gm_l15 = t219;
                gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t217) + 8) + 8));
                gm_l16 = t220;
                {
                    void * t222 = gm_l0;
                    gm_t88rcucg1b7uk t223 = gm_l16;
                    uint64_t t224 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t222, t223);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t225 = gm_f_us_cuv01fp(t224);
                    gm_l17 = t225;
                }
                {
                    void * t226 = gm_l0;
                    gm_t88rcucg1b7uk t227 = gm_l16;
                    gm_t53at80pfdus3 t228;
                    gm_f_kind_htv6093((void *)&t228, t226, GM_CONV(gm_csguankko4lmhd, t227));
                    gm_t53at80pfdus3 t229 = t228;
                    uint8_t t230 = ((GM_LD(uint8_t, ((uint8_t *)&t229) + 0) == ((uint8_t)4ULL)));
                    gm_l18 = t230;
                }
                {
                    {
                        gm_l20 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t232 = gm_l15;
                        uint64_t t233 = gm_f_us_cuv01fp(t232);
                        gm_l19 = t233;
                    }
                    {
                        for (;;) {
                            uint64_t t235 = gm_l20;
                            uint64_t t236 = gm_l19;
                            uint8_t t237 = (t235 < t236);
                            if (!t237) goto lb234;
                            {
                                uint64_t t240 = gm_l20;
                                uint64_t t241 = t240;
                                uint64_t t244 = GM_LD(uint64_t, ((uint8_t *)&t241));
                                gm_l21 = t244;
                                {
                                    uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                    void * t247 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                                    uint64_t t248 = gm_l21;
                                    uint64_t t249 = gm_l17;
                                    uint64_t r250; bool o251 = __builtin_mul_overflow(t248, t249, &r250);
                                    if (o251) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 140, 31);
                                    uint64_t t252 = r250;
                                    uint64_t t253 = (uintptr_t)t252;
                                    uint64_t t254 = gm_l21;
                                    uint64_t r255; bool o256 = __builtin_add_overflow(t254, ((uint64_t)1ULL), &r255);
                                    if (o256) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 140, 41);
                                    uint64_t t257 = r255;
                                    uint64_t t258 = gm_l17;
                                    uint64_t r259; bool o260 = __builtin_mul_overflow(t257, t258, &r259);
                                    if (o260) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 140, 40);
                                    uint64_t t261 = r259;
                                    uint64_t t262 = (uintptr_t)t261;
                                    if (t253 > t262 || t262 > (uintptr_t)t246) gm_trap_slice(t253, t262, t246, gm_srcj10g653a00lt, 140, 29);
                                    gm_b16_8 t263;
                                    GM_ST(void *, (uint8_t *)&t263, (void *)(((uint8_t *)t247 + t253 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(t262 - t253));
                                    gm_l22 = t263;
                                }
                                {
                                    uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    void * t265 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                                    uint64_t t266 = gm_l21;
                                    uint64_t t267 = gm_l17;
                                    uint64_t r268; bool o269 = __builtin_mul_overflow(t266, t267, &r268);
                                    if (o269) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 141, 31);
                                    uint64_t t270 = r268;
                                    uint64_t t271 = (uintptr_t)t270;
                                    uint64_t t272 = gm_l21;
                                    uint64_t r273; bool o274 = __builtin_add_overflow(t272, ((uint64_t)1ULL), &r273);
                                    if (o274) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 141, 41);
                                    uint64_t t275 = r273;
                                    uint64_t t276 = gm_l17;
                                    uint64_t r277; bool o278 = __builtin_mul_overflow(t275, t276, &r277);
                                    if (o278) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 141, 40);
                                    uint64_t t279 = r277;
                                    uint64_t t280 = (uintptr_t)t279;
                                    if (t271 > t280 || t280 > (uintptr_t)t264) gm_trap_slice(t271, t280, t264, gm_srcj10g653a00lt, 141, 27);
                                    gm_b16_8 t281;
                                    GM_ST(void *, (uint8_t *)&t281, (void *)(((uint8_t *)t265 + t271 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(t280 - t271));
                                    gm_l23 = t281;
                                }
                                {
                                    uint8_t t283 = gm_l18;
                                    if (!t283) goto ie282;
                                    {
                                        uint64_t t285 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                        if (t285 >= (uintptr_t)(t286)) gm_trap_index(t285, t286, gm_srcj10g653a00lt, 143, 25);
                                        uint8_t *t287 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t285 * 1);
                                        uint8_t t288;
                                        uint64_t t290 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                        if (t290 >= (uintptr_t)(t291)) gm_trap_index(t290, t291, gm_srcj10g653a00lt, 143, 36);
                                        uint8_t t292 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t290 * 1));
                                        uint8_t t293 = ((t292 == ((uint8_t)0ULL)));
                                        if (!t293) goto ie289;
                                        uint8_t t295 = ((uint8_t)1ULL);
                                        t288 = t295;
                                        goto ix289;
ie289:;
                                        uint8_t t297 = ((uint8_t)0ULL);
                                        t288 = t297;
ix289:;
                                        GM_ST(uint8_t, t287, t288);
                                    }
                                    goto ix282;
ie282:;
                                    {
                                        void * t299 = gm_l0;
                                        gm_tl874t2atnlh3 t300 = gm_l1;
                                        gm_t9qkh0rsooc8d t301 = gm_l2;
                                        gm_t88rcucg1b7uk t302 = gm_l16;
                                        gm_b16_8 t303 = gm_l22;
                                        gm_b16_8 t304 = gm_l23;
                                        gm_tcfa6l7reoosn t305;
                                        gm_f_unop_qamfooj((void *)&t305, (void *)NULL, cx, NULL, t299, t300, t301, t302, t303, t304);
                                        if (gm_unw.kind) {
                                            goto gm_ll238;
                                        }
                                        gm_tcfa6l7reoosn t306 = t305;
                                        if (((((uint8_t *)&t306))[0] != 0)) {
                                            gm_t76123gfet73g t307 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t306) + 8));
                                            gm_tcfa6l7reoosn t308;
                                            GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
                                            ((uint8_t *)&t308)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t308 + 8), t307);
                                            F.ret = t308;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t309 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 8));
                                    }
ix282:;
                                }
                                goto me242;
mn243:;
                                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 139, 17);
me242:;
                            }
lc234:;
                            {
                                uint64_t t311 = gm_l20;
                                uint64_t r312; bool o313 = __builtin_add_overflow(t311, ((uint64_t)1ULL), &r312);
                                uint64_t t314 = r312;
                                gm_l20 = t314;
                            }
                        }
gm_ll238:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb234; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc234; }
                        }
                        goto gm_lad;
lb234:;
                    }
                }
                {
                    gm_b0_1 t315;
                    gm_tcfa6l7reoosn t316;
                    GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t316 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t316 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t316 + 24, 0);
                    ((uint8_t *)&t316)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t316 + 8), t315);
                    gm_tcfa6l7reoosn t317 = t316;
                    uint8_t t318 = ((((uint8_t *)&t317))[0] != 0);
                    F.ret = t316;
                    goto gm_fret;
                }
                goto ix213;
ie213:;
ix213:;
            }
            {
                void * t319 = gm_l0;
                gm_t88rcucg1b7uk t320 = gm_l3;
                gm_tkn42u7na3t81 t321 = gm_f_bits_struct_ty_22ue20s(t319, GM_CONV(gm_csguankko4lmhd, t320));
                gm_tkn42u7na3t81 t322 = t321;
                gm_t88rcucg1b7uk t323;
                if (((((uint8_t *)&t322))[4] != 0)) {
                    gm_t88rcucg1b7uk t324 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t322) + 0));
                    t323 = t324;
                } else {
                    gm_t88rcucg1b7uk t325 = gm_l3;
                    t323 = t325;
                }
                gm_l24 = t323;
            }
            {
                void * t326 = gm_l0;
                gm_t88rcucg1b7uk t327 = gm_l24;
                gm_tg5lql6itufk7 t328 = gm_f_int_info_ulcevdv(t326, GM_CONV(gm_csguankko4lmhd, t327));
                gm_tg5lql6itufk7 t329 = t328;
                gm_ttovhfee8mf28 t330;
                if (((((uint8_t *)&t329))[8] != 0)) {
                    gm_ttovhfee8mf28 t331 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t329) + 0));
                    t330 = t331;
                } else {
                    void * t332 = gm_l0;
                    gm_tl874t2atnlh3 t333 = gm_l1;
                    void * t334 = gm_l0;
                    void * t335 = GM_LD(void *, (((uint8_t *)t334) + 8));
                    gm_t88rcucg1b7uk t336 = gm_l24;
                    gm_b16_8 t337 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t335, t336);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t338;
                    GM_ST(gm_b16_8, ((uint8_t *)&t338 + 0), t337);
                    gm_tabb710d9mq2e t339;
                    gm_f_format_gd3p27d((void *)&t339, (void *)NULL, cx, NULL, t338);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t340 = t339;
                    if (((((uint8_t *)&t340))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t340) + 8, gm_srcj10g653a00lt, 151, 61);
                    gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&t340) + 8));
                    gm_b16_8 t342 = t341;
                    gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
                    gm_t76123gfet73g t344;
                    gm_f_trap_qh84l9d((void *)&t344, (void *)NULL, cx, NULL, t332, t333, t343);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t345;
                    GM_ST(uint8_t, (uint8_t *)&t345 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t345 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t345 + 4, 0);
                    ((uint8_t *)&t345)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t345 + 8), t344);
                    gm_tcfa6l7reoosn t346 = t345;
                    uint8_t t347 = ((((uint8_t *)&t346))[0] != 0);
                    F.ret = t345;
                    goto gm_fret;
                }
                gm_l25 = t330;
            }
            {
                void * t348 = gm_l0;
                gm_b16_8 t349 = gm_l5;
                uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                uint32_t t351 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                void * t352 = gm_l0;
                gm_t88rcucg1b7uk t353 = gm_l24;
                gm_b16_8 t354 = gm_l4;
                unsigned __int128 t355 = gm_f_get_int_pg7p4gc(t352, GM_CONV(gm_csguankko4lmhd, t353), t354);
                unsigned __int128 t356 = (unsigned __int128)~t355;
                unsigned __int128 t357 = gm_f_num_ext_348kmbu(t350, t351, t356);
                gm_f_put_uint_cddin99(t348, t349, t357);
            }
            goto me3;
mn210:;
ma7:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 106, 5);
me3:;
        }
    }
    gm_b0_1 t358;
    gm_tcfa6l7reoosn t359;
    GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t359 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t359 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t359 + 24, 0);
    ((uint8_t *)&t359)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t359 + 8), t358);
    gm_tcfa6l7reoosn t360 = t359;
    F.ret = t360;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
