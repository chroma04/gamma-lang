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
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
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
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_is_bool_ajhjgeg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_3jk89qu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
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
struct gm_fr_lane_error_72hoonn {
    gm_frame h;
    void * ret;
};
struct gm_fr_lane_needs_a6gcpil {
    gm_frame h;
    void * ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mask_ty_cj7ln0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_binary_bicu7u0 {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_construction_fhm2mm5 {
    gm_frame h;
    gm_tkacitepg5fk0 l8; /* a */
    void * ret;
};
struct gm_fr_vector_conversion_32a9cuf {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_load_ssdvomk {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_method_hgn4sb1 {
    gm_frame h;
    gm_tc3oht4ksth9d l16; /* xe */
    void * ret;
};
struct gm_fr_vector_shuffle_5ime7a7 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* idx */
    gm_t7te2tdpebbt4 l12; /* a */
    gm_tc3oht4ksth9d l13; /* e */
    gm_tbci5ospk5nnn l17; /* i */
    gm_tbci5ospk5nnn l23; /* i */
    gm_trqfvmjtt4sij l26; /* all */
    gm_t69m56meqefs8 l29; /* i */
    void * ret;
};
struct gm_fr_vector_unary_2mbe97t {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_format_0gvqkke(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8eujoej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gsgjkse(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ima6g5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jm4dd7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_m1f4vhf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pepv3ej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ul5ufam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_lane_type_ok_rp8uq1p(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vector_shape_error_3invhhe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1t08grtrn18m[9];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s6lddausbiitu[9];
extern const uint8_t gm_s8guqcjcce9oc[22];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajso5sujsruj[47];
extern const uint8_t gm_salin070nim24[30];
extern const uint8_t gm_salpeh0v5rqdf[8];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sbqoiitqtfv75[11];
extern const uint8_t gm_scqrlin5l437v[9];
extern const uint8_t gm_sf1qp7r4ktsd1[11];
extern const uint8_t gm_sfmov9pb6t5lt[14];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sh1to1i0u67lu[9];
extern const uint8_t gm_sioam4mlrjuar[52];
extern const uint8_t gm_sj7s3tnnvob4e[9];
extern const uint8_t gm_skdalh0o0hret[59];
extern const uint8_t gm_skftljk77e6mu[4];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slu3lsfv09uua[25];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_smgfld86ipv3f[58];
extern const uint8_t gm_snc5ns3akncuq[6];
extern const uint8_t gm_spj0hqsg585et[14];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7u9cfdjh7s0n[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st4t0ufodvddp[59];
extern const uint8_t gm_sut92jpq3kk88[11];
extern const uint8_t gm_sv8pauo1bhltl[9];
extern const uint8_t gm_svgoe41piv341[7];
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
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bool_ajhjgeg F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)4ULL)));
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_3jk89qu F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)3ULL)) goto mn6;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 10, 56);
me5:;
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_vector_parts_crrfopm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_mask_ty_cj7ln0g F;
    void * gm_l0; /* tys */
    uint64_t gm_l1; /* len */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t1, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
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
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_61fb705_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t69m56meqefs8 t21 = F.l1;
        GM_ST(gm_t69m56meqefs8, ((uint8_t *)t20 + t18 * 88), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 89u);
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
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        uint8_t t7 = 0;
        F.ret = t6;
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
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
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
inline __attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_error_72hoonn F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* opn */
    gm_b16_8 gm_l2; /* what */
    gm_t88rcucg1b7uk gm_l3; /* vt */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 293u);
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
        gm_tl874t2atnlh3 t3 = gm_l4;
        gm_tfeab279c16rr t4;
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_b16_8 t9 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 32, t9);
        gm_tabb710d9mq2e t10;
        gm_f_format_ima6g5u((void *)&t10, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src7u9cfdjh7s0n, 124, 20);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b0_1 t13 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        gm_tl874t2atnlh3 t16 = gm_l4;
        void * t17 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = 0;
        F.ret = t17;
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
inline __attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_needs_a6gcpil F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* what */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* lane */
    GM_STACK_CHECK(&F, 413u);
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
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_tb7u19loo3j87 t6;
        gm_f_vector_parts_crrfopm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        if (!((((uint8_t *)&t6))[16] != 0)) gm_trap("unwrap of null", gm_src7u9cfdjh7s0n, 118, 13);
        gm_tlcm4lvu3g1a8 t7 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t6));
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = gm_l4;
        gm_t0uuhaljsf410 t11;
        gm_b16_8 t12 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
        gm_b16_8 t13 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t13);
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = gm_l2;
        gm_b16_8 t16 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 32, t16);
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l5;
        gm_b16_8 t19 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 48, t19);
        gm_tabb710d9mq2e t20;
        gm_f_format_0gvqkke((void *)&t20, (void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_src7u9cfdjh7s0n, 119, 20);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t9, t10, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        gm_tl874t2atnlh3 t26 = gm_l4;
        void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = 0;
        F.ret = t27;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_binary_bicu7u0 F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* vt */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    gm_t88rcucg1b7uk gm_l8; /* lane */
    gm_b16_8 gm_l9; /* opn */
    void * gm_l10; /* r */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t9qkh0rsooc8d gm_l12; /* f */
    gm_ts6oqppjh0ba3 gm_l13; /* top */
    void * gm_l14; /* r */
    gm_t88rcucg1b7uk gm_l15; /* rt */
    gm_b16_8 gm_l16; /* hint */
    uint8_t gm_l17; /* lane_int */
    uint8_t gm_l18; /* lane_float */
    uint8_t gm_l19; /* numeric */
    gm_b16_8 gm_l20; /* m */
    gm_t9qkh0rsooc8d gm_l21; /* f */
    gm_t9qkh0rsooc8d gm_l22; /* f */
    gm_t9qkh0rsooc8d gm_l23; /* f */
    gm_ts6oqppjh0ba3 gm_l24; /* top */
    gm_ts6oqppjh0ba3 gm_l25; /* top */
    GM_STACK_CHECK(&F, 3155u);
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
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t12 = gm_l5;
        gm_t88rcucg1b7uk t13 = gm_l6;
        gm_tb7u19loo3j87 t14;
        gm_f_vector_parts_crrfopm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        gm_tlcm4lvu3g1a8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t16 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_tl874t2atnlh3 t19 = gm_l4;
            void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        gm_l7 = t15;
    }
    {
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l8 = t22;
    }
    {
        gm_ts6oqppjh0ba3 t23 = gm_l1;
        gm_b16_8 t24 = gm_f_binop_text_0ljfc0r(t23);
        gm_l9 = t24;
    }
    {
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t31) { case ((uint8_t)0ULL): goto ma29; case ((uint8_t)1ULL): goto ma29; case ((uint8_t)2ULL): goto ma29; case ((uint8_t)3ULL): goto ma28; case ((uint8_t)4ULL): goto ma29; case ((uint8_t)5ULL): goto ma29; case ((uint8_t)6ULL): goto ma28; case ((uint8_t)7ULL): goto ma29; case ((uint8_t)8ULL): goto ma29; case ((uint8_t)9ULL): goto ma29; case ((uint8_t)10ULL): goto ma27; case ((uint8_t)11ULL): goto ma27; case ((uint8_t)12ULL): goto ma29; case ((uint8_t)13ULL): goto ma29; case ((uint8_t)14ULL): goto ma27; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma29; case ((uint8_t)18ULL): goto ma29; case ((uint8_t)19ULL): goto ma29; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; default: goto ma29; }
ma26:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t35 != ((uint8_t)20ULL)) goto pa34;
        goto po33;
pa34:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t37 != ((uint8_t)21ULL)) goto pa36;
        goto po33;
pa36:;
        goto mn32;
po33:;
        {
            {
                void * t40 = gm_l0;
                void * t41 = gm_l3;
                gm_tkn42u7na3t81 t42;
                memset(&t42, 0, sizeof t42);
                void * t43 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t40, t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l4;
            gm_b16_8 t46 = gm_l9;
            gm_tnpifv20irnph t47;
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t46);
            gm_tabb710d9mq2e t48;
            gm_f_format_frnomph((void *)&t48, (void *)NULL, cx, NULL, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_src7u9cfdjh7s0n, 37, 28);
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t44, t45, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            gm_tl874t2atnlh3 t54 = gm_l4;
            void * t55 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto me25;
mn32:;
ma27:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t60 != ((uint8_t)14ULL)) goto pa59;
        goto po58;
pa59:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t62 != ((uint8_t)15ULL)) goto pa61;
        goto po58;
pa61:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t64 != ((uint8_t)10ULL)) goto pa63;
        goto po58;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t66 != ((uint8_t)11ULL)) goto pa65;
        goto po58;
pa65:;
        goto mn57;
po58:;
        {
            {
                void * t69 = gm_l0;
                void * t70 = gm_l3;
                gm_tkn42u7na3t81 t71;
                memset(&t71, 0, sizeof t71);
                void * t72 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t69, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t73 = gm_l0;
            gm_tl874t2atnlh3 t74 = gm_l4;
            gm_b16_8 t75 = gm_l9;
            gm_tnpifv20irnph t76;
            GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
            gm_tabb710d9mq2e t77;
            gm_f_format_7r64gap((void *)&t77, (void *)NULL, cx, NULL, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_src7u9cfdjh7s0n, 42, 28);
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_b0_1 t80 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t73, t74, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t81 = gm_l0;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
            gm_tl874t2atnlh3 t83 = gm_l4;
            void * t84 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t85 = 0;
            F.ret = t84;
            goto gm_fret;
        }
        goto me25;
mn57:;
ma28:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t89 != ((uint8_t)3ULL)) goto pa88;
        goto po87;
pa88:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t91 != ((uint8_t)6ULL)) goto pa90;
        goto po87;
pa90:;
        goto mn86;
po87:;
        {
            void * t93 = gm_l0;
            void * t94 = gm_l3;
            gm_tkn42u7na3t81 t95;
            memset(&t95, 0, sizeof t95);
            void * t96 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t93, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t96;
        }
        {
            void * t98 = gm_l5;
            gm_t88rcucg1b7uk t99 = gm_l8;
            uint8_t t100 = gm_f_is_integer_8d4f9ek(t98, GM_CONV(gm_csguankko4lmhd, t99));
            uint8_t t101 = (uint8_t)!t100;
            if (!t101) goto ie97;
            {
                void * t103 = gm_l0;
                gm_tl874t2atnlh3 t104 = gm_l4;
                gm_t6j6smj7qoctu t105;
                gm_b16_8 t106 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t105 + 0, t106);
                void * t107 = gm_l0;
                gm_t88rcucg1b7uk t108 = gm_l6;
                gm_b16_8 t109 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t107, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t109);
                gm_tabb710d9mq2e t110;
                gm_f_format_q2uenf3((void *)&t110, (void *)NULL, cx, NULL, &t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_src7u9cfdjh7s0n, 48, 32);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                gm_b0_1 t113 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t103, t104, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t114 = gm_l0;
                void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
                gm_tl874t2atnlh3 t116 = gm_l4;
                void * t117 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t115, t116);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t118 = 0;
                F.ret = t117;
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        {
            gm_ts6oqppjh0ba3 t120 = gm_l1;
            gm_t9qkh0rsooc8d t121;
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)3ULL));
            uint8_t t122 = (((GM_LD(uint8_t, ((uint8_t *)&t120) + 0) == ((uint8_t)3ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t120) + 1), ((uint8_t *)&t121))));
            if (!t122) goto ie119;
            {
                void * t124 = gm_l0;
                gm_tl874t2atnlh3 t125 = gm_l4;
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_st4t0ufodvddp));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(58));
                gm_b0_1 t127 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t124, t125, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t128 = gm_l0;
                void * t129 = GM_LD(void *, (((uint8_t *)t128) + 0));
                gm_tl874t2atnlh3 t130 = gm_l4;
                void * t131 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t129, t130);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t132 = 0;
                F.ret = t131;
                goto gm_fret;
            }
            goto ix119;
ie119:;
ix119:;
        }
        {
            void * t134 = gm_l0;
            void * t135 = (void *)(((uint8_t *)t134) + 96);
            void * t136 = gm_l5;
            void * t137 = gm_l10;
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + 0));
            gm_t9qkh0rsooc8d t139;
            GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)1ULL));
            uint8_t t140 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t135, t136, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t141 = (uint8_t)!t140;
            if (!t141) goto ie133;
            void * t142 = gm_l5;
            void * t143 = gm_l10;
            gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t143) + 0));
            uint8_t t145 = gm_f_is_error_8k27gd1(t142, GM_CONV(gm_csguankko4lmhd, t144));
            uint8_t t146 = (uint8_t)!t145;
            if (!t146) goto ie133;
            {
                void * t148 = gm_l0;
                void * t149 = (void *)(((uint8_t *)t148) + 96);
                void * t150 = gm_l0;
                void * t151 = GM_LD(void *, (((uint8_t *)t150) + 0));
                void * t152 = gm_l10;
                gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t152) + 0));
                gm_t88rcucg1b7uk t154 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t149, t151, t153);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t154;
            }
            {
                void * t155 = gm_l0;
                void * t156 = gm_l10;
                gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 72));
                void * t158 = gm_l0;
                gm_t88rcucg1b7uk t159 = gm_l11;
                gm_b16_8 t160 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t158, t159);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t161;
                GM_ST(gm_b16_8, ((uint8_t *)&t161 + 0), t160);
                gm_tabb710d9mq2e t162;
                gm_f_format_pdashts((void *)&t162, (void *)NULL, cx, NULL, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 8, gm_src7u9cfdjh7s0n, 57, 34);
                gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
                gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
                gm_b0_1 t165 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t155, t157, t164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix133;
ie133:;
ix133:;
        }
        {
            gm_ts6oqppjh0ba3 t166;
            uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t168 != ((uint8_t)3ULL)) goto ie167;
            gm_t9qkh0rsooc8d t169 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l12 = t169;
            gm_t9qkh0rsooc8d t171 = gm_l12;
            gm_ts6oqppjh0ba3 t172;
            GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t172 + 1), t171);
            gm_ts6oqppjh0ba3 t173 = t172;
            t166 = t173;
            goto ix167;
ie167:;
            gm_ts6oqppjh0ba3 t175;
            GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)6ULL));
            gm_ts6oqppjh0ba3 t176 = t175;
            t166 = t176;
