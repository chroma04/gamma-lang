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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { uint64_t f0; gm_t5r2drn9pjqkt f1; } gm_t0cd9vl8b1nej;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0cd9vl8b1nej) == 32 && _Alignof(gm_t0cd9vl8b1nej) == 8 && offsetof(gm_t0cd9vl8b1nej, f0) == 0 && offsetof(gm_t0cd9vl8b1nej, f1) == 8, "layout of `(i: usize, x: PendingBody)`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_t0ut88vjdgrt1 f1; } gm_to24qukftd66u;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
_Static_assert(sizeof(gm_to24qukftd66u) == 48 && _Alignof(gm_to24qukftd66u) == 8 && offsetof(gm_to24qukftd66u, f0) == 0 && offsetof(gm_to24qukftd66u, f1) == 8, "layout of `(i: usize, x: MonoRequest)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
struct gm_fr_ensure_body_tbtojbv {
    gm_frame h;
    gm_tqqhoivahirr0 l3; /* sig */
    gm_t0ut88vjdgrt1 l7; /* r */
    gm_t0ut88vjdgrt1 l8; /* req */
    gm_tdgfduqjheqdm l14; /* def */
    uint8_t ret;
};
struct gm_fr_fn_uep4lj8 {
    gm_frame h;
    gm_t710lsrq1m8gk l5; /* w */
    gm_b0_1 ret;
};
struct gm_fr_pop_i1jcgk1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_prepare_0r9bic7 {
    gm_frame h;
    void * l0; /* s */
    gm_t4c7ipjj9s3fp l2; /* seen */
    gm_trqfvmjtt4sij l3; /* work */
    gm_trqfvmjtt4sij l5; /* fns */
    gm_trqfvmjtt4sij l6; /* consts */
    uint8_t l7; /* ok */
    void * l8; /* param */
    gm_tdgfduqjheqdm l20; /* def */
    uint8_t ret;
};
bool gm_eq4kd0egplg7(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_body_kc8b374(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_check_pending_clause_33df87a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_ensure_body_tbtojbv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_uep4lj8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_format_2pdgdtp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hgc3vel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hs0gaus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_pop_i1jcgk1(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_prepare_0r9bic7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_produce_instance_kcqgj3e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_remove_d75fgh2(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_kt0vu6d(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);
extern const char gm_srcp1pl693rgtdc[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_pop_i1jcgk1(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_i1jcgk1 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* x */
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
            uint8_t t8 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t12 = r10;
        uint64_t t13 = (uintptr_t)t12;
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)t14) + 0) + 8);
        if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_srcrgc9bkquf6nr, 130, 10);
        void * t16 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)t14) + 0)) + t13 * 8));
        gm_l2 = t16;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
        void * t20 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)));
        uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t22 = gm_l1;
        uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t25 = r23;
        uint64_t t26 = (uintptr_t)t25;
        if (t21 > t26 || t26 > (uintptr_t)t19) gm_trap_slice(t21, t26, t19, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t20 + t21 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t21));
        GM_ST(gm_b16_8, (((uint8_t *)t17) + 0), t27);
    }
    {
        void * t28 = gm_l2;
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ensure_body_tbtojbv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_ensure_body_tbtojbv F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_t5r2drn9pjqkt gm_l12; /* p */
    gm_t5r2drn9pjqkt gm_l13; /* body */
    GM_STACK_CHECK(&F, 1636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_tdgfduqjheqdm t5;
        gm_f_def_of_m512sbu((void *)&t5, t3, t4);
        gm_tdgfduqjheqdm t6 = t5;
        void * t7 = GM_LD(void *, (((uint8_t *)&t6) + 40));
        void * t8 = t7;
        uint8_t t9;
        if ((GM_LD(void *, ((uint8_t *)&t8)) != 0)) goto mn11;
        t9 = ((uint8_t)0);
        goto me10;
mn11:;
        t9 = ((uint8_t)1);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 65, 8);
me10:;
        if (!t9) goto ie2;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t15 = gm_l1;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 344) + 0) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcp1pl693rgtdc, 66, 12);
        gm_tqqhoivahirr0 t20 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 344) + 0)) + t17 * 136));
        F.l3 = t20;
    }
    {
        uint8_t t22;
        if ((((((uint8_t *)&F.l3) + 96))[24] != 0)) goto mn24;
        t22 = ((uint8_t)0);
        goto me23;
mn24:;
        t22 = ((uint8_t)1);
        goto me23;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 67, 8);
