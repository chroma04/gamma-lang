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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_t90jeqj7rr485;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_tg1t0c5hkovn5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tf1bjg81u0glv f0; uint8_t f1; uint8_t pe[7]; } gm_t8sds1gdern44;
_Static_assert(sizeof(gm_t90jeqj7rr485) == 48 && _Alignof(gm_t90jeqj7rr485) == 8 && offsetof(gm_t90jeqj7rr485, f0) == 0 && offsetof(gm_t90jeqj7rr485, f1) == 8 && offsetof(gm_t90jeqj7rr485, f2) == 16 && offsetof(gm_t90jeqj7rr485, f3) == 32 && offsetof(gm_t90jeqj7rr485, f4) == 40, "layout of `CallEdge`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint64_t f0; gm_t90jeqj7rr485 f1; } gm_te7ftefsqvm53;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_toceffb697p0u f3; uint8_t pe[4]; } gm_tegs8i1nl3jqh;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg1t0c5hkovn5) == 128 && _Alignof(gm_tg1t0c5hkovn5) == 8 && offsetof(gm_tg1t0c5hkovn5, f0) == 0 && offsetof(gm_tg1t0c5hkovn5, f1) == 16 && offsetof(gm_tg1t0c5hkovn5, f2) == 32 && offsetof(gm_tg1t0c5hkovn5, f3) == 48 && offsetof(gm_tg1t0c5hkovn5, f4) == 64 && offsetof(gm_tg1t0c5hkovn5, f5) == 80 && offsetof(gm_tg1t0c5hkovn5, f6) == 96 && offsetof(gm_tg1t0c5hkovn5, f7) == 112, "layout of `([]u8, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
typedef struct { void * f0; void * f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; void * f6; } gm_tlio37tsvme2e;
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8sds1gdern44) == 32 && _Alignof(gm_t8sds1gdern44) == 8 && offsetof(gm_t8sds1gdern44, f0) == 0 && offsetof(gm_t8sds1gdern44, f1) == 24, "layout of `?(usize, string)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_te7ftefsqvm53) == 56 && _Alignof(gm_te7ftefsqvm53) == 8 && offsetof(gm_te7ftefsqvm53, f0) == 0 && offsetof(gm_te7ftefsqvm53, f1) == 8, "layout of `(i: usize, x: CallEdge)`");
_Static_assert(sizeof(gm_tegs8i1nl3jqh) == 56 && _Alignof(gm_tegs8i1nl3jqh) == 8 && offsetof(gm_tegs8i1nl3jqh, f0) == 0 && offsetof(gm_tegs8i1nl3jqh, f1) == 8 && offsetof(gm_tegs8i1nl3jqh, f2) == 24 && offsetof(gm_tegs8i1nl3jqh, f3) == 36, "layout of `Live`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
_Static_assert(sizeof(gm_tlio37tsvme2e) == 128 && _Alignof(gm_tlio37tsvme2e) == 8 && offsetof(gm_tlio37tsvme2e, f0) == 0 && offsetof(gm_tlio37tsvme2e, f1) == 8 && offsetof(gm_tlio37tsvme2e, f2) == 16 && offsetof(gm_tlio37tsvme2e, f3) == 24 && offsetof(gm_tlio37tsvme2e, f4) == 40 && offsetof(gm_tlio37tsvme2e, f5) == 80 && offsetof(gm_tlio37tsvme2e, f6) == 120, "layout of `StaleScan`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_add_growth_b20v4f1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_arg_key_3je06th {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_bind_sh36pqu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_branch_rbkqmlt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* from */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_clone_live_rrk5v52 {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* a */
    gm_trqfvmjtt4sij l1; /* r */
    gm_tegs8i1nl3jqh l4; /* x */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_collect_calls_f2qse9f {
    gm_frame h;
    void * l0; /* s */
    uint32_t l1; /* list_adt */
    uint64_t l2; /* f */
    void * l4; /* sums */
    void * l5; /* edges */
    gm_tsth9qu1dv0g3 l6; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_element_of_1c4chqi {
    gm_frame h;
    void * ret;
};
struct gm_fr_empty_slice_3acld9r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7ps0ndv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_90k6s9i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_avjvfhi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c7a529k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_i0ticdn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_ggjj7mu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_upb9j5h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_forget_2rgci5c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* kept */
    gm_tegs8i1nl3jqh l5; /* x */
    gm_b0_1 ret;
};
struct gm_fr_generic_of_1861hm9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_grow_by_call_7096mo4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_grows_buslmvn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_grows_directly_81uv5ob {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_growth_summaries_dguakce {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* sums */
    gm_trqfvmjtt4sij l5; /* edges */
    gm_trqfvmjtt4sij l10; /* incoming */
    gm_t90jeqj7rr485 l16; /* e */
    gm_trqfvmjtt4sij l17; /* work */
    gm_t90jeqj7rr485 l29; /* e */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_is_list_m0i72n0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_items_of_h89og2m {
    gm_frame h;
    void * ret;
};
struct gm_fr_join_paths_mf95qfu {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_join_tm3jl4b {
    gm_frame h;
    gm_tqetrgo0q24b5 l0; /* merged */
    gm_trqfvmjtt4sij l1; /* r */
    gm_trqfvmjtt4sij l2; /* m */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5cp093t {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mj5a0ol {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pjtmjmi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_stv2s0r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tv7nkel {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_type_bled6ne {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_urufktd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_grown_9b0hva9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_param_path_tcmbtr8 {
    gm_frame h;
    gm_t8sds1gdern44 ret;
};
struct gm_fr_place_key_8hg61p4 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_pop_fe4ef2u {
    gm_frame h;
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_scan_3flkb8v {
    gm_frame h;
    void * l0; /* sc */
    gm_trqfvmjtt4sij l5; /* base */
    gm_tqetrgo0q24b5 l6; /* merged */
    gm_trqfvmjtt4sij l7; /* r */
    gm_trqfvmjtt4sij l9; /* r */
    gm_trqfvmjtt4sij l10; /* m */
    gm_trqfvmjtt4sij l13; /* base */
    gm_tqetrgo0q24b5 l14; /* merged */
    gm_trqfvmjtt4sij l19; /* r */
    gm_trqfvmjtt4sij l20; /* m */
    gm_trqfvmjtt4sij l22; /* base */
    gm_trqfvmjtt4sij l23; /* r */
    gm_trqfvmjtt4sij l26; /* base */
    gm_t710lsrq1m8gk l27; /* x */
    gm_trqfvmjtt4sij l28; /* entry */
    gm_tsth9qu1dv0g3 l33; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_scan_block_cb34lg7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scan_conds_nodljh6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_of_favl5bk {
    gm_frame h;
    void * ret;
};
struct gm_fr_stale_element_pointers_jjgn0ll {
    gm_frame h;
    gm_tlio37tsvme2e l7; /* sc */
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_strip_coerce_vk5l7o4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_touch_aqpeu0m {
    gm_frame h;
    gm_tegs8i1nl3jqh l5; /* x */
    gm_b0_1 ret;
};
struct gm_fr_union_live_empebln {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* a */
    gm_trqfvmjtt4sij l1; /* b */
    gm_trqfvmjtt4sij l2; /* r */
    gm_tegs8i1nl3jqh l5; /* x */
    gm_trqfvmjtt4sij ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_add_growth_b20v4f1(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_arg_key_3je06th(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_sh36pqu(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_branch_rbkqmlt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_clone_live_rrk5v52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_f2qse9f(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3, void * a4, void * a5);
__attribute__((aligned(4))) uint64_t gm_f_count_9fkti0l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_element_of_1c4chqi(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3acld9r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7ps0ndv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_90k6s9i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_avjvfhi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c7a529k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ggjj7mu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_upb9j5h(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_forget_2rgci5c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ep82di1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_grow_by_call_7096mo4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_grows_buslmvn(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_grows_directly_81uv5ob(void * a0, uint32_t a1, void * a2, uint8_t a3, uint64_t a4, gm_csguankko4lmhd a5);
__attribute__((aligned(4))) void gm_f_growth_summaries_dguakce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_list_m0i72n0(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_items_of_h89og2m(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_join_paths_mf95qfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_tm3jl4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_5cp093t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mj5a0ol(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pjtmjmi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_stv2s0r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tv7nkel(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_list_type_bled6ne(void * a0, uint32_t a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_mark_grown_9b0hva9(void * a0, gm_b16_8 a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) void gm_f_param_path_tcmbtr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_place_key_8hg61p4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_fe4ef2u(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0jed0lq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1tq7lp9(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_2mg7bkm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ibbdq2p(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l73937f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_3flkb8v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_block_cb34lg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_conds_nodljh6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_source_of_favl5bk(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stale_element_pointers_jjgn0ll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_strip_coerce_vk5l7o4(void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_touch_aqpeu0m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_union_live_empebln(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s0a1978iad5c9[5];
extern const uint8_t gm_s616b6iju29ec[5];
extern const uint8_t gm_s8vgs1ersvo58[8];
extern const uint8_t gm_segmk88bjc49r[7];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slu92ianijdhp[7];
extern const uint8_t gm_sm7hp6cgh4jle[6];
extern const uint8_t gm_sqp9p8tc2dil3[8];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcfk7rg8t5apoh[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sub1g4k5l4c07[11];
extern const uint8_t gm_sv6b1v96egn1q[7];
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_arg_key_3je06th(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_arg_key_3je06th F;
    void * gm_l0; /* a */
    void * gm_l1; /* x */
    void * gm_l2; /* p */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_f_strip_coerce_vk5l7o4(t2);
        gm_l1 = t3;
    }
    {
        void * t5 = gm_l1;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t6 != ((uint8_t)24ULL)) goto ie4;
        void * t7 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l2 = t7;
        {
            void * t9 = gm_l2;
            gm_f_place_key_8hg61p4((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t10 = gm_l1;
        gm_f_place_key_8hg61p4((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t10);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3acld9r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3acld9r F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7ps0ndv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7ps0ndv F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_90k6s9i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_90k6s9i F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_avjvfhi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_avjvfhi F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c7a529k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c7a529k F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_i0ticdn F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ggjj7mu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_ggjj7mu F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_scan_3flkb8v *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_b0_1 t4 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_upb9j5h(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_upb9j5h F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_calls_f2qse9f *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_collect_calls_f2qse9f *)((&F.h)->up))->l1));
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_collect_calls_f2qse9f *)((&F.h)->up))->l2));
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_calls_f2qse9f *)((&F.h)->up))->l4));
        void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_calls_f2qse9f *)((&F.h)->up))->l5));
        gm_b0_1 t8 = gm_f_collect_calls_f2qse9f((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7);
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
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_generic_of_1861hm9 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srclv0dcbbfhves, 590, 16);
        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96))[24] != 0)) goto ie2;
        gm_tiilmfd16v3fc t8 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96));
        gm_l2 = t8;
        {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t12 = gm_l1;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_list_m0i72n0(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_list_m0i72n0 F;
    void * gm_l0; /* sc */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 16));
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_list_type_bled6ne(t2, t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_list_5cp093t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5cp093t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_90k6s9i();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_mj5a0ol(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mj5a0ol F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7ps0ndv();
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
inline __attribute__((aligned(4))) void gm_f_list_pjtmjmi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pjtmjmi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3acld9r();
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
inline __attribute__((aligned(4))) void gm_f_list_stv2s0r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_stv2s0r F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_avjvfhi();
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
inline __attribute__((aligned(4))) void gm_f_list_tv7nkel(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tv7nkel F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c7a529k();
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
inline __attribute__((aligned(4))) uint8_t gm_f_list_type_bled6ne(void * a0, uint32_t a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_list_type_bled6ne F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* list_adt */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint32_t gm_l3; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l3 = t8;
        {
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            uint32_t t12 = gm_l3;
            uint32_t t13 = gm_f_template_9o92cvv(t11, t12);
            uint32_t t14 = gm_l1;
            uint8_t t15 = ((t13 == t14));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t17 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_urufktd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_i0ticdn();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_fe4ef2u(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_fe4ef2u F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t2lqu8jsj511d gm_l2; /* x */
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
            gm_tc70a9dbbmgkg t8;
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
        gm_t2lqu8jsj511d t17 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        gm_t2lqu8jsj511d t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out))[16] = 1;
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 0, t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
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
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_strip_coerce_vk5l7o4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_coerce_vk5l7o4 F;
    void * gm_l0; /* e */
    void * gm_l1; /* cur */
    void * gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_l1 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l1;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            if (t5 != ((uint8_t)32ULL)) goto lb3;
            void * t6 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
            gm_l2 = t6;
            {
                void * t9 = gm_l2;
                gm_l1 = t9;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t10 = gm_l1;
        uint8_t t11 = 0;
        F.ret = t10;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_add_growth_b20v4f1(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_add_growth_b20v4f1 F;
    void * gm_l0; /* sum */
    uint64_t gm_l1; /* param */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tf1bjg81u0glv gm_l5; /* x */
    GM_STACK_CHECK(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t3 = gm_l2;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint64_t t5 = gm_f_count_9fkti0l(t3, t4);
        uint8_t t6 = (t5 > ((uint64_t)4ULL));
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
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 0));
            gm_l3 = t11;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l4;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l4;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tf1bjg81u0glv t23 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t19 * 24));
                    gm_l5 = t23;
                    {
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                        uint64_t t27 = gm_l1;
                        uint8_t t28 = ((t26 == t27));
                        if (!t28) goto ie25;
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                        gm_b16_8 t30 = gm_l2;
                        uint8_t t31 = (gm_eq0iqiubfm7e(((uint8_t *)&t29), ((uint8_t *)&t30)));
                        if (!t31) goto ie25;
                        {
                            uint8_t t33 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 88, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t35 = gm_l4;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l4 = t38;
                }
            }
lb12:;
        }
    }
    {
        void * t39 = gm_l0;
        gm_tf1bjg81u0glv t40;
        uint64_t t41 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t40 + 0, t41);
        gm_b16_8 t42 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 8, t42);
        gm_tftb1odc739t5 t43 = gm_f_push_ibbdq2p(t39, &t40);
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcfk7rg8t5apoh, 91, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        uint8_t t45 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_arg_key_3je06th(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_bind_sh36pqu(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_bind_sh36pqu F;
    void * gm_l0; /* sc */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* init */
    void * gm_l3; /* place */
    gm_b16_8 gm_l4; /* k */
    GM_STACK_CHECK(&F, 326u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_b0_1 t4 = gm_f_forget_2rgci5c((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l2;
        void * t7 = gm_f_source_of_favl5bk(t5, t6);
        void * t8;
        if ((GM_LD(void *, ((uint8_t *)&t7)) != 0)) {
            void * t9 = GM_LD(void *, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_b0_1 t10;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l3 = t8;
    }
    {
        void * t12 = gm_l3;
        gm_th2lqbo77pvsc t13;
        gm_f_place_key_8hg61p4((void *)&t13, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_b0_1 t16;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l4 = t14;
    }
    {
        void * t19 = gm_l3;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 72));
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t22 = (gm_eqff85ac10o3(((uint8_t *)&t20), ((uint8_t *)&t21)));
        if (!t22) goto ie18;
        {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 40);
        gm_tegs8i1nl3jqh t28;
        memset((uint8_t *)&t28 + 4, 0, 4);
        memset((uint8_t *)&t28 + 52, 0, 4);
        uint32_t t29 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t28 + 0, t29);
        gm_b16_8 t30 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 8, t30);
        void * t31 = gm_l3;
        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t28 + 24, t32);
        gm_toceffb697p0u t33;
        memset(&t33, 0, sizeof t33);
        GM_ST(gm_toceffb697p0u, (uint8_t *)&t28 + 36, t33);
        gm_tftb1odc739t5 t34 = gm_f_push_2mg7bkm(t27, &t28);
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcfk7rg8t5apoh, 276, 5);
        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
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
inline __attribute__((aligned(4))) void gm_f_branch_rbkqmlt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2) {
    struct gm_fr_branch_rbkqmlt F;
    void * gm_l0; /* sc */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint8_t *t3 = (((uint8_t *)t2) + 40);
        gm_f_clone_live_rrk5v52((void *)&(*(gm_trqfvmjtt4sij *)t3), (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        gm_b0_1 t6 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        void * t11 = gm_l2;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_t53at80pfdus3 t13;
        gm_f_kind_jcnt4hm((void *)&t13, t10, GM_CONV(gm_csguankko4lmhd, t12));
        uint8_t t14 = ((GM_LD(uint8_t, ((uint8_t *)&t13) + 0) == ((uint8_t)5ULL)));
        if (!t14) goto ie7;
        {
            gm_tqetrgo0q24b5 t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t18 = gm_l0;
        const uint8_t *t19 = (((uint8_t *)t18) + 40);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t19, 40);
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
inline __attribute__((aligned(4))) void gm_f_clone_live_rrk5v52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_clone_live_rrk5v52 F;
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_stv2s0r((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
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
                    gm_tegs8i1nl3jqh t15 = GM_LD(gm_tegs8i1nl3jqh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 56));
                    F.l4 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l1);
                        gm_tftb1odc739t5 t18 = gm_f_push_2mg7bkm(t17, &(*(gm_tegs8i1nl3jqh *)((uint8_t *)&F.l4)));
                        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcfk7rg8t5apoh, 183, 24);
                        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 183, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t21 = gm_l3;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l3 = t24;
                }
            }
lb4:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_f2qse9f(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3, void * a4, void * a5) {
    struct gm_fr_collect_calls_f2qse9f F;
    void * gm_l3; /* e */
    gm_b16_8 gm_l7; /* args */
    uint32_t gm_l8; /* g */
    void * gm_l9; /* sig */
    uint8_t gm_l10; /* core */
    uint64_t gm_l11; /* target */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint8_t gm_l15; /* is_const */
    gm_t88rcucg1b7uk gm_l16; /* pointee */
    gm_b16_8 gm_l17; /* k */
    uint64_t gm_l18; /* param */
    gm_b16_8 gm_l19; /* path */
    GM_STACK_CHECK(&F, 948u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    F.l4 = a4;
    F.l5 = a5;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        void * t5 = gm_l3;
        uint8_t t6;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t9 != ((uint8_t)27ULL)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 99, 8);
me7:;
        if (!t6) {
            void * t11 = gm_l3;
            uint8_t t12;
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            if (t15 != ((uint8_t)26ULL)) goto mn14;
            t12 = ((uint8_t)1);
            goto me13;
mn14:;
            t12 = ((uint8_t)0);
            goto me13;
mn16:;
            gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 99, 33);
me13:;
            t4 = t12;
        } else t4 = 1;
        if (!t4) {
            void * t17 = gm_l3;
            uint8_t t18;
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            if (t21 != ((uint8_t)28ULL)) goto mn20;
            t18 = ((uint8_t)1);
            goto me19;
mn20:;
            t18 = ((uint8_t)0);
            goto me19;
mn22:;
            gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 99, 56);
me19:;
            t3 = t18;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t26 = gm_l3;
        gm_closure t27 = ((gm_closure){(void *)gm_f_fn_upb9j5h, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = gm_l3;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
        if (t30 != ((uint8_t)34ULL)) goto ie28;
        gm_tsth9qu1dv0g3 t31 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t29) + 8) + 8) + 0));
        F.l6 = t31;
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((((uint8_t *)t29) + 8) + 8) + 40));
        gm_l7 = t32;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t35 != ((uint8_t)0ULL)) goto ie34;
            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
            gm_l8 = t36;
            {
                uint32_t t38 = gm_l8;
                uint64_t t39 = (uint64_t)t38;
                uint64_t t40 = (uintptr_t)t39;
                void * t41 = F.l0;
                uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 344) + 0) + 8);
                if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_srcfk7rg8t5apoh, 103, 13);
                void * t43 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 344) + 0)) + t40 * 136);
                gm_l9 = t43;
            }
            {
                void * t44 = gm_l9;
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 32));
                uint64_t t46 = (uintptr_t)t45;
                void * t47 = F.l0;
                void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 200) + 0) + 8);
                if (t46 >= (uintptr_t)(t49)) gm_trap_index(t46, t49, gm_srcfk7rg8t5apoh, 104, 13);
                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t48) + 200) + 0)) + t46 * 184) + 112));
                gm_l10 = t50;
            }
            {
                void * t51 = F.l0;
                uint32_t t52 = gm_l8;
                uint32_t t53 = gm_f_generic_of_1861hm9(t51, t52);
                uint64_t t54 = (uint64_t)t53;
                gm_l11 = t54;
            }
            {
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l12 = t56;
                }
                {
                    for (;;) {
                        uint64_t t58 = gm_l13;
                        uint64_t t59 = gm_l12;
                        uint8_t t60 = (t58 < t59);
                        if (!t60) goto lb57;
                        {
                            uint64_t t63 = gm_l13;
                            uint64_t t66 = GM_LD(uint64_t, ((uint8_t *)&t63));
                            gm_l14 = t66;
                            {
                                uint64_t t69 = gm_l14;
                                void * t70 = gm_l9;
                                uint64_t t71 = GM_LD(uintptr_t, (((uint8_t *)t70) + 40) + 8);
                                uint8_t t72 = (t69 >= t71);
                                if (!t72) goto ie68;
                                {
                                    goto lb57;
                                }
                                goto ix68;
ie68:;
ix68:;
                            }
                            {
                                void * t75 = F.l0;
                                void * t76 = (void *)(((uint8_t *)t75) + 8);
                                uint64_t t77 = gm_l14;
                                uint64_t t78 = (uintptr_t)t77;
                                void * t79 = gm_l9;
                                uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)t79) + 40) + 8);
                                if (t78 >= (uintptr_t)(t80)) gm_trap_index(t78, t80, gm_srcfk7rg8t5apoh, 108, 47);
                                gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t79) + 40)) + t78 * 56) + 16));
                                gm_t53at80pfdus3 t82;
                                gm_f_kind_jcnt4hm((void *)&t82, t76, GM_CONV(gm_csguankko4lmhd, t81));
                                uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t82) + 0));
                                if (t83 != ((uint8_t)8ULL)) goto ie74;
                                uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)&t82) + 8) + 0));
                                gm_l15 = t84;
                                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t82) + 8) + 4));
                                gm_l16 = t85;
                                {
                                    uint8_t t88 = gm_l15;
                                    if (!t88) goto ie87;
                                    {
                                        goto lc57;
                                    }
                                    goto ix87;
ie87:;
ix87:;
                                }
                                {
                                    uint64_t t90 = gm_l14;
                                    uint64_t t91 = (uintptr_t)t90;
                                    uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_srcfk7rg8t5apoh, 110, 23);
                                    void * t93 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t91 * 88);
                                    gm_th2lqbo77pvsc t94;
                                    gm_f_arg_key_3je06th((void *)&t94, (void *)NULL, cx, NULL, t93);
                                    if (gm_unw.kind) {
                                        goto gm_ll61;
                                    }
                                    gm_b16_8 t95;
                                    if (((((uint8_t *)&t94))[16] != 0)) {
                                        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 0));
                                        t95 = t96;
                                    } else {
                                        goto lc57;
                                    }
                                    gm_l17 = t95;
                                }
                                {
                                    void * t98 = F.l0;
                                    uint64_t t99 = F.l2;
                                    gm_b16_8 t100 = gm_l17;
                                    gm_t8sds1gdern44 t101;
                                    gm_f_param_path_tcmbtr8((void *)&t101, (void *)NULL, cx, NULL, t98, t99, t100);
                                    if (gm_unw.kind) {
                                        goto gm_ll61;
                                    }
                                    if (!((((uint8_t *)&t101))[24] != 0)) goto ie97;
                                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&t101) + 0));
                                    gm_l18 = t102;
                                    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
                                    gm_l19 = t103;
                                    {
                                        void * t106 = F.l0;
                                        uint32_t t107 = F.l1;
                                        void * t108 = gm_l9;
                                        uint8_t t109 = gm_l10;
                                        uint64_t t110 = gm_l14;
                                        gm_t88rcucg1b7uk t111 = gm_l16;
                                        uint8_t t112 = gm_f_grows_directly_81uv5ob(t106, t107, t108, t109, t110, GM_CONV(gm_csguankko4lmhd, t111));
                                        if (!t112) goto ie105;
                                        {
                                            {
                                                uint64_t t115 = F.l2;
                                                uint64_t t116 = (uintptr_t)t115;
                                                void * t117 = F.l4;
                                                uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)t117) + 0) + 8);
                                                if (t116 >= (uintptr_t)(t118)) gm_trap_index(t116, t118, gm_srcfk7rg8t5apoh, 112, 81);
                                                void * t119 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t117) + 0)) + t116 * 40);
                                                uint64_t t120 = gm_l18;
                                                gm_b16_8 t121 = gm_l19;
                                                uint8_t t122 = gm_f_add_growth_b20v4f1((void *)NULL, cx, NULL, t119, t120, t121);
                                                if (gm_unw.kind) {
                                                    goto gm_ll61;
                                                }
                                            }
                                        }
                                        goto ix105;