ix167:;
            gm_l13 = t166;
        }
        {
            gm_t88rcucg1b7uk t177 = gm_l6;
            gm_trl9d7m1k5lbd t178;
            memset((uint8_t *)&t178 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t179 = gm_l13;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t178 + 0, t179);
            void * t180 = gm_l2;
            GM_ST(void *, (uint8_t *)&t178 + 8, t180);
            void * t181 = gm_l10;
            GM_ST(void *, (uint8_t *)&t178 + 16, t181);
            gm_teti411eujvf4 t182;
            memset(&t182, 0, sizeof t182);
            GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t182 + 8), t178);
            gm_tl874t2atnlh3 t183 = gm_l4;
            void * t184 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t177, &t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t185 = 0;
            F.ret = t184;
            goto gm_fret;
        }
        goto me25;
mn86:;
ma29:;
        goto me25;
mn186:;
ma30:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 34, 5);
me25:;
    }
    {
        void * t188 = gm_l0;
        void * t189 = gm_l3;
        gm_t88rcucg1b7uk t190 = gm_l6;
        gm_tkn42u7na3t81 t191;
        GM_ST(uint8_t, (uint8_t *)&t191 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t191 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t191), t190);
        ((uint8_t *)&t191)[4] = 1;
        void * t192 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t188, t189, t191);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t192;
    }
    {
        void * t193 = gm_l0;
        void * t194 = (void *)(((uint8_t *)t193) + 96);
        void * t195 = gm_l0;
        void * t196 = GM_LD(void *, (((uint8_t *)t195) + 0));
        void * t197 = gm_l14;
        gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t197) + 0));
        gm_t88rcucg1b7uk t199 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t194, t196, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t199;
    }
    {
        void * t201 = gm_l5;
        gm_t88rcucg1b7uk t202 = gm_l15;
        uint8_t t203 = gm_f_is_error_8k27gd1(t201, GM_CONV(gm_csguankko4lmhd, t202));
        if (!t203) goto ie200;
        {
            void * t205 = gm_l0;
            void * t206 = GM_LD(void *, (((uint8_t *)t205) + 0));
            gm_tl874t2atnlh3 t207 = gm_l4;
            void * t208 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t206, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t209 = 0;
            F.ret = t208;
            goto gm_fret;
        }
        goto ix200;
ie200:;
ix200:;
    }
    {
        gm_t88rcucg1b7uk t211 = gm_l15;
        gm_t88rcucg1b7uk t212 = gm_l6;
        uint8_t t213 = (!gm_eq9m6mvtmgde(((uint8_t *)&t211), ((uint8_t *)&t212)));
        if (!t213) goto ie210;
        {
            gm_b16_8 t215;
            void * t217 = gm_l5;
            gm_t88rcucg1b7uk t218 = gm_l15;
            uint8_t t219 = gm_f_is_vector_gkq6b88(t217, GM_CONV(gm_csguankko4lmhd, t218));
            if (!t219) goto ie216;
            gm_b16_8 t221;
            GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(0));
            gm_b16_8 t222 = t221;
            t215 = t222;
            goto ix216;
ie216:;
            gm_b16_8 t224;
            GM_ST(void *, (uint8_t *)&t224, (void *)(gm_smgfld86ipv3f));
            GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(57));
            gm_b16_8 t225 = t224;
            t215 = t225;
ix216:;
            gm_l16 = t215;
        }
        {
            void * t226 = gm_l0;
            gm_tl874t2atnlh3 t227 = gm_l4;
            gm_t0uuhaljsf410 t228;
            gm_b16_8 t229 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t229);
            void * t230 = gm_l0;
            gm_t88rcucg1b7uk t231 = gm_l6;
            gm_b16_8 t232 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t230, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 16, t232);
            void * t233 = gm_l0;
            gm_t88rcucg1b7uk t234 = gm_l15;
            gm_b16_8 t235 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t233, t234);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 32, t235);
            gm_b16_8 t236 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 48, t236);
            gm_tabb710d9mq2e t237;
            gm_f_format_pepv3ej((void *)&t237, (void *)NULL, cx, NULL, &t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 8, gm_src7u9cfdjh7s0n, 69, 24);
            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
            gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
            gm_b0_1 t240 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t226, t227, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t241 = gm_l0;
            void * t242 = GM_LD(void *, (((uint8_t *)t241) + 0));
            gm_tl874t2atnlh3 t243 = gm_l4;
            void * t244 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t242, t243);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t245 = 0;
            F.ret = t244;
            goto gm_fret;
        }
        goto ix210;
ie210:;
ix210:;
    }
    {
        void * t246 = gm_l5;
        gm_t88rcucg1b7uk t247 = gm_l8;
        uint8_t t248 = gm_f_is_integer_8d4f9ek(t246, GM_CONV(gm_csguankko4lmhd, t247));
        gm_l17 = t248;
    }
    {
        void * t249 = gm_l5;
        gm_t88rcucg1b7uk t250 = gm_l8;
        uint8_t t251 = gm_f_is_float_3jk89qu(t249, GM_CONV(gm_csguankko4lmhd, t250));
        gm_l18 = t251;
    }
    {
        uint8_t t252 = 0;
        uint8_t t253 = gm_l17;
        if (!t253) {
            uint8_t t254 = gm_l18;
            t252 = t254;
        } else t252 = 1;
        gm_l19 = t252;
    }
    {
        uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t262) { case ((uint8_t)0ULL): goto ma257; case ((uint8_t)1ULL): goto ma257; case ((uint8_t)2ULL): goto ma257; case ((uint8_t)3ULL): goto ma260; case ((uint8_t)4ULL): goto ma258; case ((uint8_t)5ULL): goto ma260; case ((uint8_t)6ULL): goto ma260; case ((uint8_t)7ULL): goto ma259; case ((uint8_t)8ULL): goto ma259; case ((uint8_t)9ULL): goto ma259; case ((uint8_t)10ULL): goto ma260; case ((uint8_t)11ULL): goto ma260; case ((uint8_t)12ULL): goto ma260; case ((uint8_t)13ULL): goto ma260; case ((uint8_t)14ULL): goto ma260; case ((uint8_t)15ULL): goto ma260; case ((uint8_t)16ULL): goto ma256; case ((uint8_t)17ULL): goto ma256; case ((uint8_t)18ULL): goto ma256; case ((uint8_t)19ULL): goto ma256; case ((uint8_t)20ULL): goto ma260; case ((uint8_t)21ULL): goto ma260; default: goto ma260; }
ma256:;
        uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t266 != ((uint8_t)16ULL)) goto pa265;
        goto po264;
pa265:;
        uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t268 != ((uint8_t)17ULL)) goto pa267;
        goto po264;
pa267:;
        uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t270 != ((uint8_t)18ULL)) goto pa269;
        goto po264;
pa269:;
        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t272 != ((uint8_t)19ULL)) goto pa271;
        goto po264;
pa271:;
        goto mn263;
po264:;
        {
            gm_b16_8 t274;
            uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t281) { case ((uint8_t)0ULL): goto ma279; case ((uint8_t)1ULL): goto ma279; case ((uint8_t)2ULL): goto ma279; case ((uint8_t)3ULL): goto ma279; case ((uint8_t)4ULL): goto ma279; case ((uint8_t)5ULL): goto ma279; case ((uint8_t)6ULL): goto ma279; case ((uint8_t)7ULL): goto ma279; case ((uint8_t)8ULL): goto ma279; case ((uint8_t)9ULL): goto ma279; case ((uint8_t)10ULL): goto ma279; case ((uint8_t)11ULL): goto ma279; case ((uint8_t)12ULL): goto ma279; case ((uint8_t)13ULL): goto ma279; case ((uint8_t)14ULL): goto ma279; case ((uint8_t)15ULL): goto ma279; case ((uint8_t)16ULL): goto ma276; case ((uint8_t)17ULL): goto ma277; case ((uint8_t)18ULL): goto ma278; case ((uint8_t)19ULL): goto ma279; case ((uint8_t)20ULL): goto ma279; case ((uint8_t)21ULL): goto ma279; default: goto ma279; }
ma276:;
            uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t283 != ((uint8_t)16ULL)) goto mn282;
            gm_b16_8 t284;
            GM_ST(void *, (uint8_t *)&t284, (void *)(gm_sh1to1i0u67lu));
            GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(8));
            t274 = t284;
            goto me275;
mn282:;
ma277:;
            uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t286 != ((uint8_t)17ULL)) goto mn285;
            gm_b16_8 t287;
            GM_ST(void *, (uint8_t *)&t287, (void *)(gm_sv8pauo1bhltl));
            GM_ST(uintptr_t, (uint8_t *)&t287 + 8, (uintptr_t)(8));
            t274 = t287;
            goto me275;
mn285:;
ma278:;
            uint8_t t289 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t289 != ((uint8_t)18ULL)) goto mn288;
            gm_b16_8 t290;
            GM_ST(void *, (uint8_t *)&t290, (void *)(gm_s1t08grtrn18m));
            GM_ST(uintptr_t, (uint8_t *)&t290 + 8, (uintptr_t)(8));
            t274 = t290;
            goto me275;
mn288:;
ma279:;
            gm_b16_8 t292;
            GM_ST(void *, (uint8_t *)&t292, (void *)(gm_scqrlin5l437v));
            GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(8));
            t274 = t292;
            goto me275;
mn291:;
ma280:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 77, 25);
me275:;
            gm_l20 = t274;
        }
        {
            void * t293 = gm_l0;
            gm_tl874t2atnlh3 t294 = gm_l4;
            gm_b16_8 t295 = gm_l20;
            gm_tnpifv20irnph t296;
            GM_ST(gm_b16_8, ((uint8_t *)&t296 + 0), t295);
            gm_tabb710d9mq2e t297;
            gm_f_format_l2fmok5((void *)&t297, (void *)NULL, cx, NULL, t296);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 8, gm_src7u9cfdjh7s0n, 83, 28);
            gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&t297) + 8));
            gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
            gm_b0_1 t300 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t293, t294, t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t301 = gm_l0;
            void * t302 = GM_LD(void *, (((uint8_t *)t301) + 0));
            gm_tl874t2atnlh3 t303 = gm_l4;
            void * t304 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t302, t303);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t305 = 0;
            F.ret = t304;
            goto gm_fret;
        }
        goto me255;
mn263:;
ma257:;
        uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t309 != ((uint8_t)0ULL)) goto pa308;
        gm_t9qkh0rsooc8d t310 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t310;
        goto po307;
pa308:;
        uint8_t t312 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t312 != ((uint8_t)1ULL)) goto pa311;
        gm_t9qkh0rsooc8d t313 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t313;
        goto po307;
pa311:;
        uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t315 != ((uint8_t)2ULL)) goto pa314;
        gm_t9qkh0rsooc8d t316 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t316;
        goto po307;
pa314:;
        goto mn306;
po307:;
        {
            uint8_t t319 = gm_l19;
            uint8_t t320 = (uint8_t)!t319;
            if (!t320) goto ie318;
            {
                void * t322 = gm_l0;
                gm_b16_8 t323 = gm_l9;
                gm_b16_8 t324;
                GM_ST(void *, (uint8_t *)&t324, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t324 + 8, (uintptr_t)(13));
                gm_t88rcucg1b7uk t325 = gm_l6;
                gm_tl874t2atnlh3 t326 = gm_l4;
                void * t327 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t322, t323, t324, t325, t326);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t328 = 0;
                F.ret = t327;
                goto gm_fret;
            }
            goto ix318;
ie318:;
ix318:;
        }
        {
            uint8_t t330 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l21) + 0) == ((uint8_t)3ULL)));
            if (!t330) goto ie329;
            {
                void * t332 = gm_l0;
                gm_tl874t2atnlh3 t333 = gm_l4;
                gm_b16_8 t334 = gm_l9;
                gm_tnpifv20irnph t335;
                GM_ST(gm_b16_8, ((uint8_t *)&t335 + 0), t334);
                gm_tabb710d9mq2e t336;
                gm_f_format_l035o11((void *)&t336, (void *)NULL, cx, NULL, t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t336))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t336) + 8, gm_src7u9cfdjh7s0n, 89, 32);
                gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&t336) + 8));
                gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
                gm_b0_1 t339 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t332, t333, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t340 = gm_l0;
                void * t341 = GM_LD(void *, (((uint8_t *)t340) + 0));
                gm_tl874t2atnlh3 t342 = gm_l4;
                void * t343 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t341, t342);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t344 = 0;
                F.ret = t343;
                goto gm_fret;
            }
            goto ix329;
ie329:;
ix329:;
        }
        {
            uint8_t t346 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l21) + 0) == ((uint8_t)0ULL)));
            if (!t346) goto ie345;
            uint8_t t347 = gm_l17;
            uint8_t t348 = (uint8_t)!t347;
            if (!t348) goto ie345;
            {
                void * t350 = gm_l0;
                gm_b16_8 t351 = gm_l9;
                gm_b16_8 t352;
                GM_ST(void *, (uint8_t *)&t352, (void *)(gm_spj0hqsg585et));
                GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(13));
                gm_t88rcucg1b7uk t353 = gm_l6;
                gm_tl874t2atnlh3 t354 = gm_l4;
                void * t355 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t350, t351, t352, t353, t354);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t356 = 0;
                F.ret = t355;
                goto gm_fret;
            }
            goto ix345;
ie345:;
ix345:;
        }
        {
            gm_ts6oqppjh0ba3 t357;
            uint8_t t363 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t363) { case ((uint8_t)0ULL): goto ma359; case ((uint8_t)1ULL): goto ma360; case ((uint8_t)2ULL): goto ma361; case ((uint8_t)3ULL): goto ma361; case ((uint8_t)4ULL): goto ma361; case ((uint8_t)5ULL): goto ma361; case ((uint8_t)6ULL): goto ma361; case ((uint8_t)7ULL): goto ma361; case ((uint8_t)8ULL): goto ma361; case ((uint8_t)9ULL): goto ma361; case ((uint8_t)10ULL): goto ma361; case ((uint8_t)11ULL): goto ma361; case ((uint8_t)12ULL): goto ma361; case ((uint8_t)13ULL): goto ma361; case ((uint8_t)14ULL): goto ma361; case ((uint8_t)15ULL): goto ma361; case ((uint8_t)16ULL): goto ma361; case ((uint8_t)17ULL): goto ma361; case ((uint8_t)18ULL): goto ma361; case ((uint8_t)19ULL): goto ma361; case ((uint8_t)20ULL): goto ma361; case ((uint8_t)21ULL): goto ma361; default: goto ma361; }