me23:;
        if (!t22) goto ie21;
        {
            {
                void * t28 = gm_l0;
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((((uint8_t *)t28) + 1320) + 0));
                gm_b16_8 t30 = t29;
                gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                gm_l4 = t31;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t33 = gm_l5;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t35 = (t33 < t34);
                    if (!t35) goto lb32;
                    {
                        gm_to24qukftd66u t38;
                        uint64_t t39 = gm_l5;
                        GM_ST(uint64_t, (uint8_t *)&t38 + 0, t39);
                        uint64_t t40 = gm_l5;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        memcpy((uint8_t *)&t38 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t41 * 40), 40);
                        gm_to24qukftd66u t43 = t38;
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
                        gm_l6 = t46;
                        gm_t0ut88vjdgrt1 t47 = GM_LD(gm_t0ut88vjdgrt1, (((uint8_t *)&t43) + 8));
                        F.l7 = t47;
                        {
                            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 4));
                            uint32_t t51 = gm_l1;
                            uint8_t t52 = (!(t50 == t51));
                            if (!t52) goto ie49;
                            {
                                goto lc32;
                            }
                            goto ix49;
ie49:;
ix49:;
                        }
                        {
                            void * t54 = gm_l0;
                            void * t55 = (void *)(((uint8_t *)t54) + 1320);
                            uint64_t t56 = gm_l6;
                            gm_f_remove_kt0vu6d((void *)&F.l8, t55, t56);
                        }
                        {
                            void * t58 = gm_l0;
                            uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 0));
                            gm_tl874t2atnlh3 t60 = gm_l2;
                            uint8_t t61 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t58, t59, t60);
                            if (gm_unw.kind) {
                                goto gm_ll36;
                            }
                            uint8_t t62 = (uint8_t)!t61;
                            if (!t62) goto ie57;
                            {
                                uint8_t t64 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix57;
ie57:;
ix57:;
                        }
                        {
                            void * t65 = gm_l0;
                            gm_b0_1 t66 = gm_f_produce_instance_kcqgj3e((void *)NULL, cx, NULL, t65, &(*(gm_t0ut88vjdgrt1 *)((uint8_t *)&F.l8)));
                            if (gm_unw.kind) {
                                goto gm_ll36;
                            }
                        }
                        {
                            uint8_t t67 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto me44;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 68, 9);
me44:;
                    }
lc32:;
                    {
                        uint64_t t69 = gm_l5;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        uint64_t t72 = r70;
                        gm_l5 = t72;
                    }
                }
gm_ll36:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
                }
                goto gm_lad;
lb32:;
            }
        }
        {
            uint8_t t73 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        {
            void * t75 = gm_l0;
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((((uint8_t *)t75) + 424) + 0));
            gm_b16_8 t77 = t76;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_l9 = t78;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t80 = gm_l10;
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t82 = (t80 < t81);
                if (!t82) goto lb79;
                {
                    gm_t0cd9vl8b1nej t85;
                    uint64_t t86 = gm_l10;
                    GM_ST(uint64_t, (uint8_t *)&t85 + 0, t86);
                    uint64_t t87 = gm_l10;
                    uint64_t t88 = (uintptr_t)t87;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    memcpy((uint8_t *)&t85 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t88 * 24), 24);
                    gm_t0cd9vl8b1nej t90 = t85;
                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&t90) + 0));
                    gm_l11 = t93;
                    gm_t5r2drn9pjqkt t94 = GM_LD(gm_t5r2drn9pjqkt, (((uint8_t *)&t90) + 8));
                    gm_l12 = t94;
                    {
                        uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 0));
                        uint32_t t98 = gm_l1;
                        uint8_t t99 = (!(t97 == t98));
                        if (!t99) goto ie96;
                        {
                            goto lc79;
                        }
                        goto ix96;