ie105:;
ix105:;
                                    }
                                    {
                                        uint8_t t124 = gm_l10;
                                        uint8_t t125 = (uint8_t)!t124;
                                        if (!t125) goto ie123;
                                        {
                                            void * t127 = F.l5;
                                            gm_t90jeqj7rr485 t128;
                                            uint64_t t129 = F.l2;
                                            GM_ST(uint64_t, (uint8_t *)&t128 + 0, t129);
                                            uint64_t t130 = gm_l18;
                                            GM_ST(uint64_t, (uint8_t *)&t128 + 8, t130);
                                            gm_b16_8 t131 = gm_l19;
                                            GM_ST(gm_b16_8, (uint8_t *)&t128 + 16, t131);
                                            uint64_t t132 = gm_l11;
                                            GM_ST(uint64_t, (uint8_t *)&t128 + 32, t132);
                                            uint64_t t133 = gm_l14;
                                            GM_ST(uint64_t, (uint8_t *)&t128 + 40, t133);
                                            gm_tftb1odc739t5 t134 = gm_f_push_l73937f(t127, &t128);
                                            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_srcfk7rg8t5apoh, 113, 23);
                                            gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                                        }
                                        goto ix123;
ie123:;
ix123:;
                                    }
                                    goto ix97;
ie97:;
                                    {
                                        goto lc57;
                                    }