ma359:;
            uint8_t t365 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t365 != ((uint8_t)0ULL)) goto mn364;
            gm_t9qkh0rsooc8d t366 = gm_l21;
            gm_ts6oqppjh0ba3 t367;
            GM_ST(uint8_t, ((uint8_t *)&t367 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t367 + 1), t366);
            t357 = t367;
            goto me358;
mn364:;
ma360:;
            uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t369 != ((uint8_t)1ULL)) goto mn368;
            gm_t9qkh0rsooc8d t370 = gm_l21;
            gm_ts6oqppjh0ba3 t371;
            GM_ST(uint8_t, ((uint8_t *)&t371 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t371 + 1), t370);
            t357 = t371;
            goto me358;
mn368:;
ma361:;
            gm_t9qkh0rsooc8d t373 = gm_l21;
            gm_ts6oqppjh0ba3 t374;
            GM_ST(uint8_t, ((uint8_t *)&t374 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t374 + 1), t373);
            t357 = t374;
            goto me358;
mn372:;
ma362:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 93, 30);
me358:;
            gm_l24 = t357;
        }
        {
            gm_t88rcucg1b7uk t375 = gm_l6;
            gm_trl9d7m1k5lbd t376;
            memset((uint8_t *)&t376 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t377 = gm_l24;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t376 + 0, t377);
            void * t378 = gm_l2;
            GM_ST(void *, (uint8_t *)&t376 + 8, t378);
            void * t379 = gm_l14;
            GM_ST(void *, (uint8_t *)&t376 + 16, t379);
            gm_teti411eujvf4 t380;
            memset(&t380, 0, sizeof t380);
            GM_ST(uint8_t, ((uint8_t *)&t380 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t380 + 8), t376);
            gm_tl874t2atnlh3 t381 = gm_l4;
            void * t382 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t375, &t380, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t383 = 0;
            F.ret = t382;
            goto gm_fret;
        }
        goto me255;
mn306:;
ma258:;
        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t385 != ((uint8_t)4ULL)) goto mn384;
        uint8_t t387 = gm_l18;
        if (!t387) goto ie386;
        {
            gm_t88rcucg1b7uk t389 = gm_l6;
            gm_trl9d7m1k5lbd t390;
            memset((uint8_t *)&t390 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t391;
            GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)4ULL));
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t390 + 0, t391);
            void * t392 = gm_l2;
            GM_ST(void *, (uint8_t *)&t390 + 8, t392);
            void * t393 = gm_l14;
            GM_ST(void *, (uint8_t *)&t390 + 16, t393);
            gm_teti411eujvf4 t394;
            memset(&t394, 0, sizeof t394);
            GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t394 + 8), t390);
            gm_tl874t2atnlh3 t395 = gm_l4;
            void * t396 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t389, &t394, t395);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t397 = 0;
            F.ret = t396;
            goto gm_fret;
        }
        goto ix386;
ie386:;
ix386:;
        goto me255;
mn384:;
ma259:;
        uint8_t t401 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t401 != ((uint8_t)7ULL)) goto pa400;
        goto po399;
pa400:;
        uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t403 != ((uint8_t)8ULL)) goto pa402;
        goto po399;
pa402:;
        uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t405 != ((uint8_t)9ULL)) goto pa404;
        goto po399;
pa404:;
        goto mn398;
po399:;
        {
            uint8_t t408 = gm_l18;
            if (!t408) goto ie407;
            {
                void * t410 = gm_l0;
                gm_b16_8 t411 = gm_l9;
                gm_b16_8 t412;
                GM_ST(void *, (uint8_t *)&t412, (void *)(gm_slu3lsfv09uua));
                GM_ST(uintptr_t, (uint8_t *)&t412 + 8, (uintptr_t)(24));
                gm_t88rcucg1b7uk t413 = gm_l6;
                gm_tl874t2atnlh3 t414 = gm_l4;
                void * t415 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t410, t411, t412, t413, t414);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t416 = 0;
                F.ret = t415;
                goto gm_fret;
            }
            goto ix407;
ie407:;
ix407:;
        }
        {
            gm_ts6oqppjh0ba3 t417;
            uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t423) { case ((uint8_t)0ULL): goto ma421; case ((uint8_t)1ULL): goto ma421; case ((uint8_t)2ULL): goto ma421; case ((uint8_t)3ULL): goto ma421; case ((uint8_t)4ULL): goto ma421; case ((uint8_t)5ULL): goto ma421; case ((uint8_t)6ULL): goto ma421; case ((uint8_t)7ULL): goto ma419; case ((uint8_t)8ULL): goto ma420; case ((uint8_t)9ULL): goto ma421; case ((uint8_t)10ULL): goto ma421; case ((uint8_t)11ULL): goto ma421; case ((uint8_t)12ULL): goto ma421; case ((uint8_t)13ULL): goto ma421; case ((uint8_t)14ULL): goto ma421; case ((uint8_t)15ULL): goto ma421; case ((uint8_t)16ULL): goto ma421; case ((uint8_t)17ULL): goto ma421; case ((uint8_t)18ULL): goto ma421; case ((uint8_t)19ULL): goto ma421; case ((uint8_t)20ULL): goto ma421; case ((uint8_t)21ULL): goto ma421; default: goto ma421; }
ma419:;
            uint8_t t425 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t425 != ((uint8_t)7ULL)) goto mn424;
            gm_ts6oqppjh0ba3 t426;
            GM_ST(uint8_t, (uint8_t *)&t426 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t426 + 0), ((uint8_t)7ULL));
            t417 = t426;
            goto me418;
mn424:;
ma420:;
            uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t428 != ((uint8_t)8ULL)) goto mn427;
            gm_ts6oqppjh0ba3 t429;
            GM_ST(uint8_t, (uint8_t *)&t429 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t429 + 0), ((uint8_t)8ULL));
            t417 = t429;
            goto me418;
mn427:;
ma421:;
            gm_ts6oqppjh0ba3 t431;
            GM_ST(uint8_t, (uint8_t *)&t431 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t431 + 0), ((uint8_t)9ULL));
            t417 = t431;
            goto me418;
mn430:;
ma422:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 103, 30);
me418:;
            gm_l25 = t417;
        }
        {
            gm_t88rcucg1b7uk t432 = gm_l6;
            gm_trl9d7m1k5lbd t433;
            memset((uint8_t *)&t433 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t434 = gm_l25;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t433 + 0, t434);
            void * t435 = gm_l2;
            GM_ST(void *, (uint8_t *)&t433 + 8, t435);
            void * t436 = gm_l14;
            GM_ST(void *, (uint8_t *)&t433 + 16, t436);
            gm_teti411eujvf4 t437;
            memset(&t437, 0, sizeof t437);
            GM_ST(uint8_t, ((uint8_t *)&t437 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t437 + 8), t433);
            gm_tl874t2atnlh3 t438 = gm_l4;
            void * t439 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t432, &t437, t438);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t440 = 0;
            F.ret = t439;
            goto gm_fret;
        }
        goto me255;
mn398:;
ma260:;
        goto me255;
mn441:;
ma261:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 75, 5);
me255:;
    }
    {
        void * t443 = gm_l0;
        gm_tl874t2atnlh3 t444 = gm_l4;
        gm_t6j6smj7qoctu t445;
        gm_b16_8 t446 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t445 + 0, t446);
        void * t447 = gm_l0;
        gm_t88rcucg1b7uk t448 = gm_l8;
        gm_b16_8 t449 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t447, t448);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t445 + 16, t449);
        gm_tabb710d9mq2e t450;
        gm_f_format_qu5usm7((void *)&t450, (void *)NULL, cx, NULL, &t445);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t450))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t450) + 8, gm_src7u9cfdjh7s0n, 113, 20);
        gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)&t450) + 8));
        gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&t451));
        gm_b0_1 t453 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t443, t444, t452);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t454 = gm_l0;
        void * t455 = GM_LD(void *, (((uint8_t *)t454) + 0));
        gm_tl874t2atnlh3 t456 = gm_l4;
        void * t457 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t455, t456);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t458 = 0;
        F.ret = t457;
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
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_construction_fhm2mm5 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* attrs */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* call */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* st */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_t88rcucg1b7uk gm_l11; /* t */
    void * gm_l12; /* tys */
    gm_tlcm4lvu3g1a8 gm_l13; /* p */
    gm_tlcm4lvu3g1a8 gm_l14; /* p */
    GM_STACK_CHECK(&F, 1170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        void * t3 = gm_f_construction_call_ktqtrpo(t2);
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            gm_tl874t2atnlh3 t8 = gm_l3;
            void * t9 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l4 = t4;
    }
    {
        void * t12 = gm_l4;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
        if (t13 != ((uint8_t)19ULL)) goto ie11;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((((uint8_t *)t12) + 8) + 8) + 8));
        gm_l5 = t14;
        {
            {
                gm_b16_8 t17 = gm_l1;
                gm_l6 = t17;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t19 = gm_l7;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t21 = (t19 < t20);
                    if (!t21) goto lb18;
                    {
                        uint64_t t24 = gm_l7;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tkacitepg5fk0 t29 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t25 * 72));
                        F.l8 = t29;
                        {
                            uint8_t t32 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l8) + 0) + 0) + 8) == 3) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l8) + 0) + 0)), gm_sqes10usq4q5e, 3) == 0));
                            if (!t32) goto ie31;
                            {
                                void * t34 = gm_l0;
                                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                                void * t36 = gm_l0;
                                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
                                void * t38 = gm_l2;
                                void * t39 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t38);
                                if (gm_unw.kind) {
                                    goto gm_ll22;
                                }
                                gm_tl874t2atnlh3 t40 = gm_l3;
                                gm_t88rcucg1b7uk t41 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t35, t37, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_ll22;
                                }
                                gm_l9 = t41;
                            }
                            {
                                void * t42 = gm_l0;
                                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                                gm_t88rcucg1b7uk t44 = gm_l9;
                                gm_t8cc1mtbrhlpt t45;
                                gm_f_soa_info_pq4q43l((void *)&t45, t43, GM_CONV(gm_csguankko4lmhd, t44));
                                gm_tjpqvfd72o62u t46;
                                if (((((uint8_t *)&t45))[32] != 0)) {
                                    t46 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t45) + 0));
                                } else {
                                    void * t47 = gm_l0;
                                    void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                                    gm_tl874t2atnlh3 t49 = gm_l3;
                                    void * t50 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t48, t49);
                                    if (gm_unw.kind) {
                                        goto gm_ll22;
                                    }
                                    uint8_t t51 = 0;
                                    F.ret = t50;
                                    goto gm_fret;
                                }
                                gm_l10 = t46;
                            }
                            {
                                void * t52 = gm_l0;
                                gm_t88rcucg1b7uk t53 = gm_l9;
                                gm_b16_8 t54 = gm_l5;
                                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                                gm_tl874t2atnlh3 t56 = gm_l3;
                                void * t57 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t52, t53, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t55, t56);
                                if (gm_unw.kind) {
                                    goto gm_ll22;
                                }
                                uint8_t t58 = 0;
                                F.ret = t57;
                                goto gm_fret;
                            }
                            goto ix31;
ie31:;
ix31:;
                        }
                        goto me27;
mn28:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 12, 5);
me27:;
                    }
lc18:;
                    {
                        uint64_t t60 = gm_l7;
                        uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                        uint64_t t63 = r61;
                        gm_l7 = t63;
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
        }
        {
            void * t64 = gm_l0;
            void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
            void * t66 = gm_l0;
            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t66) + 8));
            gm_b16_8 t68 = gm_l1;
            void * t69 = gm_l2;
            void * t70 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t71 = gm_l3;
            gm_t88rcucg1b7uk t72 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t65, t67, t68, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t72;
        }
        {
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            void * t75 = (void *)(((uint8_t *)t74) + 8);
            gm_l12 = t75;
        }
        {
            void * t77 = gm_l12;
            gm_t88rcucg1b7uk t78 = gm_l11;
            gm_tb7u19loo3j87 t79;
            gm_f_vector_parts_crrfopm((void *)&t79, t77, GM_CONV(gm_csguankko4lmhd, t78));
            if (!((((uint8_t *)&t79))[16] != 0)) goto ie76;
            gm_tlcm4lvu3g1a8 t80 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t79));
            gm_l13 = t80;
            {
                void * t82 = gm_l0;
                gm_t88rcucg1b7uk t83 = gm_l11;
                gm_tkn42u7na3t81 t84;
                GM_ST(uint8_t, (uint8_t *)&t84 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t84 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t84), t83);
                ((uint8_t *)&t84)[4] = 1;
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 0));
                gm_tf5srh4mp4b3i t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t86), t85);
                ((uint8_t *)&t86)[8] = 1;
                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t88 = gm_l5;
                gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                gm_tl874t2atnlh3 t90 = gm_l3;
                void * t91 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t82, t84, t86, t87, t89, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t92 = 0;
                F.ret = t91;
                goto gm_fret;
            }
            goto ix76;
ie76:;
ix76:;
        }
        {
            void * t94 = gm_l12;
            gm_t88rcucg1b7uk t95 = gm_l11;
            gm_tb7u19loo3j87 t96;
            gm_f_math_vector_parts_49c7865((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
            if (!((((uint8_t *)&t96))[16] != 0)) goto ie93;
            gm_tlcm4lvu3g1a8 t97 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t96));
            gm_l14 = t97;
            {
                void * t99 = gm_l0;
                gm_t88rcucg1b7uk t100 = gm_l11;
                gm_tkn42u7na3t81 t101;
                GM_ST(uint8_t, (uint8_t *)&t101 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t101 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t101), t100);
                ((uint8_t *)&t101)[4] = 1;
                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 0));
                gm_tf5srh4mp4b3i t103;
                GM_ST(uint8_t, (uint8_t *)&t103 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t103 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t103 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t103), t102);
                ((uint8_t *)&t103)[8] = 1;
                gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                gm_b16_8 t105 = gm_l5;
                gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                gm_tl874t2atnlh3 t107 = gm_l3;
                void * t108 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t99, t101, t103, t104, t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t109 = 0;
                F.ret = t108;
                goto gm_fret;
            }
            goto ix93;