ie96:;
ix96:;
                    }
                    {
                        void * t101 = gm_l0;
                        void * t102 = (void *)(((uint8_t *)t101) + 424);
                        uint64_t t103 = gm_l11;
                        gm_f_remove_d75fgh2((void *)&gm_l13, t102, t103);
                    }
                    {
                        void * t104 = gm_l0;
                        uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 0));
                        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        void * t107 = GM_LD(void *, (((uint8_t *)&gm_l13) + 16));
                        gm_b0_1 t108 = gm_f_check_fn_body_kc8b374((void *)NULL, cx, NULL, t104, t105, t106, t107);
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                    }
                    {
                        uint8_t t109 = 0;
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 77, 5);
me91:;
                }
lc79:;
                {
                    uint64_t t111 = gm_l10;
                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                    uint64_t t114 = r112;
                    gm_l10 = t114;
                }
            }
gm_ll83:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
            }
            goto gm_lad;
lb79:;
        }
    }
    {
        void * t116 = gm_l0;
        uint32_t t117 = gm_l1;
        uint8_t t118 = gm_f_check_pending_clause_33df87a((void *)NULL, cx, NULL, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t118) goto ie115;
        {
            uint8_t t120 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        void * t122 = gm_l0;
        gm_b16_8 t123 = GM_LD(gm_b16_8, ((((uint8_t *)t122) + 2120) + 0));
        gm_b16_8 t124 = t123;
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        uint32_t t126 = gm_l1;
        gm_tf5srh4mp4b3i t127 = gm_f_index_of_41b084m(t125, t126);
        gm_tf5srh4mp4b3i t128 = t127;
        uint8_t t129;
        if (((((uint8_t *)&t128))[8] != 0)) goto mn131;
        t129 = ((uint8_t)0);
        goto me130;
mn131:;
        t129 = ((uint8_t)1);
        goto me130;
mn132:;
        gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 84, 8);
me130:;
        if (!t129) goto ie121;
        {
            void * t134 = gm_l0;
            gm_tl874t2atnlh3 t135 = gm_l2;
            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_tnpifv20irnph t137;
            GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
            gm_tabb710d9mq2e t138;
            gm_f_format_hs0gaus((void *)&t138, (void *)NULL, cx, NULL, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t139 = t138;
            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_srcp1pl693rgtdc, 85, 23);
            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
            gm_b16_8 t141 = t140;
            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
            gm_b0_1 t143 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t134, t135, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t144 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix121;
ie121:;
ix121:;
    }
    {
        uint32_t t145 = gm_l1;
        uint64_t t146 = (uint64_t)t145;
        uint64_t t147 = (uintptr_t)t146;
        void * t148 = gm_l0;
        uint64_t t149 = GM_LD(uintptr_t, ((((uint8_t *)t148) + 384) + 0) + 8);
        if (t147 >= (uintptr_t)(t149)) gm_trap_index(t147, t149, gm_srcp1pl693rgtdc, 88, 12);
        gm_tdgfduqjheqdm t150 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t148) + 384) + 0)) + t147 * 64));
        F.l14 = t150;
    }
    {
        uint8_t t152 = 0;
        uint8_t t153 = ((GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0) + 0) == ((uint8_t)3ULL)));
        if (!t153) {
            uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            uint64_t t155 = (uintptr_t)t154;
            void * t156 = gm_l0;
            void * t157 = GM_LD(void *, (((uint8_t *)t156) + 0));
            uint64_t t158 = GM_LD(uintptr_t, ((((uint8_t *)t157) + 200) + 0) + 8);
            if (t155 >= (uintptr_t)(t158)) gm_trap_index(t155, t158, gm_srcp1pl693rgtdc, 89, 32);
            uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t157) + 200) + 0)) + t155 * 184) + 112));
            t152 = t159;
        } else t152 = 1;
        if (!t152) goto ie151;
        {
            uint8_t t161 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix151;
ie151:;
ix151:;
    }
    {
        uint8_t t163 = ((GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0) + 0) == ((uint8_t)0ULL)));
        if (!t163) goto ie162;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 56));
        uint8_t t165 = (uint8_t)!t164;
        if (!t165) goto ie162;
        {
            uint8_t t167 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix162;
ie162:;
ix162:;
    }
    {
        void * t168 = gm_l0;
        gm_tl874t2atnlh3 t169 = gm_l2;
        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_tnpifv20irnph t171;
        GM_ST(gm_b16_8, ((uint8_t *)&t171 + 0), t170);
        gm_tabb710d9mq2e t172;
        gm_f_format_hgc3vel((void *)&t172, (void *)NULL, cx, NULL, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t173 = t172;
        if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 8, gm_srcp1pl693rgtdc, 93, 19);
        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&t173) + 8));
        gm_b16_8 t175 = t174;
        gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
        gm_b0_1 t177 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t168, t169, t176);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t178 = 0;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_uep4lj8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_uep4lj8 F;
    void * gm_l0; /* n */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* f */
    uint32_t gm_l3; /* f */
    uint32_t gm_l4; /* h */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t4lnf71mruqpt gm_l8; /* h */
    gm_b16_8 gm_l9; /* decls */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* d */
    GM_STACK_CHECK(&F, 669u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        uint8_t t7 = gm_f_mentions_error_37knu7r(t4, GM_CONV(gm_csguankko4lmhd, t6));
        if (!t7) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l7), ((uint8_t)0));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t18) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma16; case ((uint8_t)16ULL): goto ma16; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma16; case ((uint8_t)22ULL): goto ma16; case ((uint8_t)23ULL): goto ma16; case ((uint8_t)24ULL): goto ma16; case ((uint8_t)25ULL): goto ma16; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma16; case ((uint8_t)30ULL): goto ma16; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma16; case ((uint8_t)33ULL): goto ma16; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma16; case ((uint8_t)36ULL): goto ma16; case ((uint8_t)37ULL): goto ma16; case ((uint8_t)38ULL): goto ma16; case ((uint8_t)39ULL): goto ma16; case ((uint8_t)40ULL): goto ma16; case ((uint8_t)41ULL): goto ma16; case ((uint8_t)42ULL): goto ma16; case ((uint8_t)43ULL): goto ma16; case ((uint8_t)44ULL): goto ma16; case ((uint8_t)45ULL): goto ma16; case ((uint8_t)46ULL): goto ma16; case ((uint8_t)47ULL): goto ma16; case ((uint8_t)48ULL): goto ma16; case ((uint8_t)49ULL): goto ma16; case ((uint8_t)50ULL): goto ma16; case ((uint8_t)51ULL): goto ma16; case ((uint8_t)52ULL): goto ma16; case ((uint8_t)53ULL): goto ma16; case ((uint8_t)54ULL): goto ma16; case ((uint8_t)55ULL): goto ma16; case ((uint8_t)56ULL): goto ma16; case ((uint8_t)57ULL): goto ma16; case ((uint8_t)58ULL): goto ma14; default: goto ma16; }