ix97:;
                                }
                                goto ix74;
ie74:;
                                {
                                    goto lc57;
                                }
ix74:;
                            }
                            goto me64;
mn65:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 106, 5);
me64:;
                        }
lc57:;
                        {
                            uint64_t t139 = gm_l13;
                            uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                            uint64_t t142 = r140;
                            gm_l13 = t142;
                        }
                    }
gm_ll61:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb57; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc57; }
                    }
                    goto gm_lad;
lb57:;
                }
            }
            goto ix34;
ie34:;
            {
                gm_b0_1 t144;
                uint8_t t145 = 0;
                F.ret = t144;
                goto gm_fret;
            }
ix34:;
        }
        goto ix28;
ie28:;
        {
            gm_b0_1 t147;
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
ix28:;
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
inline __attribute__((aligned(4))) void * gm_f_element_of_1c4chqi(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_element_of_1c4chqi F;
    void * gm_l0; /* sc */
    void * gm_l1; /* p */
    void * gm_l2; /* base */
    void * gm_l3; /* l */
    void * gm_l4; /* base */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma4; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)20ULL)) goto mn9;
        void * t11 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t11;
        {
            void * t14 = gm_l0;
            void * t15 = gm_l2;
            void * t16 = gm_f_items_of_h89og2m(t14, t15);
            if (!(GM_LD(void *, ((uint8_t *)&t16)) != 0)) goto ie13;
            void * t17 = GM_LD(void *, ((uint8_t *)&t16));
            gm_l3 = t17;
            {
                void * t19 = gm_l3;
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            void * t25 = gm_l2;
            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + 0));
            gm_t53at80pfdus3 t27;
            gm_f_kind_jcnt4hm((void *)&t27, t24, GM_CONV(gm_csguankko4lmhd, t26));
            uint8_t t28;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            if (t31 != ((uint8_t)11ULL)) goto mn30;
            t28 = ((uint8_t)1);
            goto me29;
mn30:;
            t28 = ((uint8_t)0);
            goto me29;
mn32:;
            gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 247, 16);
me29:;
            if (!t28) goto ie21;
            {
                void * t34 = gm_l0;
                void * t35 = gm_l2;
                void * t36 = gm_f_element_of_1c4chqi(t34, t35);
                uint8_t t37 = 0;
                F.ret = t36;
                goto gm_fret;
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)19ULL)) goto mn38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t40;
        void * t41 = gm_l0;
        void * t42 = gm_l4;
        void * t43 = gm_f_element_of_1c4chqi(t41, t42);
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me3;
mn38:;
ma6:;
        goto me3;
mn45:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 244, 5);
me3:;
    }
    {
        uint8_t t47 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ggjj7mu(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_upb9j5h(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_forget_2rgci5c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_forget_2rgci5c F;
    void * gm_l0; /* sc */
    uint32_t gm_l1; /* l */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_stv2s0r((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 40) + 0));
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
                    gm_tegs8i1nl3jqh t16 = GM_LD(gm_tegs8i1nl3jqh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 56));
                    F.l5 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = (!(t19 == t20));
                        if (!t21) goto ie18;
                        {
                            void * t23 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t24 = gm_f_push_2mg7bkm(t23, &(*(gm_tegs8i1nl3jqh *)((uint8_t *)&F.l5)));
                            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcfk7rg8t5apoh, 282, 31);
                            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 281, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
                }
            }
lb5:;
        }
    }
    {
        void * t31 = gm_l0;
        gm_trqfvmjtt4sij t32 = F.l2;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t31) + 40), t32);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_grow_by_call_7096mo4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_grow_by_call_7096mo4 F;
    void * gm_l0; /* sc */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* sig */
    uint8_t gm_l5; /* core */
    uint64_t gm_l6; /* target */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* is_const */
    gm_t88rcucg1b7uk gm_l11; /* pointee */
    gm_b16_8 gm_l12; /* k */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tf1bjg81u0glv gm_l15; /* x */
    GM_STACK_CHECK(&F, 1001u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 40) + 0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t9 = gm_l1;
        uint64_t t10 = (uint64_t)t9;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 344) + 0) + 8);
        if (t11 >= (uintptr_t)(t14)) gm_trap_index(t11, t14, gm_srcfk7rg8t5apoh, 311, 13);
        void * t15 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 344) + 0)) + t11 * 136);
        gm_l4 = t15;
    }
    {
        void * t16 = gm_l4;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 32));
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 200) + 0) + 8);
        if (t18 >= (uintptr_t)(t22)) gm_trap_index(t18, t22, gm_srcfk7rg8t5apoh, 312, 13);
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 200) + 0)) + t18 * 184) + 112));
        gm_l5 = t23;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        uint32_t t26 = gm_l1;
        uint32_t t27 = gm_f_generic_of_1861hm9(t25, t26);
        uint64_t t28 = (uint64_t)t27;
        gm_l6 = t28;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l7 = t30;
        }
        {
            for (;;) {
                uint64_t t32 = gm_l8;
                uint64_t t33 = gm_l7;
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l8;
                    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                    gm_l9 = t40;
                    {
                        uint64_t t43 = gm_l9;
                        void * t44 = gm_l4;
                        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 40) + 8);
                        uint8_t t46 = (t43 >= t45);
                        if (!t46) goto ie42;
                        {
                            goto lb31;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    {
                        void * t49 = gm_l0;
                        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                        void * t51 = (void *)(((uint8_t *)t50) + 8);
                        uint64_t t52 = gm_l9;
                        uint64_t t53 = (uintptr_t)t52;
                        void * t54 = gm_l4;
                        uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)t54) + 40) + 8);
                        if (t53 >= (uintptr_t)(t55)) gm_trap_index(t53, t55, gm_srcfk7rg8t5apoh, 316, 50);
                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t54) + 40)) + t53 * 56) + 16));
                        gm_t53at80pfdus3 t57;
                        gm_f_kind_jcnt4hm((void *)&t57, t51, GM_CONV(gm_csguankko4lmhd, t56));
                        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
                        if (t58 != ((uint8_t)8ULL)) goto ie48;
                        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)&t57) + 8) + 0));
                        gm_l10 = t59;
                        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t57) + 8) + 4));
                        gm_l11 = t60;
                        {
                            uint8_t t63 = gm_l10;
                            if (!t63) goto ie62;
                            {
                                goto lc31;
                            }
                            goto ix62;
ie62:;
ix62:;
                        }
                        {
                            uint64_t t65 = gm_l9;
                            uint64_t t66 = (uintptr_t)t65;
                            uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcfk7rg8t5apoh, 318, 23);
                            void * t68 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t66 * 88);
                            gm_th2lqbo77pvsc t69;
                            gm_f_arg_key_3je06th((void *)&t69, (void *)NULL, cx, NULL, t68);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            gm_b16_8 t70;
                            if (((((uint8_t *)&t69))[16] != 0)) {
                                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 0));
                                t70 = t71;
                            } else {
                                goto lc31;
                            }
                            gm_l12 = t70;
                        }
                        {
                            void * t73 = gm_l0;
                            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
                            void * t75 = gm_l0;
                            uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)t75) + 16));
                            void * t77 = gm_l4;
                            uint8_t t78 = gm_l5;
                            uint64_t t79 = gm_l9;
                            gm_t88rcucg1b7uk t80 = gm_l11;
                            uint8_t t81 = gm_f_grows_directly_81uv5ob(t74, t76, t77, t78, t79, GM_CONV(gm_csguankko4lmhd, t80));
                            if (!t81) goto ie72;
                            {
                                void * t83 = gm_l0;
                                gm_b16_8 t84 = gm_l12;
                                gm_tl874t2atnlh3 t85 = gm_l3;
                                gm_f_mark_grown_9b0hva9(t83, t84, GM_CONV(gm_cseahvg8v7p0vs, t85));
                            }
                            goto ix72;
ie72:;
ix72:;
                        }
                        {
                            uint8_t t87 = 0;
                            uint8_t t88 = gm_l5;
                            if (!t88) {
                                uint64_t t89 = gm_l6;
                                void * t90 = gm_l0;
                                uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)t90) + 24) + 8);
                                uint8_t t92 = (t89 >= t91);
                                t87 = t92;
                            } else t87 = 1;
                            if (!t87) goto ie86;
                            {
                                goto lc31;
                            }
                            goto ix86;
ie86:;
ix86:;
                        }
                        {
                            {
                                uint64_t t95 = gm_l6;
                                uint64_t t96 = (uintptr_t)t95;
                                void * t97 = gm_l0;
                                uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)t97) + 24) + 8);
                                if (t96 >= (uintptr_t)(t98)) gm_trap_index(t96, t98, gm_srcfk7rg8t5apoh, 321, 18);
                                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t97) + 24)) + t96 * 40) + 0));
                                gm_l13 = t99;
                            }
                            {
                                gm_l14 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t101 = gm_l14;
                                    uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    uint8_t t103 = (t101 < t102);
                                    if (!t103) goto lb100;
                                    {
                                        uint64_t t106 = gm_l14;
                                        uint64_t t107 = (uintptr_t)t106;
                                        uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                        gm_tf1bjg81u0glv t111 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t107 * 24));
                                        gm_l15 = t111;
                                        {
                                            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                                            uint64_t t115 = gm_l9;
                                            uint8_t t116 = ((t114 == t115));
                                            if (!t116) goto ie113;
                                            {
                                                void * t118 = gm_l0;
                                                gm_t6j6smj7qoctu t119;
                                                gm_b16_8 t120 = gm_l12;
                                                GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t120);
                                                gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                                GM_ST(gm_b16_8, (uint8_t *)&t119 + 16, t121);
                                                gm_tabb710d9mq2e t122;
                                                gm_f_format_8n626pi((void *)&t122, (void *)NULL, cx, NULL, &t119);
                                                if (gm_unw.kind) {
                                                    goto gm_ll104;
                                                }
                                                if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 8, gm_srcfk7rg8t5apoh, 322, 45);
                                                gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
                                                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                                                gm_tl874t2atnlh3 t125 = gm_l3;
                                                gm_f_mark_grown_9b0hva9(t118, t124, GM_CONV(gm_cseahvg8v7p0vs, t125));
                                            }
                                            goto ix113;
ie113:;
ix113:;
                                        }
                                        goto me109;
mn110:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 321, 9);
me109:;
                                    }
lc100:;
                                    {
                                        uint64_t t127 = gm_l14;
                                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                                        uint64_t t130 = r128;
                                        gm_l14 = t130;
                                    }
                                }
gm_ll104:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb100; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc100; }
                                }
                                goto gm_ll35;
lb100:;
                            }
                        }
                        goto ix48;
ie48:;
                        {
                            goto lc31;
                        }
ix48:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 314, 5);
me38:;
                }
lc31:;
                {
                    uint64_t t133 = gm_l8;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                    uint64_t t136 = r134;
                    gm_l8 = t136;
                }
            }
gm_ll35:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
            }
            goto gm_lad;
