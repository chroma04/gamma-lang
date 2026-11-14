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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csfd6et11lk21p { _Bool f0; uint32_t f1; } gm_csfd6et11lk21p;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t1j70din2upc4;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; void * f3; } gm_tg0sdqqpruvqq;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csfd6et11lk21p) == 8 && _Alignof(gm_csfd6et11lk21p) == 4 && offsetof(gm_csfd6et11lk21p, f0) == 0 && offsetof(gm_csfd6et11lk21p, f1) == 4, "layout of `IntW`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t88rcucg1b7uk f2; uint32_t f3; gm_t88rcucg1b7uk f4; uint8_t pe[4]; } gm_t0h5lk09e9p5r;
_Static_assert(sizeof(gm_t1j70din2upc4) == 32 && _Alignof(gm_t1j70din2upc4) == 8 && offsetof(gm_t1j70din2upc4, f0) == 0 && offsetof(gm_t1j70din2upc4, f1) == 8 && offsetof(gm_t1j70din2upc4, f2) == 16 && offsetof(gm_t1j70din2upc4, f3) == 24, "layout of `(base: ^Node, index: u32, off: u64, packed: bool)`");
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_ta6jn77i9rmqf;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tdvcgvrf81sbd;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg0sdqqpruvqq) == 32 && _Alignof(gm_tg0sdqqpruvqq) == 8 && offsetof(gm_tg0sdqqpruvqq, f0) == 0 && offsetof(gm_tg0sdqqpruvqq, f1) == 8 && offsetof(gm_tg0sdqqpruvqq, f2) == 16 && offsetof(gm_tg0sdqqpruvqq, f3) == 24, "layout of `(local: u32, off: u64, size: u64, init: ?^Node)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { void * f0; void * f1; gm_t9qkh0rsooc8d f2; uint8_t p3[7]; uint64_t f3; uint64_t f4; } gm_tp0fa5qv7rqnn;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[6]; uint64_t f5; uint64_t f6; } gm_tt49maei14cl6;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t0h5lk09e9p5r) == 40 && _Alignof(gm_t0h5lk09e9p5r) == 8 && offsetof(gm_t0h5lk09e9p5r, f0) == 0 && offsetof(gm_t0h5lk09e9p5r, f1) == 16 && offsetof(gm_t0h5lk09e9p5r, f2) == 24 && offsetof(gm_t0h5lk09e9p5r, f3) == 28 && offsetof(gm_t0h5lk09e9p5r, f4) == 32, "layout of `(host: []u8, addr: u64, whole: Ty, off: u32, fty: Ty)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; void * f4; } gm_t0pc2c7dafsjt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tg0sdqqpruvqq v0; gm_tpe1hv3fekn05 v1; gm_ttggq7ng4kp3p v2; gm_t3mf58gv9kf3n v3; gm_tpe1hv3fekn05 v4; void * v5; gm_tgtub0v94d5u5 v6; } f1; } gm_t2k9dbsv5ff58;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tbl8il92l97ut v1; } f1; } gm_t9b5r56q9jk9c;
_Static_assert(sizeof(gm_ta6jn77i9rmqf) == 32 && _Alignof(gm_ta6jn77i9rmqf) == 8 && offsetof(gm_ta6jn77i9rmqf, f0) == 0 && offsetof(gm_ta6jn77i9rmqf, f1) == 8 && offsetof(gm_ta6jn77i9rmqf, f2) == 16 && offsetof(gm_ta6jn77i9rmqf, f3) == 24, "layout of `(how: WordCmp, l: ^Node, r: ^Node, negated: bool)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdvcgvrf81sbd) == 24 && _Alignof(gm_tdvcgvrf81sbd) == 8 && offsetof(gm_tdvcgvrf81sbd, f0) == 0 && offsetof(gm_tdvcgvrf81sbd, f1) == 8 && offsetof(gm_tdvcgvrf81sbd, f2) == 12 && offsetof(gm_tdvcgvrf81sbd, f3) == 16, "layout of `(base: ^Node, index: u32, bit_off: u32, fty: Ty)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_tf4r0415i49d1;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tkm39klvnn4cr;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; uint64_t v1; uint64_t v2; gm_t0h5lk09e9p5r v3; } f1; } gm_tou12vuc19e0s;
_Static_assert(sizeof(gm_tp0fa5qv7rqnn) == 40 && _Alignof(gm_tp0fa5qv7rqnn) == 8 && offsetof(gm_tp0fa5qv7rqnn, f0) == 0 && offsetof(gm_tp0fa5qv7rqnn, f1) == 8 && offsetof(gm_tp0fa5qv7rqnn, f2) == 16 && offsetof(gm_tp0fa5qv7rqnn, f3) == 24 && offsetof(gm_tp0fa5qv7rqnn, f4) == 32, "layout of `(base: ^Node, index: ^Node, seq: SeqKind, esz: u64, len: u64)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tscalq56heofe;
_Static_assert(sizeof(gm_tt49maei14cl6) == 48 && _Alignof(gm_tt49maei14cl6) == 8 && offsetof(gm_tt49maei14cl6, f0) == 0 && offsetof(gm_tt49maei14cl6, f1) == 8 && offsetof(gm_tt49maei14cl6, f2) == 16 && offsetof(gm_tt49maei14cl6, f3) == 24 && offsetof(gm_tt49maei14cl6, f4) == 25 && offsetof(gm_tt49maei14cl6, f5) == 32 && offsetof(gm_tt49maei14cl6, f6) == 40, "layout of `(base: ^Node, lo: ?^Node, hi: ?^Node, inclusive: bool, seq: SeqKind, esz: u64, len: u64)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[5]; void * f2; void * f3; } gm_tuggr5sml9o52;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t0pc2c7dafsjt) == 40 && _Alignof(gm_t0pc2c7dafsjt) == 8 && offsetof(gm_t0pc2c7dafsjt, f0) == 0 && offsetof(gm_t0pc2c7dafsjt, f1) == 4 && offsetof(gm_t0pc2c7dafsjt, f2) == 12 && offsetof(gm_t0pc2c7dafsjt, f3) == 24 && offsetof(gm_t0pc2c7dafsjt, f4) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t2k9dbsv5ff58) == 40 && _Alignof(gm_t2k9dbsv5ff58) == 8 && offsetof(gm_t2k9dbsv5ff58, f0) == 0 && offsetof(gm_t2k9dbsv5ff58, f1) == 8, "layout of `St`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_t9b5r56q9jk9c) == 32 && _Alignof(gm_t9b5r56q9jk9c) == 8 && offsetof(gm_t9b5r56q9jk9c, f0) == 0 && offsetof(gm_t9b5r56q9jk9c, f1) == 8, "layout of `Flow!(frame: ^Frame, label: u32)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tf4r0415i49d1) == 48 && _Alignof(gm_tf4r0415i49d1) == 8 && offsetof(gm_tf4r0415i49d1, f0) == 0 && offsetof(gm_tf4r0415i49d1, f1) == 24 && offsetof(gm_tf4r0415i49d1, f2) == 32, "layout of `(effect_: EffectRef, op: u32, args: []^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tkm39klvnn4cr) == 32 && _Alignof(gm_tkm39klvnn4cr) == 8 && offsetof(gm_tkm39klvnn4cr, f0) == 0 && offsetof(gm_tkm39klvnn4cr, f1) == 4 && offsetof(gm_tkm39klvnn4cr, f2) == 16 && offsetof(gm_tkm39klvnn4cr, f3) == 24, "layout of `(op: BinOp, how: WordCmp, l: ^Node, r: ^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tou12vuc19e0s v1; } f1; } gm_tl1r7a2fn1ev6;
_Static_assert(sizeof(gm_tou12vuc19e0s) == 48 && _Alignof(gm_tou12vuc19e0s) == 8 && offsetof(gm_tou12vuc19e0s, f0) == 0 && offsetof(gm_tou12vuc19e0s, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tscalq56heofe) == 40 && _Alignof(gm_tscalq56heofe) == 8 && offsetof(gm_tscalq56heofe, f0) == 0 && offsetof(gm_tscalq56heofe, f1) == 12 && offsetof(gm_tscalq56heofe, f2) == 24 && offsetof(gm_tscalq56heofe, f3) == 32, "layout of `(op: Builtin, t: IntW, x: ^Node, k: ^Node)`");
_Static_assert(sizeof(gm_tuggr5sml9o52) == 24 && _Alignof(gm_tuggr5sml9o52) == 8 && offsetof(gm_tuggr5sml9o52, f0) == 0 && offsetof(gm_tuggr5sml9o52, f1) == 2 && offsetof(gm_tuggr5sml9o52, f2) == 8 && offsetof(gm_tuggr5sml9o52, f3) == 16, "layout of `(op: BinOp, wide: bool, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tl1r7a2fn1ev6) == 56 && _Alignof(gm_tl1r7a2fn1ev6) == 8 && offsetof(gm_tl1r7a2fn1ev6, f0) == 0 && offsetof(gm_tl1r7a2fn1ev6, f1) == 8, "layout of `Flow!Place`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_coerce_im8vs3u {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_cond_v8r7708 {
    gm_frame h;
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_else__7epkk2p {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ev_25jfdar {
    gm_frame h;
    gm_tou12vuc19e0s l16; /* p */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_args_j1qj6gl {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_builtin_86a9q3q {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* vals */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_discard_8c4a8jg {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_index_bie377m {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_ev_return_8ggbvjj {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_slow_hdak0k9 {
    gm_frame h;
    gm_tou12vuc19e0s l13; /* p */
    gm_tou12vuc19e0s l15; /* p */
    gm_tou12vuc19e0s l20; /* p */
    gm_tp0fa5qv7rqnn l23; /* x */
    gm_tou12vuc19e0s l28; /* p */
    gm_tt49maei14cl6 l29; /* x */
    gm_tf4r0415i49d1 l58; /* x */
    gm_t710lsrq1m8gk l117; /* x */
    gm_tntn2tf15puib l134; /* x */
    gm_t710lsrq1m8gk l135; /* w */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ew_c5kil07 {
    gm_frame h;
    gm_t0pc2c7dafsjt l9; /* x */
    gm_tscalq56heofe l30; /* x */
    gm_t2k9dbsv5ff58 l49; /* st */
    gm_t833u93mr4iob ret;
};
struct gm_fr_ew_slow_0jkgi8d {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_builtin_s6t151o {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_is_place_expr_qlfi6ot {
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
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
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
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_string_ttuk4gr {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
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
struct gm_fr_sext_g6f00aa {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_try__q6sv04f {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_unwrap_f7c53ka {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* out */
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
struct gm_fr_word_eq_8jc0uot {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_word_less_l13biee {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_temp_9cmh124(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) void gm_f_binary_6rg6s7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((noinline)) uint64_t gm_f_bit_word_7ae821g(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits_raw_t6dqs65(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_block_5b89ev3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_call_node_a1b30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_cancel__tb288ki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_coerce_im8vs3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tmn7n0n9raou5 a2, void * a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_cond_v8r7708(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_conds_30ust77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
__attribute__((aligned(4))) void gm_f_else__7epkk2p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, void * a4, void * a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_ev_builtin_86a9q3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_ev_call_value_f4tfqs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_ev_discard_8c4a8jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_ev_index_bie377m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_ev_perform_ovt4j0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, uint32_t a3, gm_b16_8 a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_ev_return_8ggbvjj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_ev_slice_i5uin87(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4, void * a5, uint8_t a6, gm_t9qkh0rsooc8d a7, uint64_t a8, uint64_t a9, gm_b16_8 a10);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ev_slow_hdak0k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ew_slow_0jkgi8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_exec_stmt_4jh6tlr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_fiber_builtin_rpc9l9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint64_t gm_f_float_op_osevuqr(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_2hd15t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e1vik51(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r6pm0oj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) uint32_t gm_f_frame_handle_k12gn88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_frame_up_ghkkuqg(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_builtin_s6t151o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_int_conv_rqdvurn(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, gm_csfd6et11lk21p a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_int_op_5g0lv3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, gm_ttovhfee8mf28 a3, gm_ttovhfee8mf28 a4, void * a5, void * a6, void * a7);
__attribute__((aligned(4))) void gm_f_int_un_sm4tv7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_ttovhfee8mf28 a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) uint8_t gm_f_is_place_expr_qlfi6ot(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_label_dst_b5cat1e(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_load_word_ud3iuha(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_loop__3bo7tki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) void gm_f_match__tg8e3g3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_place_47rfqe5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_put_word_tfn443i(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_local_n8sc5ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_place_fcc38bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_resolve_target_bdf5i1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_te9hrk0u35rev a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_resume__mi7hk2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_set_local_ug1vgfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_string_literal_337m6cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_sys_call_6jqv32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_try__q6sv04f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_unary_sorkbs7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, void * a3, void * a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_unwrap_f7c53ka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_with__u1jp2ib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write__eua2bmh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, void * a6);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_knna6g3th5t[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0lfbgij46fm3[74];
extern const uint8_t gm_s1kvbdtols1cc[53];
extern const uint8_t gm_s76i2uidi0pjj[54];
extern const uint8_t gm_sar5aak6f8jpo[44];
extern const uint8_t gm_sco971hnpq7nk[49];
extern const uint8_t gm_sd3b7a2bnmpef[25];
extern const uint8_t gm_sd5r0412dj6fb[15];
extern const uint8_t gm_sds80esh09gni[51];
extern const uint8_t gm_sen05m4ui88rt[58];
extern const uint8_t gm_sl4daef1gs04o[37];
extern const uint8_t gm_sl9b2am1i9pmg[52];
extern const uint8_t gm_snh5ve0osdp08[7];
extern const uint8_t gm_sob69bvkm3aek[33];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_spic5gjomd7lu[62];
extern const uint8_t gm_sqnuqut1dl3fi[48];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src0jbci4ff2esa[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srch12rfrebvus5[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgr2i687arao[75];
extern const uint8_t gm_ss1mi142k161m[41];
extern const uint8_t gm_ssgce158bfb9m[55];
extern const uint8_t gm_su2imns6t7mh5[41];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 120u);
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
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_b16_8 t6 = t5;
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t6)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        if (GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_addr_odbbf6a F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uint64_t, gm_cmiriali67r);
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_mul_overflow(((uint64_t)16ULL), t3, &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src6hq50r4rk7e6, 5, 64);
    uint64_t t6 = r4;
    uint64_t r7; bool o8 = __builtin_add_overflow(t1, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 5, 52);
    uint64_t t9 = r7;
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2p7d2kv();
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
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
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
    if (t6 > t10 || t10 > (uintptr_t)t4) gm_trap_slice(t6, t10, t4, gm_srcnqtpfma6qe4a, 106, 67);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + t6 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t6));
    uint64_t t12 = gm_l2;
    gm_f_put_u64_irp77r2(t1, t11, t12);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)8ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            uint64_t t8 = gm_l2;
            gm_f_wr_word_npte9fs(t6, t7, t8);
        }
        goto ix2;
ie2:;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l1;
            uint64_t t12 = gm_l2;
            unsigned __int128 t13 = (unsigned __int128)t12;
            gm_f_put_uint_cddin99(t10, t11, t13);
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sext_g6f00aa F;
    uint64_t gm_l0; /* v */
    uint32_t gm_l1; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint32_t t2 = t1;
    int64_t t3;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint32_t)8ULL))) goto mn5;
    uint64_t t7 = gm_l0;
    uint8_t t8 = (uint8_t)t7;
    uint8_t t9 = t8;
    int8_t t10;
    memset(&t10, 0, sizeof t10); memcpy(&t10, ((uint8_t *)&t9), sizeof t10 < 1 ? sizeof t10 : 1);
    int64_t t11 = (int64_t)t10;
    t3 = t11;
    goto me4;
mn5:;
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t13 == ((uint32_t)16ULL))) goto mn12;
    uint64_t t14 = gm_l0;
    uint16_t t15 = (uint16_t)t14;
    uint16_t t16 = t15;
    int16_t t17;
    memset(&t17, 0, sizeof t17); memcpy(&t17, ((uint8_t *)&t16), sizeof t17 < 2 ? sizeof t17 : 2);
    int64_t t18 = (int64_t)t17;
    t3 = t18;
    goto me4;
mn12:;
    uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t20 == ((uint32_t)32ULL))) goto mn19;
    uint64_t t21 = gm_l0;
    uint32_t t22 = (uint32_t)t21;
    uint32_t t23 = t22;
    int32_t t24;
    memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 4 ? sizeof t24 : 4);
    int64_t t25 = (int64_t)t24;
    t3 = t25;
    goto me4;
mn19:;
    uint64_t t27 = gm_l0;
    uint64_t t28 = t27;
    int64_t t29;
    memset(&t29, 0, sizeof t29); memcpy(&t29, ((uint8_t *)&t28), sizeof t29 < 8 ? sizeof t29 : 8);
    t3 = t29;
    goto me4;
mn26:;
    gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 11, 41);
me4:;
    F.ret = t3;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 184u);
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
    gm_f_format_46el73v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t9);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_eq_8jc0uot F;
    gm_tmn7n0n9raou5 gm_l0; /* how */
    uint64_t gm_l1; /* p */
    uint64_t gm_l2; /* q */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma3; case ((uint8_t)4ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)3ULL)) goto mn9;
    uint64_t t11 = gm_l1;
    uint64_t t12 = t11;
    double t13;
    memset(&t13, 0, sizeof t13); memcpy(&t13, ((uint8_t *)&t12), sizeof t13 < 8 ? sizeof t13 : 8);
    uint64_t t14 = gm_l2;
    uint64_t t15 = t14;
    double t16;
    memset(&t16, 0, sizeof t16); memcpy(&t16, ((uint8_t *)&t15), sizeof t16 < 8 ? sizeof t16 : 8);
    uint8_t t17 = ((t13 == t16));
    t1 = t17;
    goto me2;
mn9:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)2ULL)) goto mn18;
    uint64_t t20 = gm_l1;
    uint32_t t21 = (uint32_t)t20;
    uint32_t t22 = t21;
    float t23;
    memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 4 ? sizeof t23 : 4);
    uint64_t t24 = gm_l2;
    uint32_t t25 = (uint32_t)t24;
    uint32_t t26 = t25;
    float t27;
    memset(&t27, 0, sizeof t27); memcpy(&t27, ((uint8_t *)&t26), sizeof t27 < 4 ? sizeof t27 : 4);
    uint8_t t28 = ((t23 == t27));
    t1 = t28;
    goto me2;
mn18:;
ma5:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)4ULL)) goto mn29;
    uint64_t t31 = gm_l1;
    uint8_t t32 = (!(t31 == ((uint64_t)0ULL)));
    uint64_t t33 = gm_l2;
    uint8_t t34 = (!(t33 == ((uint64_t)0ULL)));
    uint8_t t35 = ((t32 == t34));
    t1 = t35;
    goto me2;
mn29:;
ma6:;
    uint64_t t37 = gm_l1;
    uint64_t t38 = gm_l2;
    uint8_t t39 = ((t37 == t38));
    t1 = t39;
    goto me2;
mn36:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 70, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_less_l13biee F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_tmn7n0n9raou5 gm_l1; /* how */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* q */
    uint32_t gm_l4; /* bits */
    int64_t gm_l5; /* x */
    int64_t gm_l6; /* y */
    double gm_l7; /* x */
    double gm_l8; /* y */
    float gm_l9; /* x */
    float gm_l10; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma3; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_l4 = t11;
        {
            uint64_t t13 = gm_l2;
            uint32_t t14 = gm_l4;
            int64_t t15 = gm_f_sext_g6f00aa(t13, t14);
            gm_l5 = t15;
        }
        {
            uint64_t t16 = gm_l3;
            uint32_t t17 = gm_l4;
            int64_t t18 = gm_f_sext_g6f00aa(t16, t17);
            gm_l6 = t18;
        }
        {
            uint8_t t19;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma24; default: goto ma24; }
ma21:;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t28 != ((uint8_t)10ULL)) goto mn27;
            int64_t t29 = gm_l5;
            int64_t t30 = gm_l6;
            uint8_t t31 = (t29 < t30);
            t19 = t31;
            goto me20;
mn27:;
ma22:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t33 != ((uint8_t)11ULL)) goto mn32;
            int64_t t34 = gm_l5;
            int64_t t35 = gm_l6;
            uint8_t t36 = (t34 <= t35);
            t19 = t36;
            goto me20;
mn32:;
ma23:;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t38 != ((uint8_t)12ULL)) goto mn37;
            int64_t t39 = gm_l5;
            int64_t t40 = gm_l6;
            uint8_t t41 = (t39 > t40);
            t19 = t41;
            goto me20;
mn37:;
ma24:;
            int64_t t43 = gm_l5;
            int64_t t44 = gm_l6;
            uint8_t t45 = (t43 >= t44);
            t19 = t45;
            goto me20;
mn42:;
ma25:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 32, 20);
me20:;
            uint8_t t46 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto me2;
mn9:;
ma4:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t48 != ((uint8_t)3ULL)) goto mn47;
        {
            uint64_t t50 = gm_l2;
            uint64_t t51 = t50;
            double t52;
            memset(&t52, 0, sizeof t52); memcpy(&t52, ((uint8_t *)&t51), sizeof t52 < 8 ? sizeof t52 : 8);
            gm_l7 = t52;
        }
        {
            uint64_t t53 = gm_l3;
            uint64_t t54 = t53;
            double t55;
            memset(&t55, 0, sizeof t55); memcpy(&t55, ((uint8_t *)&t54), sizeof t55 < 8 ? sizeof t55 : 8);
            gm_l8 = t55;
        }
        {
            uint8_t t56;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t63) { case ((uint8_t)0ULL): goto ma61; case ((uint8_t)1ULL): goto ma61; case ((uint8_t)2ULL): goto ma61; case ((uint8_t)3ULL): goto ma61; case ((uint8_t)4ULL): goto ma61; case ((uint8_t)5ULL): goto ma61; case ((uint8_t)6ULL): goto ma61; case ((uint8_t)7ULL): goto ma61; case ((uint8_t)8ULL): goto ma61; case ((uint8_t)9ULL): goto ma61; case ((uint8_t)10ULL): goto ma58; case ((uint8_t)11ULL): goto ma59; case ((uint8_t)12ULL): goto ma60; case ((uint8_t)13ULL): goto ma61; default: goto ma61; }
ma58:;
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t65 != ((uint8_t)10ULL)) goto mn64;
            double t66 = gm_l7;
            double t67 = gm_l8;
            uint8_t t68 = (t66 < t67);
            t56 = t68;
            goto me57;
mn64:;
ma59:;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t70 != ((uint8_t)11ULL)) goto mn69;
            double t71 = gm_l7;
            double t72 = gm_l8;
            uint8_t t73 = (t71 <= t72);
            t56 = t73;
            goto me57;
mn69:;
ma60:;
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t75 != ((uint8_t)12ULL)) goto mn74;
            double t76 = gm_l7;
            double t77 = gm_l8;
            uint8_t t78 = (t76 > t77);
            t56 = t78;
            goto me57;
mn74:;
ma61:;
            double t80 = gm_l7;
            double t81 = gm_l8;
            uint8_t t82 = (t80 >= t81);
            t56 = t82;
            goto me57;
mn79:;
ma62:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 42, 20);
me57:;
            uint8_t t83 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto me2;
mn47:;
ma5:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t85 != ((uint8_t)2ULL)) goto mn84;
        {
            uint64_t t87 = gm_l2;
            uint32_t t88 = (uint32_t)t87;
            uint32_t t89 = t88;
            float t90;
            memset(&t90, 0, sizeof t90); memcpy(&t90, ((uint8_t *)&t89), sizeof t90 < 4 ? sizeof t90 : 4);
            gm_l9 = t90;
        }
        {
            uint64_t t91 = gm_l3;
            uint32_t t92 = (uint32_t)t91;
            uint32_t t93 = t92;
            float t94;
            memset(&t94, 0, sizeof t94); memcpy(&t94, ((uint8_t *)&t93), sizeof t94 < 4 ? sizeof t94 : 4);
            gm_l10 = t94;
        }
        {
            uint8_t t95;
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t102) { case ((uint8_t)0ULL): goto ma100; case ((uint8_t)1ULL): goto ma100; case ((uint8_t)2ULL): goto ma100; case ((uint8_t)3ULL): goto ma100; case ((uint8_t)4ULL): goto ma100; case ((uint8_t)5ULL): goto ma100; case ((uint8_t)6ULL): goto ma100; case ((uint8_t)7ULL): goto ma100; case ((uint8_t)8ULL): goto ma100; case ((uint8_t)9ULL): goto ma100; case ((uint8_t)10ULL): goto ma97; case ((uint8_t)11ULL): goto ma98; case ((uint8_t)12ULL): goto ma99; case ((uint8_t)13ULL): goto ma100; default: goto ma100; }
ma97:;
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t104 != ((uint8_t)10ULL)) goto mn103;
            float t105 = gm_l9;
            float t106 = gm_l10;
            uint8_t t107 = (t105 < t106);
            t95 = t107;
            goto me96;
mn103:;
ma98:;
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t109 != ((uint8_t)11ULL)) goto mn108;
            float t110 = gm_l9;
            float t111 = gm_l10;
            uint8_t t112 = (t110 <= t111);
            t95 = t112;
            goto me96;
mn108:;
ma99:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t114 != ((uint8_t)12ULL)) goto mn113;
            float t115 = gm_l9;
            float t116 = gm_l10;
            uint8_t t117 = (t115 > t116);
            t95 = t117;
            goto me96;
mn113:;
ma100:;
            float t119 = gm_l9;
            float t120 = gm_l10;
            uint8_t t121 = (t119 >= t120);
            t95 = t121;
            goto me96;
mn118:;
ma101:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 52, 20);
me96:;
            uint8_t t122 = 0;
            F.ret = t95;
            goto gm_fret;
        }
        goto me2;
mn84:;
ma6:;
        uint8_t t124;
        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t131) { case ((uint8_t)0ULL): goto ma129; case ((uint8_t)1ULL): goto ma129; case ((uint8_t)2ULL): goto ma129; case ((uint8_t)3ULL): goto ma129; case ((uint8_t)4ULL): goto ma129; case ((uint8_t)5ULL): goto ma129; case ((uint8_t)6ULL): goto ma129; case ((uint8_t)7ULL): goto ma129; case ((uint8_t)8ULL): goto ma129; case ((uint8_t)9ULL): goto ma129; case ((uint8_t)10ULL): goto ma126; case ((uint8_t)11ULL): goto ma127; case ((uint8_t)12ULL): goto ma128; case ((uint8_t)13ULL): goto ma129; default: goto ma129; }
ma126:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t133 != ((uint8_t)10ULL)) goto mn132;
        uint64_t t134 = gm_l2;
        uint64_t t135 = gm_l3;
        uint8_t t136 = (t134 < t135);
        t124 = t136;
        goto me125;
mn132:;
ma127:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t138 != ((uint8_t)11ULL)) goto mn137;
        uint64_t t139 = gm_l2;
        uint64_t t140 = gm_l3;
        uint8_t t141 = (t139 <= t140);
        t124 = t141;
        goto me125;
mn137:;
ma128:;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t143 != ((uint8_t)12ULL)) goto mn142;
        uint64_t t144 = gm_l2;
        uint64_t t145 = gm_l3;
        uint8_t t146 = (t144 > t145);
        t124 = t146;
        goto me125;
mn142:;
ma129:;
        uint64_t t148 = gm_l2;
        uint64_t t149 = gm_l3;
        uint8_t t150 = (t148 >= t149);
        t124 = t150;
        goto me125;
mn147:;
ma130:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 59, 21);
me125:;
        uint8_t t151 = 0;
        F.ret = t124;
        goto gm_fret;
        goto me2;
mn123:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 28, 5);
me2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
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
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 329, 13);
                uint64_t t12 = gm_l2;
                uint8_t t13 = (uint8_t)t12;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1), t13);
            }
            {
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 330, 13);
                uint64_t t16 = gm_l2;
                unsigned __int128 n17 = (unsigned __int128)((uint64_t)8ULL);
                if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcnqtpfma6qe4a, 330, 21);
                uint64_t t18 = (uint64_t)(t16 >> (int)n17);
                uint8_t t19 = (uint8_t)t18;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1), t19);
            }
            {
                uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcnqtpfma6qe4a, 331, 13);
                uint64_t t22 = gm_l2;
                unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
                if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 331, 21);
                uint64_t t24 = (uint64_t)(t22 >> (int)n23);
                uint8_t t25 = (uint8_t)t24;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 1), t25);
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcnqtpfma6qe4a, 332, 13);
                uint64_t t28 = gm_l2;
                unsigned __int128 n29 = (unsigned __int128)((uint64_t)24ULL);
                if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcnqtpfma6qe4a, 332, 21);
                uint64_t t30 = (uint64_t)(t28 >> (int)n29);
                uint8_t t31 = (uint8_t)t30;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1), t31);
            }
            {
                uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 333, 13);
                uint64_t t34 = gm_l2;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)32ULL);
                if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_srcnqtpfma6qe4a, 333, 21);
                uint64_t t36 = (uint64_t)(t34 >> (int)n35);
                uint8_t t37 = (uint8_t)t36;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1), t37);
            }
            {
                uint64_t t38 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcnqtpfma6qe4a, 334, 13);
                uint64_t t40 = gm_l2;
                unsigned __int128 n41 = (unsigned __int128)((uint64_t)40ULL);
                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 334, 21);
                uint64_t t42 = (uint64_t)(t40 >> (int)n41);
                uint8_t t43 = (uint8_t)t42;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 1), t43);
            }
            {
                uint64_t t44 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 335, 13);
                uint64_t t46 = gm_l2;
                unsigned __int128 n47 = (unsigned __int128)((uint64_t)48ULL);
                if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcnqtpfma6qe4a, 335, 21);
                uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t44 * 1), t49);
            }
            {
                uint64_t t50 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 336, 13);
                uint64_t t52 = gm_l2;
                unsigned __int128 n53 = (unsigned __int128)((uint64_t)56ULL);
                if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcnqtpfma6qe4a, 336, 21);
                uint64_t t54 = (uint64_t)(t52 >> (int)n53);
                uint8_t t55 = (uint8_t)t54;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1), t55);
            }
            {
                gm_b0_1 t56;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 340, 13);
                uint64_t t64 = gm_l2;
                uint8_t t65 = (uint8_t)t64;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t62 * 1), t65);
            }
            {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t70 = ((t69 == ((uint64_t)4ULL)));
            if (!t70) goto ie68;
            {
                uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcnqtpfma6qe4a, 344, 13);
                uint64_t t74 = gm_l2;
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t72 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcnqtpfma6qe4a, 345, 13);
                uint64_t t78 = gm_l2;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)8ULL);
                if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 345, 21);
                uint64_t t80 = (uint64_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 346, 13);
                uint64_t t84 = gm_l2;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)16ULL);
                if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcnqtpfma6qe4a, 346, 21);
                uint64_t t86 = (uint64_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcnqtpfma6qe4a, 347, 13);
                uint64_t t90 = gm_l2;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)24ULL);
                if (n91 >= 64) gm_trap_shift((uint64_t)n91, "u64", gm_srcnqtpfma6qe4a, 347, 21);
                uint64_t t92 = (uint64_t)(t90 >> (int)n91);
                uint8_t t93 = (uint8_t)t92;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t88 * 1), t93);
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t96 = gm_l0;
        gm_b16_8 t97 = gm_l1;
        uint64_t t98 = gm_l2;
        unsigned __int128 t99 = (unsigned __int128)t98;
        gm_f_put_uint_cddin99(t96, t97, t99);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_coerce_im8vs3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tmn7n0n9raou5 a2, void * a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_coerce_im8vs3u F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    void * gm_l3; /* x */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_b16_8 gm_l7; /* b */
    uint8_t gm_l8; /* to_fn */
    uint8_t gm_l9; /* from_fn */
    uint64_t gm_l10; /* addr */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* b */
    GM_STACK_CHECK(&F, 2133u);
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
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; default: goto ma7; }
ma4:;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t11 != ((uint8_t)5ULL)) goto mn10;
            {
                void * t13 = gm_l0;
                gm_tbl8il92l97ut t14 = gm_l1;
                void * t15 = gm_l3;
                gm_tcfa6l7reoosn t16;
                gm_f_ev_discard_8c4a8jg((void *)&t16, (void *)NULL, cx, NULL, t13, t14, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) {
                    gm_t76123gfet73g t18 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t17) + 8));
                    gm_tcfa6l7reoosn t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                    ((uint8_t *)&t19)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t19 + 8), t18);
                    F.ret = t19;
                    goto gm_fret;
                }
                gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
            }
            {
                void * t21 = gm_l0;
                void * t22 = gm_l4;
                gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 112));
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s76i2uidi0pjj));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(53));
                gm_t76123gfet73g t25;
                gm_f_trap_qh84l9d((void *)&t25, (void *)NULL, cx, NULL, t21, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                ((uint8_t *)&t26)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t26 + 8), t25);
                gm_tcfa6l7reoosn t27 = t26;
                uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
                F.ret = t26;
                goto gm_fret;
            }
            goto me3;
mn10:;
ma5:;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t30 != ((uint8_t)9ULL)) goto mn29;
            {
                void * t32 = gm_l0;
                gm_t2lqu8jsj511d t33 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t32));
                gm_l6 = t33;
            }
            {
                void * t34 = gm_l0;
                void * t35 = gm_l3;
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 104));
                gm_b16_8 t37 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t34, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t37;
            }
            {
                void * t38 = gm_l0;
                gm_tbl8il92l97ut t39 = gm_l1;
                void * t40 = gm_l3;
                gm_b16_8 t41 = gm_l7;
                gm_tcfa6l7reoosn t42;
                gm_f_ev_25jfdar((void *)&t42, (void *)NULL, cx, NULL, t38, t39, t40, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t43 = t42;
                if (((((uint8_t *)&t43))[0] != 0)) {
                    gm_t76123gfet73g t44 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t43) + 8));
                    gm_tcfa6l7reoosn t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                    ((uint8_t *)&t45)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t45 + 8), t44);
                    F.ret = t45;
                    goto gm_fret;
                }
                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 8));
            }
            {
                void * t47 = gm_l0;
                void * t48 = gm_l4;
                gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t48) + 96));
                gm_t53at80pfdus3 t50;
                gm_f_kind_htv6093((void *)&t50, t47, GM_CONV(gm_csguankko4lmhd, t49));
                gm_t53at80pfdus3 t51 = t50;
                uint8_t t52;
                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t51) + 0));
                if (t55 != ((uint8_t)15ULL)) goto mn54;
                t52 = ((uint8_t)1);
                goto me53;
mn54:;
                t52 = ((uint8_t)0);
                goto me53;
mn56:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 535, 22);
me53:;
                gm_l8 = t52;
            }
            {
                void * t57 = gm_l0;
                void * t58 = gm_l3;
                gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 96));
                gm_t53at80pfdus3 t60;
                gm_f_kind_htv6093((void *)&t60, t57, GM_CONV(gm_csguankko4lmhd, t59));
                gm_t53at80pfdus3 t61 = t60;
                uint8_t t62;
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
                if (t65 != ((uint8_t)15ULL)) goto mn64;
                t62 = ((uint8_t)1);
                goto me63;
mn64:;
                t62 = ((uint8_t)0);
                goto me63;
mn66:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 536, 24);
me63:;
                gm_l9 = t62;
            }
            {
                uint8_t t68 = gm_l8;
                if (!t68) goto ie67;
                uint8_t t69 = gm_l9;
                uint8_t t70 = (uint8_t)!t69;
                if (!t70) goto ie67;
                {
                    void * t72 = gm_l0;
                    gm_b16_8 t73 = gm_l7;
                    gm_b16_8 t74 = t73;
                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                    uint64_t t76 = gm_f_ptr_of_b77b4c9(t72, t75);
                    gm_l10 = t76;
                }
                {
                    void * t78 = gm_l0;
                    uint64_t t79 = gm_l10;
                    gm_t9805qtonbsag t80 = gm_f_fn_from_addr_7a6bpqd(t78, t79);
                    gm_t9805qtonbsag t81 = t80;
                    uint8_t t82;
                    if (((((uint8_t *)&t81))[4] != 0)) goto mn84;
                    t82 = ((uint8_t)1);
                    goto me83;
mn84:;
                    t82 = ((uint8_t)0);
                    goto me83;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 539, 20);
me83:;
                    if (!t82) goto ie77;
                    {
                        void * t87 = gm_l0;
                        void * t88 = gm_l4;
                        gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t88) + 112));
                        gm_b16_8 t90;
                        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_ss1mi142k161m));
                        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(40));
                        gm_t76123gfet73g t91;
                        gm_f_trap_qh84l9d((void *)&t91, (void *)NULL, cx, NULL, t87, t89, t90);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t92;
                        GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
                        ((uint8_t *)&t92)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t92 + 8), t91);
                        gm_tcfa6l7reoosn t93 = t92;
                        uint8_t t94 = ((((uint8_t *)&t93))[0] != 0);
                        F.ret = t92;
                        goto gm_fret;
                    }
                    goto ix77;
ie77:;
ix77:;
                }
                {
                    void * t95 = gm_l0;
                    gm_b16_8 t96 = gm_l5;
                    uint64_t t97 = gm_l10;
                    gm_f_put_word_tfn443i(t95, t96, ((uint64_t)0ULL), t97);
                }
                {
                    void * t98 = gm_l0;
                    gm_b16_8 t99 = gm_l5;
                    gm_f_put_word_tfn443i(t98, t99, ((uint64_t)1ULL), ((uint64_t)0ULL));
                }
                {
                    void * t100 = gm_l0;
                    gm_b16_8 t101 = gm_l5;
                    uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t103 = (uint64_t)t102;
                    gm_f_put_word_tfn443i(t100, t101, ((uint64_t)2ULL), t103);
                }
                goto ix67;
ie67:;
                uint8_t t105 = gm_l8;
                uint8_t t106 = (uint8_t)!t105;
                if (!t106) goto ie104;
                uint8_t t107 = gm_l9;
                if (!t107) goto ie104;
                {
                    void * t109 = gm_l0;
                    gm_b16_8 t110 = gm_l5;
                    void * t111 = gm_l0;
                    gm_b16_8 t112 = gm_l7;
                    gm_b16_8 t113 = t112;
                    gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                    uint64_t t115 = gm_f_word_o2rcrhs(t111, t114, ((uint64_t)0ULL));
                    gm_f_put_ptr_188p3mm(t109, t110, t115);
                }
                goto ix104;
ie104:;
                {
                    gm_b16_8 t117 = gm_l5;
                    gm_b16_8 t118 = gm_l7;
                    gm_b16_8 t119 = t118;
                    gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                    gm_f_copy_from_f7j6p05(t117, t120);
                }
ix104:;
ix67:;
            }
            {
                void * t121 = gm_l0;
                gm_t2lqu8jsj511d t122 = gm_l6;
                gm_f_release_jn5kqdi(t121, GM_CONV(gm_csc7lc6jlkqkuq, t122));
            }
            goto me3;
mn29:;
ma6:;
            uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t126 != ((uint8_t)6ULL)) goto pa125;
            goto po124;
pa125:;
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t128 != ((uint8_t)8ULL)) goto pa127;
            goto po124;
pa127:;
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t130 != ((uint8_t)10ULL)) goto pa129;
            goto po124;
pa129:;
            goto mn123;
po124:;
            void * t131 = gm_l0;
            gm_tbl8il92l97ut t132 = gm_l1;
            void * t133 = gm_l3;
            gm_b16_8 t134 = gm_l5;
            gm_tcfa6l7reoosn t135;
            gm_f_ev_25jfdar((void *)&t135, (void *)NULL, cx, NULL, t131, t132, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t136 = t135;
            if (((((uint8_t *)&t136))[0] != 0)) {
                gm_t76123gfet73g t137 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t136) + 8));
                gm_tcfa6l7reoosn t138;
                GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                ((uint8_t *)&t138)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t138 + 8), t137);
                F.ret = t138;
                goto gm_fret;
            }
            gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 8));
            goto me3;
mn123:;
ma7:;
            {
                void * t142 = gm_l0;
                gm_t2lqu8jsj511d t143 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t142));
                gm_l11 = t143;
            }
            {
                void * t144 = gm_l0;
                void * t145 = gm_l3;
                uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)t145) + 104));
                gm_b16_8 t147 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t144, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t147;
            }
            {
                void * t148 = gm_l0;
                gm_tbl8il92l97ut t149 = gm_l1;
                void * t150 = gm_l3;
                gm_b16_8 t151 = gm_l12;
                gm_tcfa6l7reoosn t152;
                gm_f_ev_25jfdar((void *)&t152, (void *)NULL, cx, NULL, t148, t149, t150, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t153 = t152;
                if (((((uint8_t *)&t153))[0] != 0)) {
                    gm_t76123gfet73g t154 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t153) + 8));
                    gm_tcfa6l7reoosn t155;
                    GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                    ((uint8_t *)&t155)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t155 + 8), t154);
                    F.ret = t155;
                    goto gm_fret;
                }
                gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t153) + 8));
            }
            {
                void * t157 = gm_l0;
                void * t158 = gm_l4;
                gm_tl874t2atnlh3 t159 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t158) + 112));
                gm_tmn7n0n9raou5 t160 = gm_l2;
                void * t161 = gm_l3;
                gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t161) + 96));
                void * t163 = gm_l4;
                gm_t88rcucg1b7uk t164 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t163) + 96));
                gm_b16_8 t165 = gm_l12;
                gm_b16_8 t166 = gm_l5;
                gm_tcfa6l7reoosn t167;
                gm_f_coerce_bytes_9kf3u24((void *)&t167, (void *)NULL, cx, NULL, t157, t159, t160, t162, t164, t165, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t168 = t167;
                if (((((uint8_t *)&t168))[0] != 0)) {
                    gm_t76123gfet73g t169 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t168) + 8));
                    gm_tcfa6l7reoosn t170;
                    GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                    ((uint8_t *)&t170)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t170 + 8), t169);
                    F.ret = t170;
                    goto gm_fret;
                }
                gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 8));
            }
            {
                void * t172 = gm_l0;
                gm_t2lqu8jsj511d t173 = gm_l11;
                gm_f_release_jn5kqdi(t172, GM_CONV(gm_csc7lc6jlkqkuq, t173));
            }
            goto me3;
mn140:;
ma8:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 526, 5);
me3:;
        }
    }
    gm_b0_1 t174;
    gm_tcfa6l7reoosn t175;
    GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t175 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t175 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t175 + 24, 0);
    ((uint8_t *)&t175)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t175 + 8), t174);
    gm_tcfa6l7reoosn t176 = t175;
    F.ret = t176;
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
inline __attribute__((aligned(4))) void gm_f_cond_v8r7708(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_cond_v8r7708 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    GM_STACK_CHECK(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tbl8il92l97ut t2 = gm_l1;
    void * t3 = gm_l2;
    gm_t833u93mr4iob t4;
    gm_f_ew_c5kil07((void *)&t4, (void *)NULL, cx, NULL, t1, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t833u93mr4iob t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) {
        gm_t76123gfet73g t6 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t5) + 8));
        gm_t1ec8br9ddkav t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        ((uint8_t *)&t7)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t7 + 8), t6);
        F.ret = t7;
        goto gm_fret;
    }
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
    uint8_t t9 = (!(t8 == ((uint64_t)0ULL)));
    gm_t1ec8br9ddkav t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
    ((uint8_t *)&t10)[0] = 0;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 8), t9);
    F.ret = t10;
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
__attribute__((aligned(4))) void gm_f_else__7epkk2p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, void * a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_else__7epkk2p F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* lhs */
    gm_t9805qtonbsag gm_l3; /* capture */
    void * gm_l4; /* rhs */
    void * gm_l5; /* n */
    gm_b16_8 gm_l6; /* dst */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* b */
    uint8_t gm_l9; /* present */
    gm_t88rcucg1b7uk gm_l10; /* payload_ty */
    gm_b16_8 gm_l11; /* payload */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint8_t gm_l14; /* is_err */
    GM_STACK_CHECK(&F, 1803u);
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
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l7 = t4;
        }
        {
            void * t5 = gm_l0;
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            gm_b16_8 t8 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t8;
        }
        {
            void * t9 = gm_l0;
            gm_tbl8il92l97ut t10 = gm_l1;
            void * t11 = gm_l2;
            gm_b16_8 t12 = gm_l8;
            gm_tcfa6l7reoosn t13;
            gm_f_ev_25jfdar((void *)&t13, (void *)NULL, cx, NULL, t9, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t14 = t13;
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
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
        }
        {
            gm_l9 = ((uint8_t)0);
        }
        {
            void * t18 = gm_l2;
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 96));
            gm_l10 = t19;
        }
        {
            gm_b16_8 t20 = gm_f_empty_slice_ijtp9lo();
            gm_l11 = t20;
        }
        {
            void * t21 = gm_l0;
            void * t22 = gm_l2;
            gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + 96));
            gm_t53at80pfdus3 t24;
            gm_f_kind_htv6093((void *)&t24, t21, GM_CONV(gm_csguankko4lmhd, t23));
            gm_t53at80pfdus3 t25 = t24;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            switch (t31) { case ((uint8_t)0ULL): goto ma29; case ((uint8_t)1ULL): goto ma29; case ((uint8_t)2ULL): goto ma29; case ((uint8_t)3ULL): goto ma29; case ((uint8_t)4ULL): goto ma29; case ((uint8_t)5ULL): goto ma29; case ((uint8_t)6ULL): goto ma29; case ((uint8_t)7ULL): goto ma29; case ((uint8_t)8ULL): goto ma29; case ((uint8_t)9ULL): goto ma29; case ((uint8_t)10ULL): goto ma29; case ((uint8_t)11ULL): goto ma29; case ((uint8_t)12ULL): goto ma29; case ((uint8_t)13ULL): goto ma27; case ((uint8_t)14ULL): goto ma28; case ((uint8_t)15ULL): goto ma29; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma29; case ((uint8_t)18ULL): goto ma29; case ((uint8_t)19ULL): goto ma29; case ((uint8_t)20ULL): goto ma29; case ((uint8_t)21ULL): goto ma29; case ((uint8_t)22ULL): goto ma29; case ((uint8_t)23ULL): goto ma29; case ((uint8_t)24ULL): goto ma29; case ((uint8_t)25ULL): goto ma29; case ((uint8_t)26ULL): goto ma29; default: goto ma29; }
ma27:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t33 != ((uint8_t)13ULL)) goto mn32;
            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t25) + 8));
            gm_l12 = t34;
            {
                void * t36 = gm_l0;
                void * t37 = gm_l2;
                gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t37) + 96));
                gm_b16_8 t39 = gm_l8;
                gm_b16_8 t40 = t39;
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                uint8_t t42 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t36, t38, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t42;
            }
            {
                gm_t88rcucg1b7uk t43 = gm_l12;
                gm_l10 = t43;
            }
            {
                uint8_t t45 = gm_l9;
                if (!t45) goto ie44;
                {
                    void * t47 = gm_l0;
                    void * t48 = gm_l2;
                    gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t48) + 96));
                    gm_b16_8 t50 = gm_l8;
                    gm_b16_8 t51 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t47, t49, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t51;
                }
                goto ix44;
ie44:;
ix44:;
            }
            goto me26;
mn32:;
ma28:;
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t53 != ((uint8_t)14ULL)) goto mn52;
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t25) + 8) + 4));
            gm_l13 = t54;
            {
                void * t56 = gm_l0;
                void * t57 = gm_l2;
                gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t57) + 96));
                gm_b16_8 t59 = gm_l8;
                gm_b16_8 t60 = t59;
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                uint8_t t62 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t56, t58, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t62;
            }
            {
                void * t63 = gm_l0;
                void * t64 = gm_l2;
                gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 96));
                gm_b16_8 t66 = gm_l8;
                gm_b16_8 t67 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t63, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t67;
            }
            {
                uint8_t t69 = gm_l14;
                if (!t69) goto ie68;
                uint8_t t70;
                if (((((uint8_t *)&gm_l3))[4] != 0)) goto mn72;
                t70 = ((uint8_t)0);
                goto me71;
mn72:;
                t70 = ((uint8_t)1);
                goto me71;
mn73:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 460, 27);
me71:;
                if (!t70) goto ie68;
                {
                    void * t75 = gm_l0;
                    void * t76 = gm_l5;
                    gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t76) + 112));
                    void * t78 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                    gm_t9805qtonbsag t79 = gm_l3;
                    gm_t9805qtonbsag t80 = t79;
                    uint32_t t81;
                    if (((((uint8_t *)&t80))[4] != 0)) {
                        uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&t80) + 0));
                        t81 = t82;
                    } else {
                        t81 = ((uint32_t)0ULL);
                    }
                    gm_b16_8 t83 = gm_l11;
                    gm_b16_8 t84 = t83;
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                    gm_tcfa6l7reoosn t86;
                    gm_f_set_local_ug1vgfu((void *)&t86, (void *)NULL, cx, NULL, t75, t77, t78, t81, t85);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t87 = t86;
                    if (((((uint8_t *)&t87))[0] != 0)) {
                        gm_t76123gfet73g t88 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t87) + 8));
                        gm_tcfa6l7reoosn t89;
                        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                        ((uint8_t *)&t89)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t89 + 8), t88);
                        F.ret = t89;
                        goto gm_fret;
                    }
                    gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 8));
                }
                goto ix68;
ie68:;
ix68:;
            }
            {
                uint8_t t91 = gm_l14;
                uint8_t t92 = (uint8_t)!t91;
                gm_l9 = t92;
            }
            {
                gm_t88rcucg1b7uk t93 = gm_l13;
                gm_l10 = t93;
            }
            goto me26;
mn52:;
ma29:;
            void * t95 = gm_l0;
            void * t96 = gm_l5;
            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 112));
            gm_b16_8 t98;
            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_srgr2i687arao));
            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(74));
            gm_t76123gfet73g t99;
            gm_f_trap_qh84l9d((void *)&t99, (void *)NULL, cx, NULL, t95, t97, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
            ((uint8_t *)&t100)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t100 + 8), t99);
            gm_tcfa6l7reoosn t101 = t100;
            uint8_t t102 = ((((uint8_t *)&t101))[0] != 0);
            F.ret = t100;
            goto gm_fret;
            goto me26;
mn94:;
ma30:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 451, 5);
me26:;
        }
        {
            uint8_t t104 = gm_l9;
            uint8_t t105 = (uint8_t)!t104;
            if (!t105) goto ie103;
            {
                void * t107 = gm_l0;
                gm_t2lqu8jsj511d t108 = gm_l7;
                gm_f_release_jn5kqdi(t107, GM_CONV(gm_csc7lc6jlkqkuq, t108));
            }
            {
                void * t109 = gm_l0;
                gm_tbl8il92l97ut t110 = gm_l1;
                void * t111 = gm_l4;
                gm_b16_8 t112 = gm_l6;
                gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t109, t110, t111, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix103;
ie103:;
ix103:;
        }
        {
            void * t114 = gm_l5;
            gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t114) + 96));
            gm_t88rcucg1b7uk t116 = gm_l10;
            gm_t88rcucg1b7uk t117 = t115;
            gm_t88rcucg1b7uk t118 = t116;
            uint8_t t119 = (gm_eq9m6mvtmgde(((uint8_t *)&t117), ((uint8_t *)&t118)));
            if (!t119) goto ie113;
            {
                gm_b16_8 t121 = gm_l6;
                gm_b16_8 t122 = gm_l11;
                gm_b16_8 t123 = t122;
                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                gm_f_copy_from_f7j6p05(t121, t124);
            }
            goto ix113;
ie113:;
            void * t126 = gm_l5;
            gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t126) + 96));
            void * t128 = gm_l2;
            gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t128) + 96));
            gm_t88rcucg1b7uk t130 = t127;
            gm_t88rcucg1b7uk t131 = t129;
            uint8_t t132 = (gm_eq9m6mvtmgde(((uint8_t *)&t130), ((uint8_t *)&t131)));
            if (!t132) goto ie125;
            {
                gm_b16_8 t134 = gm_l6;
                gm_b16_8 t135 = gm_l8;
                gm_b16_8 t136 = t135;
                gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                gm_f_copy_from_f7j6p05(t134, t137);
            }
            goto ix125;
ie125:;
            void * t139 = gm_l0;
            void * t140 = gm_l5;
            gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t140) + 96));
            gm_t53at80pfdus3 t142;
            gm_f_kind_htv6093((void *)&t142, t139, GM_CONV(gm_csguankko4lmhd, t141));
            gm_t53at80pfdus3 t143 = t142;
            uint8_t t144;
            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
            if (t147 != ((uint8_t)13ULL)) goto mn146;
            t144 = ((uint8_t)1);
            goto me145;
mn146:;
            t144 = ((uint8_t)0);
            goto me145;
mn148:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 474, 15);
me145:;
            if (!t144) goto ie138;
            {
                void * t150 = gm_l0;
                void * t151 = gm_l5;
                gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t151) + 96));
                gm_b16_8 t153 = gm_l6;
                gm_b16_8 t154 = gm_l11;
                gm_b16_8 t155 = t154;
                gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
                gm_b0_1 t157 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t150, t152, t153, t156);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix138;
ie138:;
            {
                void * t159 = gm_l0;
                void * t160 = gm_l5;
                gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t160) + 96));
                gm_b16_8 t162 = gm_l6;
                gm_b16_8 t163 = gm_l11;
                gm_b16_8 t164 = t163;
                gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                gm_b0_1 t166 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t159, t161, t162, ((uint8_t)0), t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix138:;
ix125:;
ix113:;
        }
        {
            void * t167 = gm_l0;
            gm_t2lqu8jsj511d t168 = gm_l7;
            gm_f_release_jn5kqdi(t167, GM_CONV(gm_csc7lc6jlkqkuq, t168));
        }
    }
    gm_b0_1 t169;
    gm_tcfa6l7reoosn t170;
    GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t170 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t170 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t170 + 24, 0);
    ((uint8_t *)&t170)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t170 + 8), t169);
    gm_tcfa6l7reoosn t171 = t170;
    F.ret = t171;
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
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_ev_25jfdar F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_b16_8 gm_l3; /* dst */
    uint32_t gm_l4; /* l */
    uint64_t gm_l5; /* off */
    void * gm_l6; /* fr */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* o */
    uint32_t gm_l9; /* f */
    gm_b16_8 gm_l10; /* args */
    void * gm_l11; /* b */
    gm_trl2khmuphncd gm_l12; /* x */
    void * gm_l13; /* el */
    uint32_t gm_l14; /* levels */
    void * gm_l15; /* value */
    GM_STACK_CHECK(&F, 1727u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l2;
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma12; case ((uint8_t)59ULL): goto ma12; case ((uint8_t)60ULL): goto ma12; case ((uint8_t)61ULL): goto ma12; case ((uint8_t)62ULL): goto ma12; default: goto ma12; }
ma5:;
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t16 != ((uint8_t)6ULL)) goto mn15;
            uint32_t t17 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l4 = t17;
            uint64_t t18 = GM_LD(uint64_t, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l5 = t18;
            {
                void * t20 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                gm_l6 = t20;
            }
            {
                uint32_t t21 = gm_l4;
                uint64_t t22 = (uint64_t)t21;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                gm_l7 = t23;
            }
            {
                uint64_t t25 = gm_l7;
                uint64_t t26 = (uintptr_t)t25;
                void * t27 = gm_l6;
                uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 64) + 8);
                if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_src0jbci4ff2esa, 11, 16);
                uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 64)) + t26 * 8));
                uint8_t t30 = (!(t29 == ((uint64_t)0ULL)));
                if (!t30) goto ie24;
                {
                    void * t32 = gm_l0;
                    void * t33 = gm_l2;
                    gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 112));
                    uint64_t t35 = gm_l7;
                    uint64_t t36 = (uintptr_t)t35;
                    void * t37 = gm_l6;
                    uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 64) + 8);
                    if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_src0jbci4ff2esa, 11, 62);
                    uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t37) + 64)) + t36 * 8));
                    uint64_t t40 = gm_l7;
                    uint64_t t41 = (uintptr_t)t40;
                    void * t42 = gm_l6;
                    void * t43 = GM_LD(void *, (((uint8_t *)t42) + 16));
                    uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 104) + 8);
                    if (t41 >= (uintptr_t)(t44)) gm_trap_index(t41, t44, gm_src0jbci4ff2esa, 11, 73);
                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 104)) + t41 * 8));
                    gm_b16_8 t46 = gm_l3;
                    gm_f_load_at_6u81jcc((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t32, t34, t39, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix24;
ie24:;
ix24:;
            }
            {
                uint64_t t47 = gm_l5;
                uint64_t t48 = gm_f_us_cuv01fp(t47);
                gm_l8 = t48;
            }
            {
                gm_b16_8 t49 = gm_l3;
                void * t50 = gm_l6;
                uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 48) + 8);
                void * t52 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t50) + 48)));
                uint64_t t53 = gm_l8;
                uint64_t t54 = (uintptr_t)t53;
                uint64_t t55 = gm_l8;
                uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t r57; bool o58 = __builtin_add_overflow(t55, t56, &r57);
                if (o58) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 13, 39);
                uint64_t t59 = r57;
                uint64_t t60 = (uintptr_t)t59;
                if (t54 > t60 || t60 > (uintptr_t)t51) gm_trap_slice(t54, t60, t51, gm_src0jbci4ff2esa, 13, 27);
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t52 + t54 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60 - t54));
                gm_b16_8 t62 = t61;
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                gm_f_copy_from_f7j6p05(t49, t63);
            }
            goto me4;
mn15:;
ma6:;
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t65 != ((uint8_t)35ULL)) goto mn64;
            uint32_t t66 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l9 = t66;
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l10 = t67;
            void * t68 = gm_l0;
            gm_tbl8il92l97ut t69 = gm_l1;
            uint32_t t70 = gm_l9;
            gm_b16_8 t71 = gm_l10;
            void * t72 = gm_l2;
            gm_b16_8 t73 = gm_l3;
            gm_f_call_node_a1b30k9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t68, t69, t70, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn64:;
ma7:;
            uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t75 != ((uint8_t)51ULL)) goto mn74;
            void * t76 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l11 = t76;
            void * t77 = gm_l0;
            gm_tbl8il92l97ut t78 = gm_l1;
            void * t79 = gm_l11;
            gm_b16_8 t80 = gm_l3;
            gm_f_block_5b89ev3((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t77, t78, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn74:;
ma8:;
            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t82 != ((uint8_t)52ULL)) goto mn81;
            gm_trl2khmuphncd t83 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t3) + 0) + 8));
            gm_l12 = t83;
            {
                void * t86 = gm_l0;
                gm_tbl8il92l97ut t87 = gm_l1;
                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                gm_t1ec8br9ddkav t89;
                gm_f_conds_30ust77((void *)&t89, (void *)NULL, cx, NULL, t86, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t90 = t89;
                if (((((uint8_t *)&t90))[0] != 0)) {
                    gm_t76123gfet73g t91 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t90) + 8));
                    gm_tcfa6l7reoosn t92;
                    GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
                    ((uint8_t *)&t92)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t92 + 8), t91);
                    F.ret = t92;
                    goto gm_fret;
                }
                uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&t90) + 8));
                if (!t93) goto ie85;
                {
                    void * t95 = gm_l0;
                    gm_tbl8il92l97ut t96 = gm_l1;
                    void * t97 = GM_LD(void *, (((uint8_t *)&gm_l12) + 16));
                    gm_b16_8 t98 = gm_l3;
                    gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t95, t96, t97, t98);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix85;
ie85:;
ix85:;
            }
            {
                void * t100 = GM_LD(void *, (((uint8_t *)&gm_l12) + 24));
                void * t101 = t100;
                if (!(GM_LD(void *, ((uint8_t *)&t101)) != 0)) goto ie99;
                void * t102 = GM_LD(void *, ((uint8_t *)&t101));
                gm_l13 = t102;
                {
                    void * t104 = gm_l0;
                    gm_tbl8il92l97ut t105 = gm_l1;
                    void * t106 = gm_l13;
                    gm_b16_8 t107 = gm_l3;
                    gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t104, t105, t106, t107);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix99;
ie99:;
ix99:;
            }
            goto me4;
mn81:;
ma9:;
            uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t109 != ((uint8_t)56ULL)) goto mn108;
            uint32_t t110 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l14 = t110;
            void * t111 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l15 = t111;
            void * t112 = gm_l0;
            gm_tbl8il92l97ut t113 = gm_l1;
            uint32_t t114 = gm_l14;
            void * t115 = gm_l15;
            void * t116 = gm_l2;
            gm_f_ev_return_8ggbvjj((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t112, t113, t114, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn108:;
ma10:;
            uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t120 != ((uint8_t)0ULL)) goto pa119;
            goto po118;
pa119:;
            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t122 != ((uint8_t)21ULL)) goto pa121;
            goto po118;
pa121:;
            uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t124 != ((uint8_t)23ULL)) goto pa123;
            goto po118;
pa123:;
            uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t126 != ((uint8_t)24ULL)) goto pa125;
            goto po118;
pa125:;
            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t128 != ((uint8_t)25ULL)) goto pa127;
            goto po118;
pa127:;
            uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t130 != ((uint8_t)32ULL)) goto pa129;
            goto po118;
pa129:;
            uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t132 != ((uint8_t)26ULL)) goto pa131;
            goto po118;
pa131:;
            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t134 != ((uint8_t)27ULL)) goto pa133;
            goto po118;
pa133:;
            uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t136 != ((uint8_t)28ULL)) goto pa135;
            goto po118;
pa135:;
            uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t138 != ((uint8_t)29ULL)) goto pa137;
            goto po118;
pa137:;
            uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t140 != ((uint8_t)22ULL)) goto pa139;
            goto po118;
pa139:;
            goto mn117;
po118:;
            void * t141 = gm_l0;
            gm_b16_8 t142 = gm_l3;
            void * t143 = gm_l0;
            gm_tbl8il92l97ut t144 = gm_l1;
            void * t145 = gm_l2;
            gm_t833u93mr4iob t146;
            gm_f_ew_c5kil07((void *)&t146, (void *)NULL, cx, NULL, t143, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t147 = t146;
            if (((((uint8_t *)&t147))[0] != 0)) {
                gm_t76123gfet73g t148 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t147) + 8));
                gm_tcfa6l7reoosn t149;
                GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                ((uint8_t *)&t149)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t149 + 8), t148);
                F.ret = t149;
                goto gm_fret;
            }
            uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&t147) + 8));
            gm_f_wr_word_npte9fs(t141, t142, t150);
            goto me4;
mn117:;
ma11:;
            uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t154 != ((uint8_t)10ULL)) goto pa153;
            goto po152;
pa153:;
            uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t156 != ((uint8_t)12ULL)) goto pa155;
            goto po152;
pa155:;
            uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t158 != ((uint8_t)9ULL)) goto pa157;
            goto po152;
pa157:;
            goto mn151;
po152:;
            {
                void * t161 = gm_l2;
                uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)t161) + 124));
                if (!t162) goto ie160;
                {
                    void * t164 = gm_l0;
                    gm_tbl8il92l97ut t165 = gm_l1;
                    void * t166 = gm_l2;
                    gm_tl1r7a2fn1ev6 t167;
                    gm_f_place_47rfqe5((void *)&t167, (void *)NULL, cx, NULL, t164, t165, t166, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl1r7a2fn1ev6 t168 = t167;
                    if (((((uint8_t *)&t168))[0] != 0)) {
                        gm_t76123gfet73g t169 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t168) + 8));
                        gm_tcfa6l7reoosn t170;
                        GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                        ((uint8_t *)&t170)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t170 + 8), t169);
                        F.ret = t170;
                        goto gm_fret;
                    }
                    gm_tou12vuc19e0s t171 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t168) + 8));
                    F.l16 = t171;
                }
                {
                    void * t172 = gm_l0;
                    void * t173 = gm_l2;
                    gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 112));
                    void * t175 = gm_l2;
                    gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t175) + 96));
                    gm_b16_8 t177 = gm_l3;
                    gm_f_read_place_fcc38bb((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t172, t174, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l16)), t176, t177);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix160;
ie160:;
ix160:;
            }
            {
                void * t178 = gm_l0;
                gm_tbl8il92l97ut t179 = gm_l1;
                void * t180 = gm_l2;
                gm_b16_8 t181 = gm_l3;
                gm_f_ev_slow_hdak0k9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t178, t179, t180, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me4;
mn151:;
ma12:;
            void * t183 = gm_l0;
            gm_tbl8il92l97ut t184 = gm_l1;
            void * t185 = gm_l2;
            gm_b16_8 t186 = gm_l3;
            gm_f_ev_slow_hdak0k9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t183, t184, t185, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn182:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 7, 5);
me4:;
        }
    }
    gm_b0_1 t187;
    gm_tcfa6l7reoosn t188;
    GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t188 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t188 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t188 + 24, 0);
    ((uint8_t *)&t188)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t188 + 8), t187);
    gm_tcfa6l7reoosn t189 = t188;
    F.ret = t189;
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
inline __attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_ev_args_j1qj6gl F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* vals */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* x */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 493u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            {
                gm_b16_8 t4 = gm_l2;
                gm_l4 = t4;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t6 = gm_l5;
                    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t8 = (t6 < t7);
                    if (!t8) goto lb5;
                    {
                        uint64_t t11 = gm_l5;
                        uint64_t t12 = (uintptr_t)t11;
                        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        void * t14 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 8));
                        void * t15 = t14;
                        void * t18 = GM_LD(void *, ((uint8_t *)&t15));
                        gm_l6 = t18;
                        {
                            void * t20 = gm_l0;
                            void * t21 = gm_l6;
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 104));
                            gm_b16_8 t23 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t20, t22);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l7 = t23;
                        }
                        {
                            void * t24 = gm_l0;
                            gm_tbl8il92l97ut t25 = gm_l1;
                            void * t26 = gm_l6;
                            gm_b16_8 t27 = gm_l7;
                            gm_tcfa6l7reoosn t28;
                            gm_f_ev_25jfdar((void *)&t28, (void *)NULL, cx, NULL, t24, t25, t26, t27);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tcfa6l7reoosn t29 = t28;
                            if (((((uint8_t *)&t29))[0] != 0)) {
                                gm_t76123gfet73g t30 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t29) + 8));
                                gm_tcfa6l7reoosn t31;
                                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                                ((uint8_t *)&t31)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t31 + 8), t30);
                                F.ret = t31;
                                goto gm_fret;
                            }
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 8));
                        }
                        {
                            void * t33 = gm_l3;
                            gm_b16_8 t34 = gm_l7;
                            gm_tftb1odc739t5 t35 = gm_f_push_rluch9r(t33, t34);
                            gm_tftb1odc739t5 t36 = t35;
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_src0jbci4ff2esa, 570, 9);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        goto me16;
mn17:;
                        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 567, 5);
me16:;
                    }
lc5:;
                    {
                        uint64_t t39 = gm_l5;
                        uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                        uint64_t t42 = r40;
                        gm_l5 = t42;
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
    }
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
    F.ret = t45;
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
__attribute__((aligned(4))) void gm_f_ev_builtin_86a9q3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_ev_builtin_86a9q3q F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_b16_8 gm_l6; /* msg */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* b */
    gm_t9qkh0rsooc8d gm_l9; /* sop */
    gm_t2lqu8jsj511d gm_l10; /* mk */
    gm_t88rcucg1b7uk gm_l12; /* t0 */
    gm_t88rcucg1b7uk gm_l13; /* t1 */
    GM_STACK_CHECK(&F, 2724u);
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
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma12; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma12; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma12; case ((uint8_t)59ULL): goto ma12; case ((uint8_t)60ULL): goto ma12; case ((uint8_t)61ULL): goto ma4; case ((uint8_t)62ULL): goto ma12; case ((uint8_t)63ULL): goto ma6; case ((uint8_t)64ULL): goto ma12; case ((uint8_t)65ULL): goto ma12; case ((uint8_t)66ULL): goto ma12; case ((uint8_t)67ULL): goto ma12; case ((uint8_t)68ULL): goto ma12; case ((uint8_t)69ULL): goto ma12; case ((uint8_t)70ULL): goto ma12; case ((uint8_t)71ULL): goto ma12; case ((uint8_t)72ULL): goto ma8; case ((uint8_t)73ULL): goto ma7; case ((uint8_t)74ULL): goto ma12; case ((uint8_t)75ULL): goto ma12; case ((uint8_t)76ULL): goto ma12; case ((uint8_t)77ULL): goto ma12; case ((uint8_t)78ULL): goto ma12; case ((uint8_t)79ULL): goto ma10; case ((uint8_t)80ULL): goto ma11; case ((uint8_t)81ULL): goto ma5; default: goto ma12; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)61ULL)) goto mn15;
            {
                void * t19 = gm_l0;
                uint8_t t20 = gm_f_fast_23em7sv(t19);
                if (!t20) goto ie18;
                {
                    gm_b0_1 t22;
                    gm_tcfa6l7reoosn t23;
                    GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t23 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t23 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
                    ((uint8_t *)&t23)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
                    gm_tcfa6l7reoosn t24 = t23;
                    uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
                    F.ret = t23;
                    goto gm_fret;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                void * t27 = gm_l0;
                gm_tbl8il92l97ut t28 = gm_l1;
                uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_src0jbci4ff2esa, 578, 30);
                void * t31 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t29 * 8));
                gm_t1ec8br9ddkav t32;
                gm_f_cond_v8r7708((void *)&t32, (void *)NULL, cx, NULL, t27, t28, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t33 = t32;
                if (((((uint8_t *)&t33))[0] != 0)) {
                    gm_t76123gfet73g t34 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t33) + 8));
                    gm_tcfa6l7reoosn t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                    ((uint8_t *)&t35)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t35 + 8), t34);
                    F.ret = t35;
                    goto gm_fret;
                }
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t33) + 8));
                if (!t36) goto ie26;
                {
                    gm_b0_1 t38;
                    gm_tcfa6l7reoosn t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
                    ((uint8_t *)&t39)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), t38);
                    gm_tcfa6l7reoosn t40 = t39;
                    uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
                    F.ret = t39;
                    goto gm_fret;
                }
                goto ix26;
ie26:;
ix26:;
            }
            {
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(gm_spi8n2atkg603));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(16));
                gm_l6 = t42;
            }
            {
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t45 = (t44 > ((uint64_t)1ULL));
                if (!t45) goto ie43;
                {
                    void * t47 = gm_l0;
                    gm_t2lqu8jsj511d t48 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t47));
                    gm_l7 = t48;
                }
                {
                    void * t49 = gm_l0;
                    uint64_t t50 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src0jbci4ff2esa, 582, 28);
                    void * t52 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t50 * 8));
                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 104));
                    gm_b16_8 t54 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t49, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t54;
                }
                {
                    void * t55 = gm_l0;
                    gm_tbl8il92l97ut t56 = gm_l1;
                    uint64_t t57 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_src0jbci4ff2esa, 583, 29);
                    void * t59 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t57 * 8));
                    gm_b16_8 t60 = gm_l8;
                    gm_tcfa6l7reoosn t61;
                    gm_f_ev_25jfdar((void *)&t61, (void *)NULL, cx, NULL, t55, t56, t59, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t62 = t61;
                    if (((((uint8_t *)&t62))[0] != 0)) {
                        gm_t76123gfet73g t63 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t62) + 8));
                        gm_tcfa6l7reoosn t64;
                        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                        ((uint8_t *)&t64)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t64 + 8), t63);
                        F.ret = t64;
                        goto gm_fret;
                    }
                    gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 8));
                }
                {
                    void * t66 = gm_l0;
                    void * t67 = gm_l4;
                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 112));
                    gm_b16_8 t69 = gm_l8;
                    gm_b16_8 t70 = t69;
                    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                    gm_td0ept12e7q8b t72;
                    gm_f_read_string_ttuk4gr((void *)&t72, (void *)NULL, cx, NULL, t66, t68, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_td0ept12e7q8b t73 = t72;
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
                    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                    gm_l6 = t76;
                }
                {
                    void * t77 = gm_l0;
                    gm_t2lqu8jsj511d t78 = gm_l7;
                    gm_f_release_jn5kqdi(t77, GM_CONV(gm_csc7lc6jlkqkuq, t78));
                }
                goto ix43;
ie43:;
ix43:;
            }
            {
                uint8_t t80 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8) == 16) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l6)), gm_spi8n2atkg603, 16) == 0));
                if (!t80) goto ie79;
                {
                    gm_b16_8 t82 = gm_l6;
                    gm_tnpifv20irnph t83;
                    GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
                    gm_tabb710d9mq2e t84;
                    gm_f_format_e1vik51((void *)&t84, (void *)NULL, cx, NULL, t83);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t85 = t84;
                    if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_src0jbci4ff2esa, 587, 54);
                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
                    gm_b16_8 t87 = t86;
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                    gm_l6 = t88;
                }
                goto ix79;
ie79:;
ix79:;
            }
            {
                void * t89 = gm_l0;
                void * t90 = gm_l4;
                gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 112));
                gm_b16_8 t92 = gm_l6;
                gm_t76123gfet73g t93;
                gm_f_trap_qh84l9d((void *)&t93, (void *)NULL, cx, NULL, t89, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t94;
                GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                ((uint8_t *)&t94)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t94 + 8), t93);
                gm_tcfa6l7reoosn t95 = t94;
                uint8_t t96 = ((((uint8_t *)&t95))[0] != 0);
                F.ret = t94;
                goto gm_fret;
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t98 != ((uint8_t)81ULL)) goto mn97;
            gm_b0_1 t99;
            gm_tcfa6l7reoosn t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t100 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t100 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t100 + 24, 0);
            ((uint8_t *)&t100)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t100 + 8), t99);
            gm_tcfa6l7reoosn t101 = t100;
            uint8_t t102 = ((((uint8_t *)&t101))[0] != 0);
            F.ret = t100;
            goto gm_fret;
            goto me3;
mn97:;
ma6:;
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t104 != ((uint8_t)63ULL)) goto mn103;
            void * t105 = gm_l0;
            void * t106 = gm_l4;
            gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 112));
            gm_b16_8 t108;
            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sd3b7a2bnmpef));
            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(24));
            gm_t76123gfet73g t109;
            gm_f_trap_qh84l9d((void *)&t109, (void *)NULL, cx, NULL, t105, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t110;
            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
            ((uint8_t *)&t110)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t110 + 8), t109);
            gm_tcfa6l7reoosn t111 = t110;
            uint8_t t112 = ((((uint8_t *)&t111))[0] != 0);
            F.ret = t110;
            goto gm_fret;
            goto me3;
mn103:;
ma7:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t114 != ((uint8_t)73ULL)) goto mn113;
            void * t115 = gm_l0;
            gm_tbl8il92l97ut t116 = gm_l1;
            gm_b16_8 t117 = gm_l3;
            void * t118 = gm_l4;
            gm_b16_8 t119 = gm_l5;
            gm_f_fiber_builtin_rpc9l9s((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t115, t116, t117, t118, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn113:;
ma8:;
            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t121 != ((uint8_t)72ULL)) goto mn120;
            gm_t9qkh0rsooc8d t122 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l9 = t122;
            void * t123 = gm_l0;
            gm_tbl8il92l97ut t124 = gm_l1;
            gm_t9qkh0rsooc8d t125 = gm_l9;
            gm_b16_8 t126 = gm_l3;
            void * t127 = gm_l4;
            gm_b16_8 t128 = gm_l5;
            gm_f_sys_call_6jqv32o((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t123, t124, t125, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn120:;
ma9:;
            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t132 != ((uint8_t)36ULL)) goto pa131;
            goto po130;
pa131:;
            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t134 != ((uint8_t)37ULL)) goto pa133;
            goto po130;
pa133:;
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t136 != ((uint8_t)38ULL)) goto pa135;
            goto po130;
pa135:;
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t138 != ((uint8_t)39ULL)) goto pa137;
            goto po130;
pa137:;
            goto mn129;
po130:;
            void * t139 = gm_l0;
            gm_tbl8il92l97ut t140 = gm_l1;
            gm_tvdg94bk7aa22 t141 = gm_l2;
            gm_b16_8 t142 = gm_l3;
            void * t143 = gm_l4;
            gm_b16_8 t144 = gm_l5;
            gm_f_get_builtin_s6t151o((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t139, t140, t141, t142, t143, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn129:;
ma10:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t146 != ((uint8_t)79ULL)) goto mn145;
            void * t147 = gm_l0;
            void * t148 = gm_l4;
            gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t148) + 112));
            gm_b16_8 t150;
            GM_ST(void *, (uint8_t *)&t150, (void *)(gm_snh5ve0osdp08));
            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(6));
            gm_t76123gfet73g t151;
            gm_f_unsupported_70vvk1e((void *)&t151, (void *)NULL, cx, NULL, t147, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t152;
            GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
            ((uint8_t *)&t152)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t152 + 8), t151);
            gm_tcfa6l7reoosn t153 = t152;
            uint8_t t154 = ((((uint8_t *)&t153))[0] != 0);
            F.ret = t152;
            goto gm_fret;
            goto me3;
mn145:;
ma11:;
            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t156 != ((uint8_t)80ULL)) goto mn155;
            gm_b0_1 t157;
            gm_tcfa6l7reoosn t158;
            GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 24, 0);
            ((uint8_t *)&t158)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t158 + 8), t157);
            gm_tcfa6l7reoosn t159 = t158;
            uint8_t t160 = ((((uint8_t *)&t159))[0] != 0);
            F.ret = t158;
            goto gm_fret;
            goto me3;
mn155:;
ma12:;
            goto me3;
mn161:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 575, 5);
me3:;
        }
        {
            void * t163 = gm_l0;
            gm_t2lqu8jsj511d t164 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t163));
            gm_l10 = t164;
        }
        {
            gm_f_list_fufbgf7((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t166 = gm_l0;
            gm_tbl8il92l97ut t167 = gm_l1;
            gm_b16_8 t168 = gm_l3;
            void * t169 = (void *)((uint8_t *)&F.l11);
            gm_tcfa6l7reoosn t170;
            gm_f_ev_args_j1qj6gl((void *)&t170, (void *)NULL, cx, NULL, t166, t167, t168, t169);
            if (gm_unw.kind) {
                goto gm_ld2_1;
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
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld2_1;
            }
            gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 8));
        }
        {
            uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t179 != ((uint8_t)74ULL)) goto pa178;
            goto po177;
pa178:;
            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t181 != ((uint8_t)75ULL)) goto pa180;
            goto po177;
pa180:;
            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t183 != ((uint8_t)76ULL)) goto pa182;
            goto po177;
pa182:;
            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t185 != ((uint8_t)77ULL)) goto pa184;
            goto po177;
pa184:;
            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t187 != ((uint8_t)78ULL)) goto pa186;
            goto po177;
pa186:;
            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t189 != ((uint8_t)64ULL)) goto pa188;
            goto po177;
pa188:;
            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t191 != ((uint8_t)65ULL)) goto pa190;
            goto po177;
pa190:;
            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t193 != ((uint8_t)66ULL)) goto pa192;
            goto po177;
pa192:;
            uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t195 != ((uint8_t)67ULL)) goto pa194;
            goto po177;
pa194:;
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t197 != ((uint8_t)68ULL)) goto pa196;
            goto po177;
pa196:;
            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t199 != ((uint8_t)70ULL)) goto pa198;
            goto po177;
pa198:;
            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t201 != ((uint8_t)71ULL)) goto pa200;
            goto po177;
pa200:;
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t203 != ((uint8_t)69ULL)) goto pa202;
            goto po177;
pa202:;
            uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t205 != ((uint8_t)60ULL)) goto pa204;
            goto po177;
pa204:;
            uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t207 != ((uint8_t)58ULL)) goto pa206;
            goto po177;
pa206:;
            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t209 != ((uint8_t)59ULL)) goto pa208;
            goto po177;
pa208:;
            goto mn176;
po177:;
            void * t210 = gm_l0;
            void * t211 = gm_l4;
            gm_tl874t2atnlh3 t212 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t211) + 112));
            gm_tvdg94bk7aa22 t213 = gm_l2;
            gm_b16_8 t214 = gm_l3;
            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            void * t216 = gm_l4;
            gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t216) + 96));
            gm_b16_8 t218 = gm_l5;
            gm_tcfa6l7reoosn t219;
            gm_f_memory_builtin_9s47dk8((void *)&t219, (void *)NULL, cx, NULL, t210, t212, t213, t214, t215, t217, t218);
            if (gm_unw.kind) {
                goto gm_ld2_1;
            }
            gm_tcfa6l7reoosn t220 = t219;
            if (((((uint8_t *)&t220))[0] != 0)) {
                gm_t76123gfet73g t221 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t220) + 8));
                gm_tcfa6l7reoosn t222;
                GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                ((uint8_t *)&t222)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t222 + 8), t221);
                F.ret = t222;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld2_1;
            }
            gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t220) + 8));
            goto me175;
mn176:;
            {
                gm_t88rcucg1b7uk t226;
                uint64_t t228 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t229 = (t228 > ((uint64_t)0ULL));
                if (!t229) goto ie227;
                uint64_t t231 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t232 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t231 >= (uintptr_t)(t232)) gm_trap_index(t231, t232, gm_src0jbci4ff2esa, 606, 37);
                void * t233 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t231 * 8));
                gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t233) + 96));
                gm_t88rcucg1b7uk t235 = t234;
                t226 = t235;
                goto ix227;
ie227:;
                void * t237 = gm_l4;
                gm_t88rcucg1b7uk t238 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t237) + 96));
                gm_t88rcucg1b7uk t239 = t238;
                t226 = t239;
ix227:;
                gm_l12 = t226;
            }
            {
                gm_t88rcucg1b7uk t240;
                uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t243 = (t242 > ((uint64_t)1ULL));
                if (!t243) goto ie241;
                uint64_t t245 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t245 >= (uintptr_t)(t246)) gm_trap_index(t245, t246, gm_src0jbci4ff2esa, 607, 37);
                void * t247 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t245 * 8));
                gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t247) + 96));
                gm_t88rcucg1b7uk t249 = t248;
                t240 = t249;
                goto ix241;
ie241:;
                gm_t88rcucg1b7uk t251 = gm_l12;
                gm_t88rcucg1b7uk t252 = t251;
                t240 = t252;
ix241:;
                gm_l13 = t240;
            }
            {
                void * t253 = gm_l0;
                void * t254 = gm_l4;
                gm_tl874t2atnlh3 t255 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t254) + 112));
                gm_tvdg94bk7aa22 t256 = gm_l2;
                void * t257 = gm_l4;
                gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t257) + 96));
                gm_t88rcucg1b7uk t259 = gm_l12;
                gm_t88rcucg1b7uk t260 = gm_l13;
                gm_b16_8 t261 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_b16_8 t262 = gm_l5;
                gm_tcfa6l7reoosn t263;
                gm_f_apply_builtin_41dffti((void *)&t263, (void *)NULL, cx, NULL, t253, t255, t256, t258, t259, t260, t261, t262);
                if (gm_unw.kind) {
                    goto gm_ld2_1;
                }
                gm_tcfa6l7reoosn t264 = t263;
                if (((((uint8_t *)&t264))[0] != 0)) {
                    gm_t76123gfet73g t265 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t264) + 8));
                    gm_tcfa6l7reoosn t266;
                    GM_ST(uint8_t, (uint8_t *)&t266 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t266 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t266 + 4, 0);
                    ((uint8_t *)&t266)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t266 + 8), t265);
                    F.ret = t266;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ld2_1;
                }
                gm_b0_1 t267 = GM_LD(gm_b0_1, (((uint8_t *)&t264) + 8));
            }
            goto me175;
mn224:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 603, 5);
me175:;
        }
        {
            void * t268 = gm_l0;
            gm_t2lqu8jsj511d t269 = gm_l10;
            gm_f_release_jn5kqdi(t268, GM_CONV(gm_csc7lc6jlkqkuq, t269));
        }
        {
            void * t270 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_tq06jtm(t270);
        }
        goto gm_ls271;
gm_ld2_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t272 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_tq06jtm(t272);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls271:;
    }
    gm_b0_1 t273;
    gm_tcfa6l7reoosn t274;
    GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t274 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t274 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t274 + 24, 0);
    ((uint8_t *)&t274)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t274 + 8), t273);
    gm_tcfa6l7reoosn t275 = t274;
    F.ret = t275;
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
inline __attribute__((aligned(4))) void gm_f_ev_discard_8c4a8jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ev_discard_8c4a8jg F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_t2lqu8jsj511d gm_l3; /* mk */
    gm_b16_8 gm_l4; /* b */
    GM_STACK_CHECK(&F, 626u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t4 = gm_l2;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 104));
            uint8_t t6 = (t5 <= ((uint64_t)8ULL));
            if (!t6) goto ie3;
            {
                {
                    void * t9 = gm_l0;
                    gm_tbl8il92l97ut t10 = gm_l1;
                    void * t11 = gm_l2;
                    gm_t833u93mr4iob t12;
                    gm_f_ew_c5kil07((void *)&t12, (void *)NULL, cx, NULL, t9, t10, t11);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t833u93mr4iob t13 = t12;
                    if (((((uint8_t *)&t13))[0] != 0)) {
                        gm_t76123gfet73g t14 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t13) + 8));
                        gm_tcfa6l7reoosn t15;
                        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
                        ((uint8_t *)&t15)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t15 + 8), t14);
                        F.ret = t15;
                        goto gm_fret;
                    }
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&t13) + 8));
                }
            }
            {
                gm_b0_1 t17;
                gm_tcfa6l7reoosn t18;
                GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
                ((uint8_t *)&t18)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
                gm_tcfa6l7reoosn t19 = t18;
                uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
                F.ret = t18;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t21 = gm_l0;
            gm_t2lqu8jsj511d t22 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t21));
            gm_l3 = t22;
        }
        {
            void * t23 = gm_l0;
            void * t24 = gm_l2;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 104));
            gm_b16_8 t26 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t23, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t26;
        }
        {
            void * t27 = gm_l0;
            gm_tbl8il92l97ut t28 = gm_l1;
            void * t29 = gm_l2;
            gm_b16_8 t30 = gm_l4;
            gm_tcfa6l7reoosn t31;
            gm_f_ev_25jfdar((void *)&t31, (void *)NULL, cx, NULL, t27, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) {
                gm_t76123gfet73g t33 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t32) + 8));
                gm_tcfa6l7reoosn t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                ((uint8_t *)&t34)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t34 + 8), t33);
                F.ret = t34;
                goto gm_fret;
            }
            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 8));
        }
        {
            void * t36 = gm_l0;
            gm_t2lqu8jsj511d t37 = gm_l3;
            gm_f_release_jn5kqdi(t36, GM_CONV(gm_csc7lc6jlkqkuq, t37));
        }
    }
    gm_b0_1 t38;
    gm_tcfa6l7reoosn t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
    ((uint8_t *)&t39)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), t38);
    gm_tcfa6l7reoosn t40 = t39;
    F.ret = t40;
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
inline __attribute__((aligned(4))) void gm_f_ev_index_bie377m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ev_index_bie377m F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_t51b2en4qp02s gm_l3; /* b */
    uint64_t gm_l4; /* k */
    unsigned __int128 gm_l5; /* v */
    GM_STACK_CHECK(&F, 715u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
        uint8_t t5 = (t4 <= ((uint64_t)8ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_tbl8il92l97ut t8 = gm_l1;
            void * t9 = gm_l2;
            gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
    }
    {
        void * t10 = gm_l2;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 104));
        uint64_t t12 = gm_f_us_cuv01fp(t11);
        gm_l4 = t12;
    }
    {
        void * t13 = ((uint8_t *)&gm_l3);
        uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t15 = gm_l4;
        uint64_t t16 = (uintptr_t)t15;
        if (t14 > t16 || t16 > (uintptr_t)16) gm_trap_slice(t14, t16, 16, gm_src0jbci4ff2esa, 384, 5);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + t14 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16 - t14));
        gm_f_fill_abmf5g5(t17, ((uint8_t)0ULL));
    }
    {
        void * t18 = gm_l0;
        gm_tbl8il92l97ut t19 = gm_l1;
        void * t20 = gm_l2;
        void * t21 = ((uint8_t *)&gm_l3);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l4;
        uint64_t t24 = (uintptr_t)t23;
        if (t22 > t24 || t24 > (uintptr_t)16) gm_trap_slice(t22, t24, 16, gm_src0jbci4ff2esa, 385, 20);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + t22 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
        gm_tcfa6l7reoosn t26;
        gm_f_ev_25jfdar((void *)&t26, (void *)NULL, cx, NULL, t18, t19, t20, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) {
            gm_t76123gfet73g t28 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t27) + 8));
            gm_t833u93mr4iob t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t29 + 8), t28);
            F.ret = t29;
            goto gm_fret;
        }
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
    }
    {
        void * t31 = gm_l0;
        void * t32 = ((uint8_t *)&gm_l3);
        uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t34 = gm_l4;
        uint64_t t35 = (uintptr_t)t34;
        if (t33 > t35 || t35 > (uintptr_t)16) gm_trap_slice(t33, t35, 16, gm_src0jbci4ff2esa, 386, 21);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t32 + t33 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t33));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        unsigned __int128 t39 = gm_f_get_uint_bq8c4b7(t31, t38);
        gm_l5 = t39;
    }
    {
        gm_t833u93mr4iob t40;
        unsigned __int128 t42 = gm_l5;
        uint64_t t43 = (uint64_t)~((uint64_t)0ULL);
        unsigned __int128 t44 = (unsigned __int128)t43;
        uint8_t t45 = (t42 > t44);
        if (!t45) goto ie41;
        uint64_t t47 = (uint64_t)~((uint64_t)0ULL);
        uint64_t t48 = t47;
        gm_t833u93mr4iob t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t49 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t49 + 24, 0);
        ((uint8_t *)&t49)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t49 + 8), t48);
        t40 = t49;
        goto ix41;
ie41:;
        unsigned __int128 t51 = gm_l5;
        uint64_t t52 = (uint64_t)t51;
        uint64_t t53 = t52;
        gm_t833u93mr4iob t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
        ((uint8_t *)&t54)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t54 + 8), t53);
        t40 = t54;
ix41:;
        gm_t833u93mr4iob t55 = t40;
        uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
        F.ret = t40;
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
inline __attribute__((aligned(4))) void gm_f_ev_return_8ggbvjj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, void * a3, void * a4) {
    struct gm_fr_ev_return_8ggbvjj F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint32_t gm_l2; /* levels */
    void * gm_l3; /* value */
    void * gm_l4; /* n */
    void * gm_l5; /* target */
    uint64_t gm_l6; /* o */
    uint8_t gm_l7; /* is_err */
    GM_STACK_CHECK(&F, 637u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        uint32_t t3 = gm_l2;
        void * t4 = gm_f_frame_up_ghkkuqg(t2, t3);
        void * t5 = t4;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            void * t9 = gm_l4;
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 112));
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sar5aak6f8jpo));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(43));
            gm_t76123gfet73g t12;
            gm_f_trap_qh84l9d((void *)&t12, (void *)NULL, cx, NULL, t8, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t13 + 8), t12);
            gm_tcfa6l7reoosn t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l5 = t6;
    }
    {
        void * t16 = gm_l0;
        gm_tbl8il92l97ut t17 = gm_l1;
        void * t18 = gm_l3;
        void * t19 = gm_l5;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 24));
        gm_tcfa6l7reoosn t21;
        gm_f_ev_25jfdar((void *)&t21, (void *)NULL, cx, NULL, t16, t17, t18, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t22 = t21;
        if (((((uint8_t *)&t22))[0] != 0)) {
            gm_t76123gfet73g t23 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t22) + 8));
            gm_tcfa6l7reoosn t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t24 + 8), t23);
            F.ret = t24;
            goto gm_fret;
        }
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 8));
    }
    {
        uint8_t t26;
        void * t28 = gm_l5;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 16));
        if (!(((((uint8_t *)t29) + 32))[8] != 0)) goto ie27;
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 32));
        gm_l6 = t30;
        void * t32 = gm_l5;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 24));
        gm_b16_8 t34 = t33;
        uint64_t t35 = gm_l6;
        uint64_t t36 = gm_f_us_cuv01fp(t35);
        uint64_t t37 = (uintptr_t)t36;
        gm_b16_8 t38 = t34;
        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&t38) + 8);
        if (t37 >= (uintptr_t)(t39)) gm_trap_index(t37, t39, gm_src0jbci4ff2esa, 127, 46);
        uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t38)) + t37 * 1));
        uint8_t t41 = (!(t40 == ((uint8_t)0ULL)));
        uint8_t t42 = t41;
        t26 = t42;
        goto ix27;
ie27:;
        uint8_t t44 = ((uint8_t)0);
        t26 = t44;
ix27:;
        gm_l7 = t26;
    }
    {
        void * t45 = gm_l5;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 0));
        uint8_t t47 = gm_l7;
        gm_tf5srh4mp4b3i t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t48 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t48 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t46);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 8), t47);
        gm_t76123gfet73g t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t49 + 8), t48);
        gm_tcfa6l7reoosn t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        ((uint8_t *)&t50)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t50 + 8), t49);
        gm_tcfa6l7reoosn t51 = t50;
        uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
        F.ret = t50;
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
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ev_slow_hdak0k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_ev_slow_hdak0k9 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_b16_8 gm_l3; /* dst */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* bytes */
    uint64_t gm_l6; /* addr */
    gm_b16_8 gm_l7; /* bytes */
    uint64_t gm_l8; /* addr */
    uint32_t gm_l9; /* l */
    uint32_t gm_l10; /* levels */
    uint32_t gm_l11; /* l */
    void * gm_l12; /* fr */
    gm_t1j70din2upc4 gm_l14; /* x */
    gm_t2lqu8jsj511d gm_l16; /* mk */
    gm_b16_8 gm_l17; /* b */
    uint64_t gm_l18; /* o */
    gm_tdvcgvrf81sbd gm_l19; /* x */
    gm_t2lqu8jsj511d gm_l21; /* mk */
    gm_b16_8 gm_l22; /* b */
    gm_t2lqu8jsj511d gm_l24; /* mk */
    gm_b16_8 gm_l25; /* b */
    uint64_t gm_l26; /* i */
    uint64_t gm_l27; /* off */
    void * gm_l30; /* x */
    gm_t2lqu8jsj511d gm_l31; /* mk */
    gm_b16_8 gm_l32; /* b */
    void * gm_l33; /* x */
    gm_t2lqu8jsj511d gm_l34; /* mk */
    gm_b16_8 gm_l35; /* b */
    void * gm_l36; /* x */
    uint64_t gm_l37; /* addr */
    uint64_t gm_l38; /* addr */
    void * gm_l39; /* x */
    uint64_t gm_l40; /* align */
    gm_t2lqu8jsj511d gm_l41; /* mk */
    gm_b16_8 gm_l42; /* b */
    uint64_t gm_l43; /* addr */
    uint32_t gm_l44; /* f */
    uint8_t gm_l45; /* second */
    uint32_t gm_l46; /* f */
    gm_t9qkh0rsooc8d gm_l47; /* op */
    void * gm_l48; /* x */
    gm_ts6oqppjh0ba3 gm_l49; /* op */
    void * gm_l50; /* l */
    void * gm_l51; /* r */
    gm_tmn7n0n9raou5 gm_l52; /* kind */
    void * gm_l53; /* x */
    gm_tvdg94bk7aa22 gm_l54; /* op */
    gm_b16_8 gm_l55; /* args */
    uint32_t gm_l56; /* f */
    gm_b16_8 gm_l57; /* args */
    void * gm_l59; /* callee */
    gm_b16_8 gm_l60; /* args */
    gm_b16_8 gm_l61; /* items */
    gm_b16_8 gm_l62; /* offs */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* <tmp> */
    uint64_t gm_l65; /* i */
    uint64_t gm_l66; /* o */
    void * gm_l67; /* x */
    void * gm_l68; /* v */
    gm_b16_8 gm_l69; /* items */
    gm_t88rcucg1b7uk gm_l70; /* u */
    gm_t2lqu8jsj511d gm_l71; /* mk */
    unsigned __int128 gm_l72; /* acc */
    uint64_t gm_l73; /* <tmp> */
    uint64_t gm_l74; /* <tmp> */
    uint64_t gm_l75; /* i */
    gm_b16_8 gm_l76; /* b */
    gm_tplpbqf3duvn3 gm_l77; /* bf */
    gm_b16_8 gm_l78; /* items */
    uint64_t gm_l79; /* k */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    uint64_t gm_l82; /* i */
    void * gm_l83; /* x */
    uint64_t gm_l84; /* k */
    uint64_t gm_l85; /* j */
    uint32_t gm_l86; /* variant */
    void * gm_l87; /* payload */
    void * gm_l88; /* p */
    gm_t2lqu8jsj511d gm_l89; /* mk */
    gm_b16_8 gm_l90; /* b */
    void * gm_l91; /* x */
    gm_t2lqu8jsj511d gm_l92; /* mk */
    gm_b16_8 gm_l93; /* b */
    void * gm_l94; /* x */
    uint8_t gm_l95; /* is_result */
    gm_t3dcoc7077t1q gm_l96; /* x */
    gm_t083kd7b1kkag gm_l97; /* x */
    void * gm_l98; /* l */
    void * gm_l99; /* r */
    uint8_t gm_l100; /* negated */
    gm_t2lqu8jsj511d gm_l101; /* mk */
    gm_b16_8 gm_l102; /* x */
    gm_b16_8 gm_l103; /* y */
    uint8_t gm_l104; /* v */
    void * gm_l105; /* elem */
    void * gm_l106; /* container */
    uint8_t gm_l107; /* negated */
    gm_t2lqu8jsj511d gm_l108; /* mk */
    gm_b16_8 gm_l109; /* x */
    gm_b16_8 gm_l110; /* c */
    uint8_t gm_l111; /* v */
    void * gm_l112; /* scrutinee */
    gm_b16_8 gm_l113; /* arms */
    void * gm_l114; /* b */
    gm_trl2khmuphncd gm_l115; /* x */
    void * gm_l116; /* el */
    gm_te9hrk0u35rev gm_l118; /* target */
    void * gm_l119; /* value */
    gm_tbl8il92l97ut gm_l120; /* t */
    void * gm_l121; /* v */
    gm_b16_8 gm_l122; /* d */
    gm_te9hrk0u35rev gm_l123; /* target */
    gm_tbl8il92l97ut gm_l124; /* t */
    uint32_t gm_l125; /* levels */
    void * gm_l126; /* value */
    void * gm_l127; /* msg */
    gm_t2lqu8jsj511d gm_l128; /* mk */
    gm_b16_8 gm_l129; /* b */
    gm_b16_8 gm_l130; /* s */
    void * gm_l131; /* k */
    void * gm_l132; /* v */
    void * gm_l133; /* k */
    GM_STACK_CHECK(&F, 20276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l2;
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            switch (t57) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma5; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma25; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma27; case ((uint8_t)35ULL): goto ma28; case ((uint8_t)36ULL): goto ma29; case ((uint8_t)37ULL): goto ma30; case ((uint8_t)38ULL): goto ma31; case ((uint8_t)39ULL): goto ma32; case ((uint8_t)40ULL): goto ma33; case ((uint8_t)41ULL): goto ma34; case ((uint8_t)42ULL): goto ma35; case ((uint8_t)43ULL): goto ma36; case ((uint8_t)44ULL): goto ma37; case ((uint8_t)45ULL): goto ma38; case ((uint8_t)46ULL): goto ma39; case ((uint8_t)47ULL): goto ma40; case ((uint8_t)48ULL): goto ma41; case ((uint8_t)49ULL): goto ma42; case ((uint8_t)50ULL): goto ma43; case ((uint8_t)51ULL): goto ma44; case ((uint8_t)52ULL): goto ma45; case ((uint8_t)53ULL): goto ma46; case ((uint8_t)54ULL): goto ma47; case ((uint8_t)55ULL): goto ma48; case ((uint8_t)56ULL): goto ma49; case ((uint8_t)57ULL): goto ma50; case ((uint8_t)58ULL): goto ma51; case ((uint8_t)59ULL): goto ma52; case ((uint8_t)60ULL): goto ma53; case ((uint8_t)61ULL): goto ma54; case ((uint8_t)62ULL): goto ma55; default: goto ma56; }
ma5:;
            uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t61 != ((uint8_t)0ULL)) goto pa60;
            goto po59;
pa60:;
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t63 != ((uint8_t)21ULL)) goto pa62;
            goto po59;
pa62:;
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t65 != ((uint8_t)23ULL)) goto pa64;
            goto po59;
pa64:;
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t67 != ((uint8_t)24ULL)) goto pa66;
            goto po59;
pa66:;
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t69 != ((uint8_t)25ULL)) goto pa68;
            goto po59;
pa68:;
            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t71 != ((uint8_t)32ULL)) goto pa70;
            goto po59;
pa70:;
            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t73 != ((uint8_t)26ULL)) goto pa72;
            goto po59;
pa72:;
            uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t75 != ((uint8_t)27ULL)) goto pa74;
            goto po59;
pa74:;
            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t77 != ((uint8_t)28ULL)) goto pa76;
            goto po59;
pa76:;
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t79 != ((uint8_t)29ULL)) goto pa78;
            goto po59;
pa78:;
            uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t81 != ((uint8_t)22ULL)) goto pa80;
            goto po59;
pa80:;
            goto mn58;
po59:;
            void * t82 = gm_l0;
            gm_b16_8 t83 = gm_l3;
            void * t84 = gm_l0;
            gm_tbl8il92l97ut t85 = gm_l1;
            void * t86 = gm_l2;
            gm_t833u93mr4iob t87;
            gm_f_ew_c5kil07((void *)&t87, (void *)NULL, cx, NULL, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t88 = t87;
            if (((((uint8_t *)&t88))[0] != 0)) {
                gm_t76123gfet73g t89 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t88) + 8));
                gm_tcfa6l7reoosn t90;
                GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
                ((uint8_t *)&t90)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t90 + 8), t89);
                F.ret = t90;
                goto gm_fret;
            }
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&t88) + 8));
            gm_f_wr_word_npte9fs(t82, t83, t91);
            goto me4;
mn58:;
ma6:;
            uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t93 != ((uint8_t)1ULL)) goto mn92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l4 = t94;
            gm_b16_8 t95 = gm_l3;
            gm_b16_8 t96 = gm_l4;
            gm_b16_8 t97 = t96;
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
            gm_f_copy_from_f7j6p05(t95, t98);
            goto me4;
mn92:;
ma7:;
            uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t100 != ((uint8_t)2ULL)) goto mn99;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l5 = t101;
            {
                void * t103 = gm_l0;
                void * t104 = gm_l2;
                gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 112));
                gm_b16_8 t106 = gm_l5;
                gm_t833u93mr4iob t107;
                gm_f_string_literal_337m6cd((void *)&t107, (void *)NULL, cx, NULL, t103, t105, t106, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t108 = t107;
                if (((((uint8_t *)&t108))[0] != 0)) {
                    gm_t76123gfet73g t109 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t108) + 8));
                    gm_tcfa6l7reoosn t110;
                    GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                    ((uint8_t *)&t110)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t110 + 8), t109);
                    F.ret = t110;
                    goto gm_fret;
                }
                uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&t108) + 8));
                gm_l6 = t111;
            }
            {
                void * t112 = gm_l0;
                gm_b16_8 t113 = gm_l3;
                uint64_t t114 = gm_l6;
                uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t116 = (uint64_t)t115;
                gm_f_put_slice_ko14loi(t112, t113, t114, t116);
            }
            goto me4;
mn99:;
ma8:;
            uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t118 != ((uint8_t)3ULL)) goto mn117;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l7 = t119;
            {
                void * t121 = gm_l0;
                void * t122 = gm_l2;
                gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 112));
                gm_b16_8 t124 = gm_l7;
                gm_t833u93mr4iob t125;
                gm_f_string_literal_337m6cd((void *)&t125, (void *)NULL, cx, NULL, t121, t123, t124, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t126 = t125;
                if (((((uint8_t *)&t126))[0] != 0)) {
                    gm_t76123gfet73g t127 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t126) + 8));
                    gm_tcfa6l7reoosn t128;
                    GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                    ((uint8_t *)&t128)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t128 + 8), t127);
                    F.ret = t128;
                    goto gm_fret;
                }
                uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&t126) + 8));
                gm_l8 = t129;
            }
            {
                void * t130 = gm_l0;
                gm_b16_8 t131 = gm_l3;
                uint64_t t132 = gm_l8;
                gm_f_put_ptr_188p3mm(t130, t131, t132);
            }
            goto me4;
mn117:;
ma9:;
            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t134 != ((uint8_t)5ULL)) goto mn133;
            gm_b16_8 t135 = gm_l3;
            uint8_t t136;
            void * t138 = gm_l0;
            uint8_t t139 = ((GM_LD(uint8_t, (((uint8_t *)t138) + 88) + 0) == ((uint8_t)0ULL)));
            if (!t139) goto ie137;
            uint8_t t141 = ((uint8_t)170ULL);
            t136 = t141;
            goto ix137;
ie137:;
            uint8_t t143 = ((uint8_t)0ULL);
            t136 = t143;
ix137:;
            gm_f_fill_abmf5g5(t135, t136);
            goto me4;
mn133:;
ma10:;
            uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t145 != ((uint8_t)6ULL)) goto mn144;
            uint32_t t146 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l9 = t146;
            void * t147 = gm_l0;
            void * t148 = gm_l2;
            gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t148) + 112));
            void * t150 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            uint32_t t151 = gm_l9;
            gm_b16_8 t152 = gm_l3;
            gm_tcfa6l7reoosn t153;
            gm_f_read_local_n8sc5ua((void *)&t153, (void *)NULL, cx, NULL, t147, t149, t150, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t154 = t153;
            if (((((uint8_t *)&t154))[0] != 0)) {
                gm_t76123gfet73g t155 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t154) + 8));
                gm_tcfa6l7reoosn t156;
                GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
                ((uint8_t *)&t156)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t156 + 8), t155);
                F.ret = t156;
                goto gm_fret;
            }
            gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 8));
            goto me4;
mn144:;
ma11:;
            uint8_t t159 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t159 != ((uint8_t)7ULL)) goto mn158;
            uint32_t t160 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l10 = t160;
            uint32_t t161 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 4));
            gm_l11 = t161;
            {
                void * t163 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint32_t t164 = gm_l10;
                void * t165 = gm_f_frame_up_ghkkuqg(t163, t164);
                void * t166 = t165;
                void * t167;
                if ((GM_LD(void *, ((uint8_t *)&t166)) != 0)) {
                    void * t168 = GM_LD(void *, (((uint8_t *)&t166) + 0));
                    t167 = t168;
                } else {
                    void * t169 = gm_l0;
                    void * t170 = gm_l2;
                    gm_tl874t2atnlh3 t171 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t170) + 112));
                    gm_b16_8 t172;
                    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sco971hnpq7nk));
                    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(48));
                    gm_t76123gfet73g t173;
                    gm_f_trap_qh84l9d((void *)&t173, (void *)NULL, cx, NULL, t169, t171, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t174;
                    GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                    ((uint8_t *)&t174)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t174 + 8), t173);
                    gm_tcfa6l7reoosn t175 = t174;
                    uint8_t t176 = ((((uint8_t *)&t175))[0] != 0);
                    F.ret = t174;
                    goto gm_fret;
                }
                gm_l12 = t167;
            }
            {
                void * t177 = gm_l0;
                void * t178 = gm_l2;
                gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 112));
                void * t180 = gm_l12;
                uint32_t t181 = gm_l11;
                gm_b16_8 t182 = gm_l3;
                gm_tcfa6l7reoosn t183;
                gm_f_read_local_n8sc5ua((void *)&t183, (void *)NULL, cx, NULL, t177, t179, t180, t181, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t184 = t183;
                if (((((uint8_t *)&t184))[0] != 0)) {
                    gm_t76123gfet73g t185 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t184) + 8));
                    gm_tcfa6l7reoosn t186;
                    GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                    ((uint8_t *)&t186)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t186 + 8), t185);
                    F.ret = t186;
                    goto gm_fret;
                }
                gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 8));
            }
            goto me4;
mn158:;
ma12:;
            uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t191 != ((uint8_t)4ULL)) goto pa190;
            goto po189;
pa190:;
            uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t193 != ((uint8_t)8ULL)) goto pa192;
            goto po189;
pa192:;
            uint8_t t195 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t195 != ((uint8_t)9ULL)) goto pa194;
            goto po189;
pa194:;
            goto mn188;
po189:;
            {
                void * t197 = gm_l0;
                gm_tbl8il92l97ut t198 = gm_l1;
                void * t199 = gm_l2;
                gm_tl1r7a2fn1ev6 t200;
                gm_f_place_47rfqe5((void *)&t200, (void *)NULL, cx, NULL, t197, t198, t199, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t201 = t200;
                if (((((uint8_t *)&t201))[0] != 0)) {
                    gm_t76123gfet73g t202 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t201) + 8));
                    gm_tcfa6l7reoosn t203;
                    GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
                    ((uint8_t *)&t203)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t203 + 8), t202);
                    F.ret = t203;
                    goto gm_fret;
                }
                gm_tou12vuc19e0s t204 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t201) + 8));
                F.l13 = t204;
            }
            {
                void * t205 = gm_l0;
                void * t206 = gm_l2;
                gm_tl874t2atnlh3 t207 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t206) + 112));
                void * t208 = gm_l2;
                gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t208) + 96));
                gm_b16_8 t210 = gm_l3;
                gm_tcfa6l7reoosn t211;
                gm_f_read_place_fcc38bb((void *)&t211, (void *)NULL, cx, NULL, t205, t207, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l13)), t209, t210);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t212 = t211;
                if (((((uint8_t *)&t212))[0] != 0)) {
                    gm_t76123gfet73g t213 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t212) + 8));
                    gm_tcfa6l7reoosn t214;
                    GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                    ((uint8_t *)&t214)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t214 + 8), t213);
                    F.ret = t214;
                    goto gm_fret;
                }
                gm_b0_1 t215 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 8));
            }
            goto me4;
mn188:;
ma13:;
            uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t217 != ((uint8_t)10ULL)) goto mn216;
            gm_t1j70din2upc4 t218 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t3) + 0) + 8));
            gm_l14 = t218;
            {
                void * t221 = gm_l2;
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)t221) + 124));
                if (!t222) goto ie220;
                {
                    void * t224 = gm_l0;
                    gm_tbl8il92l97ut t225 = gm_l1;
                    void * t226 = gm_l2;
                    gm_tl1r7a2fn1ev6 t227;
                    gm_f_place_47rfqe5((void *)&t227, (void *)NULL, cx, NULL, t224, t225, t226, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl1r7a2fn1ev6 t228 = t227;
                    if (((((uint8_t *)&t228))[0] != 0)) {
                        gm_t76123gfet73g t229 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t228) + 8));
                        gm_tcfa6l7reoosn t230;
                        GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                        ((uint8_t *)&t230)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t230 + 8), t229);
                        F.ret = t230;
                        goto gm_fret;
                    }
                    gm_tou12vuc19e0s t231 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t228) + 8));
                    F.l15 = t231;
                }
                {
                    void * t232 = gm_l0;
                    void * t233 = gm_l2;
                    gm_tl874t2atnlh3 t234 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t233) + 112));
                    void * t235 = gm_l2;
                    gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t235) + 96));
                    gm_b16_8 t237 = gm_l3;
                    gm_tcfa6l7reoosn t238;
                    gm_f_read_place_fcc38bb((void *)&t238, (void *)NULL, cx, NULL, t232, t234, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l15)), t236, t237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t239 = t238;
                    if (((((uint8_t *)&t239))[0] != 0)) {
                        gm_t76123gfet73g t240 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t239) + 8));
                        gm_tcfa6l7reoosn t241;
                        GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                        ((uint8_t *)&t241)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t241 + 8), t240);
                        F.ret = t241;
                        goto gm_fret;
                    }
                    gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 8));
                }
                {
                    gm_b0_1 t243;
                    gm_tcfa6l7reoosn t244;
                    GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 24, 0);
                    ((uint8_t *)&t244)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t244 + 8), t243);
                    gm_tcfa6l7reoosn t245 = t244;
                    uint8_t t246 = ((((uint8_t *)&t245))[0] != 0);
                    F.ret = t244;
                    goto gm_fret;
                }
                goto ix220;
ie220:;
ix220:;
            }
            {
                void * t247 = gm_l0;
                gm_t2lqu8jsj511d t248 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t247));
                gm_l16 = t248;
            }
            {
                void * t249 = gm_l0;
                void * t250 = GM_LD(void *, (((uint8_t *)&gm_l14) + 0));
                uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)t250) + 104));
                gm_b16_8 t252 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t249, t251);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t252;
            }
            {
                void * t253 = gm_l0;
                gm_tbl8il92l97ut t254 = gm_l1;
                void * t255 = GM_LD(void *, (((uint8_t *)&gm_l14) + 0));
                gm_b16_8 t256 = gm_l17;
                gm_tcfa6l7reoosn t257;
                gm_f_ev_25jfdar((void *)&t257, (void *)NULL, cx, NULL, t253, t254, t255, t256);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t258 = t257;
                if (((((uint8_t *)&t258))[0] != 0)) {
                    gm_t76123gfet73g t259 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t258) + 8));
                    gm_tcfa6l7reoosn t260;
                    GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
                    ((uint8_t *)&t260)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t260 + 8), t259);
                    F.ret = t260;
                    goto gm_fret;
                }
                gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 8));
            }
            {
                uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 16));
                uint64_t t263 = gm_f_us_cuv01fp(t262);
                gm_l18 = t263;
            }
            {
                gm_b16_8 t264 = gm_l3;
                uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                void * t266 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)));
                uint64_t t267 = gm_l18;
                uint64_t t268 = (uintptr_t)t267;
                uint64_t t269 = gm_l18;
                uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t r271; bool o272 = __builtin_add_overflow(t269, t270, &r271);
                if (o272) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 165, 32);
                uint64_t t273 = r271;
                uint64_t t274 = (uintptr_t)t273;
                if (t268 > t274 || t274 > (uintptr_t)t265) gm_trap_slice(t268, t274, t265, gm_src0jbci4ff2esa, 165, 27);
                gm_b16_8 t275;
                GM_ST(void *, (uint8_t *)&t275, (void *)(((uint8_t *)t266 + t268 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(t274 - t268));
                gm_b16_8 t276 = t275;
                gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                gm_f_copy_from_f7j6p05(t264, t277);
            }
            {
                void * t278 = gm_l0;
                gm_t2lqu8jsj511d t279 = gm_l16;
                gm_f_release_jn5kqdi(t278, GM_CONV(gm_csc7lc6jlkqkuq, t279));
            }
            goto me4;
mn216:;
ma14:;
            uint8_t t281 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t281 != ((uint8_t)11ULL)) goto mn280;
            gm_tdvcgvrf81sbd t282 = GM_LD(gm_tdvcgvrf81sbd, ((((uint8_t *)t3) + 0) + 8));
            gm_l19 = t282;
            {
                void * t285 = gm_l2;
                uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)t285) + 124));
                if (!t286) goto ie284;
                {
                    void * t288 = gm_l0;
                    gm_tbl8il92l97ut t289 = gm_l1;
                    void * t290 = gm_l2;
                    gm_tl1r7a2fn1ev6 t291;
                    gm_f_place_47rfqe5((void *)&t291, (void *)NULL, cx, NULL, t288, t289, t290, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl1r7a2fn1ev6 t292 = t291;
                    if (((((uint8_t *)&t292))[0] != 0)) {
                        gm_t76123gfet73g t293 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t292) + 8));
                        gm_tcfa6l7reoosn t294;
                        GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                        ((uint8_t *)&t294)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t294 + 8), t293);
                        F.ret = t294;
                        goto gm_fret;
                    }
                    gm_tou12vuc19e0s t295 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t292) + 8));
                    F.l20 = t295;
                }
                {
                    void * t296 = gm_l0;
                    void * t297 = gm_l2;
                    gm_tl874t2atnlh3 t298 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t297) + 112));
                    void * t299 = gm_l2;
                    gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t299) + 96));
                    gm_b16_8 t301 = gm_l3;
                    gm_tcfa6l7reoosn t302;
                    gm_f_read_place_fcc38bb((void *)&t302, (void *)NULL, cx, NULL, t296, t298, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l20)), t300, t301);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t303 = t302;
                    if (((((uint8_t *)&t303))[0] != 0)) {
                        gm_t76123gfet73g t304 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t303) + 8));
                        gm_tcfa6l7reoosn t305;
                        GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
                        ((uint8_t *)&t305)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t305 + 8), t304);
                        F.ret = t305;
                        goto gm_fret;
                    }
                    gm_b0_1 t306 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 8));
                }
                {
                    gm_b0_1 t307;
                    gm_tcfa6l7reoosn t308;
                    GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t308 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t308 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t308 + 24, 0);
                    ((uint8_t *)&t308)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t308 + 8), t307);
                    gm_tcfa6l7reoosn t309 = t308;
                    uint8_t t310 = ((((uint8_t *)&t309))[0] != 0);
                    F.ret = t308;
                    goto gm_fret;
                }
                goto ix284;
ie284:;
ix284:;
            }
            {
                void * t311 = gm_l0;
                gm_t2lqu8jsj511d t312 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t311));
                gm_l21 = t312;
            }
            {
                void * t313 = gm_l0;
                void * t314 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)t314) + 104));
                gm_b16_8 t316 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t313, t315);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t316;
            }
            {
                void * t317 = gm_l0;
                gm_tbl8il92l97ut t318 = gm_l1;
                void * t319 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                gm_b16_8 t320 = gm_l22;
                gm_tcfa6l7reoosn t321;
                gm_f_ev_25jfdar((void *)&t321, (void *)NULL, cx, NULL, t317, t318, t319, t320);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t322 = t321;
                if (((((uint8_t *)&t322))[0] != 0)) {
                    gm_t76123gfet73g t323 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t322) + 8));
                    gm_tcfa6l7reoosn t324;
                    GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
                    ((uint8_t *)&t324)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t324 + 8), t323);
                    F.ret = t324;
                    goto gm_fret;
                }
                gm_b0_1 t325 = GM_LD(gm_b0_1, (((uint8_t *)&t322) + 8));
            }
            {
                void * t326 = gm_l0;
                void * t327 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                gm_t88rcucg1b7uk t328 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t327) + 96));
                gm_b16_8 t329 = gm_l22;
                uint32_t t330 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 8));
                gm_b16_8 t331 = gm_l3;
                gm_b0_1 t332 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t326, t328, t329, t330, t331);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t333 = gm_l0;
                gm_t2lqu8jsj511d t334 = gm_l21;
                gm_f_release_jn5kqdi(t333, GM_CONV(gm_csc7lc6jlkqkuq, t334));
            }
            goto me4;
mn280:;
ma15:;
            uint8_t t336 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t336 != ((uint8_t)12ULL)) goto mn335;
            gm_tp0fa5qv7rqnn t337 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t3) + 0) + 8));
            F.l23 = t337;
            {
                uint8_t t340 = ((GM_LD(uint8_t, (((uint8_t *)&F.l23) + 16) + 0) == ((uint8_t)0ULL)));
                if (!t340) goto ie339;
                void * t341 = GM_LD(void *, (((uint8_t *)&F.l23) + 0));
                uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)t341) + 124));
                uint8_t t343 = (uint8_t)!t342;
                if (!t343) goto ie339;
                {
                    void * t345 = gm_l0;
                    gm_t2lqu8jsj511d t346 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t345));
                    gm_l24 = t346;
                }
                {
                    void * t347 = gm_l0;
                    void * t348 = GM_LD(void *, (((uint8_t *)&F.l23) + 0));
                    uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)t348) + 104));
                    gm_b16_8 t350 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t347, t349);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t350;
                }
                {
                    void * t351 = gm_l0;
                    gm_tbl8il92l97ut t352 = gm_l1;
                    void * t353 = GM_LD(void *, (((uint8_t *)&F.l23) + 0));
                    gm_b16_8 t354 = gm_l25;
                    gm_tcfa6l7reoosn t355;
                    gm_f_ev_25jfdar((void *)&t355, (void *)NULL, cx, NULL, t351, t352, t353, t354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t356 = t355;
                    if (((((uint8_t *)&t356))[0] != 0)) {
                        gm_t76123gfet73g t357 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t356) + 8));
                        gm_tcfa6l7reoosn t358;
                        GM_ST(uint8_t, (uint8_t *)&t358 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t358 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t358 + 4, 0);
                        ((uint8_t *)&t358)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t358 + 8), t357);
                        F.ret = t358;
                        goto gm_fret;
                    }
                    gm_b0_1 t359 = GM_LD(gm_b0_1, (((uint8_t *)&t356) + 8));
                }
                {
                    void * t360 = gm_l0;
                    gm_tbl8il92l97ut t361 = gm_l1;
                    void * t362 = GM_LD(void *, (((uint8_t *)&F.l23) + 8));
                    gm_t833u93mr4iob t363;
                    gm_f_ev_index_bie377m((void *)&t363, (void *)NULL, cx, NULL, t360, t361, t362);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t833u93mr4iob t364 = t363;
                    if (((((uint8_t *)&t364))[0] != 0)) {
                        gm_t76123gfet73g t365 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t364) + 8));
                        gm_tcfa6l7reoosn t366;
                        GM_ST(uint8_t, (uint8_t *)&t366 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t366 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t366 + 4, 0);
                        ((uint8_t *)&t366)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t366 + 8), t365);
                        F.ret = t366;
                        goto gm_fret;
                    }
                    uint64_t t367 = GM_LD(uint64_t, (((uint8_t *)&t364) + 8));
                    gm_l26 = t367;
                }
                {
                    uint64_t t369 = gm_l26;
                    uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 32));
                    uint8_t t371 = (t369 >= t370);
                    if (!t371) goto ie368;
                    {
                        void * t373 = gm_l0;
                        void * t374 = gm_l2;
                        gm_tl874t2atnlh3 t375 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t374) + 112));
                        uint64_t t376 = gm_l26;
                        uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 32));
                        gm_t2lqu8jsj511d t378;
                        GM_ST(uint64_t, ((uint8_t *)&t378 + 0), t376);
                        GM_ST(uint64_t, ((uint8_t *)&t378 + 8), t377);
                        gm_tabb710d9mq2e t379;
                        gm_f_format_2hd15t9((void *)&t379, (void *)NULL, cx, NULL, t378);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t380 = t379;
                        if (((((uint8_t *)&t380))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t380) + 8, gm_src0jbci4ff2esa, 186, 56);
                        gm_b16_8 t381 = GM_LD(gm_b16_8, (((uint8_t *)&t380) + 8));
                        gm_b16_8 t382 = t381;
                        gm_b16_8 t383 = GM_LD(gm_b16_8, ((uint8_t *)&t382));
                        gm_t76123gfet73g t384;
                        gm_f_trap_qh84l9d((void *)&t384, (void *)NULL, cx, NULL, t373, t375, t383);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t385;
                        GM_ST(uint8_t, (uint8_t *)&t385 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t385 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t385 + 4, 0);
                        ((uint8_t *)&t385)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t385 + 8), t384);
                        gm_tcfa6l7reoosn t386 = t385;
                        uint8_t t387 = ((((uint8_t *)&t386))[0] != 0);
                        F.ret = t385;
                        goto gm_fret;
                    }
                    goto ix368;
ie368:;
ix368:;
                }
                {
                    uint64_t t388 = gm_l26;
                    uint64_t t389 = gm_f_us_cuv01fp(t388);
                    uint64_t t390 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t r391; bool o392 = __builtin_mul_overflow(t389, t390, &r391);
                    if (o392) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 187, 24);
                    uint64_t t393 = r391;
                    gm_l27 = t393;
                }
                {
                    gm_b16_8 t394 = gm_l3;
                    uint64_t t395 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    void * t396 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)));
                    uint64_t t397 = gm_l27;
                    uint64_t t398 = (uintptr_t)t397;
                    uint64_t t399 = gm_l27;
                    uint64_t t400 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t r401; bool o402 = __builtin_add_overflow(t399, t400, &r401);
                    if (o402) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 188, 38);
                    uint64_t t403 = r401;
                    uint64_t t404 = (uintptr_t)t403;
                    if (t398 > t404 || t404 > (uintptr_t)t395) gm_trap_slice(t398, t404, t395, gm_src0jbci4ff2esa, 188, 31);
                    gm_b16_8 t405;
                    GM_ST(void *, (uint8_t *)&t405, (void *)(((uint8_t *)t396 + t398 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t405 + 8, (uintptr_t)(t404 - t398));
                    gm_b16_8 t406 = t405;
                    gm_b16_8 t407 = GM_LD(gm_b16_8, ((uint8_t *)&t406));
                    gm_f_copy_from_f7j6p05(t394, t407);
                }
                {
                    void * t408 = gm_l0;
                    gm_t2lqu8jsj511d t409 = gm_l24;
                    gm_f_release_jn5kqdi(t408, GM_CONV(gm_csc7lc6jlkqkuq, t409));
                }
                {
                    gm_b0_1 t410;
                    gm_tcfa6l7reoosn t411;
                    GM_ST(uint8_t, (uint8_t *)&t411 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t411 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t411 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t411 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t411 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t411 + 24, 0);
                    ((uint8_t *)&t411)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t411 + 8), t410);
                    gm_tcfa6l7reoosn t412 = t411;
                    uint8_t t413 = ((((uint8_t *)&t412))[0] != 0);
                    F.ret = t411;
                    goto gm_fret;
                }
                goto ix339;
ie339:;
ix339:;
            }
            {
                void * t414 = gm_l0;
                gm_tbl8il92l97ut t415 = gm_l1;
                void * t416 = gm_l2;
                gm_tl1r7a2fn1ev6 t417;
                gm_f_place_47rfqe5((void *)&t417, (void *)NULL, cx, NULL, t414, t415, t416, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t418 = t417;
                if (((((uint8_t *)&t418))[0] != 0)) {
                    gm_t76123gfet73g t419 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t418) + 8));
                    gm_tcfa6l7reoosn t420;
                    GM_ST(uint8_t, (uint8_t *)&t420 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t420 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t420 + 4, 0);
                    ((uint8_t *)&t420)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t420 + 8), t419);
                    F.ret = t420;
                    goto gm_fret;
                }
                gm_tou12vuc19e0s t421 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t418) + 8));
                F.l28 = t421;
            }
            {
                void * t422 = gm_l0;
                void * t423 = gm_l2;
                gm_tl874t2atnlh3 t424 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t423) + 112));
                void * t425 = gm_l2;
                gm_t88rcucg1b7uk t426 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t425) + 96));
                gm_b16_8 t427 = gm_l3;
                gm_tcfa6l7reoosn t428;
                gm_f_read_place_fcc38bb((void *)&t428, (void *)NULL, cx, NULL, t422, t424, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l28)), t426, t427);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t429 = t428;
                if (((((uint8_t *)&t429))[0] != 0)) {
                    gm_t76123gfet73g t430 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t429) + 8));
                    gm_tcfa6l7reoosn t431;
                    GM_ST(uint8_t, (uint8_t *)&t431 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t431 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t431 + 4, 0);
                    ((uint8_t *)&t431)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t431 + 8), t430);
                    F.ret = t431;
                    goto gm_fret;
                }
                gm_b0_1 t432 = GM_LD(gm_b0_1, (((uint8_t *)&t429) + 8));
            }
            goto me4;
mn335:;
ma16:;
            uint8_t t434 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t434 != ((uint8_t)13ULL)) goto mn433;
            gm_tt49maei14cl6 t435 = GM_LD(gm_tt49maei14cl6, ((((uint8_t *)t3) + 0) + 8));
            F.l29 = t435;
            void * t436 = gm_l0;
            gm_tbl8il92l97ut t437 = gm_l1;
            void * t438 = gm_l2;
            void * t439 = GM_LD(void *, (((uint8_t *)&F.l29) + 0));
            void * t440 = GM_LD(void *, (((uint8_t *)&F.l29) + 8));
            void * t441 = GM_LD(void *, (((uint8_t *)&F.l29) + 16));
            uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 24));
            gm_t9qkh0rsooc8d t443 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l29) + 25));
            uint64_t t444 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 32));
            uint64_t t445 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 40));
            gm_b16_8 t446 = gm_l3;
            gm_tcfa6l7reoosn t447;
            gm_f_ev_slice_i5uin87((void *)&t447, (void *)NULL, cx, NULL, t436, t437, t438, t439, t440, t441, t442, t443, t444, t445, t446);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t448 = t447;
            if (((((uint8_t *)&t448))[0] != 0)) {
                gm_t76123gfet73g t449 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t448) + 8));
                gm_tcfa6l7reoosn t450;
                GM_ST(uint8_t, (uint8_t *)&t450 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t450 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t450 + 4, 0);
                ((uint8_t *)&t450)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t450 + 8), t449);
                F.ret = t450;
                goto gm_fret;
            }
            gm_b0_1 t451 = GM_LD(gm_b0_1, (((uint8_t *)&t448) + 8));
            goto me4;
mn433:;
ma17:;
            uint8_t t453 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t453 != ((uint8_t)14ULL)) goto mn452;
            void * t454 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l30 = t454;
            {
                void * t456 = gm_l0;
                gm_t2lqu8jsj511d t457 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t456));
                gm_l31 = t457;
            }
            {
                void * t458 = gm_l0;
                void * t459 = gm_l30;
                uint64_t t460 = GM_LD(uint64_t, (((uint8_t *)t459) + 104));
                gm_b16_8 t461 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t458, t460);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t461;
            }
            {
                void * t462 = gm_l0;
                gm_tbl8il92l97ut t463 = gm_l1;
                void * t464 = gm_l30;
                gm_b16_8 t465 = gm_l32;
                gm_tcfa6l7reoosn t466;
                gm_f_ev_25jfdar((void *)&t466, (void *)NULL, cx, NULL, t462, t463, t464, t465);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t467 = t466;
                if (((((uint8_t *)&t467))[0] != 0)) {
                    gm_t76123gfet73g t468 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t467) + 8));
                    gm_tcfa6l7reoosn t469;
                    GM_ST(uint8_t, (uint8_t *)&t469 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t469 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t469 + 4, 0);
                    ((uint8_t *)&t469)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t469 + 8), t468);
                    F.ret = t469;
                    goto gm_fret;
                }
                gm_b0_1 t470 = GM_LD(gm_b0_1, (((uint8_t *)&t467) + 8));
            }
            {
                void * t471 = gm_l0;
                gm_b16_8 t472 = gm_l3;
                void * t473 = gm_l0;
                gm_b16_8 t474 = gm_l32;
                gm_b16_8 t475 = t474;
                gm_b16_8 t476 = GM_LD(gm_b16_8, ((uint8_t *)&t475));
                gm_t2lqu8jsj511d t477 = gm_f_slice_parts_1se5hu4(t473, t476);
                gm_t2lqu8jsj511d t478 = t477;
                uint64_t t479 = GM_LD(uint64_t, (((uint8_t *)&t478) + 8));
                gm_f_put_u64_irp77r2(t471, t472, t479);
            }
            {
                void * t480 = gm_l0;
                gm_t2lqu8jsj511d t481 = gm_l31;
                gm_f_release_jn5kqdi(t480, GM_CONV(gm_csc7lc6jlkqkuq, t481));
            }
            goto me4;
mn452:;
ma18:;
            uint8_t t483 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t483 != ((uint8_t)15ULL)) goto mn482;
            void * t484 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l33 = t484;
            {
                void * t486 = gm_l0;
                gm_t2lqu8jsj511d t487 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t486));
                gm_l34 = t487;
            }
            {
                void * t488 = gm_l0;
                void * t489 = gm_l33;
                uint64_t t490 = GM_LD(uint64_t, (((uint8_t *)t489) + 104));
                gm_b16_8 t491 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t488, t490);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l35 = t491;
            }
            {
                void * t492 = gm_l0;
                gm_tbl8il92l97ut t493 = gm_l1;
                void * t494 = gm_l33;
                gm_b16_8 t495 = gm_l35;
                gm_tcfa6l7reoosn t496;
                gm_f_ev_25jfdar((void *)&t496, (void *)NULL, cx, NULL, t492, t493, t494, t495);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t497 = t496;
                if (((((uint8_t *)&t497))[0] != 0)) {
                    gm_t76123gfet73g t498 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t497) + 8));
                    gm_tcfa6l7reoosn t499;
                    GM_ST(uint8_t, (uint8_t *)&t499 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t499 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t499 + 4, 0);
                    ((uint8_t *)&t499)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t499 + 8), t498);
                    F.ret = t499;
                    goto gm_fret;
                }
                gm_b0_1 t500 = GM_LD(gm_b0_1, (((uint8_t *)&t497) + 8));
            }
            {
                void * t501 = gm_l0;
                gm_b16_8 t502 = gm_l3;
                void * t503 = gm_l0;
                gm_b16_8 t504 = gm_l35;
                gm_b16_8 t505 = t504;
                gm_b16_8 t506 = GM_LD(gm_b16_8, ((uint8_t *)&t505));
                gm_t2lqu8jsj511d t507 = gm_f_slice_parts_1se5hu4(t503, t506);
                gm_t2lqu8jsj511d t508 = t507;
                uint64_t t509 = GM_LD(uint64_t, (((uint8_t *)&t508) + 0));
                gm_f_put_ptr_188p3mm(t501, t502, t509);
            }
            {
                void * t510 = gm_l0;
                gm_t2lqu8jsj511d t511 = gm_l34;
                gm_f_release_jn5kqdi(t510, GM_CONV(gm_csc7lc6jlkqkuq, t511));
            }
            goto me4;
mn482:;
ma19:;
            uint8_t t513 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t513 != ((uint8_t)16ULL)) goto mn512;
            void * t514 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l36 = t514;
            {
                void * t516 = gm_l0;
                gm_tbl8il92l97ut t517 = gm_l1;
                void * t518 = gm_l36;
                gm_tl1r7a2fn1ev6 t519;
                gm_f_place_47rfqe5((void *)&t519, (void *)NULL, cx, NULL, t516, t517, t518, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl1r7a2fn1ev6 t520 = t519;
                if (((((uint8_t *)&t520))[0] != 0)) {
                    gm_t76123gfet73g t521 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t520) + 8));
                    gm_tcfa6l7reoosn t522;
                    GM_ST(uint8_t, (uint8_t *)&t522 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t522 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t522 + 4, 0);
                    ((uint8_t *)&t522)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t522 + 8), t521);
                    F.ret = t522;
                    goto gm_fret;
                }
                gm_tou12vuc19e0s t523 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t520) + 8));
                gm_tou12vuc19e0s t524 = t523;
                uint8_t t530 = GM_LD(uint8_t, (((uint8_t *)&t524) + 0));
                switch (t530) { case ((uint8_t)0ULL): goto ma527; case ((uint8_t)1ULL): goto ma526; case ((uint8_t)2ULL): goto ma526; case ((uint8_t)3ULL): goto ma528; default: goto ma529; }
ma526:;
                uint8_t t534 = GM_LD(uint8_t, (((uint8_t *)&t524) + 0));
                if (t534 != ((uint8_t)1ULL)) goto pa533;
                uint64_t t535 = GM_LD(uint64_t, (((uint8_t *)&t524) + 8));
                gm_l37 = t535;
                goto po532;
pa533:;
                uint8_t t537 = GM_LD(uint8_t, (((uint8_t *)&t524) + 0));
                if (t537 != ((uint8_t)2ULL)) goto pa536;
                uint64_t t538 = GM_LD(uint64_t, (((uint8_t *)&t524) + 8));
                gm_l37 = t538;
                goto po532;
pa536:;
                goto mn531;
po532:;
                void * t539 = gm_l0;
                gm_b16_8 t540 = gm_l3;
                uint64_t t541 = gm_l37;
                gm_f_put_ptr_188p3mm(t539, t540, t541);
                goto me525;
mn531:;
ma527:;
                uint8_t t543 = GM_LD(uint8_t, (((uint8_t *)&t524) + 0));
                if (t543 != ((uint8_t)0ULL)) goto mn542;
                void * t544 = gm_l0;
                void * t545 = gm_l2;
                gm_tl874t2atnlh3 t546 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t545) + 112));
                gm_b16_8 t547;
                GM_ST(void *, (uint8_t *)&t547, (void *)(gm_sds80esh09gni));
                GM_ST(uintptr_t, (uint8_t *)&t547 + 8, (uintptr_t)(50));
                gm_t76123gfet73g t548;
                gm_f_trap_qh84l9d((void *)&t548, (void *)NULL, cx, NULL, t544, t546, t547);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t549;
                GM_ST(uint8_t, (uint8_t *)&t549 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t549 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t549 + 4, 0);
                ((uint8_t *)&t549)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t549 + 8), t548);
                gm_tcfa6l7reoosn t550 = t549;
                uint8_t t551 = ((((uint8_t *)&t550))[0] != 0);
                F.ret = t549;
                goto gm_fret;
                goto me525;
mn542:;
ma528:;
                uint8_t t553 = GM_LD(uint8_t, (((uint8_t *)&t524) + 0));
                if (t553 != ((uint8_t)3ULL)) goto mn552;
                void * t554 = gm_l0;
                void * t555 = gm_l2;
                gm_tl874t2atnlh3 t556 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t555) + 112));
                gm_b16_8 t557;
                GM_ST(void *, (uint8_t *)&t557, (void *)(gm_su2imns6t7mh5));
                GM_ST(uintptr_t, (uint8_t *)&t557 + 8, (uintptr_t)(40));
                gm_t76123gfet73g t558;
                gm_f_trap_qh84l9d((void *)&t558, (void *)NULL, cx, NULL, t554, t556, t557);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t559;
                GM_ST(uint8_t, (uint8_t *)&t559 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t559 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t559 + 4, 0);
                ((uint8_t *)&t559)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t559 + 8), t558);
                gm_tcfa6l7reoosn t560 = t559;
                uint8_t t561 = ((((uint8_t *)&t560))[0] != 0);
                F.ret = t559;
                goto gm_fret;
                goto me525;
mn552:;
ma529:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 211, 13);
me525:;
            }
            goto me4;
mn512:;
ma20:;
            uint8_t t563 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t563 != ((uint8_t)17ULL)) goto mn562;
            void * t564 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l39 = t564;
            uint64_t t565 = GM_LD(uint64_t, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l40 = t565;
            {
                void * t567 = gm_l0;
                gm_t2lqu8jsj511d t568 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t567));
                gm_l41 = t568;
            }
            {
                void * t569 = gm_l0;
                void * t570 = gm_l39;
                uint64_t t571 = GM_LD(uint64_t, (((uint8_t *)t570) + 104));
                gm_b16_8 t572 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t569, t571);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t572;
            }
            {
                void * t573 = gm_l0;
                gm_tbl8il92l97ut t574 = gm_l1;
                void * t575 = gm_l39;
                gm_b16_8 t576 = gm_l42;
                gm_tcfa6l7reoosn t577;
                gm_f_ev_25jfdar((void *)&t577, (void *)NULL, cx, NULL, t573, t574, t575, t576);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t578 = t577;
                if (((((uint8_t *)&t578))[0] != 0)) {
                    gm_t76123gfet73g t579 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t578) + 8));
                    gm_tcfa6l7reoosn t580;
                    GM_ST(uint8_t, (uint8_t *)&t580 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t580 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t580 + 4, 0);
                    ((uint8_t *)&t580)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t580 + 8), t579);
                    F.ret = t580;
                    goto gm_fret;
                }
                gm_b0_1 t581 = GM_LD(gm_b0_1, (((uint8_t *)&t578) + 8));
            }
            {
                void * t582 = gm_l0;
                void * t583 = gm_l2;
                gm_tl874t2atnlh3 t584 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t583) + 112));
                gm_b16_8 t585 = gm_l42;
                gm_b16_8 t586 = t585;
                gm_b16_8 t587 = GM_LD(gm_b16_8, ((uint8_t *)&t586));
                uint64_t t588 = gm_l40;
                gm_t833u93mr4iob t589;
                gm_f_alloc_temp_9cmh124((void *)&t589, (void *)NULL, cx, NULL, t582, t584, t587, t588);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t590 = t589;
                if (((((uint8_t *)&t590))[0] != 0)) {
                    gm_t76123gfet73g t591 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t590) + 8));
                    gm_tcfa6l7reoosn t592;
                    GM_ST(uint8_t, (uint8_t *)&t592 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t592 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t592 + 4, 0);
                    ((uint8_t *)&t592)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t592 + 8), t591);
                    F.ret = t592;
                    goto gm_fret;
                }
                uint64_t t593 = GM_LD(uint64_t, (((uint8_t *)&t590) + 8));
                gm_l43 = t593;
            }
            {
                void * t594 = gm_l0;
                gm_t2lqu8jsj511d t595 = gm_l41;
                gm_f_release_jn5kqdi(t594, GM_CONV(gm_csc7lc6jlkqkuq, t595));
            }
            {
                void * t596 = gm_l0;
                gm_b16_8 t597 = gm_l3;
                uint64_t t598 = gm_l43;
                gm_f_put_ptr_188p3mm(t596, t597, t598);
            }
            goto me4;
mn562:;
ma21:;
            uint8_t t600 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t600 != ((uint8_t)18ULL)) goto mn599;
            uint32_t t601 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l44 = t601;
            uint8_t t602 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 4));
            gm_l45 = t602;
            {
                void * t604 = gm_l0;
                gm_b16_8 t605 = gm_l3;
                void * t606 = gm_l0;
                uint32_t t607 = gm_l44;
                uint64_t t608 = gm_f_fn_addr_odbbf6a(t606, t607);
                gm_f_put_word_tfn443i(t604, t605, ((uint64_t)0ULL), t608);
            }
            {
                uint8_t t610 = gm_l45;
                if (!t610) goto ie609;
                {
                    void * t612 = gm_l0;
                    gm_b16_8 t613 = gm_l3;
                    gm_f_put_word_tfn443i(t612, t613, ((uint64_t)1ULL), ((uint64_t)0ULL));
                }
                {
                    void * t614 = gm_l0;
                    gm_b16_8 t615 = gm_l3;
                    uint32_t t616 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t617 = (uint64_t)t616;
                    gm_f_put_word_tfn443i(t614, t615, ((uint64_t)2ULL), t617);
                }
                goto ix609;
ie609:;
ix609:;
            }
            goto me4;
mn599:;
ma22:;
            uint8_t t619 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t619 != ((uint8_t)19ULL)) goto mn618;
            uint32_t t620 = GM_LD(uint32_t, ((((uint8_t *)t3) + 0) + 8));
            gm_l46 = t620;
            {
                void * t622 = gm_l0;
                gm_b16_8 t623 = gm_l3;
                void * t624 = gm_l0;
                uint32_t t625 = gm_l46;
                uint64_t t626 = gm_f_fn_addr_odbbf6a(t624, t625);
                gm_f_put_word_tfn443i(t622, t623, ((uint64_t)0ULL), t626);
            }
            {
                void * t627 = gm_l0;
                gm_b16_8 t628 = gm_l3;
                void * t629 = gm_l0;
                void * t630 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint32_t t631 = gm_f_frame_handle_k12gn88((void *)NULL, cx, NULL, t629, t630);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t632 = (uint64_t)t631;
                gm_f_put_word_tfn443i(t627, t628, ((uint64_t)1ULL), t632);
            }
            {
                void * t633 = gm_l0;
                gm_b16_8 t634 = gm_l3;
                uint32_t t635 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t636 = (uint64_t)t635;
                gm_f_put_word_tfn443i(t633, t634, ((uint64_t)2ULL), t636);
            }
            goto me4;
mn618:;
ma23:;
            uint8_t t638 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t638 != ((uint8_t)20ULL)) goto mn637;
            {
                void * t640 = gm_l0;
                gm_b16_8 t641 = gm_l3;
                void * t642 = gm_l0;
                void * t643 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint32_t t644 = gm_f_frame_handle_k12gn88((void *)NULL, cx, NULL, t642, t643);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t645 = (uint64_t)t644;
                gm_f_put_word_tfn443i(t640, t641, ((uint64_t)0ULL), t645);
            }
            {
                void * t646 = gm_l0;
                gm_b16_8 t647 = gm_l3;
                uint32_t t648 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t649 = (uint64_t)t648;
                gm_f_put_word_tfn443i(t646, t647, ((uint64_t)1ULL), t649);
            }
            goto me4;
mn637:;
ma24:;
            uint8_t t651 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t651 != ((uint8_t)30ULL)) goto mn650;
            gm_t9qkh0rsooc8d t652 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l47 = t652;
            void * t653 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l48 = t653;
            void * t654 = gm_l0;
            gm_tbl8il92l97ut t655 = gm_l1;
            gm_t9qkh0rsooc8d t656 = gm_l47;
            void * t657 = gm_l48;
            void * t658 = gm_l2;
            gm_b16_8 t659 = gm_l3;
            gm_tcfa6l7reoosn t660;
            gm_f_unary_sorkbs7((void *)&t660, (void *)NULL, cx, NULL, t654, t655, t656, t657, t658, t659);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t661 = t660;
            if (((((uint8_t *)&t661))[0] != 0)) {
                gm_t76123gfet73g t662 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t661) + 8));
                gm_tcfa6l7reoosn t663;
                GM_ST(uint8_t, (uint8_t *)&t663 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t663 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t663 + 4, 0);
                ((uint8_t *)&t663)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t663 + 8), t662);
                F.ret = t663;
                goto gm_fret;
            }
            gm_b0_1 t664 = GM_LD(gm_b0_1, (((uint8_t *)&t661) + 8));
            goto me4;
mn650:;
ma25:;
            uint8_t t666 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t666 != ((uint8_t)31ULL)) goto mn665;
            gm_ts6oqppjh0ba3 t667 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l49 = t667;
            void * t668 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l50 = t668;
            void * t669 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l51 = t669;
            void * t670 = gm_l0;
            gm_tbl8il92l97ut t671 = gm_l1;
            gm_ts6oqppjh0ba3 t672 = gm_l49;
            void * t673 = gm_l50;
            void * t674 = gm_l51;
            void * t675 = gm_l2;
            gm_b16_8 t676 = gm_l3;
            gm_tcfa6l7reoosn t677;
            gm_f_binary_6rg6s7q((void *)&t677, (void *)NULL, cx, NULL, t670, t671, t672, t673, t674, t675, t676);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t678 = t677;
            if (((((uint8_t *)&t678))[0] != 0)) {
                gm_t76123gfet73g t679 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t678) + 8));
                gm_tcfa6l7reoosn t680;
                GM_ST(uint8_t, (uint8_t *)&t680 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t680 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t680 + 4, 0);
                ((uint8_t *)&t680)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t680 + 8), t679);
                F.ret = t680;
                goto gm_fret;
            }
            gm_b0_1 t681 = GM_LD(gm_b0_1, (((uint8_t *)&t678) + 8));
            goto me4;
mn665:;
ma26:;
            uint8_t t683 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t683 != ((uint8_t)33ULL)) goto mn682;
            gm_tmn7n0n9raou5 t684 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l52 = t684;
            void * t685 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l53 = t685;
            void * t686 = gm_l0;
            gm_tbl8il92l97ut t687 = gm_l1;
            gm_tmn7n0n9raou5 t688 = gm_l52;
            void * t689 = gm_l53;
            void * t690 = gm_l2;
            gm_b16_8 t691 = gm_l3;
            gm_tcfa6l7reoosn t692;
            gm_f_coerce_im8vs3u((void *)&t692, (void *)NULL, cx, NULL, t686, t687, t688, t689, t690, t691);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t693 = t692;
            if (((((uint8_t *)&t693))[0] != 0)) {
                gm_t76123gfet73g t694 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t693) + 8));
                gm_tcfa6l7reoosn t695;
                GM_ST(uint8_t, (uint8_t *)&t695 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t695 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t695 + 4, 0);
                ((uint8_t *)&t695)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t695 + 8), t694);
                F.ret = t695;
                goto gm_fret;
            }
            gm_b0_1 t696 = GM_LD(gm_b0_1, (((uint8_t *)&t693) + 8));
            goto me4;
mn682:;
ma27:;
            uint8_t t698 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t698 != ((uint8_t)34ULL)) goto mn697;
            gm_tvdg94bk7aa22 t699 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l54 = t699;
            gm_b16_8 t700 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l55 = t700;
            void * t701 = gm_l0;
            gm_tbl8il92l97ut t702 = gm_l1;
            gm_tvdg94bk7aa22 t703 = gm_l54;
            gm_b16_8 t704 = gm_l55;
            void * t705 = gm_l2;
            gm_b16_8 t706 = gm_l3;
            gm_tcfa6l7reoosn t707;
            gm_f_ev_builtin_86a9q3q((void *)&t707, (void *)NULL, cx, NULL, t701, t702, t703, t704, t705, t706);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t708 = t707;
            if (((((uint8_t *)&t708))[0] != 0)) {
                gm_t76123gfet73g t709 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t708) + 8));
                gm_tcfa6l7reoosn t710;
                GM_ST(uint8_t, (uint8_t *)&t710 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t710 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t710 + 4, 0);
                ((uint8_t *)&t710)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t710 + 8), t709);
                F.ret = t710;
                goto gm_fret;
            }
            gm_b0_1 t711 = GM_LD(gm_b0_1, (((uint8_t *)&t708) + 8));
            goto me4;
mn697:;
ma28:;
            uint8_t t713 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t713 != ((uint8_t)35ULL)) goto mn712;
            uint32_t t714 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l56 = t714;
            gm_b16_8 t715 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l57 = t715;
            void * t716 = gm_l0;
            gm_tbl8il92l97ut t717 = gm_l1;
            uint32_t t718 = gm_l56;
            gm_b16_8 t719 = gm_l57;
            void * t720 = gm_l2;
            gm_b16_8 t721 = gm_l3;
            gm_tcfa6l7reoosn t722;
            gm_f_call_node_a1b30k9((void *)&t722, (void *)NULL, cx, NULL, t716, t717, t718, t719, t720, t721);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t723 = t722;
            if (((((uint8_t *)&t723))[0] != 0)) {
                gm_t76123gfet73g t724 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t723) + 8));
                gm_tcfa6l7reoosn t725;
                GM_ST(uint8_t, (uint8_t *)&t725 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t725 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t725 + 4, 0);
                ((uint8_t *)&t725)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t725 + 8), t724);
                F.ret = t725;
                goto gm_fret;
            }
            gm_b0_1 t726 = GM_LD(gm_b0_1, (((uint8_t *)&t723) + 8));
            goto me4;
mn712:;
ma29:;
            uint8_t t728 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t728 != ((uint8_t)36ULL)) goto mn727;
            gm_tf4r0415i49d1 t729 = GM_LD(gm_tf4r0415i49d1, ((((uint8_t *)t3) + 0) + 8));
            F.l58 = t729;
            void * t730 = gm_l0;
            gm_tbl8il92l97ut t731 = gm_l1;
            uint32_t t732 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 24));
            gm_b16_8 t733 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 32));
            void * t734 = gm_l2;
            gm_b16_8 t735 = gm_l3;
            gm_tcfa6l7reoosn t736;
            gm_f_ev_perform_ovt4j0p((void *)&t736, (void *)NULL, cx, NULL, t730, t731, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)&F.l58) + 0)), t732, t733, t734, t735);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t737 = t736;
            if (((((uint8_t *)&t737))[0] != 0)) {
                gm_t76123gfet73g t738 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t737) + 8));
                gm_tcfa6l7reoosn t739;
                GM_ST(uint8_t, (uint8_t *)&t739 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t739 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t739 + 4, 0);
                ((uint8_t *)&t739)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t739 + 8), t738);
                F.ret = t739;
                goto gm_fret;
            }
            gm_b0_1 t740 = GM_LD(gm_b0_1, (((uint8_t *)&t737) + 8));
            goto me4;
mn727:;
ma30:;
            uint8_t t742 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t742 != ((uint8_t)37ULL)) goto mn741;
            void * t743 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l59 = t743;
            gm_b16_8 t744 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l60 = t744;
            void * t745 = gm_l0;
            gm_tbl8il92l97ut t746 = gm_l1;
            void * t747 = gm_l59;
            gm_b16_8 t748 = gm_l60;
            void * t749 = gm_l2;
            gm_b16_8 t750 = gm_l3;
            gm_tcfa6l7reoosn t751;
            gm_f_ev_call_value_f4tfqs3((void *)&t751, (void *)NULL, cx, NULL, t745, t746, t747, t748, t749, t750);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t752 = t751;
            if (((((uint8_t *)&t752))[0] != 0)) {
                gm_t76123gfet73g t753 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t752) + 8));
                gm_tcfa6l7reoosn t754;
                GM_ST(uint8_t, (uint8_t *)&t754 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t754 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t754 + 4, 0);
                ((uint8_t *)&t754)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t754 + 8), t753);
                F.ret = t754;
                goto gm_fret;
            }
            gm_b0_1 t755 = GM_LD(gm_b0_1, (((uint8_t *)&t752) + 8));
            goto me4;
mn741:;
ma31:;
            uint8_t t757 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t757 != ((uint8_t)38ULL)) goto mn756;
            gm_b16_8 t758 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l61 = t758;
            gm_b16_8 t759 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l62 = t759;
            {
                {
                    gm_l64 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t762 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                    gm_l63 = t762;
                }
                {
                    for (;;) {
                        uint64_t t764 = gm_l64;
                        uint64_t t765 = gm_l63;
                        uint8_t t766 = (t764 < t765);
                        if (!t766) goto lb763;
                        {
                            uint64_t t769 = gm_l64;
                            uint64_t t770 = t769;
                            uint64_t t773 = GM_LD(uint64_t, ((uint8_t *)&t770));
                            gm_l65 = t773;
                            {
                                uint64_t t775 = gm_l65;
                                uint64_t t776 = (uintptr_t)t775;
                                uint64_t t777 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                                if (t776 >= (uintptr_t)(t777)) gm_trap_index(t776, t777, gm_src0jbci4ff2esa, 250, 25);
                                uint64_t t778 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t776 * 8));
                                uint64_t t779 = gm_f_us_cuv01fp(t778);
                                gm_l66 = t779;
                            }
                            {
                                void * t780 = gm_l0;
                                gm_tbl8il92l97ut t781 = gm_l1;
                                uint64_t t782 = gm_l65;
                                uint64_t t783 = (uintptr_t)t782;
                                uint64_t t784 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                if (t783 >= (uintptr_t)(t784)) gm_trap_index(t783, t784, gm_src0jbci4ff2esa, 251, 29);
                                void * t785 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l61)) + t783 * 8));
                                gm_b16_8 t786 = gm_l3;
                                gm_b16_8 t787 = t786;
                                uint64_t t788 = GM_LD(uintptr_t, ((uint8_t *)&t787) + 8);
                                void * t789 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t787)));
                                uint64_t t790 = gm_l66;
                                uint64_t t791 = (uintptr_t)t790;
                                uint64_t t792 = gm_l66;
                                uint64_t t793 = gm_l65;
                                uint64_t t794 = (uintptr_t)t793;
                                uint64_t t795 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                if (t794 >= (uintptr_t)(t795)) gm_trap_index(t794, t795, gm_src0jbci4ff2esa, 251, 53);
                                void * t796 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l61)) + t794 * 8));
                                uint64_t t797 = GM_LD(uint64_t, (((uint8_t *)t796) + 104));
                                uint64_t t798 = gm_f_us_cuv01fp(t797);
                                uint64_t r799; bool o800 = __builtin_add_overflow(t792, t798, &r799);
                                if (o800) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 251, 46);
                                uint64_t t801 = r799;
                                uint64_t t802 = (uintptr_t)t801;
                                if (t791 > t802 || t802 > (uintptr_t)t788) gm_trap_slice(t791, t802, t788, gm_src0jbci4ff2esa, 251, 39);
                                gm_b16_8 t803;
                                GM_ST(void *, (uint8_t *)&t803, (void *)(((uint8_t *)t789 + t791 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t803 + 8, (uintptr_t)(t802 - t791));
                                gm_tcfa6l7reoosn t804;
                                gm_f_ev_25jfdar((void *)&t804, (void *)NULL, cx, NULL, t780, t781, t785, t803);
                                if (gm_unw.kind) {
                                    goto gm_ll767;
                                }
                                gm_tcfa6l7reoosn t805 = t804;
                                if (((((uint8_t *)&t805))[0] != 0)) {
                                    gm_t76123gfet73g t806 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t805) + 8));
                                    gm_tcfa6l7reoosn t807;
                                    GM_ST(uint8_t, (uint8_t *)&t807 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t807 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t807 + 4, 0);
                                    ((uint8_t *)&t807)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t807 + 8), t806);
                                    F.ret = t807;
                                    goto gm_fret;
                                }
                                gm_b0_1 t808 = GM_LD(gm_b0_1, (((uint8_t *)&t805) + 8));
                            }
                            goto me771;
mn772:;
                            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 249, 13);
me771:;
                        }
lc763:;
                        {
                            uint64_t t810 = gm_l64;
                            uint64_t r811; bool o812 = __builtin_add_overflow(t810, ((uint64_t)1ULL), &r811);
                            uint64_t t813 = r811;
                            gm_l64 = t813;
                        }
                    }
gm_ll767:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb763; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc763; }
                    }
                    goto gm_lad;
lb763:;
                }
            }
            goto me4;
mn756:;
ma32:;
            uint8_t t815 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t815 != ((uint8_t)39ULL)) goto mn814;
            void * t816 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l67 = t816;
            void * t818 = gm_l67;
            void * t819 = t818;
            if (!(GM_LD(void *, ((uint8_t *)&t819)) != 0)) goto ie817;
            void * t820 = GM_LD(void *, ((uint8_t *)&t819));
            gm_l68 = t820;
            {
                void * t822 = gm_l0;
                gm_tbl8il92l97ut t823 = gm_l1;
                void * t824 = gm_l68;
                gm_b16_8 t825 = gm_l3;
                gm_b16_8 t826 = t825;
                uint64_t t827 = GM_LD(uintptr_t, ((uint8_t *)&t826) + 8);
                void * t828 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t826)));
                uint64_t t829 = (uintptr_t)((uint64_t)0ULL);
                void * t830 = gm_l68;
                uint64_t t831 = GM_LD(uint64_t, (((uint8_t *)t830) + 104));
                uint64_t t832 = gm_f_us_cuv01fp(t831);
                uint64_t t833 = (uintptr_t)t832;
                if (t829 > t833 || t833 > (uintptr_t)t827) gm_trap_slice(t829, t833, t827, gm_src0jbci4ff2esa, 254, 56);
                gm_b16_8 t834;
                GM_ST(void *, (uint8_t *)&t834, (void *)(((uint8_t *)t828 + t829 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t834 + 8, (uintptr_t)(t833 - t829));
                gm_tcfa6l7reoosn t835;
                gm_f_ev_25jfdar((void *)&t835, (void *)NULL, cx, NULL, t822, t823, t824, t834);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t836 = t835;
                if (((((uint8_t *)&t836))[0] != 0)) {
                    gm_t76123gfet73g t837 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t836) + 8));
                    gm_tcfa6l7reoosn t838;
                    GM_ST(uint8_t, (uint8_t *)&t838 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t838 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t838 + 4, 0);
                    ((uint8_t *)&t838)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t838 + 8), t837);
                    F.ret = t838;
                    goto gm_fret;
                }
                gm_b0_1 t839 = GM_LD(gm_b0_1, (((uint8_t *)&t836) + 8));
            }
            goto ix817;
ie817:;
ix817:;
            goto me4;
mn814:;
ma33:;
            uint8_t t841 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t841 != ((uint8_t)40ULL)) goto mn840;
            gm_b16_8 t842 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l69 = t842;
            {
                void * t844 = gm_l0;
                void * t845 = gm_l2;
                gm_t88rcucg1b7uk t846 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t845) + 96));
                gm_tkn42u7na3t81 t847 = gm_f_bits_struct_ty_22ue20s(t844, GM_CONV(gm_csguankko4lmhd, t846));
                gm_tkn42u7na3t81 t848 = t847;
                gm_t88rcucg1b7uk t849;
                if (((((uint8_t *)&t848))[4] != 0)) {
                    gm_t88rcucg1b7uk t850 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t848) + 0));
                    t849 = t850;
                } else {
                    void * t851 = gm_l0;
                    void * t852 = gm_l2;
                    gm_tl874t2atnlh3 t853 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t852) + 112));
                    gm_b16_8 t854;
                    GM_ST(void *, (uint8_t *)&t854, (void *)(gm_sen05m4ui88rt));
                    GM_ST(uintptr_t, (uint8_t *)&t854 + 8, (uintptr_t)(57));
                    gm_t76123gfet73g t855;
                    gm_f_trap_qh84l9d((void *)&t855, (void *)NULL, cx, NULL, t851, t853, t854);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t856;
                    GM_ST(uint8_t, (uint8_t *)&t856 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t856 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t856 + 4, 0);
                    ((uint8_t *)&t856)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t856 + 8), t855);
                    gm_tcfa6l7reoosn t857 = t856;
                    uint8_t t858 = ((((uint8_t *)&t857))[0] != 0);
                    F.ret = t856;
                    goto gm_fret;
                }
                gm_l70 = t849;
            }
            {
                void * t859 = gm_l0;
                gm_t2lqu8jsj511d t860 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t859));
                gm_l71 = t860;
            }
            {
                gm_l72 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                {
                    gm_l74 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t862 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                    gm_l73 = t862;
                }
                {
                    for (;;) {
                        uint64_t t864 = gm_l74;
                        uint64_t t865 = gm_l73;
                        uint8_t t866 = (t864 < t865);
                        if (!t866) goto lb863;
                        {
                            uint64_t t869 = gm_l74;
                            uint64_t t870 = t869;
                            uint64_t t873 = GM_LD(uint64_t, ((uint8_t *)&t870));
                            gm_l75 = t873;
                            {
                                void * t875 = gm_l0;
                                uint64_t t876 = gm_l75;
                                uint64_t t877 = (uintptr_t)t876;
                                uint64_t t878 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                if (t877 >= (uintptr_t)(t878)) gm_trap_index(t877, t878, gm_src0jbci4ff2esa, 260, 28);
                                void * t879 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t877 * 8));
                                uint64_t t880 = GM_LD(uint64_t, (((uint8_t *)t879) + 104));
                                gm_b16_8 t881 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t875, t880);
                                if (gm_unw.kind) {
                                    goto gm_ll867;
                                }
                                gm_l76 = t881;
                            }
                            {
                                void * t882 = gm_l0;
                                gm_tbl8il92l97ut t883 = gm_l1;
                                uint64_t t884 = gm_l75;
                                uint64_t t885 = (uintptr_t)t884;
                                uint64_t t886 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                if (t885 >= (uintptr_t)(t886)) gm_trap_index(t885, t886, gm_src0jbci4ff2esa, 261, 29);
                                void * t887 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t885 * 8));
                                gm_b16_8 t888 = gm_l76;
                                gm_tcfa6l7reoosn t889;
                                gm_f_ev_25jfdar((void *)&t889, (void *)NULL, cx, NULL, t882, t883, t887, t888);
                                if (gm_unw.kind) {
                                    goto gm_ll867;
                                }
                                gm_tcfa6l7reoosn t890 = t889;
                                if (((((uint8_t *)&t890))[0] != 0)) {
                                    gm_t76123gfet73g t891 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t890) + 8));
                                    gm_tcfa6l7reoosn t892;
                                    GM_ST(uint8_t, (uint8_t *)&t892 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t892 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t892 + 4, 0);
                                    ((uint8_t *)&t892)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t892 + 8), t891);
                                    F.ret = t892;
                                    goto gm_fret;
                                }
                                gm_b0_1 t893 = GM_LD(gm_b0_1, (((uint8_t *)&t890) + 8));
                            }
                            {
                                void * t894 = gm_l0;
                                void * t895 = gm_l2;
                                gm_t88rcucg1b7uk t896 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t895) + 96));
                                uint64_t t897 = gm_l75;
                                uint32_t t898 = (uint32_t)t897;
                                gm_tplpbqf3duvn3 t899 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t894, t896, t898);
                                if (gm_unw.kind) {
                                    goto gm_ll867;
                                }
                                gm_l77 = t899;
                            }
                            {
                                unsigned __int128 t900 = gm_l72;
                                void * t901 = gm_l0;
                                gm_t88rcucg1b7uk t902 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l77) + 4));
                                gm_b16_8 t903 = gm_l76;
                                gm_b16_8 t904 = t903;
                                gm_b16_8 t905 = GM_LD(gm_b16_8, ((uint8_t *)&t904));
                                unsigned __int128 t906 = gm_f_bits_raw_t6dqs65((void *)NULL, cx, NULL, t901, t902, t905);
                                if (gm_unw.kind) {
                                    goto gm_ll867;
                                }
                                uint32_t t907 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 0));
                                unsigned __int128 t908 = (unsigned __int128)t907;
                                unsigned __int128 n909 = (unsigned __int128)t908;
                                if (n909 >= 128) gm_trap_shift((uint64_t)n909, "u128", gm_src0jbci4ff2esa, 263, 24);
                                unsigned __int128 r910 = (unsigned __int128)((unsigned __int128)t906 << (int)n909);
                                bool o911 = (unsigned __int128)(r910 >> (int)n909) != t906;
                                if (o911) gm_trap("integer overflow in `<<`", gm_src0jbci4ff2esa, 263, 24);
                                unsigned __int128 t912 = r910;
                                unsigned __int128 t913 = (unsigned __int128)(t900 | t912);
                                gm_l72 = t913;
                            }
                            goto me871;
mn872:;
                            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 259, 13);
me871:;
                        }
lc863:;
                        {
                            uint64_t t915 = gm_l74;
                            uint64_t r916; bool o917 = __builtin_add_overflow(t915, ((uint64_t)1ULL), &r916);
                            uint64_t t918 = r916;
                            gm_l74 = t918;
                        }
                    }
gm_ll867:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb863; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc863; }
                    }
                    goto gm_lad;
lb863:;
                }
            }
            {
                void * t919 = gm_l0;
                gm_b16_8 t920 = gm_l3;
                gm_b16_8 t921 = t920;
                uint64_t t922 = GM_LD(uintptr_t, ((uint8_t *)&t921) + 8);
                void * t923 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t921)));
                uint64_t t924 = (uintptr_t)((uint64_t)0ULL);
                void * t925 = gm_l0;
                gm_t88rcucg1b7uk t926 = gm_l70;
                uint64_t t927 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t925, t926);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t928 = gm_f_us_cuv01fp(t927);
                uint64_t t929 = (uintptr_t)t928;
                if (t924 > t929 || t929 > (uintptr_t)t922) gm_trap_slice(t924, t929, t922, gm_src0jbci4ff2esa, 265, 24);
                gm_b16_8 t930;
                GM_ST(void *, (uint8_t *)&t930, (void *)(((uint8_t *)t923 + t924 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t930 + 8, (uintptr_t)(t929 - t924));
                unsigned __int128 t931 = gm_l72;
                gm_f_put_uint_cddin99(t919, t930, t931);
            }
            {
                void * t932 = gm_l0;
                gm_t2lqu8jsj511d t933 = gm_l71;
                gm_f_release_jn5kqdi(t932, GM_CONV(gm_csc7lc6jlkqkuq, t933));
            }
            goto me4;
mn840:;
ma34:;
            uint8_t t935 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t935 != ((uint8_t)41ULL)) goto mn934;
            gm_b16_8 t936 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l78 = t936;
            {
                uint64_t t938;
                uint64_t t940 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                uint8_t t941 = (t940 > ((uint64_t)0ULL));
                if (!t941) goto ie939;
                uint64_t t943 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t944 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                if (t943 >= (uintptr_t)(t944)) gm_trap_index(t943, t944, gm_src0jbci4ff2esa, 269, 40);
                void * t945 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l78)) + t943 * 8));
                uint64_t t946 = GM_LD(uint64_t, (((uint8_t *)t945) + 104));
                uint64_t t947 = gm_f_us_cuv01fp(t946);
                uint64_t t948 = t947;
                t938 = t948;
                goto ix939;
ie939:;
                uint64_t t950 = ((uint64_t)0ULL);
                t938 = t950;
ix939:;
                gm_l79 = t938;
            }
            {
                {
                    gm_l81 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t952 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                    gm_l80 = t952;
                }
                {
                    for (;;) {
                        uint64_t t954 = gm_l81;
                        uint64_t t955 = gm_l80;
                        uint8_t t956 = (t954 < t955);
                        if (!t956) goto lb953;
                        {
                            uint64_t t959 = gm_l81;
                            uint64_t t960 = t959;
                            uint64_t t963 = GM_LD(uint64_t, ((uint8_t *)&t960));
                            gm_l82 = t963;
                            {
                                void * t965 = gm_l0;
                                gm_tbl8il92l97ut t966 = gm_l1;
                                uint64_t t967 = gm_l82;
                                uint64_t t968 = (uintptr_t)t967;
                                uint64_t t969 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                                if (t968 >= (uintptr_t)(t969)) gm_trap_index(t968, t969, gm_src0jbci4ff2esa, 270, 49);
                                void * t970 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l78)) + t968 * 8));
                                uint64_t t971 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t972 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t973 = gm_l82;
                                uint64_t t974 = gm_l79;
                                uint64_t r975; bool o976 = __builtin_mul_overflow(t973, t974, &r975);
                                if (o976) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 270, 63);
                                uint64_t t977 = r975;
                                uint64_t t978 = (uintptr_t)t977;
                                uint64_t t979 = gm_l82;
                                uint64_t r980; bool o981 = __builtin_add_overflow(t979, ((uint64_t)1ULL), &r980);
                                if (o981) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 270, 71);
                                uint64_t t982 = r980;
                                uint64_t t983 = gm_l79;
                                uint64_t r984; bool o985 = __builtin_mul_overflow(t982, t983, &r984);
                                if (o985) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 270, 70);
                                uint64_t t986 = r984;
                                uint64_t t987 = (uintptr_t)t986;
                                if (t978 > t987 || t987 > (uintptr_t)t971) gm_trap_slice(t978, t987, t971, gm_src0jbci4ff2esa, 270, 59);
                                gm_b16_8 t988;
                                GM_ST(void *, (uint8_t *)&t988, (void *)(((uint8_t *)t972 + t978 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t988 + 8, (uintptr_t)(t987 - t978));
                                gm_tcfa6l7reoosn t989;
                                gm_f_ev_25jfdar((void *)&t989, (void *)NULL, cx, NULL, t965, t966, t970, t988);
                                if (gm_unw.kind) {
                                    goto gm_ll957;
                                }
                                gm_tcfa6l7reoosn t990 = t989;
                                if (((((uint8_t *)&t990))[0] != 0)) {
                                    gm_t76123gfet73g t991 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t990) + 8));
                                    gm_tcfa6l7reoosn t992;
                                    GM_ST(uint8_t, (uint8_t *)&t992 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t992 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t992 + 4, 0);
                                    ((uint8_t *)&t992)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t992 + 8), t991);
                                    F.ret = t992;
                                    goto gm_fret;
                                }
                                gm_b0_1 t993 = GM_LD(gm_b0_1, (((uint8_t *)&t990) + 8));
                            }
                            goto me961;
mn962:;
                            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 270, 13);
me961:;
                        }
lc953:;
                        {
                            uint64_t t995 = gm_l81;
                            uint64_t r996; bool o997 = __builtin_add_overflow(t995, ((uint64_t)1ULL), &r996);
                            uint64_t t998 = r996;
                            gm_l81 = t998;
                        }
                    }
gm_ll957:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb953; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc953; }
                    }
                    goto gm_lad;
lb953:;
                }
            }
            goto me4;
mn934:;
ma35:;
            uint8_t t1000 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1000 != ((uint8_t)42ULL)) goto mn999;
            void * t1001 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l83 = t1001;
            {
                void * t1003 = gm_l83;
                uint64_t t1004 = GM_LD(uint64_t, (((uint8_t *)t1003) + 104));
                uint64_t t1005 = gm_f_us_cuv01fp(t1004);
                gm_l84 = t1005;
            }
            {
                uint64_t t1007 = gm_l84;
                uint8_t t1008 = ((t1007 == ((uint64_t)0ULL)));
                if (!t1008) goto ie1006;
                {
                    void * t1010 = gm_l0;
                    gm_tbl8il92l97ut t1011 = gm_l1;
                    void * t1012 = gm_l83;
                    gm_b16_8 t1013 = gm_l3;
                    gm_tcfa6l7reoosn t1014;
                    gm_f_ev_25jfdar((void *)&t1014, (void *)NULL, cx, NULL, t1010, t1011, t1012, t1013);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1015 = t1014;
                    if (((((uint8_t *)&t1015))[0] != 0)) {
                        gm_t76123gfet73g t1016 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1015) + 8));
                        gm_tcfa6l7reoosn t1017;
                        GM_ST(uint8_t, (uint8_t *)&t1017 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1017 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1017 + 4, 0);
                        ((uint8_t *)&t1017)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1017 + 8), t1016);
                        F.ret = t1017;
                        goto gm_fret;
                    }
                    gm_b0_1 t1018 = GM_LD(gm_b0_1, (((uint8_t *)&t1015) + 8));
                }
                {
                    gm_b0_1 t1019;
                    gm_tcfa6l7reoosn t1020;
                    GM_ST(uint8_t, (uint8_t *)&t1020 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1020 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1020 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1020 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1020 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1020 + 24, 0);
                    ((uint8_t *)&t1020)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1020 + 8), t1019);
                    gm_tcfa6l7reoosn t1021 = t1020;
                    uint8_t t1022 = ((((uint8_t *)&t1021))[0] != 0);
                    F.ret = t1020;
                    goto gm_fret;
                }
                goto ix1006;
ie1006:;
ix1006:;
            }
            {
                void * t1023 = gm_l0;
                gm_tbl8il92l97ut t1024 = gm_l1;
                void * t1025 = gm_l83;
                uint64_t t1026 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t1027 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t1028 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1029 = gm_l84;
                uint64_t t1030 = (uintptr_t)t1029;
                if (t1028 > t1030 || t1030 > (uintptr_t)t1026) gm_trap_slice(t1028, t1030, t1026, gm_src0jbci4ff2esa, 278, 28);
                gm_b16_8 t1031;
                GM_ST(void *, (uint8_t *)&t1031, (void *)(((uint8_t *)t1027 + t1028 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t1031 + 8, (uintptr_t)(t1030 - t1028));
                gm_tcfa6l7reoosn t1032;
                gm_f_ev_25jfdar((void *)&t1032, (void *)NULL, cx, NULL, t1023, t1024, t1025, t1031);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1033 = t1032;
                if (((((uint8_t *)&t1033))[0] != 0)) {
                    gm_t76123gfet73g t1034 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1033) + 8));
                    gm_tcfa6l7reoosn t1035;
                    GM_ST(uint8_t, (uint8_t *)&t1035 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1035 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1035 + 4, 0);
                    ((uint8_t *)&t1035)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1035 + 8), t1034);
                    F.ret = t1035;
                    goto gm_fret;
                }
                gm_b0_1 t1036 = GM_LD(gm_b0_1, (((uint8_t *)&t1033) + 8));
            }
            {
                uint64_t t1037 = gm_l84;
                gm_l85 = t1037;
            }
            {
                for (;;) {
                    uint64_t t1039 = gm_l85;
                    uint64_t t1040 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t1041 = (t1039 < t1040);
                    if (!t1041) goto lb1038;
                    {
                        uint64_t t1044 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        void * t1045 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                        uint64_t t1046 = gm_l85;
                        uint64_t t1047 = (uintptr_t)t1046;
                        uint64_t t1048 = gm_l85;
                        uint64_t t1049 = gm_l84;
                        uint64_t r1050; bool o1051 = __builtin_add_overflow(t1048, t1049, &r1050);
                        if (o1051) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 281, 24);
                        uint64_t t1052 = r1050;
                        uint64_t t1053 = (uintptr_t)t1052;
                        if (t1047 > t1053 || t1053 > (uintptr_t)t1044) gm_trap_slice(t1047, t1053, t1044, gm_src0jbci4ff2esa, 281, 17);
                        gm_b16_8 t1054;
                        GM_ST(void *, (uint8_t *)&t1054, (void *)(((uint8_t *)t1045 + t1047 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t1054 + 8, (uintptr_t)(t1053 - t1047));
                        uint64_t t1055 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        void * t1056 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                        uint64_t t1057 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t1058 = gm_l84;
                        uint64_t t1059 = (uintptr_t)t1058;
                        if (t1057 > t1059 || t1059 > (uintptr_t)t1055) gm_trap_slice(t1057, t1059, t1055, gm_src0jbci4ff2esa, 281, 41);
                        gm_b16_8 t1060;
                        GM_ST(void *, (uint8_t *)&t1060, (void *)(((uint8_t *)t1056 + t1057 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t1060 + 8, (uintptr_t)(t1059 - t1057));
                        gm_b16_8 t1061 = t1060;
                        gm_b16_8 t1062 = GM_LD(gm_b16_8, ((uint8_t *)&t1061));
                        gm_f_copy_from_f7j6p05(t1054, t1062);
                    }
                    {
                        uint64_t t1063 = gm_l85;
                        uint64_t t1064 = gm_l84;
                        uint64_t r1065; bool o1066 = __builtin_add_overflow(t1063, t1064, &r1065);
                        if (o1066) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 282, 17);
                        uint64_t t1067 = r1065;
                        gm_l85 = t1067;
                    }
lc1038:;
                }
lb1038:;
            }
            goto me4;
mn999:;
ma36:;
            uint8_t t1069 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1069 != ((uint8_t)43ULL)) goto mn1068;
            uint32_t t1070 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l86 = t1070;
            void * t1071 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l87 = t1071;
            {
                void * t1074 = gm_l87;
                void * t1075 = t1074;
                if (!(GM_LD(void *, ((uint8_t *)&t1075)) != 0)) goto ie1073;
                void * t1076 = GM_LD(void *, ((uint8_t *)&t1075));
                gm_l88 = t1076;
                {
                    void * t1078 = gm_l0;
                    gm_t2lqu8jsj511d t1079 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1078));
                    gm_l89 = t1079;
                }
                {
                    void * t1080 = gm_l0;
                    void * t1081 = gm_l88;
                    uint64_t t1082 = GM_LD(uint64_t, (((uint8_t *)t1081) + 104));
                    gm_b16_8 t1083 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1080, t1082);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l90 = t1083;
                }
                {
                    void * t1084 = gm_l0;
                    gm_tbl8il92l97ut t1085 = gm_l1;
                    void * t1086 = gm_l88;
                    gm_b16_8 t1087 = gm_l90;
                    gm_tcfa6l7reoosn t1088;
                    gm_f_ev_25jfdar((void *)&t1088, (void *)NULL, cx, NULL, t1084, t1085, t1086, t1087);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1089 = t1088;
                    if (((((uint8_t *)&t1089))[0] != 0)) {
                        gm_t76123gfet73g t1090 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1089) + 8));
                        gm_tcfa6l7reoosn t1091;
                        GM_ST(uint8_t, (uint8_t *)&t1091 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1091 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1091 + 4, 0);
                        ((uint8_t *)&t1091)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1091 + 8), t1090);
                        F.ret = t1091;
                        goto gm_fret;
                    }
                    gm_b0_1 t1092 = GM_LD(gm_b0_1, (((uint8_t *)&t1089) + 8));
                }
                {
                    void * t1093 = gm_l0;
                    void * t1094 = gm_l2;
                    gm_t88rcucg1b7uk t1095 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1094) + 96));
                    gm_b16_8 t1096 = gm_l3;
                    uint32_t t1097 = gm_l86;
                    gm_b16_8 t1098 = gm_l90;
                    gm_b16_8 t1099 = t1098;
                    gm_b16_8 t1100 = GM_LD(gm_b16_8, ((uint8_t *)&t1099));
                    gm_b0_1 t1101 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t1093, t1095, t1096, t1097, t1100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t1102 = gm_l0;
                    gm_t2lqu8jsj511d t1103 = gm_l89;
                    gm_f_release_jn5kqdi(t1102, GM_CONV(gm_csc7lc6jlkqkuq, t1103));
                }
                goto ix1073;
ie1073:;
                {
                    void * t1105 = gm_l0;
                    void * t1106 = gm_l2;
                    gm_t88rcucg1b7uk t1107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1106) + 96));
                    gm_b16_8 t1108 = gm_l3;
                    uint32_t t1109 = gm_l86;
                    gm_b16_8 t1110 = gm_f_empty_slice_ijtp9lo();
                    gm_b16_8 t1111 = t1110;
                    gm_b16_8 t1112 = GM_LD(gm_b16_8, ((uint8_t *)&t1111));
                    gm_b0_1 t1113 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t1105, t1107, t1108, t1109, t1112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix1073:;
            }
            goto me4;
mn1068:;
ma37:;
            uint8_t t1115 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1115 != ((uint8_t)44ULL)) goto mn1114;
            void * t1116 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l91 = t1116;
            {
                void * t1118 = gm_l0;
                gm_t2lqu8jsj511d t1119 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1118));
                gm_l92 = t1119;
            }
            {
                void * t1120 = gm_l0;
                void * t1121 = gm_l91;
                uint64_t t1122 = GM_LD(uint64_t, (((uint8_t *)t1121) + 104));
                gm_b16_8 t1123 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1120, t1122);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l93 = t1123;
            }
            {
                void * t1124 = gm_l0;
                gm_tbl8il92l97ut t1125 = gm_l1;
                void * t1126 = gm_l91;
                gm_b16_8 t1127 = gm_l93;
                gm_tcfa6l7reoosn t1128;
                gm_f_ev_25jfdar((void *)&t1128, (void *)NULL, cx, NULL, t1124, t1125, t1126, t1127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1129 = t1128;
                if (((((uint8_t *)&t1129))[0] != 0)) {
                    gm_t76123gfet73g t1130 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1129) + 8));
                    gm_tcfa6l7reoosn t1131;
                    GM_ST(uint8_t, (uint8_t *)&t1131 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1131 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1131 + 4, 0);
                    ((uint8_t *)&t1131)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1131 + 8), t1130);
                    F.ret = t1131;
                    goto gm_fret;
                }
                gm_b0_1 t1132 = GM_LD(gm_b0_1, (((uint8_t *)&t1129) + 8));
            }
            {
                void * t1133 = gm_l0;
                void * t1134 = gm_l2;
                gm_t88rcucg1b7uk t1135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1134) + 96));
                gm_b16_8 t1136 = gm_l3;
                gm_b16_8 t1137 = gm_l93;
                gm_b16_8 t1138 = t1137;
                gm_b16_8 t1139 = GM_LD(gm_b16_8, ((uint8_t *)&t1138));
                gm_b0_1 t1140 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t1133, t1135, t1136, ((uint8_t)1), t1139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t1141 = gm_l0;
                gm_t2lqu8jsj511d t1142 = gm_l92;
                gm_f_release_jn5kqdi(t1141, GM_CONV(gm_csc7lc6jlkqkuq, t1142));
            }
            goto me4;
mn1114:;
ma38:;
            uint8_t t1144 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1144 != ((uint8_t)45ULL)) goto mn1143;
            void * t1145 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l94 = t1145;
            uint8_t t1146 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l95 = t1146;
            void * t1147 = gm_l0;
            gm_tbl8il92l97ut t1148 = gm_l1;
            void * t1149 = gm_l94;
            uint8_t t1150 = gm_l95;
            void * t1151 = gm_l2;
            gm_b16_8 t1152 = gm_l3;
            gm_tcfa6l7reoosn t1153;
            gm_f_unwrap_f7c53ka((void *)&t1153, (void *)NULL, cx, NULL, t1147, t1148, t1149, t1150, t1151, t1152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1154 = t1153;
            if (((((uint8_t *)&t1154))[0] != 0)) {
                gm_t76123gfet73g t1155 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1154) + 8));
                gm_tcfa6l7reoosn t1156;
                GM_ST(uint8_t, (uint8_t *)&t1156 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1156 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1156 + 4, 0);
                ((uint8_t *)&t1156)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1156 + 8), t1155);
                F.ret = t1156;
                goto gm_fret;
            }
            gm_b0_1 t1157 = GM_LD(gm_b0_1, (((uint8_t *)&t1154) + 8));
            goto me4;
mn1143:;
ma39:;
            uint8_t t1159 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1159 != ((uint8_t)46ULL)) goto mn1158;
            gm_t3dcoc7077t1q t1160 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t3) + 0) + 8));
            gm_l96 = t1160;
            void * t1161 = gm_l0;
            gm_tbl8il92l97ut t1162 = gm_l1;
            void * t1163 = GM_LD(void *, (((uint8_t *)&gm_l96) + 0));
            gm_t9805qtonbsag t1164 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l96) + 8));
            void * t1165 = GM_LD(void *, (((uint8_t *)&gm_l96) + 16));
            void * t1166 = gm_l2;
            gm_b16_8 t1167 = gm_l3;
            gm_tcfa6l7reoosn t1168;
            gm_f_else__7epkk2p((void *)&t1168, (void *)NULL, cx, NULL, t1161, t1162, t1163, t1164, t1165, t1166, t1167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1169 = t1168;
            if (((((uint8_t *)&t1169))[0] != 0)) {
                gm_t76123gfet73g t1170 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1169) + 8));
                gm_tcfa6l7reoosn t1171;
                GM_ST(uint8_t, (uint8_t *)&t1171 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1171 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1171 + 4, 0);
                ((uint8_t *)&t1171)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1171 + 8), t1170);
                F.ret = t1171;
                goto gm_fret;
            }
            gm_b0_1 t1172 = GM_LD(gm_b0_1, (((uint8_t *)&t1169) + 8));
            goto me4;
mn1158:;
ma40:;
            uint8_t t1174 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1174 != ((uint8_t)47ULL)) goto mn1173;
            gm_t083kd7b1kkag t1175 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t3) + 0) + 8));
            gm_l97 = t1175;
            void * t1176 = gm_l0;
            gm_tbl8il92l97ut t1177 = gm_l1;
            void * t1178 = GM_LD(void *, (((uint8_t *)&gm_l97) + 0));
            gm_t9805qtonbsag t1179 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l97) + 8));
            uint32_t t1180 = GM_LD(uint32_t, (((uint8_t *)&gm_l97) + 16));
            void * t1181 = gm_l2;
            gm_b16_8 t1182 = gm_l3;
            gm_tcfa6l7reoosn t1183;
            gm_f_try__q6sv04f((void *)&t1183, (void *)NULL, cx, NULL, t1176, t1177, t1178, t1179, t1180, t1181, t1182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1184 = t1183;
            if (((((uint8_t *)&t1184))[0] != 0)) {
                gm_t76123gfet73g t1185 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1184) + 8));
                gm_tcfa6l7reoosn t1186;
                GM_ST(uint8_t, (uint8_t *)&t1186 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1186 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1186 + 4, 0);
                ((uint8_t *)&t1186)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1186 + 8), t1185);
                F.ret = t1186;
                goto gm_fret;
            }
            gm_b0_1 t1187 = GM_LD(gm_b0_1, (((uint8_t *)&t1184) + 8));
            goto me4;
mn1173:;
ma41:;
            uint8_t t1189 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1189 != ((uint8_t)48ULL)) goto mn1188;
            void * t1190 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l98 = t1190;
            void * t1191 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l99 = t1191;
            uint8_t t1192 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l100 = t1192;
            {
                void * t1194 = gm_l0;
                gm_t2lqu8jsj511d t1195 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1194));
                gm_l101 = t1195;
            }
            {
                void * t1196 = gm_l0;
                void * t1197 = gm_l98;
                uint64_t t1198 = GM_LD(uint64_t, (((uint8_t *)t1197) + 104));
                gm_b16_8 t1199 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1196, t1198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l102 = t1199;
            }
            {
                void * t1200 = gm_l0;
                gm_tbl8il92l97ut t1201 = gm_l1;
                void * t1202 = gm_l98;
                gm_b16_8 t1203 = gm_l102;
                gm_tcfa6l7reoosn t1204;
                gm_f_ev_25jfdar((void *)&t1204, (void *)NULL, cx, NULL, t1200, t1201, t1202, t1203);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1205 = t1204;
                if (((((uint8_t *)&t1205))[0] != 0)) {
                    gm_t76123gfet73g t1206 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1205) + 8));
                    gm_tcfa6l7reoosn t1207;
                    GM_ST(uint8_t, (uint8_t *)&t1207 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1207 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1207 + 4, 0);
                    ((uint8_t *)&t1207)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1207 + 8), t1206);
                    F.ret = t1207;
                    goto gm_fret;
                }
                gm_b0_1 t1208 = GM_LD(gm_b0_1, (((uint8_t *)&t1205) + 8));
            }
            {
                void * t1209 = gm_l0;
                void * t1210 = gm_l99;
                uint64_t t1211 = GM_LD(uint64_t, (((uint8_t *)t1210) + 104));
                gm_b16_8 t1212 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1209, t1211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l103 = t1212;
            }
            {
                void * t1213 = gm_l0;
                gm_tbl8il92l97ut t1214 = gm_l1;
                void * t1215 = gm_l99;
                gm_b16_8 t1216 = gm_l103;
                gm_tcfa6l7reoosn t1217;
                gm_f_ev_25jfdar((void *)&t1217, (void *)NULL, cx, NULL, t1213, t1214, t1215, t1216);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1218 = t1217;
                if (((((uint8_t *)&t1218))[0] != 0)) {
                    gm_t76123gfet73g t1219 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1218) + 8));
                    gm_tcfa6l7reoosn t1220;
                    GM_ST(uint8_t, (uint8_t *)&t1220 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1220 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1220 + 4, 0);
                    ((uint8_t *)&t1220)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1220 + 8), t1219);
                    F.ret = t1220;
                    goto gm_fret;
                }
                gm_b0_1 t1221 = GM_LD(gm_b0_1, (((uint8_t *)&t1218) + 8));
            }
            {
                void * t1222 = gm_l0;
                void * t1223 = gm_l2;
                gm_tl874t2atnlh3 t1224 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1223) + 112));
                void * t1225 = gm_l98;
                gm_t88rcucg1b7uk t1226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1225) + 96));
                gm_b16_8 t1227 = gm_l102;
                gm_b16_8 t1228 = gm_l103;
                gm_t1ec8br9ddkav t1229;
                gm_f_equal_e5vf82h((void *)&t1229, (void *)NULL, cx, NULL, t1222, t1224, t1226, t1227, t1228);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t1230 = t1229;
                if (((((uint8_t *)&t1230))[0] != 0)) {
                    gm_t76123gfet73g t1231 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1230) + 8));
                    gm_tcfa6l7reoosn t1232;
                    GM_ST(uint8_t, (uint8_t *)&t1232 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1232 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1232 + 4, 0);
                    ((uint8_t *)&t1232)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1232 + 8), t1231);
                    F.ret = t1232;
                    goto gm_fret;
                }
                uint8_t t1233 = GM_LD(uint8_t, (((uint8_t *)&t1230) + 8));
                gm_l104 = t1233;
            }
            {
                uint64_t t1234 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t1234 >= (uintptr_t)(t1235)) gm_trap_index(t1234, t1235, gm_src0jbci4ff2esa, 313, 13);
                uint8_t *t1236 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t1234 * 1);
                uint8_t t1237;
                uint8_t t1239 = gm_l104;
                uint8_t t1240 = gm_l100;
                uint8_t t1241 = (!(t1239 == t1240));
                if (!t1241) goto ie1238;
                uint8_t t1243 = ((uint8_t)1ULL);
                t1237 = t1243;
                goto ix1238;
ie1238:;
                uint8_t t1245 = ((uint8_t)0ULL);
                t1237 = t1245;
ix1238:;
                GM_ST(uint8_t, t1236, t1237);
            }
            {
                void * t1246 = gm_l0;
                gm_t2lqu8jsj511d t1247 = gm_l101;
                gm_f_release_jn5kqdi(t1246, GM_CONV(gm_csc7lc6jlkqkuq, t1247));
            }
            goto me4;
mn1188:;
ma42:;
            uint8_t t1249 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1249 != ((uint8_t)49ULL)) goto mn1248;
            void * t1250 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l105 = t1250;
            void * t1251 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l106 = t1251;
            uint8_t t1252 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l107 = t1252;
            {
                void * t1254 = gm_l0;
                gm_t2lqu8jsj511d t1255 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1254));
                gm_l108 = t1255;
            }
            {
                void * t1256 = gm_l0;
                void * t1257 = gm_l105;
                uint64_t t1258 = GM_LD(uint64_t, (((uint8_t *)t1257) + 104));
                gm_b16_8 t1259 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1256, t1258);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l109 = t1259;
            }
            {
                void * t1260 = gm_l0;
                gm_tbl8il92l97ut t1261 = gm_l1;
                void * t1262 = gm_l105;
                gm_b16_8 t1263 = gm_l109;
                gm_tcfa6l7reoosn t1264;
                gm_f_ev_25jfdar((void *)&t1264, (void *)NULL, cx, NULL, t1260, t1261, t1262, t1263);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1265 = t1264;
                if (((((uint8_t *)&t1265))[0] != 0)) {
                    gm_t76123gfet73g t1266 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1265) + 8));
                    gm_tcfa6l7reoosn t1267;
                    GM_ST(uint8_t, (uint8_t *)&t1267 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1267 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1267 + 4, 0);
                    ((uint8_t *)&t1267)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1267 + 8), t1266);
                    F.ret = t1267;
                    goto gm_fret;
                }
                gm_b0_1 t1268 = GM_LD(gm_b0_1, (((uint8_t *)&t1265) + 8));
            }
            {
                void * t1269 = gm_l0;
                void * t1270 = gm_l106;
                uint64_t t1271 = GM_LD(uint64_t, (((uint8_t *)t1270) + 104));
                gm_b16_8 t1272 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1269, t1271);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l110 = t1272;
            }
            {
                void * t1273 = gm_l0;
                gm_tbl8il92l97ut t1274 = gm_l1;
                void * t1275 = gm_l106;
                gm_b16_8 t1276 = gm_l110;
                gm_tcfa6l7reoosn t1277;
                gm_f_ev_25jfdar((void *)&t1277, (void *)NULL, cx, NULL, t1273, t1274, t1275, t1276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1278 = t1277;
                if (((((uint8_t *)&t1278))[0] != 0)) {
                    gm_t76123gfet73g t1279 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1278) + 8));
                    gm_tcfa6l7reoosn t1280;
                    GM_ST(uint8_t, (uint8_t *)&t1280 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1280 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1280 + 4, 0);
                    ((uint8_t *)&t1280)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1280 + 8), t1279);
                    F.ret = t1280;
                    goto gm_fret;
                }
                gm_b0_1 t1281 = GM_LD(gm_b0_1, (((uint8_t *)&t1278) + 8));
            }
            {
                void * t1282 = gm_l0;
                void * t1283 = gm_l2;
                gm_tl874t2atnlh3 t1284 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1283) + 112));
                void * t1285 = gm_l105;
                gm_t88rcucg1b7uk t1286 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1285) + 96));
                gm_b16_8 t1287 = gm_l109;
                void * t1288 = gm_l106;
                gm_t88rcucg1b7uk t1289 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1288) + 96));
                gm_b16_8 t1290 = gm_l110;
                gm_t1ec8br9ddkav t1291;
                gm_f_contains_nugmipn((void *)&t1291, (void *)NULL, cx, NULL, t1282, t1284, t1286, t1287, t1289, t1290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t1292 = t1291;
                if (((((uint8_t *)&t1292))[0] != 0)) {
                    gm_t76123gfet73g t1293 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1292) + 8));
                    gm_tcfa6l7reoosn t1294;
                    GM_ST(uint8_t, (uint8_t *)&t1294 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1294 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1294 + 4, 0);
                    ((uint8_t *)&t1294)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1294 + 8), t1293);
                    F.ret = t1294;
                    goto gm_fret;
                }
                uint8_t t1295 = GM_LD(uint8_t, (((uint8_t *)&t1292) + 8));
                gm_l111 = t1295;
            }
            {
                uint64_t t1296 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1297 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t1296 >= (uintptr_t)(t1297)) gm_trap_index(t1296, t1297, gm_src0jbci4ff2esa, 323, 13);
                uint8_t *t1298 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t1296 * 1);
                uint8_t t1299;
                uint8_t t1301 = gm_l111;
                uint8_t t1302 = gm_l107;
                uint8_t t1303 = (!(t1301 == t1302));
                if (!t1303) goto ie1300;
                uint8_t t1305 = ((uint8_t)1ULL);
                t1299 = t1305;
                goto ix1300;
ie1300:;
                uint8_t t1307 = ((uint8_t)0ULL);
                t1299 = t1307;
ix1300:;
                GM_ST(uint8_t, t1298, t1299);
            }
            {
                void * t1308 = gm_l0;
                gm_t2lqu8jsj511d t1309 = gm_l108;
                gm_f_release_jn5kqdi(t1308, GM_CONV(gm_csc7lc6jlkqkuq, t1309));
            }
            goto me4;
mn1248:;
ma43:;
            uint8_t t1311 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1311 != ((uint8_t)50ULL)) goto mn1310;
            void * t1312 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l112 = t1312;
            gm_b16_8 t1313 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l113 = t1313;
            void * t1314 = gm_l0;
            gm_tbl8il92l97ut t1315 = gm_l1;
            void * t1316 = gm_l112;
            gm_b16_8 t1317 = gm_l113;
            void * t1318 = gm_l2;
            gm_b16_8 t1319 = gm_l3;
            gm_tcfa6l7reoosn t1320;
            gm_f_match__tg8e3g3((void *)&t1320, (void *)NULL, cx, NULL, t1314, t1315, t1316, t1317, t1318, t1319);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1321 = t1320;
            if (((((uint8_t *)&t1321))[0] != 0)) {
                gm_t76123gfet73g t1322 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1321) + 8));
                gm_tcfa6l7reoosn t1323;
                GM_ST(uint8_t, (uint8_t *)&t1323 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1323 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1323 + 4, 0);
                ((uint8_t *)&t1323)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1323 + 8), t1322);
                F.ret = t1323;
                goto gm_fret;
            }
            gm_b0_1 t1324 = GM_LD(gm_b0_1, (((uint8_t *)&t1321) + 8));
            goto me4;
mn1310:;
ma44:;
            uint8_t t1326 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1326 != ((uint8_t)51ULL)) goto mn1325;
            void * t1327 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l114 = t1327;
            void * t1328 = gm_l0;
            gm_tbl8il92l97ut t1329 = gm_l1;
            void * t1330 = gm_l114;
            gm_b16_8 t1331 = gm_l3;
            gm_tcfa6l7reoosn t1332;
            gm_f_block_5b89ev3((void *)&t1332, (void *)NULL, cx, NULL, t1328, t1329, t1330, t1331);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1333 = t1332;
            if (((((uint8_t *)&t1333))[0] != 0)) {
                gm_t76123gfet73g t1334 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1333) + 8));
                gm_tcfa6l7reoosn t1335;
                GM_ST(uint8_t, (uint8_t *)&t1335 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1335 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1335 + 4, 0);
                ((uint8_t *)&t1335)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1335 + 8), t1334);
                F.ret = t1335;
                goto gm_fret;
            }
            gm_b0_1 t1336 = GM_LD(gm_b0_1, (((uint8_t *)&t1333) + 8));
            goto me4;
mn1325:;
ma45:;
            uint8_t t1338 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1338 != ((uint8_t)52ULL)) goto mn1337;
            gm_trl2khmuphncd t1339 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t3) + 0) + 8));
            gm_l115 = t1339;
            {
                void * t1342 = gm_l0;
                gm_tbl8il92l97ut t1343 = gm_l1;
                gm_b16_8 t1344 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l115) + 0));
                gm_t1ec8br9ddkav t1345;
                gm_f_conds_30ust77((void *)&t1345, (void *)NULL, cx, NULL, t1342, t1343, t1344);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1ec8br9ddkav t1346 = t1345;
                if (((((uint8_t *)&t1346))[0] != 0)) {
                    gm_t76123gfet73g t1347 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1346) + 8));
                    gm_tcfa6l7reoosn t1348;
                    GM_ST(uint8_t, (uint8_t *)&t1348 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1348 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1348 + 4, 0);
                    ((uint8_t *)&t1348)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1348 + 8), t1347);
                    F.ret = t1348;
                    goto gm_fret;
                }
                uint8_t t1349 = GM_LD(uint8_t, (((uint8_t *)&t1346) + 8));
                if (!t1349) goto ie1341;
                {
                    void * t1351 = gm_l0;
                    gm_tbl8il92l97ut t1352 = gm_l1;
                    void * t1353 = GM_LD(void *, (((uint8_t *)&gm_l115) + 16));
                    gm_b16_8 t1354 = gm_l3;
                    gm_tcfa6l7reoosn t1355;
                    gm_f_ev_25jfdar((void *)&t1355, (void *)NULL, cx, NULL, t1351, t1352, t1353, t1354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1356 = t1355;
                    if (((((uint8_t *)&t1356))[0] != 0)) {
                        gm_t76123gfet73g t1357 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1356) + 8));
                        gm_tcfa6l7reoosn t1358;
                        GM_ST(uint8_t, (uint8_t *)&t1358 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1358 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1358 + 4, 0);
                        ((uint8_t *)&t1358)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1358 + 8), t1357);
                        F.ret = t1358;
                        goto gm_fret;
                    }
                    gm_b0_1 t1359 = GM_LD(gm_b0_1, (((uint8_t *)&t1356) + 8));
                }
                goto ix1341;
ie1341:;
                void * t1361 = GM_LD(void *, (((uint8_t *)&gm_l115) + 24));
                void * t1362 = t1361;
                if (!(GM_LD(void *, ((uint8_t *)&t1362)) != 0)) goto ie1360;
                void * t1363 = GM_LD(void *, ((uint8_t *)&t1362));
                gm_l116 = t1363;
                {
                    void * t1365 = gm_l0;
                    gm_tbl8il92l97ut t1366 = gm_l1;
                    void * t1367 = gm_l116;
                    gm_b16_8 t1368 = gm_l3;
                    gm_tcfa6l7reoosn t1369;
                    gm_f_ev_25jfdar((void *)&t1369, (void *)NULL, cx, NULL, t1365, t1366, t1367, t1368);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1370 = t1369;
                    if (((((uint8_t *)&t1370))[0] != 0)) {
                        gm_t76123gfet73g t1371 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1370) + 8));
                        gm_tcfa6l7reoosn t1372;
                        GM_ST(uint8_t, (uint8_t *)&t1372 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1372 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1372 + 4, 0);
                        ((uint8_t *)&t1372)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1372 + 8), t1371);
                        F.ret = t1372;
                        goto gm_fret;
                    }
                    gm_b0_1 t1373 = GM_LD(gm_b0_1, (((uint8_t *)&t1370) + 8));
                }
                goto ix1360;
ie1360:;
ix1360:;
ix1341:;
            }
            goto me4;
mn1337:;
ma46:;
            uint8_t t1375 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1375 != ((uint8_t)53ULL)) goto mn1374;
            gm_t710lsrq1m8gk t1376 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t3) + 0) + 8));
            F.l117 = t1376;
            void * t1377 = gm_l0;
            gm_tbl8il92l97ut t1378 = gm_l1;
            uint32_t t1379 = GM_LD(uint32_t, (((uint8_t *)&F.l117) + 0));
            gm_b16_8 t1380 = GM_LD(gm_b16_8, (((uint8_t *)&F.l117) + 8));
            void * t1381 = GM_LD(void *, (((uint8_t *)&F.l117) + 24));
            void * t1382 = GM_LD(void *, (((uint8_t *)&F.l117) + 32));
            gm_b16_8 t1383 = gm_l3;
            gm_tcfa6l7reoosn t1384;
            gm_f_loop__3bo7tki((void *)&t1384, (void *)NULL, cx, NULL, t1377, t1378, t1379, t1380, t1381, t1382, t1383);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1385 = t1384;
            if (((((uint8_t *)&t1385))[0] != 0)) {
                gm_t76123gfet73g t1386 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1385) + 8));
                gm_tcfa6l7reoosn t1387;
                GM_ST(uint8_t, (uint8_t *)&t1387 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1387 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1387 + 4, 0);
                ((uint8_t *)&t1387)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1387 + 8), t1386);
                F.ret = t1387;
                goto gm_fret;
            }
            gm_b0_1 t1388 = GM_LD(gm_b0_1, (((uint8_t *)&t1385) + 8));
            goto me4;
mn1374:;
ma47:;
            uint8_t t1390 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1390 != ((uint8_t)54ULL)) goto mn1389;
            gm_te9hrk0u35rev t1391 = GM_LD(gm_te9hrk0u35rev, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l118 = t1391;
            void * t1392 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l119 = t1392;
            {
                void * t1394 = gm_l0;
                gm_tbl8il92l97ut t1395 = gm_l1;
                gm_te9hrk0u35rev t1396 = gm_l118;
                void * t1397 = gm_l2;
                gm_tl874t2atnlh3 t1398 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1397) + 112));
                gm_t9b5r56q9jk9c t1399;
                gm_f_resolve_target_bdf5i1e((void *)&t1399, (void *)NULL, cx, NULL, t1394, t1395, t1396, t1398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9b5r56q9jk9c t1400 = t1399;
                if (((((uint8_t *)&t1400))[0] != 0)) {
                    gm_t76123gfet73g t1401 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1400) + 8));
                    gm_tcfa6l7reoosn t1402;
                    GM_ST(uint8_t, (uint8_t *)&t1402 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1402 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1402 + 4, 0);
                    ((uint8_t *)&t1402)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1402 + 8), t1401);
                    F.ret = t1402;
                    goto gm_fret;
                }
                gm_tbl8il92l97ut t1403 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t1400) + 8));
                gm_l120 = t1403;
            }
            {
                void * t1405 = gm_l119;
                void * t1406 = t1405;
                if (!(GM_LD(void *, ((uint8_t *)&t1406)) != 0)) goto ie1404;
                void * t1407 = GM_LD(void *, ((uint8_t *)&t1406));
                gm_l121 = t1407;
                {
                    void * t1410 = gm_l121;
                    uint64_t t1411 = GM_LD(uint64_t, (((uint8_t *)t1410) + 104));
                    uint8_t t1412 = ((t1411 == ((uint64_t)0ULL)));
                    if (!t1412) goto ie1409;
                    {
                        void * t1414 = gm_l0;
                        gm_tbl8il92l97ut t1415 = gm_l1;
                        void * t1416 = gm_l121;
                        gm_tcfa6l7reoosn t1417;
                        gm_f_ev_discard_8c4a8jg((void *)&t1417, (void *)NULL, cx, NULL, t1414, t1415, t1416);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t1418 = t1417;
                        if (((((uint8_t *)&t1418))[0] != 0)) {
                            gm_t76123gfet73g t1419 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1418) + 8));
                            gm_tcfa6l7reoosn t1420;
                            GM_ST(uint8_t, (uint8_t *)&t1420 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1420 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1420 + 4, 0);
                            ((uint8_t *)&t1420)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1420 + 8), t1419);
                            F.ret = t1420;
                            goto gm_fret;
                        }
                        gm_b0_1 t1421 = GM_LD(gm_b0_1, (((uint8_t *)&t1418) + 8));
                    }
                    goto ix1409;
ie1409:;
                    {
                        void * t1423 = GM_LD(void *, (((uint8_t *)&gm_l120) + 0));
                        uint32_t t1424 = GM_LD(uint32_t, (((uint8_t *)&gm_l120) + 8));
                        gm_th2lqbo77pvsc t1425;
                        gm_f_label_dst_b5cat1e((void *)&t1425, t1423, t1424);
                        gm_th2lqbo77pvsc t1426 = t1425;
                        gm_b16_8 t1427;
                        if (((((uint8_t *)&t1426))[16] != 0)) {
                            gm_b16_8 t1428 = GM_LD(gm_b16_8, (((uint8_t *)&t1426) + 0));
                            t1427 = t1428;
                        } else {
                            void * t1429 = gm_l0;
                            void * t1430 = gm_l2;
                            gm_tl874t2atnlh3 t1431 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1430) + 112));
                            gm_b16_8 t1432;
                            GM_ST(void *, (uint8_t *)&t1432, (void *)(gm_s1kvbdtols1cc));
                            GM_ST(uintptr_t, (uint8_t *)&t1432 + 8, (uintptr_t)(52));
                            gm_t76123gfet73g t1433;
                            gm_f_trap_qh84l9d((void *)&t1433, (void *)NULL, cx, NULL, t1429, t1431, t1432);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t1434;
                            GM_ST(uint8_t, (uint8_t *)&t1434 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1434 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1434 + 4, 0);
                            ((uint8_t *)&t1434)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1434 + 8), t1433);
                            gm_tcfa6l7reoosn t1435 = t1434;
                            uint8_t t1436 = ((((uint8_t *)&t1435))[0] != 0);
                            F.ret = t1434;
                            goto gm_fret;
                        }
                        gm_l122 = t1427;
                    }
                    {
                        void * t1437 = gm_l0;
                        gm_tbl8il92l97ut t1438 = gm_l1;
                        void * t1439 = gm_l121;
                        gm_b16_8 t1440 = gm_l122;
                        gm_tcfa6l7reoosn t1441;
                        gm_f_ev_25jfdar((void *)&t1441, (void *)NULL, cx, NULL, t1437, t1438, t1439, t1440);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t1442 = t1441;
                        if (((((uint8_t *)&t1442))[0] != 0)) {
                            gm_t76123gfet73g t1443 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1442) + 8));
                            gm_tcfa6l7reoosn t1444;
                            GM_ST(uint8_t, (uint8_t *)&t1444 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1444 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1444 + 4, 0);
                            ((uint8_t *)&t1444)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1444 + 8), t1443);
                            F.ret = t1444;
                            goto gm_fret;
                        }
                        gm_b0_1 t1445 = GM_LD(gm_b0_1, (((uint8_t *)&t1442) + 8));
                    }
ix1409:;
                }
                goto ix1404;
ie1404:;
ix1404:;
            }
            {
                void * t1446 = GM_LD(void *, (((uint8_t *)&gm_l120) + 0));
                uint64_t t1447 = GM_LD(uint64_t, (((uint8_t *)t1446) + 0));
                uint32_t t1448 = GM_LD(uint32_t, (((uint8_t *)&gm_l120) + 8));
                gm_tn68el8d14vku t1449;
                GM_ST(uint32_t, (uint8_t *)&t1449 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t1449 + 0), t1447);
                GM_ST(uint32_t, ((uint8_t *)&t1449 + 8), t1448);
                gm_t76123gfet73g t1450;
                GM_ST(uint8_t, (uint8_t *)&t1450 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1450 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1450 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1450 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t1450 + 8), t1449);
                gm_tcfa6l7reoosn t1451;
                GM_ST(uint8_t, (uint8_t *)&t1451 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1451 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1451 + 4, 0);
                ((uint8_t *)&t1451)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1451 + 8), t1450);
                gm_tcfa6l7reoosn t1452 = t1451;
                uint8_t t1453 = ((((uint8_t *)&t1452))[0] != 0);
                F.ret = t1451;
                goto gm_fret;
            }
            goto me4;
mn1389:;
ma48:;
            uint8_t t1455 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1455 != ((uint8_t)55ULL)) goto mn1454;
            gm_te9hrk0u35rev t1456 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t3) + 0) + 8));
            gm_l123 = t1456;
            {
                uint8_t t1459 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l123) + 0) == ((uint8_t)1ULL)));
                if (!t1459) goto ie1458;
                {
                    void * t1461 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                    uint64_t t1462 = GM_LD(uint64_t, (((uint8_t *)t1461) + 0));
                    gm_tf5srh4mp4b3i t1463;
                    GM_ST(uint8_t, (uint8_t *)&t1463 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1463 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1463 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t1463 + 0), t1462);
                    GM_ST(uint8_t, ((uint8_t *)&t1463 + 8), ((uint8_t)0));
                    gm_t76123gfet73g t1464;
                    GM_ST(uint8_t, (uint8_t *)&t1464 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1464 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1464 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1464 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t1464 + 8), t1463);
                    gm_tcfa6l7reoosn t1465;
                    GM_ST(uint8_t, (uint8_t *)&t1465 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1465 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1465 + 4, 0);
                    ((uint8_t *)&t1465)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1465 + 8), t1464);
                    gm_tcfa6l7reoosn t1466 = t1465;
                    uint8_t t1467 = ((((uint8_t *)&t1466))[0] != 0);
                    F.ret = t1465;
                    goto gm_fret;
                }
                goto ix1458;
ie1458:;
ix1458:;
            }
            {
                void * t1468 = gm_l0;
                gm_tbl8il92l97ut t1469 = gm_l1;
                gm_te9hrk0u35rev t1470 = gm_l123;
                void * t1471 = gm_l2;
                gm_tl874t2atnlh3 t1472 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1471) + 112));
                gm_t9b5r56q9jk9c t1473;
                gm_f_resolve_target_bdf5i1e((void *)&t1473, (void *)NULL, cx, NULL, t1468, t1469, t1470, t1472);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9b5r56q9jk9c t1474 = t1473;
                if (((((uint8_t *)&t1474))[0] != 0)) {
                    gm_t76123gfet73g t1475 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1474) + 8));
                    gm_tcfa6l7reoosn t1476;
                    GM_ST(uint8_t, (uint8_t *)&t1476 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1476 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1476 + 4, 0);
                    ((uint8_t *)&t1476)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1476 + 8), t1475);
                    F.ret = t1476;
                    goto gm_fret;
                }
                gm_tbl8il92l97ut t1477 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t1474) + 8));
                gm_l124 = t1477;
            }
            {
                void * t1478 = GM_LD(void *, (((uint8_t *)&gm_l124) + 0));
                uint64_t t1479 = GM_LD(uint64_t, (((uint8_t *)t1478) + 0));
                uint32_t t1480 = GM_LD(uint32_t, (((uint8_t *)&gm_l124) + 8));
                gm_tn68el8d14vku t1481;
                GM_ST(uint32_t, (uint8_t *)&t1481 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t1481 + 0), t1479);
                GM_ST(uint32_t, ((uint8_t *)&t1481 + 8), t1480);
                gm_t76123gfet73g t1482;
                GM_ST(uint8_t, (uint8_t *)&t1482 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1482 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1482 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1482 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t1482 + 8), t1481);
                gm_tcfa6l7reoosn t1483;
                GM_ST(uint8_t, (uint8_t *)&t1483 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1483 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1483 + 4, 0);
                ((uint8_t *)&t1483)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1483 + 8), t1482);
                gm_tcfa6l7reoosn t1484 = t1483;
                uint8_t t1485 = ((((uint8_t *)&t1484))[0] != 0);
                F.ret = t1483;
                goto gm_fret;
            }
            goto me4;
mn1454:;
ma49:;
            uint8_t t1487 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1487 != ((uint8_t)56ULL)) goto mn1486;
            uint32_t t1488 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l125 = t1488;
            void * t1489 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l126 = t1489;
            void * t1490 = gm_l0;
            gm_tbl8il92l97ut t1491 = gm_l1;
            uint32_t t1492 = gm_l125;
            void * t1493 = gm_l126;
            void * t1494 = gm_l2;
            gm_tcfa6l7reoosn t1495;
            gm_f_ev_return_8ggbvjj((void *)&t1495, (void *)NULL, cx, NULL, t1490, t1491, t1492, t1493, t1494);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1496 = t1495;
            if (((((uint8_t *)&t1496))[0] != 0)) {
                gm_t76123gfet73g t1497 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1496) + 8));
                gm_tcfa6l7reoosn t1498;
                GM_ST(uint8_t, (uint8_t *)&t1498 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1498 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1498 + 4, 0);
                ((uint8_t *)&t1498)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1498 + 8), t1497);
                F.ret = t1498;
                goto gm_fret;
            }
            gm_b0_1 t1499 = GM_LD(gm_b0_1, (((uint8_t *)&t1496) + 8));
            goto me4;
mn1486:;
ma50:;
            uint8_t t1501 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1501 != ((uint8_t)57ULL)) goto mn1500;
            void * t1502 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l127 = t1502;
            {
                void * t1504 = gm_l0;
                gm_t2lqu8jsj511d t1505 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1504));
                gm_l128 = t1505;
            }
            {
                void * t1506 = gm_l0;
                void * t1507 = gm_l127;
                uint64_t t1508 = GM_LD(uint64_t, (((uint8_t *)t1507) + 104));
                gm_b16_8 t1509 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1506, t1508);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l129 = t1509;
            }
            {
                void * t1510 = gm_l0;
                gm_tbl8il92l97ut t1511 = gm_l1;
                void * t1512 = gm_l127;
                gm_b16_8 t1513 = gm_l129;
                gm_tcfa6l7reoosn t1514;
                gm_f_ev_25jfdar((void *)&t1514, (void *)NULL, cx, NULL, t1510, t1511, t1512, t1513);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1515 = t1514;
                if (((((uint8_t *)&t1515))[0] != 0)) {
                    gm_t76123gfet73g t1516 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1515) + 8));
                    gm_tcfa6l7reoosn t1517;
                    GM_ST(uint8_t, (uint8_t *)&t1517 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1517 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1517 + 4, 0);
                    ((uint8_t *)&t1517)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1517 + 8), t1516);
                    F.ret = t1517;
                    goto gm_fret;
                }
                gm_b0_1 t1518 = GM_LD(gm_b0_1, (((uint8_t *)&t1515) + 8));
            }
            {
                void * t1519 = gm_l0;
                void * t1520 = gm_l2;
                gm_tl874t2atnlh3 t1521 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1520) + 112));
                gm_b16_8 t1522 = gm_l129;
                gm_b16_8 t1523 = t1522;
                gm_b16_8 t1524 = GM_LD(gm_b16_8, ((uint8_t *)&t1523));
                gm_td0ept12e7q8b t1525;
                gm_f_read_string_ttuk4gr((void *)&t1525, (void *)NULL, cx, NULL, t1519, t1521, t1524);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_td0ept12e7q8b t1526 = t1525;
                if (((((uint8_t *)&t1526))[0] != 0)) {
                    gm_t76123gfet73g t1527 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1526) + 8));
                    gm_tcfa6l7reoosn t1528;
                    GM_ST(uint8_t, (uint8_t *)&t1528 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1528 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1528 + 4, 0);
                    ((uint8_t *)&t1528)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1528 + 8), t1527);
                    F.ret = t1528;
                    goto gm_fret;
                }
                gm_b16_8 t1529 = GM_LD(gm_b16_8, (((uint8_t *)&t1526) + 8));
                gm_l130 = t1529;
            }
            {
                void * t1530 = gm_l0;
                gm_t2lqu8jsj511d t1531 = gm_l128;
                gm_f_release_jn5kqdi(t1530, GM_CONV(gm_csc7lc6jlkqkuq, t1531));
            }
            {
                void * t1532 = gm_l0;
                void * t1533 = gm_l2;
                gm_tl874t2atnlh3 t1534 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1533) + 112));
                gm_b16_8 t1535 = gm_l130;
                gm_t76123gfet73g t1536;
                gm_f_trap_qh84l9d((void *)&t1536, (void *)NULL, cx, NULL, t1532, t1534, t1535);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1537;
                GM_ST(uint8_t, (uint8_t *)&t1537 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1537 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1537 + 4, 0);
                ((uint8_t *)&t1537)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1537 + 8), t1536);
                gm_tcfa6l7reoosn t1538 = t1537;
                uint8_t t1539 = ((((uint8_t *)&t1538))[0] != 0);
                F.ret = t1537;
                goto gm_fret;
            }
            goto me4;
mn1500:;
ma51:;
            uint8_t t1541 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1541 != ((uint8_t)58ULL)) goto mn1540;
            void * t1542 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l131 = t1542;
            void * t1543 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l132 = t1543;
            void * t1544 = gm_l0;
            gm_tbl8il92l97ut t1545 = gm_l1;
            void * t1546 = gm_l131;
            void * t1547 = gm_l132;
            void * t1548 = gm_l2;
            gm_tcfa6l7reoosn t1549;
            gm_f_resume__mi7hk2f((void *)&t1549, (void *)NULL, cx, NULL, t1544, t1545, t1546, t1547, t1548);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1550 = t1549;
            if (((((uint8_t *)&t1550))[0] != 0)) {
                gm_t76123gfet73g t1551 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1550) + 8));
                gm_tcfa6l7reoosn t1552;
                GM_ST(uint8_t, (uint8_t *)&t1552 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1552 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1552 + 4, 0);
                ((uint8_t *)&t1552)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1552 + 8), t1551);
                F.ret = t1552;
                goto gm_fret;
            }
            gm_b0_1 t1553 = GM_LD(gm_b0_1, (((uint8_t *)&t1550) + 8));
            goto me4;
mn1540:;
ma52:;
            uint8_t t1555 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1555 != ((uint8_t)59ULL)) goto mn1554;
            void * t1556 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l133 = t1556;
            void * t1557 = gm_l0;
            gm_tbl8il92l97ut t1558 = gm_l1;
            void * t1559 = gm_l133;
            void * t1560 = gm_l2;
            gm_tcfa6l7reoosn t1561;
            gm_f_cancel__tb288ki((void *)&t1561, (void *)NULL, cx, NULL, t1557, t1558, t1559, t1560);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1562 = t1561;
            if (((((uint8_t *)&t1562))[0] != 0)) {
                gm_t76123gfet73g t1563 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1562) + 8));
                gm_tcfa6l7reoosn t1564;
                GM_ST(uint8_t, (uint8_t *)&t1564 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1564 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1564 + 4, 0);
                ((uint8_t *)&t1564)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1564 + 8), t1563);
                F.ret = t1564;
                goto gm_fret;
            }
            gm_b0_1 t1565 = GM_LD(gm_b0_1, (((uint8_t *)&t1562) + 8));
            goto me4;
mn1554:;
ma53:;
            uint8_t t1567 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1567 != ((uint8_t)60ULL)) goto mn1566;
            gm_tntn2tf15puib t1568 = GM_LD(gm_tntn2tf15puib, ((((uint8_t *)t3) + 0) + 8));
            F.l134 = t1568;
            void * t1569 = gm_l0;
            gm_tbl8il92l97ut t1570 = gm_l1;
            gm_b16_8 t1571 = GM_LD(gm_b16_8, (((uint8_t *)&F.l134) + 40));
            gm_b16_8 t1572 = GM_LD(gm_b16_8, (((uint8_t *)&F.l134) + 56));
            gm_b16_8 t1573 = GM_LD(gm_b16_8, (((uint8_t *)&F.l134) + 72));
            void * t1574 = gm_l2;
            gm_tcfa6l7reoosn t1575;
            gm_f_write__eua2bmh((void *)&t1575, (void *)NULL, cx, NULL, t1569, t1570, &(*(gm_th1g69d7vpipg *)(((uint8_t *)&F.l134) + 0)), t1571, t1572, t1573, t1574);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1576 = t1575;
            if (((((uint8_t *)&t1576))[0] != 0)) {
                gm_t76123gfet73g t1577 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1576) + 8));
                gm_tcfa6l7reoosn t1578;
                GM_ST(uint8_t, (uint8_t *)&t1578 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1578 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1578 + 4, 0);
                ((uint8_t *)&t1578)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1578 + 8), t1577);
                F.ret = t1578;
                goto gm_fret;
            }
            gm_b0_1 t1579 = GM_LD(gm_b0_1, (((uint8_t *)&t1576) + 8));
            goto me4;
mn1566:;
ma54:;
            uint8_t t1581 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1581 != ((uint8_t)61ULL)) goto mn1580;
            gm_t710lsrq1m8gk t1582 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t3) + 0) + 8));
            F.l135 = t1582;
            void * t1583 = gm_l0;
            gm_tbl8il92l97ut t1584 = gm_l1;
            void * t1585 = gm_l2;
            gm_b16_8 t1586 = gm_l3;
            gm_tcfa6l7reoosn t1587;
            gm_f_with__u1jp2ib((void *)&t1587, (void *)NULL, cx, NULL, t1583, t1584, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l135)), t1585, t1586);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1588 = t1587;
            if (((((uint8_t *)&t1588))[0] != 0)) {
                gm_t76123gfet73g t1589 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1588) + 8));
                gm_tcfa6l7reoosn t1590;
                GM_ST(uint8_t, (uint8_t *)&t1590 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1590 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1590 + 4, 0);
                ((uint8_t *)&t1590)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1590 + 8), t1589);
                F.ret = t1590;
                goto gm_fret;
            }
            gm_b0_1 t1591 = GM_LD(gm_b0_1, (((uint8_t *)&t1588) + 8));
            goto me4;
mn1580:;
ma55:;
            uint8_t t1593 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1593 != ((uint8_t)62ULL)) goto mn1592;
            void * t1594 = gm_l0;
            void * t1595 = gm_l2;
            gm_tl874t2atnlh3 t1596 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1595) + 112));
            gm_b16_8 t1597;
            GM_ST(void *, (uint8_t *)&t1597, (void *)(gm_spic5gjomd7lu));
            GM_ST(uintptr_t, (uint8_t *)&t1597 + 8, (uintptr_t)(61));
            gm_t76123gfet73g t1598;
            gm_f_trap_qh84l9d((void *)&t1598, (void *)NULL, cx, NULL, t1594, t1596, t1597);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1599;
            GM_ST(uint8_t, (uint8_t *)&t1599 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1599 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1599 + 4, 0);
            ((uint8_t *)&t1599)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1599 + 8), t1598);
            gm_tcfa6l7reoosn t1600 = t1599;
            uint8_t t1601 = ((((uint8_t *)&t1600))[0] != 0);
            F.ret = t1599;
            goto gm_fret;
            goto me4;
mn1592:;
ma56:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 134, 5);
me4:;
        }
    }
    gm_b0_1 t1602;
    gm_tcfa6l7reoosn t1603;
    GM_ST(uint8_t, (uint8_t *)&t1603 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t1603 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t1603 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t1603 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t1603 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t1603 + 24, 0);
    ((uint8_t *)&t1603)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t1603 + 8), t1602);
    gm_tcfa6l7reoosn t1604 = t1603;
    F.ret = t1604;
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
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ew_c5kil07 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    uint64_t gm_l3; /* v */
    uint32_t gm_l4; /* l */
    uint64_t gm_l5; /* off */
    void * gm_l6; /* fr */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* o */
    gm_tkm39klvnn4cr gm_l10; /* x */
    uint64_t gm_l11; /* p */
    uint64_t gm_l12; /* q */
    gm_ta6jn77i9rmqf gm_l13; /* x */
    uint64_t gm_l14; /* p */
    uint64_t gm_l15; /* q */
    void * gm_l16; /* x */
    uint8_t gm_l17; /* is_and */
    void * gm_l18; /* l */
    void * gm_l19; /* r */
    uint8_t gm_l20; /* v */
    gm_t9qkh0rsooc8d gm_l21; /* op */
    gm_ttovhfee8mf28 gm_l22; /* t */
    void * gm_l23; /* x */
    uint64_t gm_l24; /* v */
    gm_tvdg94bk7aa22 gm_l25; /* op */
    gm_ttovhfee8mf28 gm_l26; /* from */
    gm_ttovhfee8mf28 gm_l27; /* to */
    void * gm_l28; /* x */
    uint64_t gm_l29; /* v */
    uint64_t gm_l31; /* v */
    uint64_t gm_l32; /* k */
    gm_tvdg94bk7aa22 gm_l33; /* op */
    gm_ttovhfee8mf28 gm_l34; /* t */
    void * gm_l35; /* x */
    uint64_t gm_l36; /* v */
    gm_tuggr5sml9o52 gm_l37; /* x */
    uint64_t gm_l38; /* p */
    uint64_t gm_l39; /* q */
    uint32_t gm_l40; /* f */
    gm_b16_8 gm_l41; /* args */
    gm_t2doi72kmmlil gm_l42; /* b */
    gm_b16_8 gm_l43; /* d */
    gm_trl2khmuphncd gm_l44; /* x */
    void * gm_l45; /* el */
    void * gm_l46; /* b */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    void * gm_l50; /* t */
    gm_b16_8 gm_l51; /* h */
    uint64_t gm_l52; /* addr */
    uint64_t gm_l53; /* addr */
    GM_STACK_CHECK(&F, 6897u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma20; case ((uint8_t)31ULL): goto ma20; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma20; case ((uint8_t)35ULL): goto ma16; case ((uint8_t)36ULL): goto ma20; case ((uint8_t)37ULL): goto ma20; case ((uint8_t)38ULL): goto ma20; case ((uint8_t)39ULL): goto ma20; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; case ((uint8_t)51ULL): goto ma18; case ((uint8_t)52ULL): goto ma17; case ((uint8_t)53ULL): goto ma20; case ((uint8_t)54ULL): goto ma20; case ((uint8_t)55ULL): goto ma20; case ((uint8_t)56ULL): goto ma20; case ((uint8_t)57ULL): goto ma20; case ((uint8_t)58ULL): goto ma20; case ((uint8_t)59ULL): goto ma20; case ((uint8_t)60ULL): goto ma20; case ((uint8_t)61ULL): goto ma20; case ((uint8_t)62ULL): goto ma20; default: goto ma20; }
ma4:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t2) + 0) + 8));
        gm_l3 = t25;
        uint64_t t26 = gm_l3;
        gm_t833u93mr4iob t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
        ((uint8_t *)&t27)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t27 + 8), t26);
        gm_t833u93mr4iob t28 = t27;
        uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
        F.ret = t27;
        goto gm_fret;
        goto me3;
mn23:;
ma5:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t31 != ((uint8_t)6ULL)) goto mn30;
        uint32_t t32 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l4 = t32;
        uint64_t t33 = GM_LD(uint64_t, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l5 = t33;
        {
            void * t35 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_l6 = t35;
        }
        {
            uint32_t t36 = gm_l4;
            uint64_t t37 = (uint64_t)t36;
            uint64_t t38 = gm_f_us_cuv01fp(t37);
            gm_l7 = t38;
        }
        {
            uint64_t t40 = gm_l7;
            uint64_t t41 = (uintptr_t)t40;
            void * t42 = gm_l6;
            uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)t42) + 64) + 8);
            if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_src0jbci4ff2esa, 42, 16);
            uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t42) + 64)) + t41 * 8));
            uint8_t t45 = (!(t44 == ((uint64_t)0ULL)));
            if (!t45) goto ie39;
            {
                void * t47 = gm_l0;
                void * t48 = gm_l2;
                gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 112));
                uint64_t t50 = gm_l7;
                uint64_t t51 = (uintptr_t)t50;
                void * t52 = gm_l6;
                uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 64) + 8);
                if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_src0jbci4ff2esa, 42, 64);
                uint64_t t54 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t52) + 64)) + t51 * 8));
                uint64_t t55 = gm_l7;
                uint64_t t56 = (uintptr_t)t55;
                void * t57 = gm_l6;
                void * t58 = GM_LD(void *, (((uint8_t *)t57) + 16));
                uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)t58) + 104) + 8);
                if (t56 >= (uintptr_t)(t59)) gm_trap_index(t56, t59, gm_src0jbci4ff2esa, 42, 75);
                uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t58) + 104)) + t56 * 8));
                void * t61 = gm_l2;
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 104));
                gm_f_load_word_ud3iuha((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t47, t49, t54, t60, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            uint64_t t63 = gm_l5;
            uint64_t t64 = gm_f_us_cuv01fp(t63);
            gm_l8 = t64;
        }
        {
            void * t65 = gm_l0;
            void * t66 = gm_l6;
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)t66) + 48));
            gm_b16_8 t68 = t67;
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&t68) + 8);
            void * t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t68)));
            uint64_t t71 = gm_l8;
            uint64_t t72 = (uintptr_t)t71;
            uint64_t t73 = gm_l8;
            void * t74 = gm_l2;
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 104));
            uint64_t t76 = gm_f_us_cuv01fp(t75);
            uint64_t r77; bool o78 = __builtin_add_overflow(t73, t76, &r77);
            if (o78) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 44, 42);
            uint64_t t79 = r77;
            uint64_t t80 = (uintptr_t)t79;
            if (t72 > t80 || t80 > (uintptr_t)t69) gm_trap_slice(t72, t80, t69, gm_src0jbci4ff2esa, 44, 30);
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t70 + t72 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80 - t72));
            gm_b16_8 t82 = t81;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            uint64_t t84 = gm_f_rd_word_8flcq6o(t65, t83);
            gm_t833u93mr4iob t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 24, 0);
            ((uint8_t *)&t85)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t85 + 8), t84);
            gm_t833u93mr4iob t86 = t85;
            uint8_t t87 = ((((uint8_t *)&t86))[0] != 0);
            F.ret = t85;
            goto gm_fret;
        }
        goto me3;
mn30:;
ma6:;
        uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t89 != ((uint8_t)21ULL)) goto mn88;
        gm_t0pc2c7dafsjt t90 = GM_LD(gm_t0pc2c7dafsjt, ((((uint8_t *)t2) + 0) + 8));
        F.l9 = t90;
        void * t91 = gm_l0;
        gm_tbl8il92l97ut t92 = gm_l1;
        gm_ts6oqppjh0ba3 t93 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&F.l9) + 0));
        gm_ttovhfee8mf28 t94 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l9) + 4));
        gm_ttovhfee8mf28 t95 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l9) + 12));
        void * t96 = GM_LD(void *, (((uint8_t *)&F.l9) + 24));
        void * t97 = GM_LD(void *, (((uint8_t *)&F.l9) + 32));
        void * t98 = gm_l2;
        gm_f_int_op_5g0lv3e((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t91, t92, t93, t94, t95, t96, t97, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn88:;
ma7:;
        uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t100 != ((uint8_t)23ULL)) goto mn99;
        gm_tkm39klvnn4cr t101 = GM_LD(gm_tkm39klvnn4cr, ((((uint8_t *)t2) + 0) + 8));
        gm_l10 = t101;
        {
            void * t103 = gm_l0;
            gm_tbl8il92l97ut t104 = gm_l1;
            void * t105 = GM_LD(void *, (((uint8_t *)&gm_l10) + 16));
            gm_t833u93mr4iob t106;
            gm_f_ew_c5kil07((void *)&t106, (void *)NULL, cx, NULL, t103, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t107 = t106;
            if (((((uint8_t *)&t107))[0] != 0)) {
                gm_t76123gfet73g t108 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t107) + 8));
                gm_t833u93mr4iob t109;
                GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                ((uint8_t *)&t109)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t109 + 8), t108);
                F.ret = t109;
                goto gm_fret;
            }
            uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&t107) + 8));
            gm_l11 = t110;
        }
        {
            void * t111 = gm_l0;
            gm_tbl8il92l97ut t112 = gm_l1;
            void * t113 = GM_LD(void *, (((uint8_t *)&gm_l10) + 24));
            gm_t833u93mr4iob t114;
            gm_f_ew_c5kil07((void *)&t114, (void *)NULL, cx, NULL, t111, t112, t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t115 = t114;
            if (((((uint8_t *)&t115))[0] != 0)) {
                gm_t76123gfet73g t116 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t115) + 8));
                gm_t833u93mr4iob t117;
                GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                ((uint8_t *)&t117)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t117 + 8), t116);
                F.ret = t117;
                goto gm_fret;
            }
            uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&t115) + 8));
            gm_l12 = t118;
        }
        {
            gm_t833u93mr4iob t119;
            gm_ts6oqppjh0ba3 t121 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l10) + 0));
            gm_tmn7n0n9raou5 t122 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l10) + 4));
            uint64_t t123 = gm_l11;
            uint64_t t124 = gm_l12;
            uint8_t t125 = gm_f_word_less_l13biee(t121, t122, t123, t124);
            if (!t125) goto ie120;
            uint64_t t127 = ((uint64_t)1ULL);
            gm_t833u93mr4iob t128;
            GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t128 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t128 + 24, 0);
            ((uint8_t *)&t128)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t128 + 8), t127);
            t119 = t128;
            goto ix120;
ie120:;
            uint64_t t130 = ((uint64_t)0ULL);
            gm_t833u93mr4iob t131;
            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t131 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t131 + 24, 0);
            ((uint8_t *)&t131)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t131 + 8), t130);
            t119 = t131;
ix120:;
            gm_t833u93mr4iob t132 = t119;
            uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
            F.ret = t119;
            goto gm_fret;
        }
        goto me3;
mn99:;
ma8:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t135 != ((uint8_t)24ULL)) goto mn134;
        gm_ta6jn77i9rmqf t136 = GM_LD(gm_ta6jn77i9rmqf, ((((uint8_t *)t2) + 0) + 8));
        gm_l13 = t136;
        {
            void * t138 = gm_l0;
            gm_tbl8il92l97ut t139 = gm_l1;
            void * t140 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
            gm_t833u93mr4iob t141;
            gm_f_ew_c5kil07((void *)&t141, (void *)NULL, cx, NULL, t138, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t142 = t141;
            if (((((uint8_t *)&t142))[0] != 0)) {
                gm_t76123gfet73g t143 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t142) + 8));
                gm_t833u93mr4iob t144;
                GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                ((uint8_t *)&t144)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t144 + 8), t143);
                F.ret = t144;
                goto gm_fret;
            }
            uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&t142) + 8));
            gm_l14 = t145;
        }
        {
            void * t146 = gm_l0;
            gm_tbl8il92l97ut t147 = gm_l1;
            void * t148 = GM_LD(void *, (((uint8_t *)&gm_l13) + 16));
            gm_t833u93mr4iob t149;
            gm_f_ew_c5kil07((void *)&t149, (void *)NULL, cx, NULL, t146, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t150 = t149;
            if (((((uint8_t *)&t150))[0] != 0)) {
                gm_t76123gfet73g t151 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t150) + 8));
                gm_t833u93mr4iob t152;
                GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                ((uint8_t *)&t152)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t152 + 8), t151);
                F.ret = t152;
                goto gm_fret;
            }
            uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&t150) + 8));
            gm_l15 = t153;
        }
        {
            gm_t833u93mr4iob t154;
            gm_tmn7n0n9raou5 t156 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l13) + 0));
            uint64_t t157 = gm_l14;
            uint64_t t158 = gm_l15;
            uint8_t t159 = gm_f_word_eq_8jc0uot(t156, t157, t158);
            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 24));
            uint8_t t161 = (!(t159 == t160));
            if (!t161) goto ie155;
            uint64_t t163 = ((uint64_t)1ULL);
            gm_t833u93mr4iob t164;
            GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t164 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t164 + 24, 0);
            ((uint8_t *)&t164)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t164 + 8), t163);
            t154 = t164;
            goto ix155;
ie155:;
            uint64_t t166 = ((uint64_t)0ULL);
            gm_t833u93mr4iob t167;
            GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t167 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t167 + 24, 0);
            ((uint8_t *)&t167)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t167 + 8), t166);
            t154 = t167;
ix155:;
            gm_t833u93mr4iob t168 = t154;
            uint8_t t169 = ((((uint8_t *)&t168))[0] != 0);
            F.ret = t154;
            goto gm_fret;
        }
        goto me3;
mn134:;
ma9:;
        uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t171 != ((uint8_t)25ULL)) goto mn170;
        void * t172 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l16 = t172;
        gm_t833u93mr4iob t173;
        void * t175 = gm_l0;
        gm_tbl8il92l97ut t176 = gm_l1;
        void * t177 = gm_l16;
        gm_t833u93mr4iob t178;
        gm_f_ew_c5kil07((void *)&t178, (void *)NULL, cx, NULL, t175, t176, t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t179 = t178;
        if (((((uint8_t *)&t179))[0] != 0)) {
            gm_t76123gfet73g t180 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t179) + 8));
            gm_t833u93mr4iob t181;
            GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
            ((uint8_t *)&t181)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t181 + 8), t180);
            F.ret = t181;
            goto gm_fret;
        }
        uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&t179) + 8));
        uint8_t t183 = ((t182 == ((uint64_t)0ULL)));
        if (!t183) goto ie174;
        uint64_t t185 = ((uint64_t)1ULL);
        gm_t833u93mr4iob t186;
        GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t186 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t186 + 24, 0);
        ((uint8_t *)&t186)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t186 + 8), t185);
        t173 = t186;
        goto ix174;
ie174:;
        uint64_t t188 = ((uint64_t)0ULL);
        gm_t833u93mr4iob t189;
        GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t189 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t189 + 24, 0);
        ((uint8_t *)&t189)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t189 + 8), t188);
        t173 = t189;
ix174:;
        gm_t833u93mr4iob t190 = t173;
        uint8_t t191 = ((((uint8_t *)&t190))[0] != 0);
        F.ret = t173;
        goto gm_fret;
        goto me3;
mn170:;
ma10:;
        uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t193 != ((uint8_t)32ULL)) goto mn192;
        uint8_t t194 = GM_LD(uint8_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l17 = t194;
        void * t195 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l18 = t195;
        void * t196 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 16));
        gm_l19 = t196;
        {
            void * t198 = gm_l0;
            gm_tbl8il92l97ut t199 = gm_l1;
            void * t200 = gm_l18;
            gm_t833u93mr4iob t201;
            gm_f_ew_c5kil07((void *)&t201, (void *)NULL, cx, NULL, t198, t199, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t202 = t201;
            if (((((uint8_t *)&t202))[0] != 0)) {
                gm_t76123gfet73g t203 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t202) + 8));
                gm_t833u93mr4iob t204;
                GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
                ((uint8_t *)&t204)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t204 + 8), t203);
                F.ret = t204;
                goto gm_fret;
            }
            uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&t202) + 8));
            uint8_t t206 = (!(t205 == ((uint64_t)0ULL)));
            gm_l20 = t206;
        }
        {
            uint8_t t208 = gm_l20;
            uint8_t t209 = gm_l17;
            uint8_t t210 = (!(t208 == t209));
            if (!t210) goto ie207;
            {
                gm_t833u93mr4iob t212;
                uint8_t t214 = gm_l20;
                if (!t214) goto ie213;
                uint64_t t216 = ((uint64_t)1ULL);
                gm_t833u93mr4iob t217;
                GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t217 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t217 + 24, 0);
                ((uint8_t *)&t217)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t217 + 8), t216);
                t212 = t217;
                goto ix213;
ie213:;
                uint64_t t219 = ((uint64_t)0ULL);
                gm_t833u93mr4iob t220;
                GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t220 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t220 + 24, 0);
                ((uint8_t *)&t220)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t220 + 8), t219);
                t212 = t220;
ix213:;
                gm_t833u93mr4iob t221 = t212;
                uint8_t t222 = ((((uint8_t *)&t221))[0] != 0);
                F.ret = t212;
                goto gm_fret;
            }
            goto ix207;
ie207:;
ix207:;
        }
        {
            gm_t833u93mr4iob t223;
            void * t225 = gm_l0;
            gm_tbl8il92l97ut t226 = gm_l1;
            void * t227 = gm_l19;
            gm_t833u93mr4iob t228;
            gm_f_ew_c5kil07((void *)&t228, (void *)NULL, cx, NULL, t225, t226, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t229 = t228;
            if (((((uint8_t *)&t229))[0] != 0)) {
                gm_t76123gfet73g t230 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t229) + 8));
                gm_t833u93mr4iob t231;
                GM_ST(uint8_t, (uint8_t *)&t231 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t231 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t231 + 4, 0);
                ((uint8_t *)&t231)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t231 + 8), t230);
                F.ret = t231;
                goto gm_fret;
            }
            uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)&t229) + 8));
            uint8_t t233 = (!(t232 == ((uint64_t)0ULL)));
            if (!t233) goto ie224;
            uint64_t t235 = ((uint64_t)1ULL);
            gm_t833u93mr4iob t236;
            GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t236 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t236 + 24, 0);
            ((uint8_t *)&t236)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t236 + 8), t235);
            t223 = t236;
            goto ix224;
ie224:;
            uint64_t t238 = ((uint64_t)0ULL);
            gm_t833u93mr4iob t239;
            GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t239 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t239 + 24, 0);
            ((uint8_t *)&t239)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t239 + 8), t238);
            t223 = t239;
ix224:;
            gm_t833u93mr4iob t240 = t223;
            uint8_t t241 = ((((uint8_t *)&t240))[0] != 0);
            F.ret = t223;
            goto gm_fret;
        }
        goto me3;
mn192:;
ma11:;
        uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t243 != ((uint8_t)26ULL)) goto mn242;
        gm_t9qkh0rsooc8d t244 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l21 = t244;
        gm_ttovhfee8mf28 t245 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 4));
        gm_l22 = t245;
        void * t246 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 16));
        gm_l23 = t246;
        {
            void * t248 = gm_l0;
            gm_tbl8il92l97ut t249 = gm_l1;
            void * t250 = gm_l23;
            gm_t833u93mr4iob t251;
            gm_f_ew_c5kil07((void *)&t251, (void *)NULL, cx, NULL, t248, t249, t250);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t252 = t251;
            if (((((uint8_t *)&t252))[0] != 0)) {
                gm_t76123gfet73g t253 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t252) + 8));
                gm_t833u93mr4iob t254;
                GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
                ((uint8_t *)&t254)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t254 + 8), t253);
                F.ret = t254;
                goto gm_fret;
            }
            uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&t252) + 8));
            gm_l24 = t255;
        }
        {
            void * t256 = gm_l0;
            gm_t9qkh0rsooc8d t257 = gm_l21;
            gm_ttovhfee8mf28 t258 = gm_l22;
            uint64_t t259 = gm_l24;
            void * t260 = gm_l2;
            gm_f_int_un_sm4tv7d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t256, t257, t258, t259, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn242:;
ma12:;
        uint8_t t262 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t262 != ((uint8_t)27ULL)) goto mn261;
        gm_tvdg94bk7aa22 t263 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l25 = t263;
        gm_ttovhfee8mf28 t264 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 12));
        gm_l26 = t264;
        gm_ttovhfee8mf28 t265 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 20));
        gm_l27 = t265;
        void * t266 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 32));
        gm_l28 = t266;
        {
            void * t268 = gm_l0;
            gm_tbl8il92l97ut t269 = gm_l1;
            void * t270 = gm_l28;
            gm_t833u93mr4iob t271;
            gm_f_ew_c5kil07((void *)&t271, (void *)NULL, cx, NULL, t268, t269, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t272 = t271;
            if (((((uint8_t *)&t272))[0] != 0)) {
                gm_t76123gfet73g t273 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t272) + 8));
                gm_t833u93mr4iob t274;
                GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
                ((uint8_t *)&t274)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t274 + 8), t273);
                F.ret = t274;
                goto gm_fret;
            }
            uint64_t t275 = GM_LD(uint64_t, (((uint8_t *)&t272) + 8));
            gm_l29 = t275;
        }
        {
            gm_tvdg94bk7aa22 t276 = gm_l25;
            gm_ttovhfee8mf28 t277 = gm_l26;
            gm_ttovhfee8mf28 t278 = gm_l27;
            uint64_t t279 = gm_l29;
            uint64_t t280 = gm_f_int_conv_rqdvurn(t276, GM_CONV(gm_csfd6et11lk21p, t277), GM_CONV(gm_csfd6et11lk21p, t278), t279);
            gm_t833u93mr4iob t281;
            GM_ST(uint8_t, (uint8_t *)&t281 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t281 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t281 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t281 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t281 + 24, 0);
            ((uint8_t *)&t281)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t281 + 8), t280);
            gm_t833u93mr4iob t282 = t281;
            uint8_t t283 = ((((uint8_t *)&t282))[0] != 0);
            F.ret = t281;
            goto gm_fret;
        }
        goto me3;
mn261:;
ma13:;
        uint8_t t285 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t285 != ((uint8_t)28ULL)) goto mn284;
        gm_tscalq56heofe t286 = GM_LD(gm_tscalq56heofe, ((((uint8_t *)t2) + 0) + 8));
        F.l30 = t286;
        {
            void * t288 = gm_l0;
            gm_tbl8il92l97ut t289 = gm_l1;
            void * t290 = GM_LD(void *, (((uint8_t *)&F.l30) + 24));
            gm_t833u93mr4iob t291;
            gm_f_ew_c5kil07((void *)&t291, (void *)NULL, cx, NULL, t288, t289, t290);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t292 = t291;
            if (((((uint8_t *)&t292))[0] != 0)) {
                gm_t76123gfet73g t293 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t292) + 8));
                gm_t833u93mr4iob t294;
                GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                ((uint8_t *)&t294)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t294 + 8), t293);
                F.ret = t294;
                goto gm_fret;
            }
            uint64_t t295 = GM_LD(uint64_t, (((uint8_t *)&t292) + 8));
            gm_l31 = t295;
        }
        {
            void * t296 = gm_l0;
            gm_tbl8il92l97ut t297 = gm_l1;
            void * t298 = GM_LD(void *, (((uint8_t *)&F.l30) + 32));
            gm_t833u93mr4iob t299;
            gm_f_ew_c5kil07((void *)&t299, (void *)NULL, cx, NULL, t296, t297, t298);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t300 = t299;
            if (((((uint8_t *)&t300))[0] != 0)) {
                gm_t76123gfet73g t301 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t300) + 8));
                gm_t833u93mr4iob t302;
                GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                ((uint8_t *)&t302)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t302 + 8), t301);
                F.ret = t302;
                goto gm_fret;
            }
            uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&t300) + 8));
            gm_l32 = t303;
        }
        {
            gm_tvdg94bk7aa22 t304 = GM_LD(gm_tvdg94bk7aa22, (((uint8_t *)&F.l30) + 0));
            gm_ttovhfee8mf28 t305 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l30) + 12));
            uint64_t t306 = gm_l31;
            uint64_t t307 = gm_l32;
            uint64_t t308 = gm_f_bit_word_7ae821g(t304, GM_CONV(gm_csfd6et11lk21p, t305), t306, t307);
            gm_t833u93mr4iob t309;
            GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t309 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t309 + 24, 0);
            ((uint8_t *)&t309)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t309 + 8), t308);
            gm_t833u93mr4iob t310 = t309;
            uint8_t t311 = ((((uint8_t *)&t310))[0] != 0);
            F.ret = t309;
            goto gm_fret;
        }
        goto me3;
mn284:;
ma14:;
        uint8_t t313 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t313 != ((uint8_t)29ULL)) goto mn312;
        gm_tvdg94bk7aa22 t314 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l33 = t314;
        gm_ttovhfee8mf28 t315 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 12));
        gm_l34 = t315;
        void * t316 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 24));
        gm_l35 = t316;
        {
            void * t318 = gm_l0;
            gm_tbl8il92l97ut t319 = gm_l1;
            void * t320 = gm_l35;
            gm_t833u93mr4iob t321;
            gm_f_ew_c5kil07((void *)&t321, (void *)NULL, cx, NULL, t318, t319, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t322 = t321;
            if (((((uint8_t *)&t322))[0] != 0)) {
                gm_t76123gfet73g t323 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t322) + 8));
                gm_t833u93mr4iob t324;
                GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
                ((uint8_t *)&t324)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t324 + 8), t323);
                F.ret = t324;
                goto gm_fret;
            }
            uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&t322) + 8));
            gm_l36 = t325;
        }
        {
            gm_tvdg94bk7aa22 t326 = gm_l33;
            gm_ttovhfee8mf28 t327 = gm_l34;
            uint64_t t328 = gm_l36;
            uint64_t t329 = gm_f_bit_word_7ae821g(t326, GM_CONV(gm_csfd6et11lk21p, t327), t328, ((uint64_t)0ULL));
            gm_t833u93mr4iob t330;
            GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t330 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t330 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t330 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t330 + 24, 0);
            ((uint8_t *)&t330)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t330 + 8), t329);
            gm_t833u93mr4iob t331 = t330;
            uint8_t t332 = ((((uint8_t *)&t331))[0] != 0);
            F.ret = t330;
            goto gm_fret;
        }
        goto me3;
mn312:;
ma15:;
        uint8_t t334 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t334 != ((uint8_t)22ULL)) goto mn333;
        gm_tuggr5sml9o52 t335 = GM_LD(gm_tuggr5sml9o52, ((((uint8_t *)t2) + 0) + 8));
        gm_l37 = t335;
        {
            void * t337 = gm_l0;
            gm_tbl8il92l97ut t338 = gm_l1;
            void * t339 = GM_LD(void *, (((uint8_t *)&gm_l37) + 8));
            gm_t833u93mr4iob t340;
            gm_f_ew_c5kil07((void *)&t340, (void *)NULL, cx, NULL, t337, t338, t339);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t341 = t340;
            if (((((uint8_t *)&t341))[0] != 0)) {
                gm_t76123gfet73g t342 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t341) + 8));
                gm_t833u93mr4iob t343;
                GM_ST(uint8_t, (uint8_t *)&t343 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t343 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t343 + 4, 0);
                ((uint8_t *)&t343)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t343 + 8), t342);
                F.ret = t343;
                goto gm_fret;
            }
            uint64_t t344 = GM_LD(uint64_t, (((uint8_t *)&t341) + 8));
            gm_l38 = t344;
        }
        {
            void * t345 = gm_l0;
            gm_tbl8il92l97ut t346 = gm_l1;
            void * t347 = GM_LD(void *, (((uint8_t *)&gm_l37) + 16));
            gm_t833u93mr4iob t348;
            gm_f_ew_c5kil07((void *)&t348, (void *)NULL, cx, NULL, t345, t346, t347);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t349 = t348;
            if (((((uint8_t *)&t349))[0] != 0)) {
                gm_t76123gfet73g t350 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t349) + 8));
                gm_t833u93mr4iob t351;
                GM_ST(uint8_t, (uint8_t *)&t351 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t351 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t351 + 4, 0);
                ((uint8_t *)&t351)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t351 + 8), t350);
                F.ret = t351;
                goto gm_fret;
            }
            uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&t349) + 8));
            gm_l39 = t352;
        }
        {
            gm_ts6oqppjh0ba3 t353 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l37) + 0));
            uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 2));
            uint64_t t355 = gm_l38;
            uint64_t t356 = gm_l39;
            uint64_t t357 = gm_f_float_op_osevuqr(t353, t354, t355, t356);
            gm_t833u93mr4iob t358;
            GM_ST(uint8_t, (uint8_t *)&t358 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t358 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t358 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t358 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t358 + 24, 0);
            ((uint8_t *)&t358)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t358 + 8), t357);
            gm_t833u93mr4iob t359 = t358;
            uint8_t t360 = ((((uint8_t *)&t359))[0] != 0);
            F.ret = t358;
            goto gm_fret;
        }
        goto me3;
mn333:;
ma16:;
        uint8_t t362 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t362 != ((uint8_t)35ULL)) goto mn361;
        uint32_t t363 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l40 = t363;
        gm_b16_8 t364 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l41 = t364;
        {
            gm_t2doi72kmmlil t366;
            GM_ST(uint8_t, ((uint8_t *)&t366 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 1), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 2), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 3), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 4), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 5), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 6), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t366 + 7), ((uint8_t)0ULL));
            gm_l42 = t366;
        }
        {
            void * t367 = ((uint8_t *)&gm_l42);
            uint64_t t368 = (uintptr_t)((uint64_t)0ULL);
            void * t369 = gm_l2;
            uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)t369) + 104));
            uint64_t t371 = gm_f_us_cuv01fp(t370);
            uint64_t t372 = (uintptr_t)t371;
            if (t368 > t372 || t372 > (uintptr_t)8) gm_trap_slice(t368, t372, 8, gm_src0jbci4ff2esa, 87, 18);
            gm_b16_8 t373;
            GM_ST(void *, (uint8_t *)&t373, (void *)(((uint8_t *)t367 + t368 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(t372 - t368));
            gm_l43 = t373;
        }
        {
            void * t374 = gm_l0;
            gm_tbl8il92l97ut t375 = gm_l1;
            uint32_t t376 = gm_l40;
            gm_b16_8 t377 = gm_l41;
            void * t378 = gm_l2;
            gm_b16_8 t379 = gm_l43;
            gm_tcfa6l7reoosn t380;
            gm_f_call_node_a1b30k9((void *)&t380, (void *)NULL, cx, NULL, t374, t375, t376, t377, t378, t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t381 = t380;
            if (((((uint8_t *)&t381))[0] != 0)) {
                gm_t76123gfet73g t382 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t381) + 8));
                gm_t833u93mr4iob t383;
                GM_ST(uint8_t, (uint8_t *)&t383 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t383 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t383 + 4, 0);
                ((uint8_t *)&t383)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t383 + 8), t382);
                F.ret = t383;
                goto gm_fret;
            }
            gm_b0_1 t384 = GM_LD(gm_b0_1, (((uint8_t *)&t381) + 8));
        }
        {
            void * t385 = gm_l0;
            gm_b16_8 t386 = gm_l43;
            gm_b16_8 t387 = t386;
            gm_b16_8 t388 = GM_LD(gm_b16_8, ((uint8_t *)&t387));
            uint64_t t389 = gm_f_rd_word_8flcq6o(t385, t388);
            gm_t833u93mr4iob t390;
            GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t390 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t390 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t390 + 24, 0);
            ((uint8_t *)&t390)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t390 + 8), t389);
            gm_t833u93mr4iob t391 = t390;
            uint8_t t392 = ((((uint8_t *)&t391))[0] != 0);
            F.ret = t390;
            goto gm_fret;
        }
        goto me3;
mn361:;
ma17:;
        uint8_t t394 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t394 != ((uint8_t)52ULL)) goto mn393;
        gm_trl2khmuphncd t395 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 0) + 8));
        gm_l44 = t395;
        {
            void * t398 = gm_l0;
            gm_tbl8il92l97ut t399 = gm_l1;
            gm_b16_8 t400 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 0));
            gm_t1ec8br9ddkav t401;
            gm_f_conds_30ust77((void *)&t401, (void *)NULL, cx, NULL, t398, t399, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1ec8br9ddkav t402 = t401;
            if (((((uint8_t *)&t402))[0] != 0)) {
                gm_t76123gfet73g t403 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t402) + 8));
                gm_t833u93mr4iob t404;
                GM_ST(uint8_t, (uint8_t *)&t404 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t404 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t404 + 4, 0);
                ((uint8_t *)&t404)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t404 + 8), t403);
                F.ret = t404;
                goto gm_fret;
            }
            uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&t402) + 8));
            if (!t405) goto ie397;
            {
                void * t407 = gm_l0;
                gm_tbl8il92l97ut t408 = gm_l1;
                void * t409 = GM_LD(void *, (((uint8_t *)&gm_l44) + 16));
                gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t407, t408, t409);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix397;
ie397:;
ix397:;
        }
        {
            void * t411 = GM_LD(void *, (((uint8_t *)&gm_l44) + 24));
            void * t412 = t411;
            if (!(GM_LD(void *, ((uint8_t *)&t412)) != 0)) goto ie410;
            void * t413 = GM_LD(void *, ((uint8_t *)&t412));
            gm_l45 = t413;
            {
                void * t415 = gm_l0;
                gm_tbl8il92l97ut t416 = gm_l1;
                void * t417 = gm_l45;
                gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t415, t416, t417);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix410;
ie410:;
ix410:;
        }
        {
            gm_t833u93mr4iob t418;
            GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t418 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t418 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t418 + 24, 0);
            ((uint8_t *)&t418)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t418 + 8), ((uint64_t)0ULL));
            gm_t833u93mr4iob t419 = t418;
            uint8_t t420 = ((((uint8_t *)&t419))[0] != 0);
            F.ret = t418;
            goto gm_fret;
        }
        goto me3;
mn393:;
ma18:;
        uint8_t t422 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t422 != ((uint8_t)51ULL)) goto mn421;
        void * t423 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l46 = t423;
        {
            void * t426 = gm_l46;
            uint8_t t427 = GM_LD(uint8_t, (((uint8_t *)t426) + 24));
            if (!t427) goto ie425;
            {
                void * t429 = gm_l0;
                gm_tbl8il92l97ut t430 = gm_l1;
                void * t431 = gm_l2;
                gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t429, t430, t431);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix425;
ie425:;
ix425:;
        }
        {
            {
                void * t433 = gm_l46;
                gm_b16_8 t434 = GM_LD(gm_b16_8, (((uint8_t *)t433) + 0));
                gm_l47 = t434;
            }
            {
                gm_l48 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t436 = gm_l48;
                    uint64_t t437 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    uint8_t t438 = (t436 < t437);
                    if (!t438) goto lb435;
                    {
                        uint64_t t441 = gm_l48;
                        uint64_t t442 = (uintptr_t)t441;
                        uint64_t t443 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                        gm_t2k9dbsv5ff58 t446 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t442 * 40));
                        F.l49 = t446;
                        {
                            void * t448 = gm_l0;
                            gm_tbl8il92l97ut t449 = gm_l1;
                            gm_tcfa6l7reoosn t450;
                            gm_f_exec_stmt_4jh6tlr((void *)&t450, (void *)NULL, cx, NULL, t448, t449, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l49)));
                            if (gm_unw.kind) {
                                goto gm_ll439;
                            }
                            gm_tcfa6l7reoosn t451 = t450;
                            if (((((uint8_t *)&t451))[0] != 0)) {
                                gm_t76123gfet73g t452 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t451) + 8));
                                gm_t833u93mr4iob t453;
                                GM_ST(uint8_t, (uint8_t *)&t453 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t453 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t453 + 4, 0);
                                ((uint8_t *)&t453)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t453 + 8), t452);
                                F.ret = t453;
                                goto gm_fret;
                            }
                            gm_b0_1 t454 = GM_LD(gm_b0_1, (((uint8_t *)&t451) + 8));
                        }
                        goto me444;
mn445:;
                        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 98, 13);
me444:;
                    }
lc435:;
                    {
                        uint64_t t456 = gm_l48;
                        uint64_t r457; bool o458 = __builtin_add_overflow(t456, ((uint64_t)1ULL), &r457);
                        uint64_t t459 = r457;
                        gm_l48 = t459;
                    }
                }
gm_ll439:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb435; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc435; }
                }
                goto gm_lad;
lb435:;
            }
        }
        {
            void * t461 = gm_l46;
            void * t462 = GM_LD(void *, (((uint8_t *)t461) + 16));
            void * t463 = t462;
            if (!(GM_LD(void *, ((uint8_t *)&t463)) != 0)) goto ie460;
            void * t464 = GM_LD(void *, ((uint8_t *)&t463));
            gm_l50 = t464;
            {
                void * t466 = gm_l0;
                gm_tbl8il92l97ut t467 = gm_l1;
                void * t468 = gm_l50;
                gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t466, t467, t468);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix460;
ie460:;
ix460:;
        }
        {
            gm_t833u93mr4iob t469;
            GM_ST(uint8_t, (uint8_t *)&t469 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t469 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t469 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t469 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t469 + 24, 0);
            ((uint8_t *)&t469)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t469 + 8), ((uint64_t)0ULL));
            gm_t833u93mr4iob t470 = t469;
            uint8_t t471 = ((((uint8_t *)&t470))[0] != 0);
            F.ret = t469;
            goto gm_fret;
        }
        goto me3;
mn421:;
ma19:;
        uint8_t t475 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t475 != ((uint8_t)10ULL)) goto pa474;
        goto po473;
pa474:;
        uint8_t t477 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t477 != ((uint8_t)12ULL)) goto pa476;
        goto po473;
pa476:;
        uint8_t t479 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t479 != ((uint8_t)9ULL)) goto pa478;
        goto po473;
pa478:;
        goto mn472;
po473:;
        {
            void * t482 = gm_l2;
            uint8_t t483 = GM_LD(uint8_t, (((uint8_t *)t482) + 124));
            uint8_t t484 = (uint8_t)!t483;
            if (!t484) goto ie481;
            {
                void * t486 = gm_l0;
                gm_tbl8il92l97ut t487 = gm_l1;
                void * t488 = gm_l2;
                gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t486, t487, t488);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix481;
ie481:;
ix481:;
        }
        {
            void * t489 = gm_l0;
            gm_tbl8il92l97ut t490 = gm_l1;
            void * t491 = gm_l2;
            gm_tl1r7a2fn1ev6 t492;
            gm_f_place_47rfqe5((void *)&t492, (void *)NULL, cx, NULL, t489, t490, t491, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl1r7a2fn1ev6 t493 = t492;
            if (((((uint8_t *)&t493))[0] != 0)) {
                gm_t76123gfet73g t494 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t493) + 8));
                gm_t833u93mr4iob t495;
                GM_ST(uint8_t, (uint8_t *)&t495 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t495 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t495 + 4, 0);
                ((uint8_t *)&t495)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t495 + 8), t494);
                F.ret = t495;
                goto gm_fret;
            }
            gm_tou12vuc19e0s t496 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t493) + 8));
            gm_tou12vuc19e0s t497 = t496;
            uint8_t t504 = GM_LD(uint8_t, (((uint8_t *)&t497) + 0));
            switch (t504) { case ((uint8_t)0ULL): goto ma499; case ((uint8_t)1ULL): goto ma500; case ((uint8_t)2ULL): goto ma501; case ((uint8_t)3ULL): goto ma502; default: goto ma503; }
ma499:;
            uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&t497) + 0));
            if (t506 != ((uint8_t)0ULL)) goto mn505;
            gm_b16_8 t507 = GM_LD(gm_b16_8, (((uint8_t *)&t497) + 8));
            gm_l51 = t507;
            void * t508 = gm_l0;
            gm_b16_8 t509 = gm_l51;
            gm_b16_8 t510 = t509;
            uint64_t t511 = GM_LD(uintptr_t, ((uint8_t *)&t510) + 8);
            void * t512 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t510)));
            uint64_t t513 = (uintptr_t)((uint64_t)0ULL);
            void * t514 = gm_l2;
            uint64_t t515 = GM_LD(uint64_t, (((uint8_t *)t514) + 104));
            uint64_t t516 = gm_f_us_cuv01fp(t515);
            uint64_t t517 = (uintptr_t)t516;
            if (t513 > t517 || t517 > (uintptr_t)t511) gm_trap_slice(t513, t517, t511, gm_src0jbci4ff2esa, 105, 46);
            gm_b16_8 t518;
            GM_ST(void *, (uint8_t *)&t518, (void *)(((uint8_t *)t512 + t513 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t518 + 8, (uintptr_t)(t517 - t513));
            gm_b16_8 t519 = t518;
            gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)&t519));
            uint64_t t521 = gm_f_rd_word_8flcq6o(t508, t520);
            gm_t833u93mr4iob t522;
            GM_ST(uint8_t, (uint8_t *)&t522 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t522 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t522 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t522 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t522 + 24, 0);
            ((uint8_t *)&t522)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t522 + 8), t521);
            gm_t833u93mr4iob t523 = t522;
            uint8_t t524 = ((((uint8_t *)&t523))[0] != 0);
            F.ret = t522;
            goto gm_fret;
            goto me498;
mn505:;
ma500:;
            uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&t497) + 0));
            if (t526 != ((uint8_t)1ULL)) goto mn525;
            uint64_t t527 = GM_LD(uint64_t, (((uint8_t *)&t497) + 8));
            gm_l52 = t527;
            void * t528 = gm_l0;
            void * t529 = gm_l2;
            gm_tl874t2atnlh3 t530 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t529) + 112));
            uint64_t t531 = gm_l52;
            void * t532 = gm_l0;
            void * t533 = gm_l2;
            gm_t88rcucg1b7uk t534 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t533) + 96));
            uint64_t t535 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t532, t534);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t536 = gm_l2;
            uint64_t t537 = GM_LD(uint64_t, (((uint8_t *)t536) + 104));
            gm_f_load_word_ud3iuha((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t528, t530, t531, t535, t537);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me498;
mn525:;
ma501:;
            uint8_t t539 = GM_LD(uint8_t, (((uint8_t *)&t497) + 0));
            if (t539 != ((uint8_t)2ULL)) goto mn538;
            uint64_t t540 = GM_LD(uint64_t, (((uint8_t *)&t497) + 8));
            gm_l53 = t540;
            void * t541 = gm_l0;
            void * t542 = gm_l2;
            gm_tl874t2atnlh3 t543 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t542) + 112));
            uint64_t t544 = gm_l53;
            void * t545 = gm_l2;
            uint64_t t546 = GM_LD(uint64_t, (((uint8_t *)t545) + 104));
            gm_f_load_word_ud3iuha((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t541, t543, t544, ((uint64_t)1ULL), t546);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me498;
mn538:;
ma502:;
            uint8_t t548 = GM_LD(uint8_t, (((uint8_t *)&t497) + 0));
            if (t548 != ((uint8_t)3ULL)) goto mn547;
            void * t549 = gm_l0;
            gm_tbl8il92l97ut t550 = gm_l1;
            void * t551 = gm_l2;
            gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t549, t550, t551);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me498;
mn547:;
ma503:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 104, 13);
me498:;
        }
        goto me3;
mn472:;
ma20:;
        void * t553 = gm_l0;
        gm_tbl8il92l97ut t554 = gm_l1;
        void * t555 = gm_l2;
        gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t553, t554, t555);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn552:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 37, 5);
me3:;
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
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ew_slow_0jkgi8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ew_slow_0jkgi8d F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_t2doi72kmmlil gm_l3; /* b */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_t2doi72kmmlil t2;
        GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 2), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 3), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 4), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 5), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 6), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 7), ((uint8_t)0ULL));
        gm_l3 = t2;
    }
    {
        void * t3 = ((uint8_t *)&gm_l3);
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        void * t5 = gm_l2;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 104));
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        uint64_t t8 = (uintptr_t)t7;
        if (t4 > t8 || t8 > (uintptr_t)8) gm_trap_slice(t4, t8, 8, gm_src0jbci4ff2esa, 119, 10);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t3 + t4 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t4));
        gm_l4 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_tbl8il92l97ut t11 = gm_l1;
        void * t12 = gm_l2;
        gm_b16_8 t13 = gm_l4;
        gm_tcfa6l7reoosn t14;
        gm_f_ev_25jfdar((void *)&t14, (void *)NULL, cx, NULL, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_t76123gfet73g t16 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t15) + 8));
            gm_t833u93mr4iob t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t17 + 8), t16);
            F.ret = t17;
            goto gm_fret;
        }
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 8));
    }
    {
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_l4;
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        uint64_t t23 = gm_f_rd_word_8flcq6o(t19, t22);
        gm_t833u93mr4iob t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
        ((uint8_t *)&t24)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t24 + 8), t23);
        gm_t833u93mr4iob t25 = t24;
        uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
        F.ret = t24;
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
__attribute__((aligned(4))) void gm_f_get_builtin_s6t151o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_get_builtin_s6t151o F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t88rcucg1b7uk gm_l6; /* inner */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    uint64_t gm_l9; /* esz */
    void * gm_l10; /* base */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    uint64_t gm_l12; /* ptr */
    uint64_t gm_l13; /* len */
    gm_b16_8 gm_l14; /* owned */
    uint8_t gm_l15; /* host */
    gm_b16_8 gm_l16; /* b */
    gm_t2lqu8jsj511d gm_l17; /* sp */
    uint64_t gm_l18; /* k */
    uint64_t gm_l19; /* addr */
    uint64_t gm_l20; /* addr */
    gm_b16_8 gm_l21; /* h */
    gm_t88rcucg1b7uk gm_l22; /* pointee */
    uint64_t gm_l23; /* k */
    uint64_t gm_l24; /* i */
    gm_t2doi72kmmlil gm_l25; /* pb */
    uint64_t gm_l26; /* off */
    gm_b16_8 gm_l27; /* v */
    GM_STACK_CHECK(&F, 3677u);
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
            void * t4 = gm_l0;
            void * t5 = gm_l4;
            gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 96));
            gm_t53at80pfdus3 t7;
            gm_f_kind_htv6093((void *)&t7, t4, GM_CONV(gm_csguankko4lmhd, t6));
            gm_t53at80pfdus3 t8 = t7;
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
            if (t9 != ((uint8_t)13ULL)) goto ie3;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 8));
            gm_l6 = t10;
            {
                gm_t88rcucg1b7uk t12 = gm_l6;
                gm_l7 = t12;
            }
            {
                uint8_t t14 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                if (!t14) goto ie13;
                void * t15 = gm_l0;
                gm_t88rcucg1b7uk t16 = gm_l6;
                gm_t53at80pfdus3 t17;
                gm_f_kind_htv6093((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
                gm_t53at80pfdus3 t18 = t17;
                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
                if (t19 != ((uint8_t)8ULL)) goto ie13;
                gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t18) + 8) + 4));
                gm_l8 = t20;
                {
                    gm_t88rcucg1b7uk t22 = gm_l8;
                    gm_l7 = t22;
                }
                goto ix13;
ie13:;
ix13:;
            }
            {
                void * t23 = gm_l0;
                gm_t88rcucg1b7uk t24 = gm_l7;
                uint64_t t25 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t25;
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_src0jbci4ff2esa, 620, 13);
                void * t28 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t26 * 8));
                gm_l10 = t28;
            }
            {
                void * t29 = gm_l0;
                gm_t2lqu8jsj511d t30 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t29));
                gm_l11 = t30;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                gm_b16_8 t31 = gm_f_empty_slice_ijtp9lo();
                gm_l14 = t31;
            }
            {
                gm_l15 = ((uint8_t)0);
            }
            {
                void * t32 = gm_l0;
                void * t33 = gm_l10;
                gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 96));
                gm_t53at80pfdus3 t35;
                gm_f_kind_htv6093((void *)&t35, t32, GM_CONV(gm_csguankko4lmhd, t34));
                gm_t53at80pfdus3 t36 = t35;
                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                switch (t43) { case ((uint8_t)0ULL): goto ma41; case ((uint8_t)1ULL): goto ma41; case ((uint8_t)2ULL): goto ma41; case ((uint8_t)3ULL): goto ma41; case ((uint8_t)4ULL): goto ma41; case ((uint8_t)5ULL): goto ma41; case ((uint8_t)6ULL): goto ma41; case ((uint8_t)7ULL): goto ma41; case ((uint8_t)8ULL): goto ma40; case ((uint8_t)9ULL): goto ma41; case ((uint8_t)10ULL): goto ma38; case ((uint8_t)11ULL): goto ma39; case ((uint8_t)12ULL): goto ma41; case ((uint8_t)13ULL): goto ma41; case ((uint8_t)14ULL): goto ma41; case ((uint8_t)15ULL): goto ma41; case ((uint8_t)16ULL): goto ma41; case ((uint8_t)17ULL): goto ma41; case ((uint8_t)18ULL): goto ma41; case ((uint8_t)19ULL): goto ma41; case ((uint8_t)20ULL): goto ma41; case ((uint8_t)21ULL): goto ma41; case ((uint8_t)22ULL): goto ma41; case ((uint8_t)23ULL): goto ma41; case ((uint8_t)24ULL): goto ma41; case ((uint8_t)25ULL): goto ma41; case ((uint8_t)26ULL): goto ma41; default: goto ma41; }
ma38:;
                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                if (t45 != ((uint8_t)10ULL)) goto mn44;
                {
                    void * t47 = gm_l0;
                    void * t48 = gm_l10;
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 104));
                    gm_b16_8 t50 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t47, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t50;
                }
                {
                    void * t51 = gm_l0;
                    gm_tbl8il92l97ut t52 = gm_l1;
                    void * t53 = gm_l10;
                    gm_b16_8 t54 = gm_l16;
                    gm_tcfa6l7reoosn t55;
                    gm_f_ev_25jfdar((void *)&t55, (void *)NULL, cx, NULL, t51, t52, t53, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t56 = t55;
                    if (((((uint8_t *)&t56))[0] != 0)) {
                        gm_t76123gfet73g t57 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t56) + 8));
                        gm_tcfa6l7reoosn t58;
                        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                        ((uint8_t *)&t58)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t58 + 8), t57);
                        F.ret = t58;
                        goto gm_fret;
                    }
                    gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 8));
                }
                {
                    void * t60 = gm_l0;
                    gm_b16_8 t61 = gm_l16;
                    gm_b16_8 t62 = t61;
                    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                    gm_t2lqu8jsj511d t64 = gm_f_slice_parts_1se5hu4(t60, t63);
                    gm_l17 = t64;
                }
                {
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 0));
                    gm_l12 = t65;
                }
                {
                    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    gm_l13 = t66;
                }
                goto me37;
mn44:;
ma39:;
                uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                if (t68 != ((uint8_t)11ULL)) goto mn67;
                uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)&t36) + 8) + 0));
                gm_l18 = t69;
                {
                    uint64_t t71 = gm_l18;
                    gm_l13 = t71;
                }
                {
                    uint8_t t73 = 0;
                    uint8_t t74 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                    if (!t74) {
                        void * t75 = gm_l10;
                        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)t75) + 124));
                        t73 = t76;
                    } else t73 = 1;
                    if (!t73) goto ie72;
                    {
                        void * t78 = gm_l0;
                        gm_tbl8il92l97ut t79 = gm_l1;
                        void * t80 = gm_l10;
                        uint8_t t81 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                        gm_tl1r7a2fn1ev6 t82;
                        gm_f_place_47rfqe5((void *)&t82, (void *)NULL, cx, NULL, t78, t79, t80, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl1r7a2fn1ev6 t83 = t82;
                        if (((((uint8_t *)&t83))[0] != 0)) {
                            gm_t76123gfet73g t84 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t83) + 8));
                            gm_tcfa6l7reoosn t85;
                            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                            ((uint8_t *)&t85)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t85 + 8), t84);
                            F.ret = t85;
                            goto gm_fret;
                        }
                        gm_tou12vuc19e0s t86 = GM_LD(gm_tou12vuc19e0s, (((uint8_t *)&t83) + 8));
                        gm_tou12vuc19e0s t87 = t86;
                        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&t87) + 0));
                        switch (t93) { case ((uint8_t)0ULL): goto ma90; case ((uint8_t)1ULL): goto ma89; case ((uint8_t)2ULL): goto ma89; case ((uint8_t)3ULL): goto ma91; default: goto ma92; }
ma89:;
                        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t87) + 0));
                        if (t97 != ((uint8_t)1ULL)) goto pa96;
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&t87) + 8));
                        gm_l19 = t98;
                        goto po95;
pa96:;
                        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t87) + 0));
                        if (t100 != ((uint8_t)2ULL)) goto pa99;
                        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&t87) + 8));
                        gm_l19 = t101;
                        goto po95;
pa99:;
                        goto mn94;
po95:;
                        {
                            uint64_t t103 = gm_l19;
                            gm_l12 = t103;
                        }
                        goto me88;
mn94:;
ma90:;
                        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t87) + 0));
                        if (t105 != ((uint8_t)0ULL)) goto mn104;
                        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
                        gm_l21 = t106;
                        {
                            gm_b16_8 t108 = gm_l21;
                            gm_l14 = t108;
                        }
                        {
                            gm_l15 = ((uint8_t)1);
                        }
                        goto me88;
mn104:;
ma91:;
                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t87) + 0));
                        if (t110 != ((uint8_t)3ULL)) goto mn109;
                        void * t111 = gm_l0;
                        void * t112 = gm_l4;
                        gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t112) + 112));
                        gm_b16_8 t114;
                        GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sl4daef1gs04o));
                        GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(36));
                        gm_t76123gfet73g t115;
                        gm_f_trap_qh84l9d((void *)&t115, (void *)NULL, cx, NULL, t111, t113, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t116;
                        GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                        ((uint8_t *)&t116)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t116 + 8), t115);
                        gm_tcfa6l7reoosn t117 = t116;
                        uint8_t t118 = ((((uint8_t *)&t117))[0] != 0);
                        F.ret = t116;
                        goto gm_fret;
                        goto me88;
mn109:;
ma92:;
                        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 637, 17);
me88:;
                    }
                    goto ix72;
ie72:;
                    {
                        void * t120 = gm_l0;
                        void * t121 = gm_l10;
                        uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)t121) + 104));
                        gm_b16_8 t123 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t120, t122);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l14 = t123;
                    }
                    {
                        void * t124 = gm_l0;
                        gm_tbl8il92l97ut t125 = gm_l1;
                        void * t126 = gm_l10;
                        gm_b16_8 t127 = gm_l14;
                        gm_tcfa6l7reoosn t128;
                        gm_f_ev_25jfdar((void *)&t128, (void *)NULL, cx, NULL, t124, t125, t126, t127);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t129 = t128;
                        if (((((uint8_t *)&t129))[0] != 0)) {
                            gm_t76123gfet73g t130 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t129) + 8));
                            gm_tcfa6l7reoosn t131;
                            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                            ((uint8_t *)&t131)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t131 + 8), t130);
                            F.ret = t131;
                            goto gm_fret;
                        }
                        gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 8));
                    }
                    {
                        gm_l15 = ((uint8_t)1);
                    }
ix72:;
                }
                goto me37;
mn67:;
ma40:;
                uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                if (t134 != ((uint8_t)8ULL)) goto mn133;
                gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 4));
                gm_l22 = t135;
                {
                    void * t138 = gm_l0;
                    gm_t88rcucg1b7uk t139 = gm_l22;
                    gm_t53at80pfdus3 t140;
                    gm_f_kind_htv6093((void *)&t140, t138, GM_CONV(gm_csguankko4lmhd, t139));
                    gm_t53at80pfdus3 t141 = t140;
                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t141) + 0));
                    if (t142 != ((uint8_t)11ULL)) goto ie137;
                    uint64_t t143 = GM_LD(uint64_t, ((((uint8_t *)&t141) + 8) + 0));
                    gm_l23 = t143;
                    {
                        void * t145 = gm_l0;
                        gm_tbl8il92l97ut t146 = gm_l1;
                        void * t147 = gm_l10;
                        gm_t833u93mr4iob t148;
                        gm_f_ew_c5kil07((void *)&t148, (void *)NULL, cx, NULL, t145, t146, t147);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t833u93mr4iob t149 = t148;
                        if (((((uint8_t *)&t149))[0] != 0)) {
                            gm_t76123gfet73g t150 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t149) + 8));
                            gm_tcfa6l7reoosn t151;
                            GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                            ((uint8_t *)&t151)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t151 + 8), t150);
                            F.ret = t151;
                            goto gm_fret;
                        }
                        uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&t149) + 8));
                        gm_l12 = t152;
                    }
                    {
                        uint64_t t153 = gm_l23;
                        gm_l13 = t153;
                    }
                    goto ix137;
ie137:;
                    {
                        void * t155 = gm_l0;
                        void * t156 = gm_l4;
                        gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 112));
                        gm_b16_8 t158;
                        GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sob69bvkm3aek));
                        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(32));
                        gm_t76123gfet73g t159;
                        gm_f_trap_qh84l9d((void *)&t159, (void *)NULL, cx, NULL, t155, t157, t158);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t160;
                        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                        ((uint8_t *)&t160)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t160 + 8), t159);
                        gm_tcfa6l7reoosn t161 = t160;
                        uint8_t t162 = ((((uint8_t *)&t161))[0] != 0);
                        F.ret = t160;
                        goto gm_fret;
                    }
ix137:;
                }
                goto me37;
mn133:;
ma41:;
                void * t164 = gm_l0;
                void * t165 = gm_l4;
                gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t165) + 112));
                gm_b16_8 t167;
                GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sqnuqut1dl3fi));
                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(47));
                gm_t76123gfet73g t168;
                gm_f_trap_qh84l9d((void *)&t168, (void *)NULL, cx, NULL, t164, t166, t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t169;
                GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                ((uint8_t *)&t169)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t169 + 8), t168);
                gm_tcfa6l7reoosn t170 = t169;
                uint8_t t171 = ((((uint8_t *)&t170))[0] != 0);
                F.ret = t169;
                goto gm_fret;
                goto me37;
mn163:;
ma42:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 626, 5);
me37:;
            }
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t177) { case ((uint8_t)0ULL): goto ma175; case ((uint8_t)1ULL): goto ma175; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; case ((uint8_t)7ULL): goto ma175; case ((uint8_t)8ULL): goto ma175; case ((uint8_t)9ULL): goto ma175; case ((uint8_t)10ULL): goto ma175; case ((uint8_t)11ULL): goto ma175; case ((uint8_t)12ULL): goto ma175; case ((uint8_t)13ULL): goto ma175; case ((uint8_t)14ULL): goto ma175; case ((uint8_t)15ULL): goto ma175; case ((uint8_t)16ULL): goto ma175; case ((uint8_t)17ULL): goto ma175; case ((uint8_t)18ULL): goto ma175; case ((uint8_t)19ULL): goto ma175; case ((uint8_t)20ULL): goto ma175; case ((uint8_t)21ULL): goto ma175; case ((uint8_t)22ULL): goto ma175; case ((uint8_t)23ULL): goto ma175; case ((uint8_t)24ULL): goto ma175; case ((uint8_t)25ULL): goto ma175; case ((uint8_t)26ULL): goto ma175; case ((uint8_t)27ULL): goto ma175; case ((uint8_t)28ULL): goto ma175; case ((uint8_t)29ULL): goto ma175; case ((uint8_t)30ULL): goto ma175; case ((uint8_t)31ULL): goto ma175; case ((uint8_t)32ULL): goto ma175; case ((uint8_t)33ULL): goto ma175; case ((uint8_t)34ULL): goto ma175; case ((uint8_t)35ULL): goto ma175; case ((uint8_t)36ULL): goto ma175; case ((uint8_t)37ULL): goto ma175; case ((uint8_t)38ULL): goto ma173; case ((uint8_t)39ULL): goto ma174; case ((uint8_t)40ULL): goto ma175; case ((uint8_t)41ULL): goto ma175; case ((uint8_t)42ULL): goto ma175; case ((uint8_t)43ULL): goto ma175; case ((uint8_t)44ULL): goto ma175; case ((uint8_t)45ULL): goto ma175; case ((uint8_t)46ULL): goto ma175; case ((uint8_t)47ULL): goto ma175; case ((uint8_t)48ULL): goto ma175; case ((uint8_t)49ULL): goto ma175; case ((uint8_t)50ULL): goto ma175; case ((uint8_t)51ULL): goto ma175; case ((uint8_t)52ULL): goto ma175; case ((uint8_t)53ULL): goto ma175; case ((uint8_t)54ULL): goto ma175; case ((uint8_t)55ULL): goto ma175; case ((uint8_t)56ULL): goto ma175; case ((uint8_t)57ULL): goto ma175; case ((uint8_t)58ULL): goto ma175; case ((uint8_t)59ULL): goto ma175; case ((uint8_t)60ULL): goto ma175; case ((uint8_t)61ULL): goto ma175; case ((uint8_t)62ULL): goto ma175; case ((uint8_t)63ULL): goto ma175; case ((uint8_t)64ULL): goto ma175; case ((uint8_t)65ULL): goto ma175; case ((uint8_t)66ULL): goto ma175; case ((uint8_t)67ULL): goto ma175; case ((uint8_t)68ULL): goto ma175; case ((uint8_t)69ULL): goto ma175; case ((uint8_t)70ULL): goto ma175; case ((uint8_t)71ULL): goto ma175; case ((uint8_t)72ULL): goto ma175; case ((uint8_t)73ULL): goto ma175; case ((uint8_t)74ULL): goto ma175; case ((uint8_t)75ULL): goto ma175; case ((uint8_t)76ULL): goto ma175; case ((uint8_t)77ULL): goto ma175; case ((uint8_t)78ULL): goto ma175; case ((uint8_t)79ULL): goto ma175; case ((uint8_t)80ULL): goto ma175; case ((uint8_t)81ULL): goto ma175; default: goto ma175; }
ma173:;
                uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t179 != ((uint8_t)38ULL)) goto mn178;
                goto me172;
mn178:;
ma174:;
                uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t182 != ((uint8_t)39ULL)) goto mn181;
                {
                    uint64_t t184 = gm_l13;
                    uint64_t r185; bool o186 = __builtin_sub_overflow(t184, ((uint64_t)1ULL), &r185);
                    uint64_t t187 = r185;
                    gm_l24 = t187;
                }
                goto me172;
mn181:;
ma175:;
                {
                    void * t190 = gm_l0;
                    gm_tbl8il92l97ut t191 = gm_l1;
                    uint64_t t192 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_src0jbci4ff2esa, 662, 38);
                    void * t194 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t192 * 8));
                    gm_t833u93mr4iob t195;
                    gm_f_ev_index_bie377m((void *)&t195, (void *)NULL, cx, NULL, t190, t191, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t833u93mr4iob t196 = t195;
                    if (((((uint8_t *)&t196))[0] != 0)) {
                        gm_t76123gfet73g t197 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t196) + 8));
                        gm_tcfa6l7reoosn t198;
                        GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                        ((uint8_t *)&t198)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t198 + 8), t197);
                        F.ret = t198;
                        goto gm_fret;
                    }
                    uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&t196) + 8));
                    gm_l24 = t199;
                }
                goto me172;
mn188:;
ma176:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 659, 5);
me172:;
            }
            {
                uint64_t t201 = gm_l24;
                uint64_t t202 = gm_l13;
                uint8_t t203 = (t201 >= t202);
                if (!t203) goto ie200;
                {
                    gm_b16_8 t205 = gm_l5;
                    gm_f_fill_abmf5g5(t205, ((uint8_t)0ULL));
                }
                {
                    void * t206 = gm_l0;
                    gm_t2lqu8jsj511d t207 = gm_l11;
                    gm_f_release_jn5kqdi(t206, GM_CONV(gm_csc7lc6jlkqkuq, t207));
                }
                {
                    gm_b0_1 t208;
                    gm_tcfa6l7reoosn t209;
                    GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t209 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t209 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t209 + 24, 0);
                    ((uint8_t *)&t209)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t209 + 8), t208);
                    gm_tcfa6l7reoosn t210 = t209;
                    uint8_t t211 = ((((uint8_t *)&t210))[0] != 0);
                    F.ret = t209;
                    goto gm_fret;
                }
                goto ix200;
ie200:;
ix200:;
            }
            {
                uint8_t t213 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                if (!t213) goto ie212;
                {
                    gm_t2doi72kmmlil t215;
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 1), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 2), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 3), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 4), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 5), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 6), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t215 + 7), ((uint8_t)0ULL));
                    gm_l25 = t215;
                }
                {
                    void * t216 = gm_l0;
                    void * t217 = ((uint8_t *)&gm_l25);
                    if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_src0jbci4ff2esa, 671, 19);
                    gm_b16_8 t218;
                    GM_ST(void *, (uint8_t *)&t218, (void *)(((uint8_t *)t217 + (uintptr_t)0 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                    uint64_t t219 = gm_l12;
                    uint64_t t220 = gm_l24;
                    uint64_t t221 = gm_l9;
                    uint64_t r222; bool o223 = __builtin_mul_overflow(t220, t221, &r222);
                    if (o223) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 671, 34);
                    uint64_t t224 = r222;
                    uint64_t r225; bool o226 = __builtin_add_overflow(t219, t224, &r225);
                    uint64_t t227 = r225;
                    gm_f_put_ptr_188p3mm(t216, t218, t227);
                }
                {
                    void * t228 = gm_l0;
                    void * t229 = gm_l4;
                    gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t229) + 96));
                    gm_b16_8 t231 = gm_l5;
                    void * t232 = ((uint8_t *)&gm_l25);
                    uint64_t t233 = (uintptr_t)((uint64_t)0ULL);
                    void * t234 = gm_l0;
                    uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)t234) + 176));
                    uint64_t t236 = gm_f_us_cuv01fp(t235);
                    uint64_t t237 = (uintptr_t)t236;
                    if (t233 > t237 || t237 > (uintptr_t)8) gm_trap_slice(t233, t237, 8, gm_src0jbci4ff2esa, 672, 32);
                    gm_b16_8 t238;
                    GM_ST(void *, (uint8_t *)&t238, (void *)(((uint8_t *)t232 + t233 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(t237 - t233));
                    gm_b16_8 t239 = t238;
                    gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                    gm_b0_1 t241 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t228, t230, t231, t240);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix212;
ie212:;
                uint8_t t243 = gm_l15;
                if (!t243) goto ie242;
                {
                    uint64_t t245 = gm_l24;
                    uint64_t t246 = gm_l9;
                    uint64_t r247; bool o248 = __builtin_mul_overflow(t245, t246, &r247);
                    if (o248) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 674, 19);
                    uint64_t t249 = r247;
                    uint64_t t250 = gm_f_us_cuv01fp(t249);
                    gm_l26 = t250;
                }
                {
                    void * t251 = gm_l0;
                    void * t252 = gm_l4;
                    gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t252) + 96));
                    gm_b16_8 t254 = gm_l5;
                    gm_b16_8 t255 = gm_l14;
                    gm_b16_8 t256 = t255;
                    uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&t256) + 8);
                    void * t258 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t256)));
                    uint64_t t259 = gm_l26;
                    uint64_t t260 = (uintptr_t)t259;
                    uint64_t t261 = gm_l26;
                    uint64_t t262 = gm_l9;
                    uint64_t t263 = gm_f_us_cuv01fp(t262);
                    uint64_t r264; bool o265 = __builtin_add_overflow(t261, t263, &r264);
                    if (o265) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 675, 43);
                    uint64_t t266 = r264;
                    uint64_t t267 = (uintptr_t)t266;
                    if (t260 > t267 || t267 > (uintptr_t)t257) gm_trap_slice(t260, t267, t257, gm_src0jbci4ff2esa, 675, 32);
                    gm_b16_8 t268;
                    GM_ST(void *, (uint8_t *)&t268, (void *)(((uint8_t *)t258 + t260 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(t267 - t260));
                    gm_b16_8 t269 = t268;
                    gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                    gm_b0_1 t271 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t251, t253, t254, t270);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix242;
ie242:;
                {
                    void * t273 = gm_l0;
                    uint64_t t274 = gm_l9;
                    gm_b16_8 t275 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t273, t274);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t275;
                }
                {
                    void * t276 = gm_l0;
                    void * t277 = gm_l4;
                    gm_tl874t2atnlh3 t278 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t277) + 112));
                    uint64_t t279 = gm_l12;
                    uint64_t t280 = gm_l24;
                    uint64_t t281 = gm_l9;
                    uint64_t r282; bool o283 = __builtin_mul_overflow(t280, t281, &r282);
                    if (o283) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 678, 38);
                    uint64_t t284 = r282;
                    uint64_t r285; bool o286 = __builtin_add_overflow(t279, t284, &r285);
                    uint64_t t287 = r285;
                    void * t288 = gm_l0;
                    gm_t88rcucg1b7uk t289 = gm_l7;
                    uint64_t t290 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t288, t289);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t291 = gm_l27;
                    gm_tcfa6l7reoosn t292;
                    gm_f_load_at_6u81jcc((void *)&t292, (void *)NULL, cx, NULL, t276, t278, t287, t290, t291);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t293 = t292;
                    if (((((uint8_t *)&t293))[0] != 0)) {
                        gm_t76123gfet73g t294 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t293) + 8));
                        gm_tcfa6l7reoosn t295;
                        GM_ST(uint8_t, (uint8_t *)&t295 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t295 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t295 + 4, 0);
                        ((uint8_t *)&t295)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t295 + 8), t294);
                        F.ret = t295;
                        goto gm_fret;
                    }
                    gm_b0_1 t296 = GM_LD(gm_b0_1, (((uint8_t *)&t293) + 8));
                }
                {
                    void * t297 = gm_l0;
                    void * t298 = gm_l4;
                    gm_t88rcucg1b7uk t299 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t298) + 96));
                    gm_b16_8 t300 = gm_l5;
                    gm_b16_8 t301 = gm_l27;
                    gm_b16_8 t302 = t301;
                    gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                    gm_b0_1 t304 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t297, t299, t300, t303);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix242:;
ix212:;
            }
            {
                void * t305 = gm_l0;
                gm_t2lqu8jsj511d t306 = gm_l11;
                gm_f_release_jn5kqdi(t305, GM_CONV(gm_csc7lc6jlkqkuq, t306));
            }
            goto ix3;
ie3:;
            {
                void * t308 = gm_l0;
                void * t309 = gm_l4;
                gm_tl874t2atnlh3 t310 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t309) + 112));
                gm_b16_8 t311;
                GM_ST(void *, (uint8_t *)&t311, (void *)(gm_ssgce158bfb9m));
                GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(54));
                gm_t76123gfet73g t312;
                gm_f_trap_qh84l9d((void *)&t312, (void *)NULL, cx, NULL, t308, t310, t311);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t313;
                GM_ST(uint8_t, (uint8_t *)&t313 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t313 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t313 + 4, 0);
                ((uint8_t *)&t313)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t313 + 8), t312);
                gm_tcfa6l7reoosn t314 = t313;
                uint8_t t315 = ((((uint8_t *)&t314))[0] != 0);
                F.ret = t313;
                goto gm_fret;
            }
ix3:;
        }
    }
    gm_b0_1 t316;
    gm_tcfa6l7reoosn t317;
    GM_ST(uint8_t, (uint8_t *)&t317 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t317 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t317 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t317 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t317 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t317 + 24, 0);
    ((uint8_t *)&t317)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t317 + 8), t316);
    gm_tcfa6l7reoosn t318 = t317;
    F.ret = t318;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_place_expr_qlfi6ot(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_place_expr_qlfi6ot F;
    void * gm_l0; /* m */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    GM_STACK_CHECK(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma4; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma4; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t13 != ((uint8_t)15ULL)) goto pa12;
    goto po11;
pa12:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)16ULL)) goto pa14;
    goto po11;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t17 != ((uint8_t)17ULL)) goto pa16;
    goto po11;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t19 != ((uint8_t)18ULL)) goto pa18;
    goto po11;
pa18:;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t21 != ((uint8_t)14ULL)) goto pa20;
    goto po11;
pa20:;
    goto mn10;
po11:;
    t2 = ((uint8_t)1);
    goto me3;
mn10:;
ma5:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)19ULL)) goto mn22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l2 = t24;
    void * t25 = gm_l0;
    void * t26 = gm_l2;
    uint8_t t27 = gm_f_is_place_expr_qlfi6ot(t25, t26);
    t2 = t27;
    goto me3;
mn22:;
ma6:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t29 != ((uint8_t)20ULL)) goto mn28;
    void * t30 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l3 = t30;
    uint8_t t31;
    void * t33 = gm_l0;
    void * t34 = gm_l3;
    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
    gm_t53at80pfdus3 t36;
    gm_f_kind_htv6093((void *)&t36, t33, GM_CONV(gm_csguankko4lmhd, t35));
    gm_t53at80pfdus3 t37 = t36;
    uint8_t t38;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t37) + 0));
    if (t41 != ((uint8_t)11ULL)) goto mn40;
    t38 = ((uint8_t)1);
    goto me39;
mn40:;
    t38 = ((uint8_t)0);
    goto me39;
mn42:;
    gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 406, 30);
me39:;
    if (!t38) goto ie32;
    void * t44 = gm_l0;
    void * t45 = gm_l3;
    uint8_t t46 = gm_f_is_place_expr_qlfi6ot(t44, t45);
    uint8_t t47 = t46;
    t31 = t47;
    goto ix32;
ie32:;
    uint8_t t49 = ((uint8_t)1);
    t31 = t49;
ix32:;
    t2 = t31;
    goto me3;
mn28:;
ma7:;
    t2 = ((uint8_t)0);
    goto me3;
mn50:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 403, 67);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_read_string_ttuk4gr F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* data */
    gm_b16_8 gm_l6; /* out */
    GM_STACK_CHECK(&F, 772u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
            gm_td0ept12e7q8b t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
            gm_td0ept12e7q8b t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 184);
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_t934k1j7u70ct t17;
        gm_f_read_u6r4vns((void *)&t17, t14, t15, t16, ((uint64_t)1ULL));
        gm_t934k1j7u70ct t18 = t17;
        gm_b16_8 t19;
        if (!((((uint8_t *)&t18))[0] != 0)) {
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            t19 = t20;
        } else {
            gm_tpekhfs32op0f t21 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t18) + 8));
            F.l4 = t21;
            void * t22 = gm_l0;
            gm_tl874t2atnlh3 t23 = gm_l1;
            gm_t76123gfet73g t24;
            gm_f_mem_trap_ecogko0((void *)&t24, (void *)NULL, cx, NULL, t22, t23, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t25 + 8), t24);
            gm_td0ept12e7q8b t26 = t25;
            uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
            F.ret = t25;
            goto gm_fret;
        }
        gm_l5 = t19;
    }
    {
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        gm_tabb710d9mq2e t29;
        gm_f_make_2ip78bj((void *)&t29, (void *)NULL, cx, NULL, t28, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knna6g3th5t)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src0jbci4ff2esa, 689, 12);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
        gm_l6 = t31;
    }
    {
        gm_b16_8 t32 = gm_l6;
        gm_b16_8 t33 = gm_l5;
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_f_copy_from_f7j6p05(t32, t35);
    }
    {
        gm_b16_8 t36 = gm_l6;
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_td0ept12e7q8b t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
        ((uint8_t *)&t39)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
        gm_td0ept12e7q8b t40 = t39;
        uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
        F.ret = t39;
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
__attribute__((aligned(4))) void gm_f_try__q6sv04f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_try__q6sv04f F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* inner */
    gm_t9805qtonbsag gm_l3; /* lift */
    uint32_t gm_l4; /* levels */
    void * gm_l5; /* n */
    gm_b16_8 gm_l6; /* dst */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* b */
    void * gm_l9; /* target */
    gm_b16_8 gm_l10; /* ev */
    gm_t88rcucg1b7uk gm_l11; /* ret_ty */
    gm_t88rcucg1b7uk gm_l12; /* rerr */
    uint32_t gm_l13; /* v */
    gm_b16_8 gm_l14; /* lifted */
    GM_STACK_CHECK(&F, 2024u);
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
        void * t2 = gm_l0;
        gm_t2lqu8jsj511d t3 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t2));
        gm_l7 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 104));
        gm_b16_8 t7 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_tbl8il92l97ut t9 = gm_l1;
        void * t10 = gm_l2;
        gm_b16_8 t11 = gm_l8;
        gm_tcfa6l7reoosn t12;
        gm_f_ev_25jfdar((void *)&t12, (void *)NULL, cx, NULL, t8, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_t76123gfet73g t14 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t13) + 8));
            gm_tcfa6l7reoosn t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
    }
    {
        void * t17 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        uint32_t t18 = gm_l4;
        void * t19 = gm_f_frame_up_ghkkuqg(t17, t18);
        void * t20 = t19;
        void * t21;
        if ((GM_LD(void *, ((uint8_t *)&t20)) != 0)) {
            void * t22 = GM_LD(void *, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            void * t23 = gm_l0;
            void * t24 = gm_l5;
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 112));
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sl9b2am1i9pmg));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(51));
            gm_t76123gfet73g t27;
            gm_f_trap_qh84l9d((void *)&t27, (void *)NULL, cx, NULL, t23, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            ((uint8_t *)&t28)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), t27);
            gm_tcfa6l7reoosn t29 = t28;
            uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
            F.ret = t28;
            goto gm_fret;
        }
        gm_l9 = t21;
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l2;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 96));
        gm_t53at80pfdus3 t34;
        gm_f_kind_htv6093((void *)&t34, t31, GM_CONV(gm_csguankko4lmhd, t33));
        gm_t53at80pfdus3 t35 = t34;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        switch (t41) { case ((uint8_t)0ULL): goto ma39; case ((uint8_t)1ULL): goto ma39; case ((uint8_t)2ULL): goto ma39; case ((uint8_t)3ULL): goto ma39; case ((uint8_t)4ULL): goto ma39; case ((uint8_t)5ULL): goto ma39; case ((uint8_t)6ULL): goto ma39; case ((uint8_t)7ULL): goto ma39; case ((uint8_t)8ULL): goto ma39; case ((uint8_t)9ULL): goto ma39; case ((uint8_t)10ULL): goto ma39; case ((uint8_t)11ULL): goto ma39; case ((uint8_t)12ULL): goto ma39; case ((uint8_t)13ULL): goto ma37; case ((uint8_t)14ULL): goto ma38; case ((uint8_t)15ULL): goto ma39; case ((uint8_t)16ULL): goto ma39; case ((uint8_t)17ULL): goto ma39; case ((uint8_t)18ULL): goto ma39; case ((uint8_t)19ULL): goto ma39; case ((uint8_t)20ULL): goto ma39; case ((uint8_t)21ULL): goto ma39; case ((uint8_t)22ULL): goto ma39; case ((uint8_t)23ULL): goto ma39; case ((uint8_t)24ULL): goto ma39; case ((uint8_t)25ULL): goto ma39; case ((uint8_t)26ULL): goto ma39; default: goto ma39; }
ma37:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t43 != ((uint8_t)13ULL)) goto mn42;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l2;
            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 96));
            gm_b16_8 t49 = gm_l8;
            gm_b16_8 t50 = t49;
            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
            uint8_t t52 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t46, t48, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t52) goto ie45;
            {
                gm_b16_8 t54 = gm_l6;
                void * t55 = gm_l0;
                void * t56 = gm_l2;
                gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 96));
                gm_b16_8 t58 = gm_l8;
                gm_b16_8 t59 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t55, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t60 = t59;
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_f_copy_from_f7j6p05(t54, t61);
            }
            {
                void * t62 = gm_l0;
                gm_t2lqu8jsj511d t63 = gm_l7;
                gm_f_release_jn5kqdi(t62, GM_CONV(gm_csc7lc6jlkqkuq, t63));
            }
            {
                gm_b0_1 t64;
                gm_tcfa6l7reoosn t65;
                GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t65 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t65 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t65 + 24, 0);
                ((uint8_t *)&t65)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t65 + 8), t64);
                gm_tcfa6l7reoosn t66 = t65;
                uint8_t t67 = ((((uint8_t *)&t66))[0] != 0);
                F.ret = t65;
                goto gm_fret;
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            void * t68 = gm_l9;
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 24));
            gm_f_fill_abmf5g5(t69, ((uint8_t)0ULL));
        }
        {
            void * t70 = gm_l9;
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 0));
            gm_tf5srh4mp4b3i t72;
            GM_ST(uint8_t, (uint8_t *)&t72 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t72 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t72 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t72 + 0), t71);
            GM_ST(uint8_t, ((uint8_t *)&t72 + 8), ((uint8_t)0));
            gm_t76123gfet73g t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t73 + 8), t72);
            gm_tcfa6l7reoosn t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            ((uint8_t *)&t74)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t74 + 8), t73);
            gm_tcfa6l7reoosn t75 = t74;
            uint8_t t76 = ((((uint8_t *)&t75))[0] != 0);
            F.ret = t74;
            goto gm_fret;
        }
        goto me36;
mn42:;
ma38:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t78 != ((uint8_t)14ULL)) goto mn77;
        {
            void * t81 = gm_l0;
            void * t82 = gm_l2;
            gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t82) + 96));
            gm_b16_8 t84 = gm_l8;
            gm_b16_8 t85 = t84;
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
            uint8_t t87 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t81, t83, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t88 = (uint8_t)!t87;
            if (!t88) goto ie80;
            {
                gm_b16_8 t90 = gm_l6;
                void * t91 = gm_l0;
                void * t92 = gm_l2;
                gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 96));
                gm_b16_8 t94 = gm_l8;
                gm_b16_8 t95 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t91, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t96 = t95;
                gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                gm_f_copy_from_f7j6p05(t90, t97);
            }
            {
                void * t98 = gm_l0;
                gm_t2lqu8jsj511d t99 = gm_l7;
                gm_f_release_jn5kqdi(t98, GM_CONV(gm_csc7lc6jlkqkuq, t99));
            }
            {
                gm_b0_1 t100;
                gm_tcfa6l7reoosn t101;
                GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t101 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t101 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t101 + 24, 0);
                ((uint8_t *)&t101)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t101 + 8), t100);
                gm_tcfa6l7reoosn t102 = t101;
                uint8_t t103 = ((((uint8_t *)&t102))[0] != 0);
                F.ret = t101;
                goto gm_fret;
            }
            goto ix80;
ie80:;
ix80:;
        }
        {
            void * t104 = gm_l0;
            void * t105 = gm_l2;
            gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t105) + 96));
            gm_b16_8 t107 = gm_l8;
            gm_b16_8 t108 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t104, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t108;
        }
        {
            void * t109 = gm_l0;
            void * t110 = gm_l9;
            uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)t110) + 12));
            void * t112 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t109, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t112) + 64));
            gm_l11 = t113;
        }
        {
            void * t114 = gm_l0;
            gm_t88rcucg1b7uk t115 = gm_l11;
            gm_tgglga8ro3isc t116 = gm_f_res_types_pg7rvck(t114, GM_CONV(gm_csguankko4lmhd, t115));
            gm_tgglga8ro3isc t117 = t116;
            gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t117) + 0));
            gm_l12 = t118;
        }
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie119;
            uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            gm_l13 = t120;
            {
                void * t122 = gm_l0;
                void * t123 = gm_l0;
                gm_t88rcucg1b7uk t124 = gm_l12;
                uint64_t t125 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t123, t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t126 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t122, t125);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t126;
            }
            {
                void * t127 = gm_l0;
                gm_t88rcucg1b7uk t128 = gm_l12;
                gm_b16_8 t129 = gm_l14;
                uint32_t t130 = gm_l13;
                gm_b16_8 t131 = gm_l10;
                gm_b16_8 t132 = t131;
                gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                gm_b0_1 t134 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t127, t128, t129, t130, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t135 = gm_l0;
                gm_t88rcucg1b7uk t136 = gm_l11;
                void * t137 = gm_l9;
                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)t137) + 24));
                gm_b16_8 t139 = gm_l14;
                gm_b16_8 t140 = t139;
                gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                gm_b0_1 t142 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t135, t136, t138, ((uint8_t)1), t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix119;
ie119:;
            {
                void * t144 = gm_l0;
                gm_t88rcucg1b7uk t145 = gm_l11;
                void * t146 = gm_l9;
                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)t146) + 24));
                gm_b16_8 t148 = gm_l10;
                gm_b16_8 t149 = t148;
                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                gm_b0_1 t151 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t144, t145, t147, ((uint8_t)1), t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix119:;
        }
        {
            void * t152 = gm_l9;
            uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)t152) + 0));
            gm_tf5srh4mp4b3i t154;
            GM_ST(uint8_t, (uint8_t *)&t154 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t154 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t154 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t154 + 0), t153);
            GM_ST(uint8_t, ((uint8_t *)&t154 + 8), ((uint8_t)1));
            gm_t76123gfet73g t155;
            GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t155 + 8), t154);
            gm_tcfa6l7reoosn t156;
            GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
            ((uint8_t *)&t156)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t156 + 8), t155);
            gm_tcfa6l7reoosn t157 = t156;
            uint8_t t158 = ((((uint8_t *)&t157))[0] != 0);
            F.ret = t156;
            goto gm_fret;
        }
        goto me36;
mn77:;
ma39:;
        void * t160 = gm_l0;
        void * t161 = gm_l5;
        gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t161) + 112));
        gm_b16_8 t163;
        GM_ST(void *, (uint8_t *)&t163, (void *)(gm_s0lfbgij46fm3));
        GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(73));
        gm_t76123gfet73g t164;
        gm_f_trap_qh84l9d((void *)&t164, (void *)NULL, cx, NULL, t160, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t165;
        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
        ((uint8_t *)&t165)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t165 + 8), t164);
        gm_tcfa6l7reoosn t166 = t165;
        uint8_t t167 = ((((uint8_t *)&t166))[0] != 0);
        F.ret = t165;
        goto gm_fret;
        goto me36;
mn159:;
ma40:;
        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 489, 5);
me36:;
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
__attribute__((aligned(4))) void gm_f_unwrap_f7c53ka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_unwrap_f7c53ka F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* x */
    uint8_t gm_l3; /* is_result */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 1511u);
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
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l6 = t4;
        }
        {
            void * t5 = gm_l0;
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            gm_b16_8 t8 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t8;
        }
        {
            void * t9 = gm_l0;
            gm_tbl8il92l97ut t10 = gm_l1;
            void * t11 = gm_l2;
            gm_b16_8 t12 = gm_l7;
            gm_tcfa6l7reoosn t13;
            gm_f_ev_25jfdar((void *)&t13, (void *)NULL, cx, NULL, t9, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t14 = t13;
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
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
        }
        {
            uint8_t t19 = gm_l3;
            if (!t19) goto ie18;
            {
                void * t22 = gm_l0;
                void * t23 = gm_l2;
                gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 96));
                gm_b16_8 t25 = gm_l7;
                gm_b16_8 t26 = t25;
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                uint8_t t28 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t22, t24, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t28) goto ie21;
                {
                    void * t31 = gm_l0;
                    uint8_t t32 = gm_f_fast_23em7sv(t31);
                    if (!t32) goto ie30;
                    {
                        gm_b16_8 t34 = gm_l5;
                        gm_f_fill_abmf5g5(t34, ((uint8_t)0ULL));
                    }
                    {
                        void * t35 = gm_l0;
                        gm_t2lqu8jsj511d t36 = gm_l6;
                        gm_f_release_jn5kqdi(t35, GM_CONV(gm_csc7lc6jlkqkuq, t36));
                    }
                    {
                        gm_b0_1 t37;
                        gm_tcfa6l7reoosn t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t38 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t38 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t38 + 24, 0);
                        ((uint8_t *)&t38)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t38 + 8), t37);
                        gm_tcfa6l7reoosn t39 = t38;
                        uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
                        F.ret = t38;
                        goto gm_fret;
                    }
                    goto ix30;
ie30:;
ix30:;
                }
                {
                    gm_f_list_uvfu1k1((void *)&F.l8, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t41 = gm_l0;
                    void * t42 = (void *)((uint8_t *)&F.l8);
                    void * t43 = gm_l0;
                    void * t44 = gm_l2;
                    gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 96));
                    gm_tgglga8ro3isc t46 = gm_f_res_types_pg7rvck(t43, GM_CONV(gm_csguankko4lmhd, t45));
                    gm_tgglga8ro3isc t47 = t46;
                    gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t47) + 0));
                    void * t49 = gm_l0;
                    void * t50 = gm_l2;
                    gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + 96));
                    gm_b16_8 t52 = gm_l7;
                    gm_b16_8 t53 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t49, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl572d7579hns t54;
                    gm_f_format_value_vmle7qr((void *)&t54, (void *)NULL, cx, NULL, t41, t42, t48, t53, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl572d7579hns t55 = t54;
                    gm_b0_1 t56;
                    if (!((((uint8_t *)&t55))[0] != 0)) {
                        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 8));
                        t56 = t57;
                    } else {
                        t56 = GM_ZERO(gm_b0_1);
                    }
                }
                {
                    void * t59 = gm_l0;
                    void * t60 = gm_l4;
                    gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 112));
                    gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                    gm_tnpifv20irnph t63;
                    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                    gm_tabb710d9mq2e t64;
                    gm_f_format_r6pm0oj((void *)&t64, (void *)NULL, cx, NULL, t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t65 = t64;
                    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_src0jbci4ff2esa, 427, 36);
                    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                    gm_b16_8 t67 = t66;
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                    gm_t76123gfet73g t69;
                    gm_f_trap_qh84l9d((void *)&t69, (void *)NULL, cx, NULL, t59, t61, t68);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t70;
                    GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                    ((uint8_t *)&t70)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t70 + 8), t69);
                    gm_tcfa6l7reoosn t71 = t70;
                    uint8_t t72 = ((((uint8_t *)&t71))[0] != 0);
                    F.ret = t70;
                    goto gm_fret;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                gm_b16_8 t73 = gm_l5;
                void * t74 = gm_l0;
                void * t75 = gm_l2;
                gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t75) + 96));
                gm_b16_8 t77 = gm_l7;
                gm_b16_8 t78 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t74, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t79 = t78;
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                gm_f_copy_from_f7j6p05(t73, t80);
            }
            goto ix18;
ie18:;
            {
                void * t83 = gm_l0;
                void * t84 = gm_l2;
                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t84) + 96));
                gm_b16_8 t86 = gm_l7;
                gm_b16_8 t87 = t86;
                gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                uint8_t t89 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t83, t85, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t90 = (uint8_t)!t89;
                if (!t90) goto ie82;
                {
                    void * t93 = gm_l0;
                    uint8_t t94 = gm_f_fast_23em7sv(t93);
                    if (!t94) goto ie92;
                    {
                        gm_b16_8 t96 = gm_l5;
                        gm_f_fill_abmf5g5(t96, ((uint8_t)0ULL));
                    }
                    {
                        void * t97 = gm_l0;
                        gm_t2lqu8jsj511d t98 = gm_l6;
                        gm_f_release_jn5kqdi(t97, GM_CONV(gm_csc7lc6jlkqkuq, t98));
                    }
                    {
                        gm_b0_1 t99;
                        gm_tcfa6l7reoosn t100;
                        GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t100 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t100 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t100 + 24, 0);
                        ((uint8_t *)&t100)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t100 + 8), t99);
                        gm_tcfa6l7reoosn t101 = t100;
                        uint8_t t102 = ((((uint8_t *)&t101))[0] != 0);
                        F.ret = t100;
                        goto gm_fret;
                    }
                    goto ix92;
ie92:;
ix92:;
                }
                {
                    void * t103 = gm_l0;
                    void * t104 = gm_l4;
                    gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 112));
                    gm_b16_8 t106;
                    GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sd5r0412dj6fb));
                    GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(14));
                    gm_t76123gfet73g t107;
                    gm_f_trap_qh84l9d((void *)&t107, (void *)NULL, cx, NULL, t103, t105, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t108;
                    GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                    ((uint8_t *)&t108)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t108 + 8), t107);
                    gm_tcfa6l7reoosn t109 = t108;
                    uint8_t t110 = ((((uint8_t *)&t109))[0] != 0);
                    F.ret = t108;
                    goto gm_fret;
                }
                goto ix82;
ie82:;
ix82:;
            }
            {
                gm_b16_8 t111 = gm_l5;
                void * t112 = gm_l0;
                void * t113 = gm_l2;
                gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t113) + 96));
                gm_b16_8 t115 = gm_l7;
                gm_b16_8 t116 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t112, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t117 = t116;
                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                gm_f_copy_from_f7j6p05(t111, t118);
            }
ix18:;
        }
        {
            void * t119 = gm_l0;
            gm_t2lqu8jsj511d t120 = gm_l6;
            gm_f_release_jn5kqdi(t119, GM_CONV(gm_csc7lc6jlkqkuq, t120));
        }
    }
    gm_b0_1 t121;
    gm_tcfa6l7reoosn t122;
    GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t122 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t122 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t122 + 24, 0);
    ((uint8_t *)&t122)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t122 + 8), t121);
    gm_tcfa6l7reoosn t123 = t122;
    F.ret = t123;
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