ma11:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t20 != ((uint8_t)9ULL)) goto mn19;
        {
            void * t22 = gm_l0;
            GM_ST(void *, ((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l8), t22);
        }
        goto me10;
mn19:;
ma12:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t26 != ((uint8_t)34ULL)) goto pa25;
        uint8_t t27 = GM_LD(uint8_t, ((((((uint8_t *)t9) + 8) + 8) + 0) + 0));
        if (t27 != ((uint8_t)0ULL)) goto pa25;
        uint32_t t28 = GM_LD(uint32_t, ((((((uint8_t *)t9) + 8) + 8) + 0) + 8));
        gm_l1 = t28;
        goto po24;
pa25:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t30 != ((uint8_t)26ULL)) goto pa29;
        uint32_t t31 = GM_LD(uint32_t, ((((uint8_t *)t9) + 8) + 8));
        gm_l1 = t31;
        goto po24;
pa29:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t33 != ((uint8_t)27ULL)) goto pa32;
        uint32_t t34 = GM_LD(uint32_t, ((((uint8_t *)t9) + 8) + 8));
        gm_l1 = t34;
        goto po24;
pa32:;
        goto mn23;
po24:;
        void * t35 = (void *)((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l5);
        uint32_t t36 = gm_l1;
        gm_tftb1odc739t5 t37 = gm_f_push_im9d4ro(t35, t36);
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcp1pl693rgtdc, 22, 61);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
        goto me10;