lb31:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_grows_buslmvn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grows_buslmvn F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tg1t0c5hkovn5 t2;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s616b6iju29ec));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_segmk88bjc49r));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t4);
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sv6b1v96egn1q));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(6));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t5);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sqp9p8tc2dil3));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t6);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s8vgs1ersvo58));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 64, t7);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sm7hp6cgh4jle));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 80, t8);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_slu92ianijdhp));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(6));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 96, t9);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sub1g4k5l4c07));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(10));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 112, t10);
    uint8_t t11 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 0, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 16, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 32, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 48, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 64, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 80, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 96, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 112, ((uint8_t *)&t1))) t11 = 1;
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_grows_directly_81uv5ob(void * a0, uint32_t a1, void * a2, uint8_t a3, uint64_t a4, gm_csguankko4lmhd a5) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grows_directly_81uv5ob F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* list_adt */
    void * gm_l2; /* sig */
    uint8_t gm_l3; /* core */
    uint64_t gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    GM_STACK_CHECK_SHALLOW(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = GM_CONV(gm_t88rcucg1b7uk, a5);
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l5;
        uint8_t t6 = gm_f_list_type_bled6ne(t3, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t10 = 0;
        uint8_t t11 = gm_l3;
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) {
            uint8_t t13 = 0;
            uint64_t t14 = gm_l4;
            uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
            if (t15) {
                void * t16 = gm_l2;
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 0));
                uint8_t t18 = gm_f_grows_buslmvn(t17);
                t13 = t18;
            } else t13 = 0;
            t10 = t13;
        } else t10 = 1;
        uint8_t t19 = 0;
        F.ret = t10;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_growth_summaries_dguakce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_growth_summaries_dguakce F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint32_t gm_l4; /* id */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* body */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* f */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* k */
    gm_t2lqu8jsj511d gm_l24; /* w */
    gm_tf1bjg81u0glv gm_l25; /* x */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint32_t gm_l28; /* ei */
    gm_b16_8 gm_l30; /* p */
    GM_STACK_CHECK(&F, 1616u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_mj5a0ol((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 344) + 0) + 8);
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
                    {
                        void * t15 = (void *)((uint8_t *)&F.l1);
                        gm_trqfvmjtt4sij t16;
                        gm_f_list_5cp093t((void *)&t16, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t17 = gm_f_push_0jed0lq(t15, &t16);
                        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcfk7rg8t5apoh, 45, 35);
                        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 45, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t20 = gm_l3;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    uint64_t t23 = r21;
                    gm_l3 = t23;
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
    {
        void * t24 = gm_l0;
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s0a1978iad5c9));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
        gm_t9805qtonbsag t26 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t27;
        if (((((uint8_t *)&t26))[4] != 0)) {
            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&t26) + 0));
            t27 = t28;
        } else {
            memmove(gm_out, ((uint8_t *)&F.l1), 40);
            goto gm_fdone;
        }
        gm_l4 = t27;
    }
    {
        gm_f_list_tv7nkel((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 384) + 0) + 8);
            gm_l6 = t31;
        }
        {
            for (;;) {
                uint64_t t33 = gm_l7;
                uint64_t t34 = gm_l6;
                uint8_t t35 = (t33 < t34);
                if (!t35) goto lb32;
                {
                    uint64_t t38 = gm_l7;
                    uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&t38));
                    gm_l8 = t41;
                    {
                        uint64_t t44 = gm_l8;
                        uint64_t t45 = (uintptr_t)t44;
                        void * t46 = gm_l0;
                        uint64_t t47 = GM_LD(uintptr_t, ((((uint8_t *)t46) + 344) + 0) + 8);
                        if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_srcfk7rg8t5apoh, 49, 12);
                        uint8_t t48;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t46) + 344) + 0)) + t45 * 136) + 96))[24] != 0)) goto mn50;
                        t48 = ((uint8_t)0);
                        goto me49;
mn50:;
                        t48 = ((uint8_t)1);
                        goto me49;
mn51:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 49, 12);
me49:;
                        if (!t48) goto ie43;
                        {
                            goto lc32;
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    {
                        uint64_t t53 = gm_l8;
                        uint64_t t54 = (uintptr_t)t53;
                        void * t55 = gm_l0;
                        uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 384) + 0) + 8);
                        if (t54 >= (uintptr_t)(t56)) gm_trap_index(t54, t56, gm_srcfk7rg8t5apoh, 50, 17);
                        void * t57 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 384) + 0)) + t54 * 64) + 40));
                        void * t58;
                        if ((GM_LD(void *, ((uint8_t *)&t57)) != 0)) {
                            void * t59 = GM_LD(void *, (((uint8_t *)&t57) + 0));
                            t58 = t59;
                        } else {
                            goto lc32;
                        }
                        gm_l9 = t58;
                    }
                    {
                        void * t60 = gm_l0;
                        uint32_t t61 = gm_l4;
                        uint64_t t62 = gm_l8;
                        void * t63 = gm_l9;
                        void * t64 = (void *)((uint8_t *)&F.l1);
                        void * t65 = (void *)((uint8_t *)&F.l5);
                        gm_b0_1 t66 = gm_f_collect_calls_f2qse9f((void *)NULL, cx, NULL, t60, t61, t62, t63, t64, t65);
                        if (gm_unw.kind) {
                            goto gm_ll36;
                        }
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 48, 5);
me39:;
                }
lc32:;
                {
                    uint64_t t68 = gm_l7;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l7 = t71;
                }
            }
gm_ll36:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
            }
            goto gm_lad;
lb32:;
        }
    }
    {
        gm_f_list_urufktd((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            void * t73 = gm_l0;
            uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 344) + 0) + 8);
            gm_l11 = t74;
        }
        {
            for (;;) {
                uint64_t t76 = gm_l12;
                uint64_t t77 = gm_l11;
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    uint64_t t81 = gm_l12;
                    {
                        void * t85 = (void *)((uint8_t *)&F.l10);
                        gm_trqfvmjtt4sij t86;
                        gm_f_list_go2qg0b((void *)&t86, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll79;
                        }
                        gm_tftb1odc739t5 t87 = gm_f_push_u46i39f(t85, &t86);
                        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcfk7rg8t5apoh, 56, 35);
                        gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                    }
                    goto me82;
mn83:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 56, 5);
me82:;
                }
lc75:;
                {
                    uint64_t t90 = gm_l12;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l12 = t93;
                }
            }
gm_ll79:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
            }
            goto gm_lad;
lb75:;
        }
    }
    {
        {
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_l13 = t96;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t98 = gm_l14;
                uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t100 = (t98 < t99);
                if (!t100) goto lb97;
                {
                    gm_te7ftefsqvm53 t103;
                    uint64_t t104 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t103 + 0, t104);
                    uint64_t t105 = gm_l14;
                    uint64_t t106 = (uintptr_t)t105;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    memcpy((uint8_t *)&t103 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t106 * 48), 48);
                    uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&t103) + 0));
                    gm_l15 = t110;
                    F.l16 = (*(gm_t90jeqj7rr485 *)(((uint8_t *)&t103) + 8));
                    {
                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 32));
                        uint64_t t113 = (uintptr_t)t112;
                        uint64_t t114 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                        if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srcfk7rg8t5apoh, 57, 42);
                        void * t115 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t113 * 40);
                        uint64_t t116 = gm_l15;
                        uint32_t t117 = (uint32_t)t116;
                        gm_tftb1odc739t5 t118 = gm_f_push_im9d4ro(t115, t117);
                        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srcfk7rg8t5apoh, 57, 42);
                        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                    }
                    goto me108;
mn109:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 57, 5);
me108:;
                }
lc97:;
                {
                    uint64_t t121 = gm_l14;
                    uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                    uint64_t t124 = r122;
                    gm_l14 = t124;
                }
            }
lb97:;
        }
    }
    {
        gm_f_list_pjtmjmi((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            uint64_t t126 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
            gm_l18 = t126;
        }
        {
            for (;;) {
                uint64_t t128 = gm_l19;
                uint64_t t129 = gm_l18;
                uint8_t t130 = (t128 < t129);
                if (!t130) goto lb127;
                {
                    uint64_t t133 = gm_l19;
                    uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)&t133));
                    gm_l20 = t136;
                    {
                        {
                            gm_l22 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t139 = gm_l20;
                            uint64_t t140 = (uintptr_t)t139;
                            uint64_t t141 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                            if (t140 >= (uintptr_t)(t141)) gm_trap_index(t140, t141, gm_srcfk7rg8t5apoh, 60, 21);
                            uint64_t t142 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t140 * 40) + 0) + 8);
                            gm_l21 = t142;
                        }
                        {
                            for (;;) {
                                uint64_t t144 = gm_l22;
                                uint64_t t145 = gm_l21;
                                uint8_t t146 = (t144 < t145);
                                if (!t146) goto lb143;
                                {
                                    uint64_t t149 = gm_l22;
                                    uint64_t t152 = GM_LD(uint64_t, ((uint8_t *)&t149));
                                    gm_l23 = t152;
                                    {
                                        void * t154 = (void *)((uint8_t *)&F.l17);
                                        uint64_t t155 = gm_l20;
                                        uint64_t t156 = gm_l23;
                                        gm_t2lqu8jsj511d t157;
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 0), t155);
                                        GM_ST(uint64_t, ((uint8_t *)&t157 + 8), t156);
                                        gm_tftb1odc739t5 t158 = gm_f_push_1tq7lp9(t154, t157);
                                        if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_srcfk7rg8t5apoh, 60, 47);
                                        gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                                    }
                                    goto me150;
mn151:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 60, 9);
me150:;
                                }
lc143:;
                                {
                                    uint64_t t161 = gm_l22;
                                    uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                                    uint64_t t164 = r162;
                                    gm_l22 = t164;
                                }
                            }
lb143:;
                        }
                    }
                    goto me134;
mn135:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 59, 5);
me134:;
                }
lc127:;
                {
                    uint64_t t166 = gm_l19;
                    uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                    uint64_t t169 = r167;
                    gm_l19 = t169;
                }
            }
lb127:;
        }
    }
    {
        for (;;) {
            void * t171 = (void *)((uint8_t *)&F.l17);
            gm_tc70a9dbbmgkg t172;
            gm_f_pop_fe4ef2u((void *)&t172, t171);
            if (!((((uint8_t *)&t172))[16] != 0)) goto lb170;
            gm_t2lqu8jsj511d t173 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t172));
            gm_l24 = t173;
            {
                uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                uint64_t t177 = (uintptr_t)t176;
                uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 0));
                uint64_t t179 = (uintptr_t)t178;
                uint64_t t180 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                if (t179 >= (uintptr_t)(t180)) gm_trap_index(t179, t180, gm_srcfk7rg8t5apoh, 63, 14);
                uint64_t t181 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t179 * 40) + 0) + 8);
                if (t177 >= (uintptr_t)(t181)) gm_trap_index(t177, t181, gm_srcfk7rg8t5apoh, 63, 14);
                gm_tf1bjg81u0glv t182 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t179 * 40) + 0)) + t177 * 24));
                gm_l25 = t182;
            }
            {
                {
                    uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 0));
                    uint64_t t185 = (uintptr_t)t184;
                    uint64_t t186 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                    if (t185 >= (uintptr_t)(t186)) gm_trap_index(t185, t186, gm_srcfk7rg8t5apoh, 64, 19);
                    gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t185 * 40) + 0));
                    gm_l26 = t187;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t189 = gm_l27;
                        uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint8_t t191 = (t189 < t190);
                        if (!t191) goto lb188;
                        {
                            uint64_t t194 = gm_l27;
                            uint64_t t195 = (uintptr_t)t194;
                            uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                            uint32_t t197 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t195 * 4));
                            uint32_t t200 = GM_LD(uint32_t, ((uint8_t *)&t197));
                            gm_l28 = t200;
                            {
                                uint32_t t202 = gm_l28;
                                uint64_t t203 = (uint64_t)t202;
                                uint64_t t204 = (uintptr_t)t203;
                                uint64_t t205 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_srcfk7rg8t5apoh, 65, 18);
                                gm_t90jeqj7rr485 t206 = GM_LD(gm_t90jeqj7rr485, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t204 * 48));
                                F.l29 = t206;
                            }
                            {
                                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 40));
                                uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 0));
                                uint8_t t210 = (!(t208 == t209));
                                if (!t210) goto ie207;
                                {
                                    goto lc188;
                                }
                                goto ix207;