ie93:;
ix93:;
        }
        {
            void * t110 = gm_l0;
            void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
            gm_tl874t2atnlh3 t112 = gm_l3;
            void * t113 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t114 = 0;
            F.ret = t113;
            goto gm_fret;
        }
        goto ix11;
ie11:;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, (((uint8_t *)t116) + 0));
            gm_tl874t2atnlh3 t118 = gm_l3;
            void * t119 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t120 = 0;
            F.ret = t119;
            goto gm_fret;
        }
ix11:;
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
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_conversion_32a9cuf F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* target */
    void * gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* st */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* a */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* b */
    uint8_t gm_l10; /* both */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    gm_tn3r4rvlh0rsv gm_l12; /* <temporary array> */
    gm_b16_8 gm_l13; /* why */
    GM_STACK_CHECK(&F, 1552u);
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
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = gm_l1;
        uint8_t t15 = gm_f_is_vector_gkq6b88(t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie12;
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l5;
        uint8_t t19 = gm_f_is_vector_gkq6b88(t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie12;
        {
            uint8_t t22 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t24 = gm_l4;
        gm_t88rcucg1b7uk t25 = gm_l5;
        gm_t53at80pfdus3 t26;
        gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t27 != ((uint8_t)11ULL)) goto ie23;
        uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)&t26) + 8) + 0));
        gm_l6 = t28;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t26) + 8) + 8));
        gm_l7 = t29;
        {
            void * t32 = gm_l4;
            gm_t88rcucg1b7uk t33 = gm_l1;
            gm_t53at80pfdus3 t34;
            gm_f_kind_jcnt4hm((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
            if (t35 != ((uint8_t)11ULL)) goto ie31;
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)&t34) + 8) + 0));
            gm_l8 = t36;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 8));
            gm_l9 = t37;
            {
                uint8_t t39 = 0;
                void * t40 = gm_l4;
                gm_t88rcucg1b7uk t41 = gm_l5;
                uint8_t t42 = gm_f_is_vector_gkq6b88(t40, GM_CONV(gm_csguankko4lmhd, t41));
                if (t42) {
                    void * t43 = gm_l4;
                    gm_t88rcucg1b7uk t44 = gm_l1;
                    uint8_t t45 = gm_f_is_vector_gkq6b88(t43, GM_CONV(gm_csguankko4lmhd, t44));
                    t39 = t45;
                } else t39 = 0;
                gm_l10 = t39;
            }
            {
                uint64_t t47 = gm_l6;
                uint64_t t48 = gm_l8;
                uint8_t t49 = ((t47 == t48));
                if (!t49) goto ie46;
                gm_t88rcucg1b7uk t50 = gm_l7;
                gm_t88rcucg1b7uk t51 = gm_l9;
                uint8_t t52 = (gm_eq9m6mvtmgde(((uint8_t *)&t50), ((uint8_t *)&t51)));
                if (!t52) goto ie46;
                uint8_t t53 = 0;
                uint8_t t54 = 0;
                uint8_t t55 = gm_l10;
                if (!t55) {
                    void * t56 = gm_l4;
                    gm_t88rcucg1b7uk t57 = gm_l5;
                    uint8_t t58 = gm_f_is_positional_array_6sufsk8(t56, GM_CONV(gm_csguankko4lmhd, t57));
                    t54 = t58;
                } else t54 = 1;
                if (!t54) {
                    void * t59 = gm_l4;
                    gm_t88rcucg1b7uk t60 = gm_l1;
                    uint8_t t61 = gm_f_is_positional_array_6sufsk8(t59, GM_CONV(gm_csguankko4lmhd, t60));
                    t53 = t61;
                } else t53 = 1;
                if (!t53) goto ie46;
                {
                }
                {
                    gm_tvdg94bk7aa22 t63;
                    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)7ULL));
                    gm_t88rcucg1b7uk t64 = gm_l1;
                    {
                        void * t66 = gm_l2;
                        gm_tn3r4rvlh0rsv t67;
                        GM_ST(void *, ((uint8_t *)&t67 + 0), t66);
                        gm_l11 = t67;
                    }
                    void * t68 = ((uint8_t *)&gm_l11);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 325, 39);
                    gm_b16_8 t69;
                    GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t68 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t70 = t69;
                    gm_tl874t2atnlh3 t71 = gm_l3;
                    void * t72 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t63, t64, t70, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t73 = 0;
                    F.ret = t72;
                    goto gm_fret;
                }
                goto ix46;
ie46:;
ix46:;
            }
            {
                uint8_t t75 = gm_l10;
                if (!t75) goto ie74;
                uint64_t t76 = gm_l6;
                uint64_t t77 = gm_l8;
                uint8_t t78 = ((t76 == t77));
                if (!t78) goto ie74;
                void * t79 = gm_l0;
                void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                gm_t88rcucg1b7uk t81 = gm_l7;
                gm_t88rcucg1b7uk t82 = gm_l9;
                uint8_t t83 = gm_f_lossless_a54o69s(t80, GM_CONV(gm_csguankko4lmhd, t81), GM_CONV(gm_csguankko4lmhd, t82));
                if (!t83) goto ie74;
                {
                }
                {
                    gm_tvdg94bk7aa22 t85;
                    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)56ULL));
                    gm_t88rcucg1b7uk t86 = gm_l1;
                    {
                        void * t88 = gm_l2;
                        gm_tn3r4rvlh0rsv t89;
                        GM_ST(void *, ((uint8_t *)&t89 + 0), t88);
                        gm_l12 = t89;
                    }
                    void * t90 = ((uint8_t *)&gm_l12);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 327, 88);
                    gm_b16_8 t91;
                    GM_ST(void *, (uint8_t *)&t91, (void *)(((uint8_t *)t90 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t92 = t91;
                    gm_tl874t2atnlh3 t93 = gm_l3;
                    void * t94 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t85, t86, t92, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t95 = 0;
                    F.ret = t94;
                    goto gm_fret;
                }
                goto ix74;
ie74:;
ix74:;
            }
            {
                gm_b16_8 t96;
                uint8_t t98 = gm_l10;
                if (!t98) goto ie97;
                uint64_t t99 = gm_l6;
                uint64_t t100 = gm_l8;
                uint8_t t101 = ((t99 == t100));
                if (!t101) goto ie97;
                gm_t6j6smj7qoctu t103;
                void * t104 = gm_l0;
                gm_t88rcucg1b7uk t105 = gm_l7;
                gm_b16_8 t106 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t103 + 0, t106);
                void * t107 = gm_l0;
                gm_t88rcucg1b7uk t108 = gm_l9;
                gm_b16_8 t109 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t107, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t103 + 16, t109);
                gm_tabb710d9mq2e t110;
                gm_f_format_mrlevsk((void *)&t110, (void *)NULL, cx, NULL, &t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_src7u9cfdjh7s0n, 328, 40);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                gm_b16_8 t112 = t111;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                t96 = t113;
                goto ix97;
ie97:;
                gm_b16_8 t115;
                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(0));
                gm_b16_8 t116 = t115;
                t96 = t116;
ix97:;
                gm_l13 = t96;
            }
            {
                void * t117 = gm_l0;
                gm_tl874t2atnlh3 t118 = gm_l3;
                gm_tfeab279c16rr t119;
                void * t120 = gm_l0;
                gm_t88rcucg1b7uk t121 = gm_l5;
                gm_b16_8 t122 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t120, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t122);
                void * t123 = gm_l0;
                gm_t88rcucg1b7uk t124 = gm_l1;
                gm_b16_8 t125 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t123, t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t119 + 16, t125);
                gm_b16_8 t126 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t119 + 32, t126);
                gm_tabb710d9mq2e t127;
                gm_f_format_d0lfl1k((void *)&t127, (void *)NULL, cx, NULL, &t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 8, gm_src7u9cfdjh7s0n, 329, 20);
                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                gm_b0_1 t130 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t117, t118, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t131 = gm_l0;
                void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
                gm_tl874t2atnlh3 t133 = gm_l3;
                void * t134 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t132, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t135 = 0;
                F.ret = t134;
                goto gm_fret;
            }
            goto ix31;
ie31:;
            {
                {
                    void * t138 = gm_l0;
                    gm_tl874t2atnlh3 t139 = gm_l3;
                    gm_t6j6smj7qoctu t140;
                    void * t141 = gm_l0;
                    gm_t88rcucg1b7uk t142 = gm_l5;
                    gm_b16_8 t143 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t141, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t140 + 0, t143);
                    void * t144 = gm_l0;
                    gm_t88rcucg1b7uk t145 = gm_l1;
                    gm_b16_8 t146 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t144, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t140 + 16, t146);
                    gm_tabb710d9mq2e t147;
                    gm_f_format_9rhpk5t((void *)&t147, (void *)NULL, cx, NULL, &t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 8, gm_src7u9cfdjh7s0n, 319, 24);
                    gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&t147) + 8));
                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                    gm_b0_1 t150 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t138, t139, t149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t151 = gm_l0;
                    void * t152 = GM_LD(void *, (((uint8_t *)t151) + 0));
                    gm_tl874t2atnlh3 t153 = gm_l3;
                    void * t154 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t152, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t155 = 0;
                    F.ret = t154;
                    goto gm_fret;
                }
            }
ix31:;
        }
        goto ix23;
ie23:;
        {
            {
                void * t158 = gm_l0;
                gm_tl874t2atnlh3 t159 = gm_l3;
                gm_t6j6smj7qoctu t160;
                void * t161 = gm_l0;
                gm_t88rcucg1b7uk t162 = gm_l5;
                gm_b16_8 t163 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t161, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t160 + 0, t163);
                void * t164 = gm_l0;
                gm_t88rcucg1b7uk t165 = gm_l1;
                gm_b16_8 t166 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t164, t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t160 + 16, t166);
                gm_tabb710d9mq2e t167;
                gm_f_format_9rhpk5t((void *)&t167, (void *)NULL, cx, NULL, &t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 8, gm_src7u9cfdjh7s0n, 315, 24);
                gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 8));
                gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                gm_b0_1 t170 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t158, t159, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t171 = gm_l0;
                void * t172 = GM_LD(void *, (((uint8_t *)t171) + 0));
                gm_tl874t2atnlh3 t173 = gm_l3;
                void * t174 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t175 = 0;
                F.ret = t174;
                goto gm_fret;
            }
        }
ix23:;
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
inline __attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_load_ssdvomk F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tlcm4lvu3g1a8 gm_l5; /* parts */
    void * gm_l6; /* s */
    gm_tn3r4rvlh0rsv gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 469u);
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
        void * t5 = gm_l4;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tlcm4lvu3g1a8 t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            gm_tl874t2atnlh3 t12 = gm_l3;
            void * t13 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l5 = t8;
    }
    {
        void * t16 = gm_l0;
        gm_b16_8 t17 = gm_l2;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqfrv3g5v9m2n));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
        gm_tl874t2atnlh3 t19 = gm_l3;
        uint8_t t20 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t16, t17, ((uint64_t)1ULL), t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!t20;
        if (!t21) goto ie15;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            gm_tl874t2atnlh3 t25 = gm_l3;
            void * t26 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t28 = gm_l0;
        gm_b16_8 t29 = gm_l2;
        void * t30 = gm_l4;
        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_t88rcucg1b7uk t32 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t30, ((uint8_t)1), t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t33 = gm_l3;
        void * t34 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t28, t29, ((uint64_t)0ULL), t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t34;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)58ULL));
        gm_t88rcucg1b7uk t36 = gm_l1;
        {
            void * t38 = gm_l6;
            gm_tn3r4rvlh0rsv t39;
            GM_ST(void *, ((uint8_t *)&t39 + 0), t38);
            gm_l7 = t39;
        }
        void * t40 = ((uint8_t *)&gm_l7);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 304, 34);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t40 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t42 = t41;
        gm_tl874t2atnlh3 t43 = gm_l3;
        void * t44 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t35, t36, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t45 = 0;
        F.ret = t44;
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
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_vector_method_hgn4sb1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* lane */
    uint8_t gm_l10; /* numeric */
    gm_tn3r4rvlh0rsv gm_l11; /* one */
    gm_tvdg94bk7aa22 gm_l12; /* op */
    void * gm_l13; /* y */
    gm_tvdg94bk7aa22 gm_l14; /* op */
    gm_t5fru0lp11n7t gm_l15; /* <temporary array> */
    void * gm_l17; /* x */
    gm_t88rcucg1b7uk gm_l18; /* xt */
    uint8_t gm_l19; /* same_lanes */
    gm_tlcm4lvu3g1a8 gm_l20; /* xp */
    void * gm_l21; /* y */
    gm_thmgeksj5pdth gm_l22; /* <temporary array> */
    void * gm_l23; /* y */
    gm_t5fru0lp11n7t gm_l24; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l25; /* f */
    gm_b16_8 gm_l26; /* msg */
    void * gm_l27; /* dst */
    gm_t5fru0lp11n7t gm_l28; /* <temporary array> */
    GM_STACK_CHECK(&F, 4326u);
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
        void * t5 = gm_l6;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tlcm4lvu3g1a8 t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            gm_tl874t2atnlh3 t12 = gm_l5;
            void * t13 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l7 = t8;
    }
    {
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t15;
    }
    {
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t16;
    }
    {
        uint8_t t17 = 0;
        void * t18 = gm_l6;
        gm_t88rcucg1b7uk t19 = gm_l9;
        uint8_t t20 = gm_f_is_integer_8d4f9ek(t18, GM_CONV(gm_csguankko4lmhd, t19));
        if (!t20) {
            void * t21 = gm_l6;
            gm_t88rcucg1b7uk t22 = gm_l9;
            uint8_t t23 = gm_f_is_float_3jk89qu(t21, GM_CONV(gm_csguankko4lmhd, t22));
            t17 = t23;
        } else t17 = 1;
        gm_l10 = t17;
    }
    {
        void * t24 = gm_l1;
        gm_tn3r4rvlh0rsv t25;
        GM_ST(void *, ((uint8_t *)&t25 + 0), t24);
        gm_l11 = t25;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sut92jpq3kk88, 10) == 0)) goto pa29;
        goto po28;
pa29:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbqoiitqtfv75, 10) == 0)) goto pa30;
        goto po28;