mn23:;
ma13:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t41 != ((uint8_t)28ULL)) goto mn40;
        uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)t9) + 8) + 8));
        gm_l4 = t42;
        void * t43 = (void *)((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l5);
        uint32_t t44 = gm_l4;
        uint64_t t45 = (uint64_t)t44;
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l0));
        uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 504) + 0) + 8);
        if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_srcp1pl693rgtdc, 23, 46);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t47) + 504) + 0)) + t46 * 40) + 24));
        gm_b16_8 t50 = t49;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        gm_tftb1odc739t5 t52 = gm_f_extend_7hs67uj(t43, t51);
        gm_tftb1odc739t5 t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcp1pl693rgtdc, 23, 35);
        gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
        goto me10;
mn40:;
ma14:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t56 != ((uint8_t)58ULL)) goto mn55;
        gm_t710lsrq1m8gk t57 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t9) + 8) + 8));
        F.l5 = t57;
        {
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
            gm_l6 = t59;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t61 = gm_l7;
                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                {
                    uint64_t t66 = gm_l7;
                    uint64_t t67 = (uintptr_t)t66;
                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t4lnf71mruqpt t71 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 32));
                    gm_l8 = t71;
                    {
                        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                        if (t74 != ((uint8_t)1ULL)) goto ie73;
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l8) + 8) + 0));
                        gm_l9 = t75;
                        {
                            {
                                gm_b16_8 t78 = gm_l9;
                                gm_l10 = t78;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t80 = gm_l11;
                                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t82 = (t80 < t81);
                                    if (!t82) goto lb79;
                                    {
                                        uint64_t t85 = gm_l11;
                                        uint64_t t86 = (uintptr_t)t85;
                                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint32_t t88 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t86 * 4));
                                        uint32_t t89 = t88;
                                        uint32_t t92 = GM_LD(uint32_t, ((uint8_t *)&t89));
                                        gm_l12 = t92;
                                        {
                                            void * t94 = (void *)((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l5);
                                            uint32_t t95 = gm_l12;
                                            uint64_t t96 = (uint64_t)t95;
                                            uint64_t t97 = (uintptr_t)t96;
                                            void * t98 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l0));
                                            uint64_t t99 = GM_LD(uintptr_t, ((((uint8_t *)t98) + 544) + 0) + 8);
                                            if (t97 >= (uintptr_t)(t99)) gm_trap_index(t97, t99, gm_srcp1pl693rgtdc, 26, 53);
                                            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t98) + 544) + 0)) + t97 * 64) + 48));
                                            gm_b16_8 t101 = t100;
                                            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                                            gm_tftb1odc739t5 t103 = gm_f_extend_7hs67uj(t94, t102);
                                            gm_tftb1odc739t5 t104 = t103;
                                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcp1pl693rgtdc, 26, 42);
                                            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                                        }
                                        goto me90;
mn91:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 26, 25);
me90:;
                                    }
lc79:;
                                    {
                                        uint64_t t107 = gm_l11;
                                        uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                                        uint64_t t110 = r108;
                                        gm_l11 = t110;
                                    }
                                }
lb79:;
                            }
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    goto me69;
mn70:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 24, 29);
me69:;
                }
lc60:;
                {
                    uint64_t t112 = gm_l7;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l7 = t115;
                }
            }
lb60:;
        }
        goto me10;
mn55:;
ma15:;
        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t117 != ((uint8_t)14ULL)) goto mn116;
        void * t118 = (void *)((uint8_t *)&((struct gm_fr_prepare_0r9bic7 *)((&F.h)->up))->l6);
        void * t119 = gm_l0;
        gm_tftb1odc739t5 t120 = gm_f_push_pf02nv6(t118, t119);
        gm_tftb1odc739t5 t121 = t120;
        if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srcp1pl693rgtdc, 29, 30);
        gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        goto me10;
mn116:;
ma16:;
        goto me10;
mn123:;
ma17:;
        gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 20, 13);