ie207:;
ix207:;
                            }
                            {
                                gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 16));
                                gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                                gm_th2lqbo77pvsc t214;
                                gm_f_join_paths_mf95qfu((void *)&t214, (void *)NULL, cx, NULL, t212, t213);
                                if (gm_unw.kind) {
                                    goto gm_ll192;
                                }
                                gm_b16_8 t215;
                                if (((((uint8_t *)&t214))[16] != 0)) {
                                    gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 0));
                                    t215 = t216;
                                } else {
                                    goto lc188;
                                }
                                gm_l30 = t215;
                            }
                            {
                                uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 0));
                                uint64_t t219 = (uintptr_t)t218;
                                uint64_t t220 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                                if (t219 >= (uintptr_t)(t220)) gm_trap_index(t219, t220, gm_srcfk7rg8t5apoh, 68, 28);
                                void * t221 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t219 * 40);
                                uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 8));
                                gm_b16_8 t223 = gm_l30;
                                uint8_t t224 = gm_f_add_growth_b20v4f1((void *)NULL, cx, NULL, t221, t222, t223);
                                if (gm_unw.kind) {
                                    goto gm_ll192;
                                }
                                if (!t224) goto ie217;
                                {
                                    void * t226 = (void *)((uint8_t *)&F.l17);
                                    uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 0));
                                    uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 0));
                                    uint64_t t229 = (uintptr_t)t228;
                                    uint64_t t230 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                                    if (t229 >= (uintptr_t)(t230)) gm_trap_index(t229, t230, gm_srcfk7rg8t5apoh, 69, 44);
                                    uint64_t t231 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t229 * 40) + 0) + 8);
                                    uint64_t r232; bool o233 = __builtin_sub_overflow(t231, ((uint64_t)1ULL), &r232);
                                    if (o233) gm_trap("integer overflow in `-`", gm_srcfk7rg8t5apoh, 69, 44);
                                    uint64_t t234 = r232;
                                    gm_t2lqu8jsj511d t235;
                                    GM_ST(uint64_t, ((uint8_t *)&t235 + 0), t227);
                                    GM_ST(uint64_t, ((uint8_t *)&t235 + 8), t234);
                                    gm_tftb1odc739t5 t236 = gm_f_push_1tq7lp9(t226, t235);
                                    if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 1, gm_srcfk7rg8t5apoh, 69, 17);
                                    gm_b0_1 t237 = GM_LD(gm_b0_1, (((uint8_t *)&t236) + 1));
                                }
                                goto ix217;
ie217:;
ix217:;
                            }
                            goto me198;
mn199:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 64, 9);
me198:;
                        }
lc188:;
                        {
                            uint64_t t239 = gm_l27;
                            uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                            uint64_t t242 = r240;
                            gm_l27 = t242;
                        }
                    }
gm_ll192:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb188; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc188; }
                    }
                    goto gm_ll174;
lb188:;
                }
            }
lc170:;
        }
gm_ll174:;
        if (gm_unw.frame == &F.h && gm_unw.label == 6) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb170; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc170; }
        }
        goto gm_lad;
lb170:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_list_m0i72n0(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_items_of_h89og2m(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_items_of_h89og2m F;
    void * gm_l0; /* sc */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    void * gm_l3; /* base */
    uint32_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3 = gm_f_strip_coerce_vk5l7o4(t2);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l2;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t6 != ((uint8_t)19ULL)) goto ie4;
        void * t7 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l3 = t7;
        uint32_t t8 = GM_LD(uint32_t, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l4 = t8;
        {
            uint32_t t11 = gm_l4;
            uint8_t t12 = ((t11 == ((uint32_t)0ULL)));
            if (!t12) goto ie10;
            void * t13 = gm_l0;
            void * t14 = gm_l3;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
            uint8_t t16 = gm_f_is_list_m0i72n0(t13, GM_CONV(gm_csguankko4lmhd, t15));
            if (!t16) goto ie10;
            {
                void * t18 = gm_l3;
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t20 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_join_paths_mf95qfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_paths_mf95qfu F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint64_t t5 = gm_f_count_9fkti0l(t3, t4);
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        uint64_t t8 = gm_f_count_9fkti0l(t6, t7);
        uint64_t r9; bool o10 = __builtin_add_overflow(t5, t8, &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_srcfk7rg8t5apoh, 78, 8);
        uint64_t t11 = r9;
        uint8_t t12 = (t11 > ((uint64_t)4ULL));
        if (!t12) goto ie2;
        {
            gm_th2lqbo77pvsc t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = ((t17 == ((uint64_t)0ULL)));
        if (!t18) goto ie16;
        {
            gm_b16_8 t20 = gm_l0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t20);
            goto gm_fdone;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t23 = ((t22 == ((uint64_t)0ULL)));
        if (!t23) goto ie21;
        {
            gm_b16_8 t25 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_t6j6smj7qoctu t26;
        gm_b16_8 t27 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
        gm_b16_8 t28 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t28);
        gm_tabb710d9mq2e t29;
        gm_f_format_8n626pi((void *)&t29, (void *)NULL, cx, NULL, &t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srcfk7rg8t5apoh, 81, 12);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t31);
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
inline __attribute__((aligned(4))) void gm_f_join_tm3jl4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_join_tm3jl4b F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        if (!((((uint8_t *)&F.l0))[40] != 0)) goto ie2;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l0));
        F.l2 = t3;
        {
            gm_trqfvmjtt4sij t5;
            gm_f_union_live_empebln((void *)&t5, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)a1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
            ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
            GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t5);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        const uint8_t *t6 = ((uint8_t *)a1);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t6, 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_list_type_bled6ne(void * a0, uint32_t a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) void gm_f_mark_grown_9b0hva9(void * a0, gm_b16_8 a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_grown_9b0hva9 F;
    void * gm_l0; /* sc */
    gm_b16_8 gm_l1; /* k */
    gm_tl874t2atnlh3 gm_l2; /* at */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 40) + 0) + 8);
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
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 40) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcfk7rg8t5apoh, 330, 12);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 40) + 0)) + t18 * 56) + 8));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie16;
                        uint64_t t24 = gm_l5;
                        uint64_t t25 = (uintptr_t)t24;
                        void * t26 = gm_l0;
                        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 40) + 0) + 8);
                        if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcfk7rg8t5apoh, 330, 42);
                        uint8_t t28;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 40) + 0)) + t25 * 56) + 36))[12] != 0)) goto mn30;
                        t28 = ((uint8_t)1);
                        goto me29;
mn30:;
                        t28 = ((uint8_t)0);
                        goto me29;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 330, 42);
me29:;
                        if (!t28) goto ie16;
                        {
                            uint64_t t33 = gm_l5;
                            uint64_t t34 = (uintptr_t)t33;
                            void * t35 = gm_l0;
                            uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 40) + 0) + 8);
                            if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcfk7rg8t5apoh, 330, 75);
                            gm_tl874t2atnlh3 t37 = gm_l2;
                            gm_toceffb697p0u t38;
                            GM_ST(uint8_t, (uint8_t *)&t38 + 13, 0);
                            GM_ST(uint16_t, (uint8_t *)&t38 + 14, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t38), t37);
                            ((uint8_t *)&t38)[12] = 1;
                            GM_ST(gm_toceffb697p0u, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 40) + 0)) + t34 * 56) + 36), t38);
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 329, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
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
__attribute__((aligned(4))) void gm_f_param_path_tcmbtr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_param_path_tcmbtr8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* k */
    void * gm_l3; /* def */
    void * gm_l4; /* sig */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* j */
    uint32_t gm_l8; /* l */
    uint8_t gm_l9; /* is_const */
    gm_b16_8 gm_l10; /* root */
    GM_STACK_CHECK(&F, 826u);
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
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 384) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcfk7rg8t5apoh, 135, 13);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 384) + 0)) + t3 * 64);
        gm_l3 = t6;
    }
    {
        uint64_t t7 = gm_l1;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 344) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcfk7rg8t5apoh, 136, 13);
        void * t11 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 344) + 0)) + t8 * 136);
        gm_l4 = t11;
    }
    {
        {
            void * t13 = gm_l3;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 8));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_l5 = t15;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l6;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l6;
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 4));
                    gm_tn68el8d14vku t27;
                    GM_ST(uint32_t, (uint8_t *)&t27 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t22);
                    GM_ST(uint32_t, ((uint8_t *)&t27 + 8), t26);
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t27) + 0));
                    gm_l7 = t30;
                    uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&t27) + 8));
                    gm_l8 = t31;
                    {
                        uint64_t t34 = gm_l7;
                        void * t35 = gm_l4;
                        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 40) + 8);
                        uint8_t t37 = (t34 >= t36);
                        if (!t37) goto ie33;
                        {
                            goto lb16;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    {
                        void * t40 = gm_l0;
                        void * t41 = (void *)(((uint8_t *)t40) + 8);
                        uint64_t t42 = gm_l7;
                        uint64_t t43 = (uintptr_t)t42;
                        void * t44 = gm_l4;
                        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 40) + 8);
                        if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_srcfk7rg8t5apoh, 139, 41);
                        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t44) + 40)) + t43 * 56) + 16));
                        gm_t53at80pfdus3 t47;
                        gm_f_kind_jcnt4hm((void *)&t47, t41, GM_CONV(gm_csguankko4lmhd, t46));
                        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
                        if (t48 != ((uint8_t)8ULL)) goto ie39;
                        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)&t47) + 8) + 0));
                        gm_l9 = t49;
                        {
                            uint8_t t52 = gm_l9;
                            if (!t52) goto ie51;
                            {
                                goto lc16;
                            }
                            goto ix51;
ie51:;
ix51:;
                        }
                        {
                            uint32_t t54 = gm_l8;
                            gm_t88rcucg1b7uk t55;
                            GM_ST(uint32_t, ((uint8_t *)&t55 + 0), t54);
                            gm_tabb710d9mq2e t56;
                            gm_f_format_32n178h((void *)&t56, (void *)NULL, cx, NULL, t55);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcfk7rg8t5apoh, 141, 17);
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                            gm_l10 = t57;
                        }
                        {
                            gm_b16_8 t59 = gm_l2;
                            gm_b16_8 t60 = gm_l10;
                            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                            uint8_t t62 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t61)));
                            if (!t62) goto ie58;
                            {
                                gm_tf1bjg81u0glv t64;
                                uint64_t t65 = gm_l7;
                                GM_ST(uint64_t, (uint8_t *)&t64 + 0, t65);
                                gm_b16_8 t66;
                                GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(0));
                                GM_ST(gm_b16_8, (uint8_t *)&t64 + 8, t66);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 28, 0);
                                ((uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out))[24] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 0, t64);
                                goto gm_fdone;
                            }
                            goto ix58;
ie58:;
ix58:;
                        }
                        {
                            gm_b16_8 t68 = gm_l2;
                            gm_b16_8 t69 = gm_l10;
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                            uint8_t t71 = gm_f_starts_with_6vvt01e(t68, t70);
                            if (!t71) goto ie67;
                            uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            void * t73 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                            uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            uint64_t t75 = (uintptr_t)t74;
                            if (t75 > (uintptr_t)t72 || (uintptr_t)t72 > (uintptr_t)t72) gm_trap_slice(t75, (uintptr_t)t72, t72, gm_srcfk7rg8t5apoh, 143, 36);
                            gm_b16_8 t76;
                            GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t73 + t75 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)((uintptr_t)t72 - t75));
                            gm_b16_8 t77;
                            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sl3q52ep6aa53));
                            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
                            uint8_t t78 = gm_f_starts_with_6vvt01e(t76, t77);
                            if (!t78) goto ie67;
                            {
                                gm_tf1bjg81u0glv t80;
                                uint64_t t81 = gm_l7;
                                GM_ST(uint64_t, (uint8_t *)&t80 + 0, t81);
                                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                void * t83 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint64_t t85 = (uintptr_t)t84;
                                if (t85 > (uintptr_t)t82 || (uintptr_t)t82 > (uintptr_t)t82) gm_trap_slice(t85, (uintptr_t)t82, t82, gm_srcfk7rg8t5apoh, 143, 80);
                                gm_b16_8 t86;
                                GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t83 + t85 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)((uintptr_t)t82 - t85));
                                GM_ST(gm_b16_8, (uint8_t *)&t80 + 8, t86);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 28, 0);
                                ((uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out))[24] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_t8sds1gdern44 *)gm_out) + 0, t80);
                                goto gm_fdone;
                            }
                            goto ix67;
ie67:;
ix67:;
                        }
                        goto ix39;
ie39:;
                        {
                            goto lc16;
                        }
ix39:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 137, 5);
me28:;
                }
lc16:;
                {
                    uint64_t t89 = gm_l6;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l6 = t92;
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
        gm_t8sds1gdern44 t93;
        memset(&t93, 0, sizeof t93);
        uint8_t t94 = 0;
        F.ret = t93;
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
inline __attribute__((aligned(4))) void gm_f_place_key_8hg61p4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_place_key_8hg61p4 F;
    void * gm_l0; /* e */
    void * gm_l1; /* x */
    uint32_t gm_l2; /* l */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* inner */
    void * gm_l5; /* base */
    uint32_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* k */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_f_strip_coerce_vk5l7o4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; default: goto ma10; }