pa30:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sf1qp7r4ktsd1, 10) == 0)) goto pa31;
        goto po28;
pa31:;
        goto mn27;
po28:;
        {
            void * t34 = gm_l0;
            gm_b16_8 t35 = gm_l4;
            gm_b16_8 t36 = gm_l3;
            gm_tl874t2atnlh3 t37 = gm_l5;
            uint8_t t38 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t34, t35, ((uint64_t)0ULL), t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = (uint8_t)!t38;
            if (!t39) goto ie33;
            {
                void * t41 = gm_l0;
                void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
                gm_tl874t2atnlh3 t43 = gm_l5;
                void * t44 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            uint8_t t47 = gm_l10;
            uint8_t t48 = (uint8_t)!t47;
            if (!t48) goto ie46;
            {
                void * t50 = gm_l0;
                gm_b16_8 t51 = gm_l3;
                gm_t88rcucg1b7uk t52 = gm_l2;
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(13));
                gm_tl874t2atnlh3 t54 = gm_l5;
                void * t55 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t50, t51, t52, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t56 = 0;
                F.ret = t55;
                goto gm_fret;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            gm_tvdg94bk7aa22 t57;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sut92jpq3kk88, 10) == 0)) goto mn59;
            gm_tvdg94bk7aa22 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)46ULL));
            t57 = t60;
            goto me58;
mn59:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbqoiitqtfv75, 10) == 0)) goto mn61;
            gm_tvdg94bk7aa22 t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)47ULL));
            t57 = t62;
            goto me58;
mn61:;
            gm_tvdg94bk7aa22 t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)48ULL));
            t57 = t64;
            goto me58;
mn63:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 161, 31);
me58:;
            gm_l12 = t57;
        }
        {
            gm_tvdg94bk7aa22 t65 = gm_l12;
            gm_t88rcucg1b7uk t66 = gm_l9;
            void * t67 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 166, 38);
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t67 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_tl874t2atnlh3 t70 = gm_l5;
            void * t71 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t65, t66, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto me26;
mn27:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sj7s3tnnvob4e, 8) == 0)) goto pa75;
        goto po74;
pa75:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6lddausbiitu, 8) == 0)) goto pa76;
        goto po74;
pa76:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sh1to1i0u67lu, 8) == 0)) goto pa77;
        goto po74;
pa77:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sv8pauo1bhltl, 8) == 0)) goto pa78;
        goto po74;
pa78:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s1t08grtrn18m, 8) == 0)) goto pa79;
        goto po74;
pa79:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_scqrlin5l437v, 8) == 0)) goto pa80;
        goto po74;
pa80:;
        goto mn73;
po74:;
        {
            void * t83 = gm_l0;
            gm_b16_8 t84 = gm_l4;
            gm_b16_8 t85 = gm_l3;
            gm_tl874t2atnlh3 t86 = gm_l5;
            uint8_t t87 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t83, t84, ((uint64_t)1ULL), t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t88 = (uint8_t)!t87;
            if (!t88) goto ie82;
            {
                void * t90 = gm_l0;
                void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
                gm_tl874t2atnlh3 t92 = gm_l5;
                void * t93 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t94 = 0;
                F.ret = t93;
                goto gm_fret;
            }
            goto ix82;
ie82:;
ix82:;
        }
        {
            gm_b16_8 t96 = gm_l3;
            gm_t6j6smj7qoctu t97;
            gm_b16_8 t98;
            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sj7s3tnnvob4e));
            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 0, t98);
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s6lddausbiitu));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 16, t99);
            uint8_t t100 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t97) + 0, ((uint8_t *)&t96))) t100 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t97) + 16, ((uint8_t *)&t96))) t100 = 1;
            uint8_t t101 = (uint8_t)(!t100);
            if (!t101) goto ie95;
            uint8_t t102 = gm_l10;
            uint8_t t103 = (uint8_t)!t102;
            if (!t103) goto ie95;
            {
                void * t105 = gm_l0;
                gm_b16_8 t106 = gm_l3;
                gm_t88rcucg1b7uk t107 = gm_l2;
                gm_b16_8 t108;
                GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(13));
                gm_tl874t2atnlh3 t109 = gm_l5;
                void * t110 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t105, t106, t107, t108, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t111 = 0;
                F.ret = t110;
                goto gm_fret;
            }
            goto ix95;
ie95:;
ix95:;
        }
        {
            void * t112 = gm_l0;
            gm_b16_8 t113 = gm_l4;
            gm_t88rcucg1b7uk t114 = gm_l2;
            gm_tl874t2atnlh3 t115 = gm_l5;
            void * t116 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t112, t113, ((uint64_t)0ULL), t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t116;
        }
        {
            gm_tvdg94bk7aa22 t117;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sj7s3tnnvob4e, 8) == 0)) goto mn119;
            gm_tvdg94bk7aa22 t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)40ULL));
            t117 = t120;
            goto me118;
mn119:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6lddausbiitu, 8) == 0)) goto mn121;
            gm_tvdg94bk7aa22 t122;
            GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t122 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)41ULL));
            t117 = t122;
            goto me118;
mn121:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sh1to1i0u67lu, 8) == 0)) goto mn123;
            gm_tvdg94bk7aa22 t124;
            GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t124 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)42ULL));
            t117 = t124;
            goto me118;
mn123:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sv8pauo1bhltl, 8) == 0)) goto mn125;
            gm_tvdg94bk7aa22 t126;
            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)43ULL));
            t117 = t126;
            goto me118;
mn125:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s1t08grtrn18m, 8) == 0)) goto mn127;
            gm_tvdg94bk7aa22 t128;
            GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)44ULL));
            t117 = t128;
            goto me118;
mn127:;
            gm_tvdg94bk7aa22 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)45ULL));
            t117 = t130;
            goto me118;
mn129:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 172, 31);
me118:;
            gm_l14 = t117;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t131 = gm_l14;
            void * t132 = gm_l6;
            uint64_t t133 = gm_l8;
            gm_t88rcucg1b7uk t134 = gm_f_mask_ty_cj7ln0g((void *)NULL, cx, NULL, t132, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t136 = gm_l1;
                void * t137 = gm_l13;
                gm_t5fru0lp11n7t t138;
                GM_ST(void *, ((uint8_t *)&t138 + 0), t136);
                GM_ST(void *, ((uint8_t *)&t138 + 8), t137);
                gm_l15 = t138;
            }
            void * t139 = ((uint8_t *)&gm_l15);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src7u9cfdjh7s0n, 180, 51);
            gm_b16_8 t140;
            GM_ST(void *, (uint8_t *)&t140, (void *)(((uint8_t *)t139 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t141 = t140;
            gm_tl874t2atnlh3 t142 = gm_l5;
            void * t143 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t131, t134, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t144 = 0;
            F.ret = t143;
            goto gm_fret;
        }
        goto me26;
mn73:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s0nf1rg04f3m5, 3) == 0)) goto pa147;
        goto po146;
pa147:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_skftljk77e6mu, 3) == 0)) goto pa148;
        goto po146;
pa148:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_snc5ns3akncuq, 5) == 0)) goto pa149;
        goto po146;
pa149:;
        goto mn145;
po146:;
        {
            void * t152 = gm_l0;
            gm_b16_8 t153 = gm_l4;
            gm_b16_8 t154 = gm_l3;
            gm_tl874t2atnlh3 t155 = gm_l5;
            uint8_t t156 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t152, t153, ((uint64_t)0ULL), t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t157 = (uint8_t)!t156;
            if (!t157) goto ie151;
            {
                void * t159 = gm_l0;
                void * t160 = GM_LD(void *, (((uint8_t *)t159) + 0));
                gm_tl874t2atnlh3 t161 = gm_l5;
                void * t162 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t160, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t163 = 0;
                F.ret = t162;
                goto gm_fret;
            }
            goto ix151;
ie151:;
ix151:;
        }
        {
            void * t165 = gm_l6;
            gm_t88rcucg1b7uk t166 = gm_l9;
            uint8_t t167 = gm_f_is_bool_ajhjgeg(t165, GM_CONV(gm_csguankko4lmhd, t166));
            uint8_t t168 = (uint8_t)!t167;
            if (!t168) goto ie164;
            {
                void * t170 = gm_l0;
                gm_b16_8 t171 = gm_l3;
                gm_t88rcucg1b7uk t172 = gm_l2;
                gm_b16_8 t173;
                GM_ST(void *, (uint8_t *)&t173, (void *)(gm_s8guqcjcce9oc));
                GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(21));
                gm_tl874t2atnlh3 t174 = gm_l5;
                void * t175 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t170, t171, t172, t173, t174);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t176 = 0;
                F.ret = t175;
                goto gm_fret;
            }
            goto ix164;
ie164:;
ix164:;
        }
        {
            uint8_t t178 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s0nf1rg04f3m5, 3) == 0));
            if (!t178) goto ie177;
            {
                gm_tvdg94bk7aa22 t180;
                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t180 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)49ULL));
                void * t181 = gm_l6;
                gm_t88rcucg1b7uk t182 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t183 = ((uint8_t *)&gm_l11);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 185, 69);
                gm_b16_8 t184;
                GM_ST(void *, (uint8_t *)&t184, (void *)(((uint8_t *)t183 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
                gm_tl874t2atnlh3 t186 = gm_l5;
                void * t187 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t180, t182, t185, t186);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t188 = 0;
                F.ret = t187;
                goto gm_fret;
            }
            goto ix177;
ie177:;
ix177:;
        }
        {
            uint8_t t190 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_skftljk77e6mu, 3) == 0));
            if (!t190) goto ie189;
            {
                gm_tvdg94bk7aa22 t192;
                GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t192 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)50ULL));
                void * t193 = gm_l6;
                gm_t88rcucg1b7uk t194 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t195 = ((uint8_t *)&gm_l11);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 186, 69);
                gm_b16_8 t196;
                GM_ST(void *, (uint8_t *)&t196, (void *)(((uint8_t *)t195 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                gm_tl874t2atnlh3 t198 = gm_l5;
                void * t199 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t192, t194, t197, t198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t200 = 0;
                F.ret = t199;
                goto gm_fret;
            }
            goto ix189;
ie189:;
ix189:;
        }
        {
            gm_tvdg94bk7aa22 t201;
            GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t201 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t201 + 0), ((uint8_t)51ULL));
            void * t202 = gm_l6;
            gm_t88rcucg1b7uk t203 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t202);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t204 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 187, 56);
            gm_b16_8 t205;
            GM_ST(void *, (uint8_t *)&t205, (void *)(((uint8_t *)t204 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
            gm_tl874t2atnlh3 t207 = gm_l5;
            void * t208 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t201, t203, t206, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t209 = 0;
            F.ret = t208;
            goto gm_fret;
        }
        goto me26;
mn145:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_svgoe41piv341, 6) == 0)) goto mn210;
        {
            void * t213 = gm_l0;
            gm_b16_8 t214 = gm_l4;
            gm_b16_8 t215 = gm_l3;
            gm_tl874t2atnlh3 t216 = gm_l5;
            uint8_t t217 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t213, t214, ((uint64_t)2ULL), t215, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t218 = (uint8_t)!t217;
            if (!t218) goto ie212;
            {
                void * t220 = gm_l0;
                void * t221 = GM_LD(void *, (((uint8_t *)t220) + 0));
                gm_tl874t2atnlh3 t222 = gm_l5;
                void * t223 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t221, t222);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t224 = 0;
                F.ret = t223;
                goto gm_fret;
            }
            goto ix212;
ie212:;
ix212:;
        }
        {
            void * t226 = gm_l6;
            gm_t88rcucg1b7uk t227 = gm_l9;
            uint8_t t228 = gm_f_is_bool_ajhjgeg(t226, GM_CONV(gm_csguankko4lmhd, t227));
            uint8_t t229 = (uint8_t)!t228;
            if (!t229) goto ie225;
            {
                void * t231 = gm_l0;
                gm_b16_8 t232 = gm_l3;
                gm_t88rcucg1b7uk t233 = gm_l2;
                gm_b16_8 t234;
                GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s8guqcjcce9oc));
                GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(21));
                gm_tl874t2atnlh3 t235 = gm_l5;
                void * t236 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t231, t232, t233, t234, t235);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t237 = 0;
                F.ret = t236;
                goto gm_fret;
            }
            goto ix225;
ie225:;
ix225:;
        }
        {
            uint64_t t239 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t239 >= (uintptr_t)(t240)) gm_trap_index(t239, t240, gm_src7u9cfdjh7s0n, 192, 32);
            uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t239 * 144) + 0) + 0));
            if (t241 != ((uint8_t)0ULL)) goto ie238;
            gm_tc3oht4ksth9d t242 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t239 * 144) + 0) + 8));
            F.l16 = t242;
            {
                void * t244 = gm_l0;
                void * t245 = (void *)((uint8_t *)&F.l16);
                gm_tkn42u7na3t81 t246;
                memset(&t246, 0, sizeof t246);
                void * t247 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t244, t245, t246);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t247;
            }
            {
                void * t248 = gm_l0;
                void * t249 = (void *)(((uint8_t *)t248) + 96);
                void * t250 = gm_l0;
                void * t251 = GM_LD(void *, (((uint8_t *)t250) + 0));
                void * t252 = gm_l17;
                gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t252) + 0));
                gm_t88rcucg1b7uk t254 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t249, t251, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t254;
            }
            {
                void * t256 = gm_l6;
                gm_t88rcucg1b7uk t257 = gm_l18;
                uint8_t t258 = gm_f_is_error_8k27gd1(t256, GM_CONV(gm_csguankko4lmhd, t257));
                if (!t258) goto ie255;
                {
                    void * t260 = gm_l0;
                    void * t261 = GM_LD(void *, (((uint8_t *)t260) + 0));
                    gm_tl874t2atnlh3 t262 = gm_l5;
                    void * t263 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t261, t262);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t264 = 0;
                    F.ret = t263;
                    goto gm_fret;
                }
                goto ix255;
ie255:;
ix255:;
            }
            {
                gm_l19 = ((uint8_t)0);
            }
            {
                void * t266 = gm_l6;
                gm_t88rcucg1b7uk t267 = gm_l18;
                gm_tb7u19loo3j87 t268;
                gm_f_vector_parts_crrfopm((void *)&t268, t266, GM_CONV(gm_csguankko4lmhd, t267));
                if (!((((uint8_t *)&t268))[16] != 0)) goto ie265;
                gm_tlcm4lvu3g1a8 t269 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t268));
                gm_l20 = t269;
                {
                    uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                    uint64_t t272 = gm_l8;
                    uint8_t t273 = ((t271 == t272));
                    gm_l19 = t273;
                }
                goto ix265;