me10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_prepare_0r9bic7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_prepare_0r9bic7 F;
    void * gm_l1; /* e */
    void * gm_l4; /* x */
    void * gm_l9; /* p */
    uint32_t gm_l10; /* id */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l13; /* c */
    gm_tl874t2atnlh3 gm_l14; /* span */
    uint32_t gm_l15; /* id */
    gm_t9qkh0rsooc8d gm_l16; /* state */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint32_t gm_l19; /* f */
    void * gm_l21; /* b */
    uint32_t gm_l22; /* p */
    GM_STACK_CHECK(&F, 976u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        gm_f_hash_map_70470jk((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        void * t3 = gm_l1;
        gm_tftb1odc739t5 t4 = gm_f_push_pf02nv6(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcp1pl693rgtdc, 12, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        for (;;) {
            void * t8 = (void *)((uint8_t *)&F.l3);
            void * t9 = gm_f_pop_i1jcgk1(t8);
            void * t10 = t9;
            if (!(GM_LD(void *, ((uint8_t *)&t10)) != 0)) goto lb7;
            void * t11 = GM_LD(void *, ((uint8_t *)&t10));
            gm_l4 = t11;
            {
                gm_f_list_go2qg0b((void *)&F.l5, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                gm_f_list_s1kpkpr((void *)&F.l6, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                F.l7 = ((uint8_t)1);
            }
            {
                F.l8 = ((void *)0);
            }
            {
                void * t14 = gm_l4;
                gm_closure t15 = ((gm_closure){(void *)gm_f_fn_uep4lj8, (void *)&F.h, cx});
                gm_f_visit_e3tpkkh(t14, t15);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                uint8_t t17 = F.l7;
                uint8_t t18 = (uint8_t)!t17;
                if (!t18) goto ie16;
                {
                    uint8_t t20 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix16;
ie16:;
ix16:;
            }
            {
                void * t22 = F.l8;
                void * t23 = t22;
                if (!(GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto ie21;
                void * t24 = GM_LD(void *, ((uint8_t *)&t23));
                gm_l9 = t24;
                {
                    void * t27 = gm_l9;
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
                    if (t28 != ((uint8_t)9ULL)) goto ie26;
                    uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t27) + 8) + 8));
                    gm_l10 = t29;
                    {
                        void * t31 = F.l0;
                        void * t32 = gm_l9;
                        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 208));
                        void * t34 = F.l0;
                        void * t35 = (void *)(((uint8_t *)t34) + 8);
                        uint32_t t36 = gm_l10;
                        gm_b16_8 t37 = gm_f_param_name_rbnop5l(t35, t36);
                        gm_tnpifv20irnph t38;
                        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
                        gm_tabb710d9mq2e t39;
                        gm_f_format_2pdgdtp((void *)&t39, (void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_tabb710d9mq2e t40 = t39;
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcp1pl693rgtdc, 37, 29);
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
                        gm_b16_8 t42 = t41;
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                        gm_b0_1 t44 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t31, t33, t43);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                    }
                    {
                        uint8_t t45 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    goto ix26;
ie26:;
                    {
                        uint8_t t47 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
ix26:;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                {
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_l11 = t49;
                }
                {
                    gm_l12 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t51 = gm_l12;
                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        uint8_t t53 = (t51 < t52);
                        if (!t53) goto lb50;
                        {
                            uint64_t t56 = gm_l12;
                            uint64_t t57 = (uintptr_t)t56;
                            uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                            void * t59 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t57 * 8));
                            void * t60 = t59;
                            void * t63 = GM_LD(void *, ((uint8_t *)&t60));
                            gm_l13 = t63;
                            {
                                void * t66 = gm_l13;
                                uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t66) + 8) + 0));
                                if (t67 != ((uint8_t)14ULL)) goto ie65;
                                gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t66) + 8) + 8) + 16));
                                gm_l14 = t68;
                                {
                                    void * t70 = F.l0;
                                    gm_tl874t2atnlh3 t71 = gm_l14;
                                    void * t72 = gm_l13;
                                    gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 0));
                                    gm_t9805qtonbsag t74 = gm_f_const_id_2mlh0og(t70, GM_CONV(gm_cseahvg8v7p0vs, t71), GM_CONV(gm_csguankko4lmhd, t73));
                                    gm_t9805qtonbsag t75 = t74;
                                    uint32_t t76;
                                    if (((((uint8_t *)&t75))[4] != 0)) {
                                        uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)&t75) + 0));
                                        t76 = t77;
                                    } else {
                                        goto lc50;
                                    }
                                    gm_l15 = t76;
                                }
                                {
                                    uint32_t t78 = gm_l15;
                                    uint64_t t79 = (uint64_t)t78;
                                    uint64_t t80 = (uintptr_t)t79;
                                    void * t81 = F.l0;
                                    uint64_t t82 = GM_LD(uintptr_t, ((((uint8_t *)t81) + 1968) + 0) + 8);
                                    if (t80 >= (uintptr_t)(t82)) gm_trap_index(t80, t82, gm_srcp1pl693rgtdc, 43, 22);
                                    gm_t9qkh0rsooc8d t83 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t81) + 1968) + 0)) + t80 * 88) + 72));
                                    gm_l16 = t83;
                                }
                                {
                                    uint8_t t85 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l16) + 0) == ((uint8_t)0ULL)));
                                    if (!t85) goto ie84;
                                    void * t86 = F.l0;
                                    gm_tl874t2atnlh3 t87 = gm_l14;
                                    void * t88 = gm_l13;
                                    gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 0));
                                    void * t90 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t86, t87, t89);
                                    if (gm_unw.kind) {
                                        goto gm_ll54;
                                    }
                                    void * t91 = t90;
                                    uint8_t t92;
                                    if ((GM_LD(void *, ((uint8_t *)&t91)) != 0)) goto mn94;
                                    t92 = ((uint8_t)1);
                                    goto me93;