ma6:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t14 != ((uint8_t)15ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l2 = t15;
        uint32_t t16 = gm_l2;
        gm_t88rcucg1b7uk t17;
        GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t16);
        gm_tabb710d9mq2e t18;
        gm_f_format_32n178h((void *)&t18, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcfk7rg8t5apoh, 214, 29);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t20);
        goto gm_fdone;
        goto me5;
mn13:;
ma7:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t22 != ((uint8_t)17ULL)) goto mn21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l3 = t23;
        gm_b16_8 t24 = gm_l3;
        gm_tnpifv20irnph t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
        gm_tabb710d9mq2e t26;
        gm_f_format_fi775k0((void *)&t26, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcfk7rg8t5apoh, 215, 36);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t28);
        goto gm_fdone;
        goto me5;
mn21:;
ma8:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t30 != ((uint8_t)18ULL)) goto mn29;
        void * t31 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l4 = t31;
        void * t32 = gm_l4;
        gm_f_place_key_8hg61p4((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn29:;
ma9:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t34 != ((uint8_t)19ULL)) goto mn33;
        void * t35 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l5 = t35;
        uint32_t t36 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l6 = t36;
        {
            void * t38 = gm_l5;
            gm_th2lqbo77pvsc t39;
            gm_f_place_key_8hg61p4((void *)&t39, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t40;
            if (((((uint8_t *)&t39))[16] != 0)) {
                gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 0));
                t40 = t41;
            } else {
                gm_th2lqbo77pvsc t42;
                memset(&t42, 0, sizeof t42);
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            gm_l7 = t40;
        }
        {
            gm_tommf4j9upomc t44;
            memset((uint8_t *)&t44 + 20, 0, 4);
            gm_b16_8 t45 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
            uint32_t t46 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t44 + 16, t46);
            gm_tabb710d9mq2e t47;
            gm_f_format_ep82di1((void *)&t47, (void *)NULL, cx, NULL, &t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srcfk7rg8t5apoh, 219, 20);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t49);
            goto gm_fdone;
        }
        goto me5;
mn33:;
ma10:;
        gm_th2lqbo77pvsc t51;
        memset(&t51, 0, sizeof t51);
        uint8_t t52 = 0;
        F.ret = t51;
        goto gm_fret;
        goto me5;
mn50:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 213, 5);
me5:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_3flkb8v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_scan_3flkb8v F;
    void * gm_l1; /* e */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* b */
    gm_trl2khmuphncd gm_l4; /* x */
    void * gm_l8; /* el */
    void * gm_l11; /* scrutinee */
    gm_b16_8 gm_l12; /* arms */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l17; /* a */
    void * gm_l18; /* g */
    gm_t3dcoc7077t1q gm_l21; /* x */
    void * gm_l24; /* lhs */
    void * gm_l25; /* rhs */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* pass */
    void * gm_l32; /* st */
    gm_b16_8 gm_l34; /* args */
    uint32_t gm_l35; /* g */
    GM_STACK_CHECK(&F, 2032u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma12; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma12; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma12; default: goto ma12; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)15ULL)) goto mn15;
        uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t17;
        void * t18 = F.l0;
        uint32_t t19 = gm_l2;
        void * t20 = gm_l1;
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
        gm_b0_1 t22 = gm_f_touch_aqpeu0m((void *)NULL, cx, NULL, t18, t19, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me3;
mn15:;
ma5:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)46ULL)) goto mn24;
        void * t26 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t26;
        void * t27 = F.l0;
        void * t28 = gm_l3;
        gm_b0_1 t29 = gm_f_scan_block_cb34lg7((void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
        goto me3;
mn24:;
ma6:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t32 != ((uint8_t)47ULL)) goto mn31;
        gm_trl2khmuphncd t33 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t33;
        {
            void * t35 = F.l0;
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_b0_1 t37 = gm_f_scan_conds_nodljh6((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t38 = F.l0;
            gm_f_clone_live_rrk5v52((void *)&F.l5, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t38) + 40)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tqetrgo0q24b5 t39;
            memset(&t39, 0, sizeof t39);
            F.l6 = t39;
        }
        {
            void * t41 = F.l0;
            void * t42 = GM_LD(void *, (((uint8_t *)&gm_l4) + 16));
            gm_tqetrgo0q24b5 t43;
            gm_f_branch_rbkqmlt((void *)&t43, (void *)NULL, cx, NULL, t41, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)), t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t43))[40] != 0)) goto ie40;
            F.l7 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t43) + 0));
            {
                gm_f_join_tm3jl4b((void *)&(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l6)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            void * t46 = GM_LD(void *, (((uint8_t *)&gm_l4) + 24));
            if (!(GM_LD(void *, ((uint8_t *)&t46)) != 0)) goto ie45;
            void * t47 = GM_LD(void *, ((uint8_t *)&t46));
            gm_l8 = t47;
            {
                void * t50 = F.l0;
                void * t51 = gm_l8;
                gm_tqetrgo0q24b5 t52;
                gm_f_branch_rbkqmlt((void *)&t52, (void *)NULL, cx, NULL, t50, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)), t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t52))[40] != 0)) goto ie49;
                F.l9 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t52) + 0));
                {
                    gm_f_join_tm3jl4b((void *)&(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l6)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l9)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix49;
ie49:;
ix49:;
            }
            goto ix45;
ie45:;
            {
                gm_f_join_tm3jl4b((void *)&(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l6)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix45:;
        }
        {
            void * t55 = F.l0;
            uint8_t *t56 = (((uint8_t *)t55) + 40);
            gm_trqfvmjtt4sij t57;
            if (!((((uint8_t *)&F.l6))[40] != 0)) goto ie58;
            gm_trqfvmjtt4sij t59 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l6));
            F.l10 = t59;
            gm_trqfvmjtt4sij t61 = F.l10;
            gm_trqfvmjtt4sij t62 = t61;
            t57 = t62;
            goto ix58;
ie58:;
            gm_trqfvmjtt4sij t64 = F.l5;
            gm_trqfvmjtt4sij t65 = t64;
            t57 = t65;
ix58:;
            GM_ST(gm_trqfvmjtt4sij, t56, t57);
        }
        {
            gm_b0_1 t66;
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto me3;
mn31:;
ma7:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)44ULL)) goto mn68;
        void * t70 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l11 = t70;
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l12 = t71;
        {
            void * t73 = F.l0;
            void * t74 = gm_l11;
            gm_b0_1 t75 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t76 = F.l0;
            gm_f_clone_live_rrk5v52((void *)&F.l13, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t76) + 40)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tqetrgo0q24b5 t77;
            memset(&t77, 0, sizeof t77);
            F.l14 = t77;
        }
        {
            {
                gm_b16_8 t79 = gm_l12;
                gm_l15 = t79;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t81 = gm_l16;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t83 = (t81 < t82);
                    if (!t83) goto lb80;
                    {
                        uint64_t t86 = gm_l16;
                        uint64_t t87 = (uintptr_t)t86;
                        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_tbbqefuhv2r8h t91 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t87 * 24));
                        gm_l17 = t91;
                        {
                            void * t93 = F.l0;
                            uint8_t *t94 = (((uint8_t *)t93) + 40);
                            gm_f_clone_live_rrk5v52((void *)&(*(gm_trqfvmjtt4sij *)t94), (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13)));
                            if (gm_unw.kind) {
                                goto gm_ll84;
                            }
                        }
                        {
                            void * t96 = GM_LD(void *, (((uint8_t *)&gm_l17) + 8));
                            if (!(GM_LD(void *, ((uint8_t *)&t96)) != 0)) goto ie95;
                            void * t97 = GM_LD(void *, ((uint8_t *)&t96));
                            gm_l18 = t97;
                            {
                                void * t99 = F.l0;
                                void * t100 = gm_l18;
                                gm_b0_1 t101 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t99, t100);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                            }
                            goto ix95;
ie95:;
ix95:;
                        }
                        {
                            void * t103 = F.l0;
                            void * t104 = F.l0;
                            void * t105 = GM_LD(void *, (((uint8_t *)&gm_l17) + 16));
                            gm_tqetrgo0q24b5 t106;
                            gm_f_branch_rbkqmlt((void *)&t106, (void *)NULL, cx, NULL, t103, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t104) + 40)), t105);
                            if (gm_unw.kind) {
                                goto gm_ll84;
                            }
                            if (!((((uint8_t *)&t106))[40] != 0)) goto ie102;
                            F.l19 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t106) + 0));
                            {
                                gm_f_join_tm3jl4b((void *)&(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l14)), (void *)NULL, cx, NULL, &(*(gm_tqetrgo0q24b5 *)((uint8_t *)&F.l14)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l19)));
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                            }
                            goto ix102;
ie102:;
ix102:;
                        }
                        goto me89;
mn90:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 415, 13);
me89:;
                    }
lc80:;
                    {
                        uint64_t t109 = gm_l16;
                        uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                        uint64_t t112 = r110;
                        gm_l16 = t112;
                    }
                }
gm_ll84:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb80; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc80; }
                }
                goto gm_lad;
lb80:;
            }
        }
        {
            void * t113 = F.l0;
            uint8_t *t114 = (((uint8_t *)t113) + 40);
            gm_trqfvmjtt4sij t115;
            if (!((((uint8_t *)&F.l14))[40] != 0)) goto ie116;
            gm_trqfvmjtt4sij t117 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l14));
            F.l20 = t117;
            gm_trqfvmjtt4sij t119 = F.l20;
            gm_trqfvmjtt4sij t120 = t119;
            t115 = t120;
            goto ix116;
ie116:;
            gm_trqfvmjtt4sij t122 = F.l13;
            gm_trqfvmjtt4sij t123 = t122;
            t115 = t123;
ix116:;
            GM_ST(gm_trqfvmjtt4sij, t114, t115);
        }
        {
            gm_b0_1 t124;
            uint8_t t125 = 0;
            F.ret = t124;
            goto gm_fret;
        }
        goto me3;
mn68:;
ma8:;
        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t127 != ((uint8_t)41ULL)) goto mn126;
        gm_t3dcoc7077t1q t128 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t2) + 8) + 8));
        gm_l21 = t128;
        {
            void * t130 = F.l0;
            void * t131 = GM_LD(void *, (((uint8_t *)&gm_l21) + 0));
            gm_b0_1 t132 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t130, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t133 = F.l0;
            gm_f_clone_live_rrk5v52((void *)&F.l22, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t133) + 40)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t135 = F.l0;
            void * t136 = GM_LD(void *, (((uint8_t *)&gm_l21) + 16));
            gm_tqetrgo0q24b5 t137;
            gm_f_branch_rbkqmlt((void *)&t137, (void *)NULL, cx, NULL, t135, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l22)), t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t137))[40] != 0)) goto ie134;
            F.l23 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t137) + 0));
            {
                void * t139 = F.l0;
                uint8_t *t140 = (((uint8_t *)t139) + 40);
                gm_f_union_live_empebln((void *)&(*(gm_trqfvmjtt4sij *)t140), (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l22)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l23)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix134;
ie134:;
            {
                void * t142 = F.l0;
                gm_trqfvmjtt4sij t143 = F.l22;
                GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t142) + 40), t143);
            }
ix134:;
        }
        {
            gm_b0_1 t144;
            uint8_t t145 = 0;
            F.ret = t144;
            goto gm_fret;
        }
        goto me3;
mn126:;
ma9:;
        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t147 != ((uint8_t)31ULL)) goto mn146;
        void * t148 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l24 = t148;
        void * t149 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l25 = t149;
        {
            void * t151 = F.l0;
            void * t152 = gm_l24;
            gm_b0_1 t153 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t154 = F.l0;
            gm_f_clone_live_rrk5v52((void *)&F.l26, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t154) + 40)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t155 = F.l0;
            void * t156 = gm_l25;
            gm_b0_1 t157 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t158 = F.l0;
            uint8_t *t159 = (((uint8_t *)t158) + 40);
            void * t160 = F.l0;
            gm_f_union_live_empebln((void *)&(*(gm_trqfvmjtt4sij *)t159), (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26)), &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t160) + 40)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t161;
            uint8_t t162 = 0;
            F.ret = t161;
            goto gm_fret;
        }
        goto me3;