ie265:;
ix265:;
            }
            {
                uint8_t t275 = gm_l19;
                uint8_t t276 = (uint8_t)!t275;
                if (!t276) goto ie274;
                {
                    void * t278 = gm_l0;
                    void * t279 = gm_l17;
                    gm_tl874t2atnlh3 t280 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t279) + 72));
                    gm_t7neromnu8t62 t281;
                    uint64_t t282 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t281 + 0, t282);
                    void * t283 = gm_l0;
                    gm_t88rcucg1b7uk t284 = gm_l2;
                    gm_b16_8 t285 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t283, t284);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t281 + 8, t285);
                    void * t286 = gm_l0;
                    gm_t88rcucg1b7uk t287 = gm_l18;
                    gm_b16_8 t288 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t286, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t281 + 24, t288);
                    gm_tabb710d9mq2e t289;
                    gm_f_format_4nuejdf((void *)&t289, (void *)NULL, cx, NULL, &t281);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 8, gm_src7u9cfdjh7s0n, 199, 34);
                    gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&t289) + 8));
                    gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&t290));
                    gm_b0_1 t292 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t278, t280, t291);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t293 = gm_l0;
                    void * t294 = GM_LD(void *, (((uint8_t *)t293) + 0));
                    gm_tl874t2atnlh3 t295 = gm_l5;
                    void * t296 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t294, t295);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t297 = 0;
                    F.ret = t296;
                    goto gm_fret;
                }
                goto ix274;
ie274:;
ix274:;
            }
            {
                void * t298 = gm_l0;
                gm_b16_8 t299 = gm_l4;
                gm_t88rcucg1b7uk t300 = gm_l18;
                gm_tl874t2atnlh3 t301 = gm_l5;
                void * t302 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t298, t299, ((uint64_t)1ULL), t300, t301);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t302;
            }
            {
            }
            {
                gm_tvdg94bk7aa22 t303;
                GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t303 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t303 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)52ULL));
                gm_t88rcucg1b7uk t304 = gm_l18;
                {
                    void * t306 = gm_l1;
                    void * t307 = gm_l17;
                    void * t308 = gm_l21;
                    GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l22)) + 0, t306);
                    GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l22)) + 8, t307);
                    GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l22)) + 16, t308);
                }
                void * t309 = ((uint8_t *)&gm_l22);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_src7u9cfdjh7s0n, 203, 41);
                gm_b16_8 t310;
                GM_ST(void *, (uint8_t *)&t310, (void *)(((uint8_t *)t309 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t311 = t310;
                gm_tl874t2atnlh3 t312 = gm_l5;
                void * t313 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t303, t304, t311, t312);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t314 = 0;
                F.ret = t313;
                goto gm_fret;
            }
            goto ix238;
ie238:;
            {
                void * t316 = gm_l0;
                void * t317 = GM_LD(void *, (((uint8_t *)t316) + 0));
                gm_tl874t2atnlh3 t318 = gm_l5;
                void * t319 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t317, t318);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t320 = 0;
                F.ret = t319;
                goto gm_fret;
            }
ix238:;
        }
        goto me26;
mn210:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0)) goto pa323;
        goto po322;
pa323:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s2mtoab140jp6, 3) == 0)) goto pa324;
        goto po322;
pa324:;
        goto mn321;
po322:;
        {
            void * t327 = gm_l0;
            gm_b16_8 t328 = gm_l4;
            gm_b16_8 t329 = gm_l3;
            gm_tl874t2atnlh3 t330 = gm_l5;
            uint8_t t331 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t327, t328, ((uint64_t)1ULL), t329, t330);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t332 = (uint8_t)!t331;
            if (!t332) goto ie326;
            {
                void * t334 = gm_l0;
                void * t335 = GM_LD(void *, (((uint8_t *)t334) + 0));
                gm_tl874t2atnlh3 t336 = gm_l5;
                void * t337 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t335, t336);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t338 = 0;
                F.ret = t337;
                goto gm_fret;
            }
            goto ix326;
ie326:;
ix326:;
        }
        {
            uint8_t t340 = gm_l10;
            uint8_t t341 = (uint8_t)!t340;
            if (!t341) goto ie339;
            {
                void * t343 = gm_l0;
                gm_b16_8 t344 = gm_l3;
                gm_t88rcucg1b7uk t345 = gm_l2;
                gm_b16_8 t346;
                GM_ST(void *, (uint8_t *)&t346, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(13));
                gm_tl874t2atnlh3 t347 = gm_l5;
                void * t348 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t343, t344, t345, t346, t347);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t349 = 0;
                F.ret = t348;
                goto gm_fret;
            }
            goto ix339;
ie339:;
ix339:;
        }
        {
            void * t350 = gm_l0;
            gm_b16_8 t351 = gm_l4;
            gm_t88rcucg1b7uk t352 = gm_l2;
            gm_tl874t2atnlh3 t353 = gm_l5;
            void * t354 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t350, t351, ((uint64_t)0ULL), t352, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t354;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t355;
            uint8_t t357 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0));
            if (!t357) goto ie356;
            gm_tvdg94bk7aa22 t359;
            GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t359 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t359 + 0), ((uint8_t)53ULL));
            gm_tvdg94bk7aa22 t360 = t359;
            t355 = t360;
            goto ix356;
ie356:;
            gm_tvdg94bk7aa22 t362;
            GM_ST(uint8_t, (uint8_t *)&t362 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t362 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t362 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t362 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t362 + 0), ((uint8_t)54ULL));
            gm_tvdg94bk7aa22 t363 = t362;
            t355 = t363;
ix356:;
            gm_t88rcucg1b7uk t364 = gm_l2;
            {
                void * t366 = gm_l1;
                void * t367 = gm_l23;
                gm_t5fru0lp11n7t t368;
                GM_ST(void *, ((uint8_t *)&t368 + 0), t366);
                GM_ST(void *, ((uint8_t *)&t368 + 8), t367);
                gm_l24 = t368;
            }
            void * t369 = ((uint8_t *)&gm_l24);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src7u9cfdjh7s0n, 209, 100);
            gm_b16_8 t370;
            GM_ST(void *, (uint8_t *)&t370, (void *)(((uint8_t *)t369 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t371 = t370;
            gm_tl874t2atnlh3 t372 = gm_l5;
            void * t373 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t355, t364, t371, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t374 = 0;
            F.ret = t373;
            goto gm_fret;
        }
        goto me26;
mn321:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbl3pbs9195ho, 3) == 0)) goto mn375;
        {
            void * t378 = gm_l0;
            gm_b16_8 t379 = gm_l4;
            gm_b16_8 t380 = gm_l3;
            gm_tl874t2atnlh3 t381 = gm_l5;
            uint8_t t382 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t378, t379, ((uint64_t)0ULL), t380, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t383 = (uint8_t)!t382;
            if (!t383) goto ie377;
            {
                void * t385 = gm_l0;
                void * t386 = GM_LD(void *, (((uint8_t *)t385) + 0));
                gm_tl874t2atnlh3 t387 = gm_l5;
                void * t388 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t386, t387);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t389 = 0;
                F.ret = t388;
                goto gm_fret;
            }
            goto ix377;
ie377:;
ix377:;
        }
        {
            uint8_t t391 = 0;
            void * t392 = gm_l6;
            gm_t88rcucg1b7uk t393 = gm_l9;
            uint8_t t394 = gm_f_is_signed_integer_s66djm7(t392, GM_CONV(gm_csguankko4lmhd, t393));
            if (!t394) {
                void * t395 = gm_l6;
                gm_t88rcucg1b7uk t396 = gm_l9;
                uint8_t t397 = gm_f_is_float_3jk89qu(t395, GM_CONV(gm_csguankko4lmhd, t396));
                t391 = t397;
            } else t391 = 1;
            uint8_t t398 = (uint8_t)!t391;
            if (!t398) goto ie390;
            {
                void * t400 = gm_l0;
                gm_b16_8 t401 = gm_l3;
                gm_t88rcucg1b7uk t402 = gm_l2;
                gm_b16_8 t403;
                GM_ST(void *, (uint8_t *)&t403, (void *)(gm_salin070nim24));
                GM_ST(uintptr_t, (uint8_t *)&t403 + 8, (uintptr_t)(29));
                gm_tl874t2atnlh3 t404 = gm_l5;
                void * t405 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t400, t401, t402, t403, t404);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t406 = 0;
                F.ret = t405;
                goto gm_fret;
            }
            goto ix390;
ie390:;
ix390:;
        }
        {
            gm_tvdg94bk7aa22 t407;
            GM_ST(uint8_t, (uint8_t *)&t407 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t407 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t407 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t407 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t407 + 0), ((uint8_t)55ULL));
            gm_t88rcucg1b7uk t408 = gm_l2;
            void * t409 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 214, 42);
            gm_b16_8 t410;
            GM_ST(void *, (uint8_t *)&t410, (void *)(((uint8_t *)t409 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t410 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t411 = GM_LD(gm_b16_8, ((uint8_t *)&t410));
            gm_tl874t2atnlh3 t412 = gm_l5;
            void * t413 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t407, t408, t411, t412);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t414 = 0;
            F.ret = t413;
            goto gm_fret;
        }
        goto me26;
mn375:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sfrjcsmisriv1, 2) == 0)) goto mn415;
        {
            void * t418 = gm_l0;
            gm_b16_8 t419 = gm_l4;
            gm_b16_8 t420 = gm_l3;
            gm_tl874t2atnlh3 t421 = gm_l5;
            uint8_t t422 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t418, t419, ((uint64_t)1ULL), t420, t421);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t423 = (uint8_t)!t422;
            if (!t423) goto ie417;
            {
                void * t425 = gm_l0;
                void * t426 = GM_LD(void *, (((uint8_t *)t425) + 0));
                gm_tl874t2atnlh3 t427 = gm_l5;
                void * t428 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t426, t427);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t429 = 0;
                F.ret = t428;
                goto gm_fret;
            }
            goto ix417;
ie417:;
ix417:;
        }
        {
            void * t430 = gm_l0;
            gm_b16_8 t431 = gm_l4;
            gm_tl874t2atnlh3 t432 = gm_l5;
            gm_tkn42u7na3t81 t433 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t430, t431, ((uint64_t)0ULL), t432);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t434;
            if (((((uint8_t *)&t433))[4] != 0)) {
                gm_t88rcucg1b7uk t435 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t433) + 0));
                t434 = t435;
            } else {
                void * t436 = gm_l0;
                void * t437 = GM_LD(void *, (((uint8_t *)t436) + 0));
                gm_tl874t2atnlh3 t438 = gm_l5;
                void * t439 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t437, t438);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t440 = 0;
                F.ret = t439;
                goto gm_fret;
            }
            gm_l25 = t434;
        }
        {
            uint8_t t442 = 0;
            uint8_t t443 = 0;
            void * t444 = gm_l6;
            gm_t88rcucg1b7uk t445 = gm_l25;
            uint8_t t446 = gm_f_is_float_3jk89qu(t444, GM_CONV(gm_csguankko4lmhd, t445));
            uint8_t t447 = (uint8_t)!t446;
            if (!t447) {
                void * t448 = gm_l6;
                gm_t88rcucg1b7uk t449 = gm_l25;
                uint8_t t450 = gm_f_lane_type_ok_rp8uq1p(t448, GM_CONV(gm_csguankko4lmhd, t449));
                uint8_t t451 = (uint8_t)!t450;
                t443 = t451;
            } else t443 = 1;
            if (!t443) {
                uint8_t t452 = gm_l10;
                uint8_t t453 = (uint8_t)!t452;
                t442 = t453;
            } else t442 = 1;
            if (!t442) goto ie441;
            {
                void * t455 = gm_l0;
                gm_tl874t2atnlh3 t456 = gm_l5;
                gm_t6j6smj7qoctu t457;
                void * t458 = gm_l0;
                gm_t88rcucg1b7uk t459 = gm_l9;
                gm_b16_8 t460 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t458, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t457 + 0, t460);
                void * t461 = gm_l0;
                gm_t88rcucg1b7uk t462 = gm_l25;
                gm_b16_8 t463 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t461, t462);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t457 + 16, t463);
                gm_tabb710d9mq2e t464;
                gm_f_format_m1f4vhf((void *)&t464, (void *)NULL, cx, NULL, &t457);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t464))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t464) + 8, gm_src7u9cfdjh7s0n, 220, 32);
                gm_b16_8 t465 = GM_LD(gm_b16_8, (((uint8_t *)&t464) + 8));
                gm_b16_8 t466 = GM_LD(gm_b16_8, ((uint8_t *)&t465));
                gm_b0_1 t467 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t455, t456, t466);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t468 = gm_l0;
                void * t469 = GM_LD(void *, (((uint8_t *)t468) + 0));
                gm_tl874t2atnlh3 t470 = gm_l5;
                void * t471 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t469, t470);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t472 = 0;
                F.ret = t471;
                goto gm_fret;
            }
            goto ix441;
ie441:;
ix441:;
        }
        {
            void * t474 = gm_l0;
            void * t475 = GM_LD(void *, (((uint8_t *)t474) + 0));
            uint64_t t476 = gm_l8;
            gm_t88rcucg1b7uk t477 = gm_l25;
            gm_th2lqbo77pvsc t478;
            gm_f_vector_shape_error_3invhhe((void *)&t478, (void *)NULL, cx, NULL, t475, t476, t477);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t478))[16] != 0)) goto ie473;
            gm_b16_8 t479 = GM_LD(gm_b16_8, ((uint8_t *)&t478));
            gm_l26 = t479;
            {
                void * t481 = gm_l0;
                gm_tl874t2atnlh3 t482 = gm_l5;
                gm_b16_8 t483 = gm_l26;
                gm_b0_1 t484 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t481, t482, t483);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t485 = gm_l0;
                void * t486 = GM_LD(void *, (((uint8_t *)t485) + 0));
                gm_tl874t2atnlh3 t487 = gm_l5;
                void * t488 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t486, t487);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t489 = 0;
                F.ret = t488;
                goto gm_fret;
            }
            goto ix473;