mn94:;
                                    t92 = ((uint8_t)0);
                                    goto me93;
mn95:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 44, 40);
me93:;
                                    if (!t92) goto ie84;
                                    {
                                        uint8_t t97 = 0;
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    goto ix84;
ie84:;
ix84:;
                                }
                                {
                                    gm_t9qkh0rsooc8d t99 = gm_l16;
                                    gm_t9qkh0rsooc8d t100;
                                    GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)3ULL));
                                    gm_t9qkh0rsooc8d t101;
                                    GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)4ULL));
                                    gm_t0am46iohlpol t102;
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 0), t100);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 1), t101);
                                    gm_t0am46iohlpol t103 = t102;
                                    uint8_t t104 = 0;
                                    gm_t9qkh0rsooc8d t105 = t99;
                                    if (gm_eq4kd0egplg7(((uint8_t *)&t103) + 0, ((uint8_t *)&t105))) t104 = 1;
                                    if (gm_eq4kd0egplg7(((uint8_t *)&t103) + 1, ((uint8_t *)&t105))) t104 = 1;
                                    uint8_t t106 = (uint8_t)(t104);
                                    if (!t106) goto ie98;
                                    {
                                        uint8_t t108 = 0;
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    goto ix98;
ie98:;
ix98:;
                                }
                                goto ix65;
ie65:;
                                {
                                    goto lc50;
                                }
ix65:;
                            }
                            goto me61;
mn62:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 40, 9);
me61:;
                        }
lc50:;
                        {
                            uint64_t t111 = gm_l12;
                            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                            uint64_t t114 = r112;
                            gm_l12 = t114;
                        }
                    }
gm_ll54:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb50; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc50; }
                    }
                    goto gm_ll12;