mn146:;
ma10:;
        uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t164 != ((uint8_t)48ULL)) goto mn163;
        gm_t710lsrq1m8gk t165 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l27 = t165;
        {
            void * t167 = F.l0;
            gm_f_clone_live_rrk5v52((void *)&F.l28, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t167) + 40)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                gm_l29 = ((uint64_t)2ULL);
            }
            {
                for (;;) {
                    uint64_t t170 = gm_l30;
                    uint64_t t171 = gm_l29;
                    uint8_t t172 = (t170 < t171);
                    if (!t172) goto lb169;
                    {
                        uint64_t t175 = gm_l30;
                        uint64_t t178 = GM_LD(uint64_t, ((uint8_t *)&t175));
                        gm_l31 = t178;
                        {
                            void * t180 = F.l0;
                            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 8));
                            gm_b0_1 t182 = gm_f_scan_conds_nodljh6((void *)NULL, cx, NULL, t180, t181);
                            if (gm_unw.kind) {
                                goto gm_ll173;
                            }
                        }
                        {
                            void * t183 = F.l0;
                            void * t184 = GM_LD(void *, (((uint8_t *)&F.l27) + 24));
                            gm_b0_1 t185 = gm_f_scan_block_cb34lg7((void *)NULL, cx, NULL, t183, t184);
                            if (gm_unw.kind) {
                                goto gm_ll173;
                            }
                        }
                        {
                            void * t187 = GM_LD(void *, (((uint8_t *)&F.l27) + 32));
                            if (!(GM_LD(void *, ((uint8_t *)&t187)) != 0)) goto ie186;
                            void * t188 = GM_LD(void *, ((uint8_t *)&t187));
                            gm_l32 = t188;
                            {
                                void * t190 = F.l0;
                                void * t191 = gm_l32;
                                gm_b0_1 t192 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t190, t191);
                                if (gm_unw.kind) {
                                    goto gm_ll173;
                                }
                            }
                            goto ix186;
ie186:;
ix186:;
                        }
                        {
                            uint64_t t194 = gm_l31;
                            uint8_t t195 = ((t194 == ((uint64_t)0ULL)));
                            if (!t195) goto ie193;
                            {
                                void * t197 = F.l0;
                                uint8_t *t198 = (((uint8_t *)t197) + 40);
                                void * t199 = F.l0;
                                gm_f_union_live_empebln((void *)&(*(gm_trqfvmjtt4sij *)t198), (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t199) + 40)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l28)));
                                if (gm_unw.kind) {
                                    goto gm_ll173;
                                }
                            }
                            goto ix193;
ie193:;
ix193:;
                        }
                        goto me176;
mn177:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 438, 13);
me176:;
                    }
lc169:;
                    {
                        uint64_t t201 = gm_l30;
                        uint64_t r202; bool o203 = __builtin_add_overflow(t201, ((uint64_t)1ULL), &r202);
                        uint64_t t204 = r202;
                        gm_l30 = t204;
                    }
                }
gm_ll173:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb169; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc169; }
                }
                goto gm_lad;
lb169:;
            }
        }
        {
            void * t205 = F.l0;
            uint8_t *t206 = (((uint8_t *)t205) + 40);
            void * t207 = F.l0;
            gm_f_union_live_empebln((void *)&(*(gm_trqfvmjtt4sij *)t206), (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)(((uint8_t *)t207) + 40)), &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l28)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t208;
            uint8_t t209 = 0;
            F.ret = t208;
            goto gm_fret;
        }
        goto me3;
mn163:;
ma11:;
        uint8_t t213 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t213 != ((uint8_t)27ULL)) goto pa212;
        goto po211;
pa212:;
        uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t215 != ((uint8_t)26ULL)) goto pa214;
        goto po211;
pa214:;
        uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t217 != ((uint8_t)28ULL)) goto pa216;
        goto po211;
pa216:;
        goto mn210;
po211:;
        gm_b0_1 t218;
        uint8_t t219 = 0;
        F.ret = t218;
        goto gm_fret;
        goto me3;
mn210:;
ma12:;
        goto me3;
mn220:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 395, 5);
me3:;
    }
    {
        void * t222 = gm_l1;
        gm_closure t223 = ((gm_closure){(void *)gm_f_fn_ggjj7mu, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t222, t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t225 = gm_l1;
        uint8_t t226 = GM_LD(uint8_t, ((((uint8_t *)t225) + 8) + 0));
        if (t226 != ((uint8_t)34ULL)) goto ie224;
        gm_tsth9qu1dv0g3 t227 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t225) + 8) + 8) + 0));
        F.l33 = t227;
        gm_b16_8 t228 = GM_LD(gm_b16_8, (((((uint8_t *)t225) + 8) + 8) + 40));
        gm_l34 = t228;
        {
            uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&F.l33) + 0));
            if (t231 != ((uint8_t)0ULL)) goto ie230;
            uint32_t t232 = GM_LD(uint32_t, (((uint8_t *)&F.l33) + 8));
            gm_l35 = t232;
            {
                void * t234 = F.l0;
                uint32_t t235 = gm_l35;
                gm_b16_8 t236 = gm_l34;
                void * t237 = gm_l1;
                gm_tl874t2atnlh3 t238 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t237) + 72));
                gm_b0_1 t239 = gm_f_grow_by_call_7096mo4((void *)NULL, cx, NULL, t234, t235, t236, t238);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix230;
ie230:;
ix230:;
        }
        goto ix224;
ie224:;
ix224:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_block_cb34lg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_scan_block_cb34lg7 F;
    void * gm_l0; /* sc */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* st */
    uint32_t gm_l5; /* local */
    void * gm_l6; /* init */
    void * gm_l7; /* x */
    gm_b16_8 gm_l8; /* locals */
    void * gm_l9; /* x */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t9805qtonbsag gm_l12; /* l */
    uint32_t gm_l13; /* id */
    void * gm_l14; /* place */
    void * gm_l15; /* value */
    uint32_t gm_l16; /* l */
    gm_b16_8 gm_l17; /* places */
    void * gm_l18; /* value */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    void * gm_l21; /* p */
    void * gm_l22; /* x */
    uint32_t gm_l23; /* l */
    void * gm_l24; /* x */
    void * gm_l25; /* x */
    void * gm_l26; /* t */
    GM_STACK_CHECK(&F, 940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        switch (t26) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma24; default: goto ma25; }
ma19:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t28 != ((uint8_t)0ULL)) goto mn27;
                        uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l5 = t29;
                        void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l6 = t30;
                        void * t32 = gm_l6;
                        if (!(GM_LD(void *, ((uint8_t *)&t32)) != 0)) goto ie31;
                        void * t33 = GM_LD(void *, ((uint8_t *)&t32));
                        gm_l7 = t33;
                        {
                            void * t35 = gm_l0;
                            void * t36 = gm_l7;
                            gm_b0_1 t37 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t38 = gm_l0;
                            uint32_t t39 = gm_l5;
                            void * t40 = gm_l7;
                            gm_b0_1 t41 = gm_f_bind_sh36pqu((void *)NULL, cx, NULL, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix31;
ie31:;
ix31:;
                        goto me18;
mn27:;
ma20:;
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t43 != ((uint8_t)1ULL)) goto mn42;
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l8 = t44;
                        void * t45 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l9 = t45;
                        {
                            void * t47 = gm_l0;
                            void * t48 = gm_l9;
                            gm_b0_1 t49 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t47, t48);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t51 = gm_l8;
                                gm_l10 = t51;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t53 = gm_l11;
                                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t55 = (t53 < t54);
                                    if (!t55) goto lb52;
                                    {
                                        uint64_t t58 = gm_l11;
                                        uint64_t t59 = (uintptr_t)t58;
                                        uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        gm_t9805qtonbsag t63 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t59 * 8));
                                        gm_l12 = t63;
                                        {
                                            if (!((((uint8_t *)&gm_l12))[4] != 0)) goto ie65;
                                            uint32_t t66 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
                                            gm_l13 = t66;
                                            {
                                                void * t68 = gm_l0;
                                                uint32_t t69 = gm_l13;
                                                gm_b0_1 t70 = gm_f_forget_2rgci5c((void *)NULL, cx, NULL, t68, t69);
                                                if (gm_unw.kind) {
                                                    goto gm_ll56;
                                                }
                                            }
                                            goto ix65;
ie65:;
ix65:;
                                        }
                                        goto me61;
mn62:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 367, 17);
me61:;
                                    }
lc52:;
                                    {
                                        uint64_t t72 = gm_l11;
                                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                        uint64_t t75 = r73;
                                        gm_l11 = t75;
                                    }
                                }
gm_ll56:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb52; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc52; }
                                }
                                goto gm_ll9;
lb52:;
                            }
                        }
                        goto me18;
mn42:;
ma21:;
                        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t77 != ((uint8_t)2ULL)) goto mn76;
                        void * t78 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l14 = t78;
                        void * t79 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l15 = t79;
                        {
                            void * t81 = gm_l0;
                            void * t82 = gm_l15;
                            gm_b0_1 t83 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t81, t82);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t85 = gm_l14;
                            uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t85) + 8) + 0));
                            if (t86 != ((uint8_t)15ULL)) goto ie84;
                            uint32_t t87 = GM_LD(uint32_t, ((((uint8_t *)t85) + 8) + 8));
                            gm_l16 = t87;
                            {
                                void * t89 = gm_l0;
                                uint32_t t90 = gm_l16;
                                void * t91 = gm_l15;
                                gm_b0_1 t92 = gm_f_bind_sh36pqu((void *)NULL, cx, NULL, t89, t90, t91);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                            }
                            goto ix84;
ie84:;
                            {
                                void * t94 = gm_l0;
                                void * t95 = gm_l14;
                                gm_b0_1 t96 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t94, t95);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                            }
ix84:;
                        }
                        goto me18;
mn76:;
ma22:;
                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t98 != ((uint8_t)3ULL)) goto mn97;
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l17 = t99;
                        void * t100 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l18 = t100;
                        {
                            void * t102 = gm_l0;
                            void * t103 = gm_l18;
                            gm_b0_1 t104 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t102, t103);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t106 = gm_l17;
                                gm_l19 = t106;
                            }
                            {
                                gm_l20 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t108 = gm_l20;
                                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                    uint8_t t110 = (t108 < t109);
                                    if (!t110) goto lb107;
                                    {
                                        uint64_t t113 = gm_l20;
                                        uint64_t t114 = (uintptr_t)t113;
                                        uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                        void * t116 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t114 * 8));
                                        void * t119 = GM_LD(void *, ((uint8_t *)&t116));
                                        gm_l21 = t119;
                                        {
                                            void * t122 = gm_l21;
                                            if (!(GM_LD(void *, ((uint8_t *)&t122)) != 0)) goto ie121;
                                            void * t123 = GM_LD(void *, ((uint8_t *)&t122));
                                            gm_l22 = t123;
                                            {
                                                void * t126 = gm_l22;
                                                uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t126) + 8) + 0));
                                                if (t127 != ((uint8_t)15ULL)) goto ie125;
                                                uint32_t t128 = GM_LD(uint32_t, ((((uint8_t *)t126) + 8) + 8));
                                                gm_l23 = t128;
                                                {
                                                    void * t130 = gm_l0;
                                                    uint32_t t131 = gm_l23;
                                                    gm_b0_1 t132 = gm_f_forget_2rgci5c((void *)NULL, cx, NULL, t130, t131);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll111;
                                                    }
                                                }
                                                goto ix125;
ie125:;
                                                {
                                                    void * t134 = gm_l0;
                                                    void * t135 = gm_l22;
                                                    gm_b0_1 t136 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t134, t135);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll111;
                                                    }
                                                }
ix125:;
                                            }
                                            goto ix121;
ie121:;
ix121:;
                                        }
                                        goto me117;
mn118:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 381, 17);
me117:;
                                    }
lc107:;
                                    {
                                        uint64_t t138 = gm_l20;
                                        uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                                        uint64_t t141 = r139;
                                        gm_l20 = t141;
                                    }
                                }
gm_ll111:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb107; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc107; }
                                }
                                goto gm_ll9;
lb107:;
                            }
                        }
                        goto me18;
mn97:;
ma23:;
                        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t143 != ((uint8_t)4ULL)) goto mn142;
                        void * t144 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l24 = t144;
                        void * t145 = gm_l0;
                        void * t146 = gm_l24;
                        gm_b0_1 t147 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t145, t146);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn142:;
ma24:;
                        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t149 != ((uint8_t)5ULL)) goto mn148;
                        void * t150 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l25 = t150;
                        void * t151 = gm_l0;
                        void * t152 = gm_l25;
                        gm_b0_1 t153 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t151, t152);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn148:;