ie473:;
ix473:;
        }
        {
            gm_tvdg94bk7aa22 t490;
            GM_ST(uint8_t, (uint8_t *)&t490 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t490 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t490 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t490 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t490 + 0), ((uint8_t)56ULL));
            void * t491 = gm_l6;
            uint64_t t492 = gm_l8;
            gm_t88rcucg1b7uk t493 = gm_l25;
            gm_t88rcucg1b7uk t494 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t491, t492, t493);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t495 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 227, 60);
            gm_b16_8 t496;
            GM_ST(void *, (uint8_t *)&t496, (void *)(((uint8_t *)t495 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t496 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t497 = GM_LD(gm_b16_8, ((uint8_t *)&t496));
            gm_tl874t2atnlh3 t498 = gm_l5;
            void * t499 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t490, t494, t497, t498);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t500 = 0;
            F.ret = t499;
            goto gm_fret;
        }
        goto me26;
mn415:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_salpeh0v5rqdf, 7) == 0)) goto mn501;
        void * t502 = gm_l0;
        void * t503 = gm_l1;
        gm_t88rcucg1b7uk t504 = gm_l2;
        gm_b16_8 t505 = gm_l4;
        gm_tl874t2atnlh3 t506 = gm_l5;
        void * t507 = gm_f_vector_shuffle_5ime7a7((void *)NULL, cx, NULL, t502, t503, t504, t505, t506);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t508 = 0;
        F.ret = t507;
        goto gm_fret;
        goto me26;
mn501:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s0ugop4l95o1j, 5) == 0)) goto mn509;
        {
            void * t512 = gm_l0;
            gm_b16_8 t513 = gm_l4;
            gm_b16_8 t514 = gm_l3;
            gm_tl874t2atnlh3 t515 = gm_l5;
            uint8_t t516 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t512, t513, ((uint64_t)1ULL), t514, t515);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t517 = (uint8_t)!t516;
            if (!t517) goto ie511;
            {
                void * t519 = gm_l0;
                void * t520 = GM_LD(void *, (((uint8_t *)t519) + 0));
                gm_tl874t2atnlh3 t521 = gm_l5;
                void * t522 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t520, t521);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t523 = 0;
                F.ret = t522;
                goto gm_fret;
            }
            goto ix511;
ie511:;
ix511:;
        }
        {
            void * t524 = gm_l0;
            gm_b16_8 t525 = gm_l4;
            void * t526 = gm_l6;
            gm_t88rcucg1b7uk t527 = gm_l9;
            gm_t88rcucg1b7uk t528 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t526, ((uint8_t)0), t527);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t529 = gm_l5;
            void * t530 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t524, t525, ((uint64_t)0ULL), t528, t529);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t530;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t531;
            GM_ST(uint8_t, (uint8_t *)&t531 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t531 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t531 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t531 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t531 + 0), ((uint8_t)59ULL));
            void * t532 = gm_l6;
            gm_t88rcucg1b7uk t533 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t532);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t535 = gm_l1;
                void * t536 = gm_l27;
                gm_t5fru0lp11n7t t537;
                GM_ST(void *, ((uint8_t *)&t537 + 0), t535);
                GM_ST(void *, ((uint8_t *)&t537 + 8), t536);
                gm_l28 = t537;
            }
            void * t538 = ((uint8_t *)&gm_l28);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src7u9cfdjh7s0n, 233, 51);
            gm_b16_8 t539;
            GM_ST(void *, (uint8_t *)&t539, (void *)(((uint8_t *)t538 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t539 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t540 = t539;
            gm_tl874t2atnlh3 t541 = gm_l5;
            void * t542 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t531, t533, t540, t541);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t543 = 0;
            F.ret = t542;
            goto gm_fret;
        }
        goto me26;
mn509:;
        goto me26;
mn544:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 157, 5);
me26:;
    }
    {
        void * t546 = gm_l0;
        gm_tl874t2atnlh3 t547 = gm_l5;
        gm_b16_8 t548 = gm_l3;
        gm_tnpifv20irnph t549;
        GM_ST(gm_b16_8, ((uint8_t *)&t549 + 0), t548);
        gm_tabb710d9mq2e t550;
        gm_f_format_gsgjkse((void *)&t550, (void *)NULL, cx, NULL, t549);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t550))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t550) + 8, gm_src7u9cfdjh7s0n, 237, 20);
        gm_b16_8 t551 = GM_LD(gm_b16_8, (((uint8_t *)&t550) + 8));
        gm_b16_8 t552 = GM_LD(gm_b16_8, ((uint8_t *)&t551));
        gm_b0_1 t553 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t546, t547, t552);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t554 = gm_l0;
        void * t555 = GM_LD(void *, (((uint8_t *)t554) + 0));
        gm_tl874t2atnlh3 t556 = gm_l5;
        void * t557 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t555, t556);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t558 = 0;
        F.ret = t557;
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
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_shuffle_5ime7a7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    uint64_t gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* usize_t */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l14; /* v */
    gm_tf5srh4mp4b3i gm_l15; /* k */
    uint8_t gm_l16; /* int_ok */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_tl874t2atnlh3 gm_l19; /* at */
    void * gm_l20; /* cell */
    unsigned __int128 gm_l21; /* n */
    uint64_t gm_l22; /* x */
    uint64_t gm_l24; /* m */
    gm_b16_8 gm_l25; /* msg */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    GM_STACK_CHECK(&F, 2825u);
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
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l5;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tlcm4lvu3g1a8 t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
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
        gm_l6 = t8;
    }
    {
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
        gm_l7 = t15;
    }
    {
        void * t16 = gm_l5;
        gm_t88rcucg1b7uk t17 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t17;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t19 = gm_l3;
            gm_l10 = t19;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t21 = gm_l11;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t23 = (t21 < t22);
                if (!t23) goto lb20;
                {
                    uint64_t t26 = gm_l11;
                    uint64_t t27 = (uintptr_t)t26;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t7te2tdpebbt4 t31 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t27 * 144));
                    F.l12 = t31;
                    {
                        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                        if (t34 != ((uint8_t)0ULL)) goto ie33;
                        gm_tc3oht4ksth9d t35 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l12) + 0) + 8));
                        F.l13 = t35;
                        {
                            void * t37 = gm_l0;
                            void * t38 = (void *)((uint8_t *)&F.l13);
                            gm_t88rcucg1b7uk t39 = gm_l8;
                            gm_tkn42u7na3t81 t40;
                            GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40), t39);
                            ((uint8_t *)&t40)[4] = 1;
                            void * t41 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t37, t38, t40);
                            if (gm_unw.kind) {
                                goto gm_ll24;
                            }
                            gm_l14 = t41;
                        }
                        {
                            gm_tf5srh4mp4b3i t42;
                            memset(&t42, 0, sizeof t42);
                            gm_l15 = t42;
                        }
                        {
                            gm_l16 = ((uint8_t)0);
                        }
                        {
                            void * t44 = gm_l14;
                            uint8_t t45;
                            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
                            if (t48 != ((uint8_t)0ULL)) goto mn47;
                            t45 = ((uint8_t)1);
                            goto me46;
mn47:;
                            t45 = ((uint8_t)0);
                            goto me46;
mn49:;
                            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 12);
me46:;
                            if (!t45) goto ie43;
                            {
                                void * t51 = gm_l0;
                                void * t52 = (void *)(((uint8_t *)t51) + 96);
                                void * t53 = gm_l5;
                                void * t54 = gm_l14;
                                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
                                gm_t88rcucg1b7uk t56 = gm_l8;
                                gm_t9qkh0rsooc8d t57 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t52, t53, t55, t56);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                uint8_t t58;
                                if (!((((uint8_t *)&t57))[0] != 0)) goto mn60;
                                t58 = ((uint8_t)1);
                                goto me59;
mn60:;
                                t58 = ((uint8_t)0);
                                goto me59;
mn61:;
                                gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 46);
me59:;
                                uint8_t t62 = (uint8_t)!t58;
                                gm_l16 = t62;
                            }
                            goto ix43;
ie43:;
ix43:;
                        }
                        {
                            uint8_t t64 = gm_l16;
                            if (!t64) goto ie63;
                            {
                                void * t67 = gm_l14;
                                uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
                                if (t68 != ((uint8_t)0ULL)) goto ie66;
                                gm_tbci5ospk5nnn t69 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t67) + 8) + 8));
                                F.l17 = t69;
                                {
                                    gm_tf5srh4mp4b3i t71 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
                                    gm_l15 = t71;
                                }
                                goto ix66;
ie66:;
                                {
                                    void * t73 = gm_l0;
                                    void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
                                    gm_tl874t2atnlh3 t75 = gm_l4;
                                    void * t76 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t75);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    uint8_t t77 = 0;
                                    F.ret = t76;
                                    goto gm_fret;
                                }
ix66:;
                            }
                            goto ix63;
ie63:;
                            void * t79 = gm_l5;
                            void * t80 = gm_l14;
                            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 0));
                            uint8_t t82 = gm_f_is_error_8k27gd1(t79, GM_CONV(gm_csguankko4lmhd, t81));
                            if (!t82) goto ie78;
                            {
                                void * t84 = gm_l0;
                                void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
                                gm_tl874t2atnlh3 t86 = gm_l4;
                                void * t87 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t85, t86);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                uint8_t t88 = 0;
                                F.ret = t87;
                                goto gm_fret;
                            }
                            goto ix78;
ie78:;
                            void * t90 = gm_l0;
                            void * t91 = (void *)(((uint8_t *)t90) + 96);
                            void * t92 = gm_l0;
                            void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
                            void * t94 = gm_l14;
                            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 0));
                            gm_t88rcucg1b7uk t96 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t91, t93, t95);
                            if (gm_unw.kind) {
                                goto gm_ll24;
                            }
                            gm_t88rcucg1b7uk t97 = gm_l8;
                            uint8_t t98 = (!gm_eq9m6mvtmgde(((uint8_t *)&t96), ((uint8_t *)&t97)));
                            if (!t98) goto ie89;
                            {
                                void * t100 = gm_l0;
                                void * t101 = (void *)(((uint8_t *)t100) + 96);
                                void * t102 = gm_l0;
                                void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
                                void * t104 = gm_l14;
                                gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + 0));
                                gm_t88rcucg1b7uk t106 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t101, t103, t105);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                gm_l18 = t106;
                            }
                            {
                                void * t107 = gm_l0;
                                void * t108 = gm_l14;
                                gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t108) + 72));
                                void * t110 = gm_l0;
                                gm_t88rcucg1b7uk t111 = gm_l18;
                                gm_b16_8 t112 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t110, t111);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                gm_tnpifv20irnph t113;
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t112);
                                gm_tabb710d9mq2e t114;
                                gm_f_format_jrngvku((void *)&t114, (void *)NULL, cx, NULL, t113);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_src7u9cfdjh7s0n, 265, 30);
                                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                                gm_b0_1 t117 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t107, t109, t116);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                            }
                            {
                                void * t118 = gm_l0;
                                void * t119 = GM_LD(void *, (((uint8_t *)t118) + 0));
                                gm_tl874t2atnlh3 t120 = gm_l4;
                                void * t121 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t119, t120);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                uint8_t t122 = 0;
                                F.ret = t121;
                                goto gm_fret;
                            }
                            goto ix89;
ie89:;
                            void * t124 = gm_l14;
                            uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t124) + 8) + 0));
                            if (t125 != ((uint8_t)14ULL)) goto ie123;
                            gm_tl874t2atnlh3 t126 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t124) + 8) + 8) + 16));
                            gm_l19 = t126;
                            {
                                void * t129 = gm_l0;
                                void * t130 = GM_LD(void *, (((uint8_t *)t129) + 0));
                                gm_tl874t2atnlh3 t131 = gm_l19;
                                void * t132 = gm_l14;
                                gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t132) + 0));
                                void * t134 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t130, t131, t133);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                if (!(GM_LD(void *, ((uint8_t *)&t134)) != 0)) goto ie128;
                                void * t135 = GM_LD(void *, ((uint8_t *)&t134));
                                gm_l20 = t135;
                                {
                                    void * t138 = gm_l20;
                                    uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t138) + 0) + 0));
                                    if (t139 != ((uint8_t)0ULL)) goto ie137;
                                    unsigned __int128 t140 = GM_LD(unsigned __int128, ((((uint8_t *)t138) + 0) + 16));
                                    gm_l21 = t140;
                                    {
                                        unsigned __int128 t142 = gm_l21;
                                        gm_tf5srh4mp4b3i t143;
                                        uint64_t t144 = (uint64_t)t142;
                                        gm_tf5srh4mp4b3i t145;
                                        GM_ST(uint8_t, (uint8_t *)&t145 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t145 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t145 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t145), t144);
                                        ((uint8_t *)&t145)[8] = 1;
                                        gm_tf5srh4mp4b3i t146;
                                        memset(&t146, 0, sizeof t146);
                                        t143 = ((unsigned __int128)(t142) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t145 : t146;
                                        gm_l15 = t143;
                                    }
                                    goto ix137;
ie137:;
ix137:;
                                }
                                goto ix128;
ie128:;
ix128:;
                            }
                            goto ix123;
ie123:;
ix123:;
ix89:;
ix78:;
ix63:;
                        }
                        {
                            if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie147;
                            uint64_t t148 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            gm_l22 = t148;
                            {
                                uint64_t t151 = gm_l22;
                                uint64_t t152 = gm_l7;
                                uint8_t t153 = (t151 >= t152);
                                if (!t153) goto ie150;
                                {
                                    void * t155 = gm_l0;
                                    void * t156 = gm_l14;
                                    gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 72));
                                    gm_tob1snuii5bme t158;
                                    uint64_t t159 = gm_l22;
                                    GM_ST(uint64_t, (uint8_t *)&t158 + 0, t159);
                                    void * t160 = gm_l0;
                                    gm_t88rcucg1b7uk t161 = gm_l2;
                                    gm_b16_8 t162 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t160, t161);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t158 + 8, t162);
                                    uint64_t t163 = gm_l7;
                                    uint64_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint64_t)1ULL), &r164);
                                    if (o165) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 274, 111);
                                    uint64_t t166 = r164;
                                    GM_ST(uint64_t, (uint8_t *)&t158 + 24, t166);
                                    gm_tabb710d9mq2e t167;
                                    gm_f_format_jm4dd7q((void *)&t167, (void *)NULL, cx, NULL, &t158);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 8, gm_src7u9cfdjh7s0n, 274, 34);
                                    gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 8));
                                    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                                    gm_b0_1 t170 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t155, t157, t169);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                }
                                {
                                    void * t171 = gm_l0;
                                    void * t172 = GM_LD(void *, (((uint8_t *)t171) + 0));
                                    gm_tl874t2atnlh3 t173 = gm_l4;
                                    void * t174 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t173);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    uint8_t t175 = 0;
                                    F.ret = t174;
                                    goto gm_fret;
                                }
                                goto ix150;