lb50:;
                }
            }
            {
                {
                    gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                    gm_l17 = t116;
                }
                {
                    gm_l18 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t118 = gm_l18;
                        uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        uint8_t t120 = (t118 < t119);
                        if (!t120) goto lb117;
                        {
                            uint64_t t123 = gm_l18;
                            uint64_t t124 = (uintptr_t)t123;
                            uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                            uint32_t t126 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t124 * 4));
                            uint32_t t127 = t126;
                            uint32_t t130 = GM_LD(uint32_t, ((uint8_t *)&t127));
                            gm_l19 = t130;
                            {
                                void * t133 = (void *)((uint8_t *)&F.l2);
                                uint32_t t134 = gm_l19;
                                gm_tqs5p4n2tq19v t135 = gm_f_get_dk5gh28(t133, t134);
                                gm_tqs5p4n2tq19v t136 = t135;
                                uint8_t t137;
                                if (((((uint8_t *)&t136))[1] != 0)) goto mn139;
                                t137 = ((uint8_t)0);
                                goto me138;
mn139:;
                                t137 = ((uint8_t)1);
                                goto me138;
mn140:;
                                gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 48, 16);
me138:;
                                if (!t137) goto ie132;
                                {
                                    goto lc117;
                                }
                                goto ix132;
ie132:;
ix132:;
                            }
                            {
                                void * t142 = (void *)((uint8_t *)&F.l2);
                                uint32_t t143 = gm_l19;
                                gm_tftb1odc739t5 t144 = gm_f_put_v6g5pup(t142, t143, ((uint8_t)1));
                                gm_tftb1odc739t5 t145 = t144;
                                if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcp1pl693rgtdc, 49, 13);
                                gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                            }
                            {
                                void * t148 = F.l0;
                                uint32_t t149 = gm_l19;
                                void * t150 = gm_l1;
                                gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t150) + 208));
                                uint8_t t152 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t148, t149, t151);
                                if (gm_unw.kind) {
                                    goto gm_ll121;
                                }
                                uint8_t t153 = (uint8_t)!t152;
                                if (!t153) goto ie147;
                                {
                                    uint8_t t155 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix147;
ie147:;
ix147:;
                            }
                            {
                                void * t156 = F.l0;
                                uint32_t t157 = gm_l19;
                                gm_f_def_of_m512sbu((void *)&F.l20, t156, t157);
                            }
                            {
                                void * t159 = GM_LD(void *, (((uint8_t *)&F.l20) + 40));
                                void * t160 = t159;
                                if (!(GM_LD(void *, ((uint8_t *)&t160)) != 0)) goto ie158;
                                void * t161 = GM_LD(void *, ((uint8_t *)&t160));
                                gm_l21 = t161;
                                {
                                    void * t163 = (void *)((uint8_t *)&F.l3);
                                    void * t164 = gm_l21;
                                    gm_tftb1odc739t5 t165 = gm_f_push_pf02nv6(t163, t164);
                                    gm_tftb1odc739t5 t166 = t165;
                                    if (((((uint8_t *)&t166))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t166) + 1, gm_srcp1pl693rgtdc, 52, 32);
                                    gm_b0_1 t167 = GM_LD(gm_b0_1, (((uint8_t *)&t166) + 1));
                                }
                                goto ix158;
ie158:;
ix158:;
                            }
                            {
                                if (!(((((uint8_t *)&F.l20) + 48))[4] != 0)) goto ie168;
                                uint32_t t169 = GM_LD(uint32_t, (((uint8_t *)&F.l20) + 48));
                                gm_l22 = t169;
                                void * t170 = (void *)((uint8_t *)&F.l2);
                                uint32_t t171 = gm_l22;
                                gm_tqs5p4n2tq19v t172 = gm_f_get_dk5gh28(t170, t171);
                                gm_tqs5p4n2tq19v t173 = t172;
                                uint8_t t174;
                                if (((((uint8_t *)&t173))[1] != 0)) goto mn176;
                                t174 = ((uint8_t)1);
                                goto me175;
mn176:;
                                t174 = ((uint8_t)0);
                                goto me175;
mn177:;
                                gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 53, 36);
me175:;
                                if (!t174) goto ie168;
                                {
                                    void * t179 = (void *)((uint8_t *)&F.l2);
                                    uint32_t t180 = gm_l22;
                                    gm_tftb1odc739t5 t181 = gm_f_put_v6g5pup(t179, t180, ((uint8_t)1));
                                    gm_tftb1odc739t5 t182 = t181;
                                    if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srcp1pl693rgtdc, 54, 17);
                                    gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
                                }
                                {
                                    void * t185 = F.l0;
                                    uint32_t t186 = gm_l22;
                                    void * t187 = gm_l1;
                                    gm_tl874t2atnlh3 t188 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t187) + 208));
                                    uint8_t t189 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t185, t186, t188);
                                    if (gm_unw.kind) {
                                        goto gm_ll121;
                                    }
                                    uint8_t t190 = (uint8_t)!t189;
                                    if (!t190) goto ie184;
                                    {
                                        uint8_t t192 = 0;
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    goto ix184;
ie184:;
ix184:;
                                }
                                goto ix168;
ie168:;
ix168:;
                            }
                            goto me128;
mn129:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp1pl693rgtdc, 47, 9);
me128:;
                        }
lc117:;
                        {
                            uint64_t t194 = gm_l18;
                            uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                            uint64_t t197 = r195;
                            gm_l18 = t197;
                        }
                    }
gm_ll121:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb117; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc117; }
                    }
                    goto gm_ll12;
lb117:;
                }
            }
lc7:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
        }
        goto gm_lad;
lb7:;
    }
    {
        uint8_t t198 = 0;
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