ma25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 360, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 359, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t155 = gm_l3;
                    uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                    uint64_t t158 = r156;
                    gm_l3 = t158;
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
    {
        void * t160 = gm_l1;
        void * t161 = GM_LD(void *, (((uint8_t *)t160) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t161)) != 0)) goto ie159;
        void * t162 = GM_LD(void *, ((uint8_t *)&t161));
        gm_l26 = t162;
        {
            void * t164 = gm_l0;
            void * t165 = gm_l26;
            gm_b0_1 t166 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix159;
ie159:;
ix159:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_scan_conds_nodljh6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_scan_conds_nodljh6 F;
    void * gm_l0; /* sc */
    gm_b16_8 gm_l1; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l5 = t20;
                        void * t21 = gm_l0;
                        void * t22 = gm_l5;
                        gm_b0_1 t23 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn18:;
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t25 != ((uint8_t)1ULL)) goto mn24;
                        void * t26 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l6 = t26;
                        void * t27 = gm_l0;
                        void * t28 = gm_l6;
                        gm_b0_1 t29 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t27, t28);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 351, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 350, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
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
inline __attribute__((aligned(4))) void * gm_f_source_of_favl5bk(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_of_favl5bk F;
    void * gm_l0; /* sc */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    void * gm_l3; /* p */
    gm_tao63pk3r18v5 gm_l4; /* sl */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3 = gm_f_strip_coerce_vk5l7o4(t2);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; default: goto ma9; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t13 != ((uint8_t)24ULL)) goto mn12;
        void * t14 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l3 = t14;
        void * t15 = gm_l0;
        void * t16 = gm_l3;
        void * t17 = gm_f_element_of_1c4chqi(t15, t16);
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me5;
mn12:;
ma7:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t20 != ((uint8_t)21ULL)) goto mn19;
        gm_tao63pk3r18v5 t21 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t4) + 8) + 8));
        gm_l4 = t21;
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
        void * t24 = gm_f_items_of_h89og2m(t22, t23);
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
        goto me5;
mn19:;
ma8:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t27 != ((uint8_t)19ULL)) goto mn26;
        void * t28 = gm_l0;
        void * t29 = gm_l2;
        void * t30 = gm_f_items_of_h89og2m(t28, t29);
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
        goto me5;
mn26:;
ma9:;
        uint8_t t33 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me5;
mn32:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 259, 5);
me5:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_stale_element_pointers_jjgn0ll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_stale_element_pointers_jjgn0ll F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* growth */
    void * gm_l3; /* out */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* def */
    void * gm_l6; /* body */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0a1978iad5c9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_t9805qtonbsag t4 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t5;
        if (((((uint8_t *)&t4))[4] != 0)) {
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l4 = t5;
    }
    {
        uint64_t t9 = gm_l1;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 384) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcfk7rg8t5apoh, 175, 13);
        void * t13 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 384) + 0)) + t10 * 64);
        gm_l5 = t13;
    }
    {
        void * t14 = gm_l5;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 40));
        void * t16;
        if ((GM_LD(void *, ((uint8_t *)&t15)) != 0)) {
            void * t17 = GM_LD(void *, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            gm_b0_1 t18;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l6 = t16;
    }
    {
        memset((uint8_t *)&F.l7 + 20, 0, 4);
        void * t20 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l7 + 0, t20);
        void * t21 = gm_l5;
        GM_ST(void *, (uint8_t *)&F.l7 + 8, t21);
        uint32_t t22 = gm_l4;
        GM_ST(uint32_t, (uint8_t *)&F.l7 + 16, t22);
        gm_b16_8 t23 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 24, t23);
        gm_f_list_stv2s0r((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7 + 40)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7s4om10((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7 + 80)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t24 = gm_l3;
        GM_ST(void *, (uint8_t *)&F.l7 + 120, t24);
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l7);
        void * t26 = gm_l6;
        gm_b0_1 t27 = gm_f_scan_3flkb8v((void *)NULL, cx, NULL, t25, t26);
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
extern __attribute__((aligned(4))) void * gm_f_strip_coerce_vk5l7o4(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_touch_aqpeu0m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_touch_aqpeu0m F;
    void * gm_l0; /* sc */
    uint32_t gm_l1; /* l */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tl874t2atnlh3 gm_l6; /* g */
    uint8_t gm_l7; /* seen */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tl874t2atnlh3 gm_l10; /* r */
    GM_STACK_CHECK(&F, 713u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 40) + 0));
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
                    gm_tegs8i1nl3jqh t16 = GM_LD(gm_tegs8i1nl3jqh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 56));
                    F.l5 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            gm_toceffb697p0u t23 = GM_LD(gm_toceffb697p0u, (((uint8_t *)&F.l5) + 36));
                            gm_tl874t2atnlh3 t24;
                            if (((((uint8_t *)&t23))[12] != 0)) {
                                gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t23) + 0));
                                t24 = t25;
                            } else {
                                goto lc5;
                            }
                            gm_l6 = t24;
                        }
                        {
                            gm_tl874t2atnlh3 t27 = gm_l2;
                            gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            uint8_t t29 = (gm_eqff85ac10o3(((uint8_t *)&t27), ((uint8_t *)&t28)));
                            if (!t29) goto ie26;
                            {
                                goto lc5;
                            }
                            goto ix26;
ie26:;
ix26:;
                        }
                        {
                            gm_l7 = ((uint8_t)0);
                        }
                        {
                            {
                                void * t32 = gm_l0;
                                gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 80) + 0));
                                gm_l8 = t33;
                            }
                            {
                                gm_l9 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t35 = gm_l9;
                                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    uint8_t t37 = (t35 < t36);
                                    if (!t37) goto lb34;
                                    {
                                        uint64_t t40 = gm_l9;
                                        uint64_t t41 = (uintptr_t)t40;
                                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                        gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t41 * 12));
                                        gm_l10 = t45;
                                        {
                                            gm_tl874t2atnlh3 t48 = gm_l10;
                                            gm_tl874t2atnlh3 t49 = gm_l2;
                                            uint8_t t50 = (gm_eqff85ac10o3(((uint8_t *)&t48), ((uint8_t *)&t49)));
                                            if (!t50) goto ie47;
                                            {
                                                gm_l7 = ((uint8_t)1);
                                            }
                                            goto ix47;
ie47:;
ix47:;
                                        }
                                        goto me43;
mn44:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 294, 13);
me43:;
                                    }
lc34:;
                                    {
                                        uint64_t t53 = gm_l9;
                                        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                                        uint64_t t56 = r54;
                                        gm_l9 = t56;
                                    }
                                }
lb34:;
                            }
                        }
                        {
                            uint8_t t58 = gm_l7;
                            uint8_t t59 = (uint8_t)!t58;
                            if (!t59) goto ie57;
                            {
                                void * t61 = gm_l0;
                                void * t62 = (void *)(((uint8_t *)t61) + 80);
                                gm_tl874t2atnlh3 t63 = gm_l2;
                                gm_tftb1odc739t5 t64 = gm_f_push_0mk7udh(t62, GM_CONV(gm_cseahvg8v7p0vs, t63));
                                if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcfk7rg8t5apoh, 298, 17);
                                gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                            }
                            {
                                void * t66 = gm_l0;
                                void * t67 = GM_LD(void *, (((uint8_t *)t66) + 120));
                                gm_tis7ttivmj935 t68;
                                memset((uint8_t *)&t68 + 12, 0, 4);
                                gm_tl874t2atnlh3 t69 = gm_l2;
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t68 + 0, t69);
                                uint32_t t70 = gm_l1;
                                uint64_t t71 = (uint64_t)t70;
                                uint64_t t72 = (uintptr_t)t71;
                                void * t73 = gm_l0;
                                void * t74 = GM_LD(void *, (((uint8_t *)t73) + 8));
                                uint64_t t75 = GM_LD(uintptr_t, (((uint8_t *)t74) + 24) + 8);
                                if (t72 >= (uintptr_t)(t75)) gm_trap_index(t72, t75, gm_srcfk7rg8t5apoh, 299, 61);
                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t74) + 24)) + t72 * 40) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t76);
                                gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t68 + 32, t77);
                                gm_tl874t2atnlh3 t78 = gm_l6;
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t68 + 44, t78);
                                gm_tj5f8vajf35o7 t79;
                                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t79 + 64, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)10ULL));
                                GM_ST(gm_tis7ttivmj935, ((uint8_t *)&t79 + 8), t68);
                                gm_tftb1odc739t5 t80 = gm_f_push_rqlbv1e(t67, &t79);
                                if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcfk7rg8t5apoh, 299, 17);
                                gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                            }
                            goto ix57;
ie57:;
ix57:;
                        }
                        {
                            void * t82 = gm_l0;
                            uint32_t t83 = gm_l1;
                            gm_b0_1 t84 = gm_f_forget_2rgci5c((void *)NULL, cx, NULL, t82, t83);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            gm_b0_1 t85;
                            uint8_t t86 = 0;
                            F.ret = t85;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 289, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t88 = gm_l4;
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                    uint64_t t91 = r89;
                    gm_l4 = t91;
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
__attribute__((aligned(4))) void gm_f_union_live_empebln(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_union_live_empebln F;
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l6; /* found */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    GM_STACK_CHECK(&F, 402u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_f_clone_live_rrk5v52((void *)&F.l2, (void *)NULL, cx, NULL, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tegs8i1nl3jqh t15 = GM_LD(gm_tegs8i1nl3jqh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 56));
                    F.l5 = t15;
                    {
                        gm_l6 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            gm_l7 = t18;
                        }
                        {
                            for (;;) {
                                uint64_t t20 = gm_l8;
                                uint64_t t21 = gm_l7;
                                uint8_t t22 = (t20 < t21);
                                if (!t22) goto lb19;
                                {
                                    uint64_t t25 = gm_l8;
                                    uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&t25));
                                    gm_l9 = t28;
                                    {
                                        uint64_t t31 = gm_l9;
                                        uint64_t t32 = (uintptr_t)t31;
                                        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcfk7rg8t5apoh, 193, 16);
                                        uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t32 * 56) + 0));
                                        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 0));
                                        uint8_t t36 = ((t34 == t35));
                                        if (!t36) goto ie30;
                                        uint64_t t37 = gm_l9;
                                        uint64_t t38 = (uintptr_t)t37;
                                        uint64_t t39 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcfk7rg8t5apoh, 193, 48);
                                        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t38 * 56) + 8));
                                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                                        uint8_t t42 = (gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
                                        if (!t42) goto ie30;
                                        {
                                            gm_l6 = ((uint8_t)1);
                                        }
                                        {
                                            uint64_t t45 = gm_l9;
                                            uint64_t t46 = (uintptr_t)t45;
                                            uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                            if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_srcfk7rg8t5apoh, 195, 20);
                                            uint8_t t48;
                                            if ((((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t46 * 56) + 36))[12] != 0)) goto mn50;
                                            t48 = ((uint8_t)1);
                                            goto me49;
mn50:;
                                            t48 = ((uint8_t)0);
                                            goto me49;
mn51:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 195, 20);
me49:;
                                            if (!t48) goto ie44;
                                            {
                                                uint64_t t53 = gm_l9;
                                                uint64_t t54 = (uintptr_t)t53;
                                                uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                                if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcfk7rg8t5apoh, 195, 47);
                                                gm_toceffb697p0u t56 = GM_LD(gm_toceffb697p0u, (((uint8_t *)&F.l5) + 36));
                                                GM_ST(gm_toceffb697p0u, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t54 * 56) + 36), t56);
                                            }
                                            goto ix44;
ie44:;
ix44:;
                                        }
                                        goto ix30;
ie30:;
ix30:;
                                    }
                                    goto me26;
mn27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 192, 9);
me26:;
                                }
lc19:;
                                {
                                    uint64_t t58 = gm_l8;
                                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                                    uint64_t t61 = r59;
                                    gm_l8 = t61;
                                }
                            }
lb19:;
                        }
                    }
                    {
                        uint8_t t63 = gm_l6;
                        uint8_t t64 = (uint8_t)!t63;
                        if (!t64) goto ie62;
                        {
                            void * t66 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t67 = gm_f_push_2mg7bkm(t66, &(*(gm_tegs8i1nl3jqh *)((uint8_t *)&F.l5)));
                            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcfk7rg8t5apoh, 198, 24);
                            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                        }
                        goto ix62;
ie62:;
ix62:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfk7rg8t5apoh, 190, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t70 = gm_l4;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l4 = t73;
                }
            }
lb4:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