ie150:;
ix150:;
                            }
                            {
                                void * t176 = (void *)((uint8_t *)&F.l9);
                                gm_t88rcucg1b7uk t177 = gm_l8;
                                uint64_t t178 = gm_l22;
                                gm_tbci5ospk5nnn t179;
                                gm_f_from_u64_ud13q5v((void *)&t179, (void *)NULL, cx, NULL, t178);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                gm_teti411eujvf4 t180;
                                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t180 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t180 + 8), t179);
                                void * t181 = gm_l14;
                                gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t181) + 72));
                                void * t183 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t177, &t180, t182);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                gm_tftb1odc739t5 t184 = gm_f_push_d943vgp_2(t176, &(*(gm_t69m56meqefs8 *)((uint8_t *)t183)));
                                if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_src7u9cfdjh7s0n, 277, 13);
                                gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
                            }
                            goto ix147;
ie147:;
                            {
                                void * t188 = gm_l14;
                                uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t188) + 8) + 0));
                                if (t189 != ((uint8_t)0ULL)) goto ie187;
                                gm_tbci5ospk5nnn t190 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t188) + 8) + 8));
                                F.l23 = t190;
                                {
                                    void * t192 = gm_l0;
                                    void * t193 = gm_l14;
                                    gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t193) + 72));
                                    gm_tftksr4op114e t195;
                                    gm_b16_8 t196 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t196);
                                    void * t197 = gm_l0;
                                    gm_t88rcucg1b7uk t198 = gm_l2;
                                    gm_b16_8 t199 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t197, t198);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t195 + 16, t199);
                                    uint64_t t200 = gm_l7;
                                    uint64_t r201; bool o202 = __builtin_sub_overflow(t200, ((uint64_t)1ULL), &r201);
                                    if (o202) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 280, 124);
                                    uint64_t t203 = r201;
                                    GM_ST(uint64_t, (uint8_t *)&t195 + 32, t203);
                                    gm_tabb710d9mq2e t204;
                                    gm_f_format_8eujoej((void *)&t204, (void *)NULL, cx, NULL, &t195);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 8, gm_src7u9cfdjh7s0n, 280, 34);
                                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&t204) + 8));
                                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                                    gm_b0_1 t207 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t192, t194, t206);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                }
                                {
                                    void * t208 = gm_l0;
                                    void * t209 = GM_LD(void *, (((uint8_t *)t208) + 0));
                                    gm_tl874t2atnlh3 t210 = gm_l4;
                                    void * t211 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t209, t210);
                                    if (gm_unw.kind) {
                                        goto gm_ll24;
                                    }
                                    uint8_t t212 = 0;
                                    F.ret = t211;
                                    goto gm_fret;
                                }
                                goto ix187;
ie187:;
ix187:;
                            }
                            {
                                void * t213 = gm_l0;
                                void * t214 = gm_l14;
                                gm_tl874t2atnlh3 t215 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t214) + 72));
                                gm_b16_8 t216;
                                GM_ST(void *, (uint8_t *)&t216, (void *)(gm_skdalh0o0hret));
                                GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(58));
                                gm_b0_1 t217 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t213, t215, t216);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                            }
                            {
                                void * t218 = gm_l0;
                                void * t219 = GM_LD(void *, (((uint8_t *)t218) + 0));
                                gm_tl874t2atnlh3 t220 = gm_l4;
                                void * t221 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t219, t220);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                uint8_t t222 = 0;
                                F.ret = t221;
                                goto gm_fret;
                            }
ix147:;
                        }
                        goto ix33;
ie33:;
                        {
                            {
                                void * t225 = gm_l0;
                                gm_tl874t2atnlh3 t226 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                                gm_b16_8 t227;
                                GM_ST(void *, (uint8_t *)&t227, (void *)(gm_sioam4mlrjuar));
                                GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(51));
                                gm_b0_1 t228 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t225, t226, t227);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                            }
                            {
                                void * t229 = gm_l0;
                                void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
                                gm_tl874t2atnlh3 t231 = gm_l4;
                                void * t232 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t230, t231);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                uint8_t t233 = 0;
                                F.ret = t232;
                                goto gm_fret;
                            }
                        }
ix33:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 249, 5);
me29:;
                }
lc20:;
                {
                    uint64_t t235 = gm_l11;
                    uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                    uint64_t t238 = r236;
                    gm_l11 = t238;
                }
            }
gm_ll24:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
            }
            goto gm_lad;
lb20:;
        }
    }
    {
        uint64_t t239 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint64_t t240 = (uint64_t)t239;
        gm_l24 = t240;
    }
    {
        void * t242 = gm_l0;
        void * t243 = GM_LD(void *, (((uint8_t *)t242) + 0));
        uint64_t t244 = gm_l24;
        gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_th2lqbo77pvsc t246;
        gm_f_vector_shape_error_3invhhe((void *)&t246, (void *)NULL, cx, NULL, t243, t244, t245);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t246))[16] != 0)) goto ie241;
        gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
        gm_l25 = t247;
        {
            void * t249 = gm_l0;
            gm_tl874t2atnlh3 t250 = gm_l4;
            gm_b16_8 t251 = gm_l25;
            gm_tnpifv20irnph t252;
            GM_ST(gm_b16_8, ((uint8_t *)&t252 + 0), t251);
            gm_tabb710d9mq2e t253;
            gm_f_format_7cgva50((void *)&t253, (void *)NULL, cx, NULL, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t253))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t253) + 8, gm_src7u9cfdjh7s0n, 289, 24);
            gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&t253) + 8));
            gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
            gm_b0_1 t256 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t249, t250, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t257 = gm_l0;
            void * t258 = GM_LD(void *, (((uint8_t *)t257) + 0));
            gm_tl874t2atnlh3 t259 = gm_l4;
            void * t260 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t258, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t261 = 0;
            F.ret = t260;
            goto gm_fret;
        }
        goto ix241;
ie241:;
ix241:;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t262 = (void *)((uint8_t *)&F.l26);
        void * t263 = gm_l1;
        gm_tftb1odc739t5 t264 = gm_f_push_d943vgp_2(t262, &(*(gm_t69m56meqefs8 *)((uint8_t *)t263)));
        if (((((uint8_t *)&t264))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t264) + 1, gm_src7u9cfdjh7s0n, 293, 5);
        gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t264) + 1));
    }
    {
        {
            gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_l27 = t267;
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t269 = gm_l28;
                uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                uint8_t t271 = (t269 < t270);
                if (!t271) goto lb268;
                {
                    uint64_t t274 = gm_l28;
                    uint64_t t275 = (uintptr_t)t274;
                    uint64_t t276 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    gm_t69m56meqefs8 t279 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t275 * 88));
                    F.l29 = t279;
                    {
                        void * t281 = (void *)((uint8_t *)&F.l26);
                        gm_tftb1odc739t5 t282 = gm_f_push_d943vgp_2(t281, &(*(gm_t69m56meqefs8 *)((uint8_t *)&F.l29)));
                        if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 1, gm_src7u9cfdjh7s0n, 294, 26);
                        gm_b0_1 t283 = GM_LD(gm_b0_1, (((uint8_t *)&t282) + 1));
                    }
                    goto me277;
mn278:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 294, 5);
me277:;
                }
lc268:;
                {
                    uint64_t t285 = gm_l28;
                    uint64_t r286; bool o287 = __builtin_add_overflow(t285, ((uint64_t)1ULL), &r286);
                    uint64_t t288 = r286;
                    gm_l28 = t288;
                }
            }
lb268:;
        }
    }
    {
        void * t289 = gm_l5;
        uint64_t t290 = gm_l24;
        gm_t88rcucg1b7uk t291 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_t88rcucg1b7uk t292 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t289, t290, t291);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt8o5kebou62r t293;
        memset((uint8_t *)&t293 + 12, 0, 4);
        gm_tvdg94bk7aa22 t294;
        GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t294 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t293 + 0, t294);
        gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t293 + 16, t295);
        gm_teti411eujvf4 t296;
        GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t296 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t296 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t296 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t296 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t296 + 8), t293);
        gm_tl874t2atnlh3 t297 = gm_l4;
        void * t298 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t292, &t296, t297);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t299 = 0;
        F.ret = t298;
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
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_unary_2mbe97t F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    gm_t88rcucg1b7uk gm_l7; /* lane */
    gm_b16_8 gm_l8; /* hint */
    gm_b16_8 gm_l9; /* s */
    GM_STACK_CHECK(&F, 1115u);
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
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l4;
        gm_t88rcucg1b7uk t13 = gm_l5;
        gm_tb7u19loo3j87 t14;
        gm_f_vector_parts_crrfopm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        gm_tlcm4lvu3g1a8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t16 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_tl874t2atnlh3 t19 = gm_l3;
            void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        gm_l6 = t15;
    }
    {
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t22;
    }
    {
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma25; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma27; case ((uint8_t)5ULL): goto ma26; default: goto ma27; }
ma24:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t31 != ((uint8_t)0ULL)) goto mn30;
        uint8_t t33 = 0;
        void * t34 = gm_l4;
        gm_t88rcucg1b7uk t35 = gm_l7;
        uint8_t t36 = gm_f_is_signed_integer_s66djm7(t34, GM_CONV(gm_csguankko4lmhd, t35));
        if (!t36) {
            void * t37 = gm_l4;
            gm_t88rcucg1b7uk t38 = gm_l7;
            uint8_t t39 = gm_f_is_float_3jk89qu(t37, GM_CONV(gm_csguankko4lmhd, t38));
            t33 = t39;
        } else t33 = 1;
        if (!t33) goto ie32;
        {
            gm_t88rcucg1b7uk t41 = gm_l5;
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
            void * t43 = gm_l2;
            gm_t8ghgl2jj0qha t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 0), t42);
            GM_ST(void *, ((uint8_t *)&t44 + 8), t43);
            gm_teti411eujvf4 t45;
            memset(&t45, 0, sizeof t45);
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t45 + 8), t44);
            gm_tl874t2atnlh3 t46 = gm_l3;
            void * t47 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t41, &t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
        goto me23;
mn30:;
ma25:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t50 != ((uint8_t)1ULL)) goto mn49;
        void * t52 = gm_l4;
        gm_t88rcucg1b7uk t53 = gm_l7;
        uint8_t t54 = gm_f_is_float_3jk89qu(t52, GM_CONV(gm_csguankko4lmhd, t53));
        uint8_t t55 = (uint8_t)!t54;
        if (!t55) goto ie51;
        {
            gm_t88rcucg1b7uk t57 = gm_l5;
            gm_t9qkh0rsooc8d t58;
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)1ULL));
            void * t59 = gm_l2;
            gm_t8ghgl2jj0qha t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t60 + 0), t58);
            GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
            gm_teti411eujvf4 t61;
            memset(&t61, 0, sizeof t61);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t61 + 8), t60);
            gm_tl874t2atnlh3 t62 = gm_l3;
            void * t63 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t57, &t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t64 = 0;
            F.ret = t63;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
        goto me23;
mn49:;
ma26:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t66 != ((uint8_t)5ULL)) goto mn65;
        {
            gm_b16_8 t68;
            void * t70 = gm_l4;
            gm_t88rcucg1b7uk t71 = gm_l7;
            uint8_t t72 = gm_f_is_bool_ajhjgeg(t70, GM_CONV(gm_csguankko4lmhd, t71));
            if (!t72) goto ie69;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sajso5sujsruj));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(46));
            gm_b16_8 t75 = t74;
            t68 = t75;
            goto ix69;
ie69:;
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(0));
            gm_b16_8 t78 = t77;
            t68 = t78;
ix69:;
            gm_l8 = t68;
        }
        {
            void * t79 = gm_l0;
            gm_tl874t2atnlh3 t80 = gm_l3;
            gm_t6j6smj7qoctu t81;
            void * t82 = gm_l0;
            gm_t88rcucg1b7uk t83 = gm_l5;
            gm_b16_8 t84 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t84);
            gm_b16_8 t85 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 16, t85);
            gm_tabb710d9mq2e t86;
            gm_f_format_s7ci5ul((void *)&t86, (void *)NULL, cx, NULL, &t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src7u9cfdjh7s0n, 139, 28);
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
            gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
            gm_b0_1 t89 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t79, t80, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t90 = gm_l0;
            void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
            gm_tl874t2atnlh3 t92 = gm_l3;
            void * t93 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t94 = 0;
            F.ret = t93;
            goto gm_fret;
        }
        goto me23;
mn65:;
ma27:;
        goto me23;
mn95:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 134, 5);
me23:;
    }
    {
        gm_b16_8 t97;
        uint8_t t99 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
        if (!t99) goto ie98;
        gm_b16_8 t101;
        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
        gm_b16_8 t102 = t101;
        t97 = t102;
        goto ix98;
ie98:;
        gm_b16_8 t104;
        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sa831sfid0hf3));
        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(1));
        gm_b16_8 t105 = t104;
        t97 = t105;
ix98:;
        gm_l9 = t97;
    }
    {
        void * t106 = gm_l0;
        gm_tl874t2atnlh3 t107 = gm_l3;
        gm_t6j6smj7qoctu t108;
        gm_b16_8 t109 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t109);
        void * t110 = gm_l0;
        gm_t88rcucg1b7uk t111 = gm_l5;
        gm_b16_8 t112 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t112);
        gm_tabb710d9mq2e t113;
        gm_f_format_ul5ufam((void *)&t113, (void *)NULL, cx, NULL, &t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_src7u9cfdjh7s0n, 145, 20);
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
        gm_b0_1 t116 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t106, t107, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
        gm_tl874t2atnlh3 t119 = gm_l3;
        void * t120 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
