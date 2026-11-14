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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t pe[6]; } gm_t553jgmqd1tlr;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { gm_b16_8 a[2]; } gm_t0kl141g5m2qf;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t553jgmqd1tlr) == 16 && _Alignof(gm_t553jgmqd1tlr) == 8 && offsetof(gm_t553jgmqd1tlr, f0) == 0 && offsetof(gm_t553jgmqd1tlr, f1) == 8 && offsetof(gm_t553jgmqd1tlr, f2) == 9, "layout of `Policy`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { double f0; gm_b16_8 f1; } gm_tut1ci3k0af5f;
_Static_assert(sizeof(gm_t0kl141g5m2qf) == 32 && _Alignof(gm_t0kl141g5m2qf) == 8 && offsetof(gm_t0kl141g5m2qf, a) == 0, "layout of `[2][]const string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_trqfvmjtt4sij f4; } gm_t9j922kfqcdi9;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tut1ci3k0af5f) == 24 && _Alignof(gm_tut1ci3k0af5f) == 8 && offsetof(gm_tut1ci3k0af5f, f0) == 0 && offsetof(gm_tut1ci3k0af5f, f1) == 8, "layout of `(f64, string)`");
_Static_assert(sizeof(gm_t9j922kfqcdi9) == 72 && _Alignof(gm_t9j922kfqcdi9) == 8 && offsetof(gm_t9j922kfqcdi9, f0) == 0 && offsetof(gm_t9j922kfqcdi9, f1) == 8 && offsetof(gm_t9j922kfqcdi9, f2) == 16 && offsetof(gm_t9j922kfqcdi9, f3) == 24 && offsetof(gm_t9j922kfqcdi9, f4) == 32, "layout of `CleanReport`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_t553jgmqd1tlr f0; int64_t f1; gm_t9j922kfqcdi9 f2; } gm_ttnkva0sb026g;
_Static_assert(sizeof(gm_ttnkva0sb026g) == 96 && _Alignof(gm_ttnkva0sb026g) == 8 && offsetof(gm_ttnkva0sb026g, f0) == 0 && offsetof(gm_ttnkva0sb026g, f1) == 16 && offsetof(gm_ttnkva0sb026g, f2) == 24, "layout of `Cleaner`");
struct gm_fr_age_aenpk6g {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_base_name_6m6nisr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_builds_1d6q4v2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clean_97pd952 {
    gm_frame h;
    gm_ttnkva0sb026g l2; /* c */
    gm_trqfvmjtt4sij l3; /* kept_git */
    gm_tfeab279c16rr l7; /* <tmp> */
    gm_t9bmh80edou9c l8; /* <tmp> */
    gm_t9j922kfqcdi9 ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_delete_jp89bug {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_exists_1uo1rk0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_4g8ig10 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_human_size_dq2d7gs {
    gm_frame h;
    gm_tdmi4fk9nhqa7 l1; /* units */
    gm_b16_8 ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_writable_6uascrq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mirrors_ptf4sjn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_modified_977m57j {
    gm_frame h;
    gm_t913bb735pvt5 ret;
};
struct gm_fr_plural_4m5qson {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remove_tree_rp98g1s {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_report_is_empty_ad76r8j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_size_of_tree_769ctj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stale_0kln20t {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stamp_of_li2ggn0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_subdirs_69pr9gp {
    gm_frame h;
    gm_b16_8 l0; /* p */
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_summary_s3h16om {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tmp_6ua6orh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trees_9it6grn {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* kept */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_with_extension_jpfmsok {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint64_t gm_f_age_aenpk6g(void * a0, gm_t913bb735pvt5 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_base_name_6m6nisr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_builds_1d6q4v2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_clean_97pd952(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t553jgmqd1tlr a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_delete_jp89bug(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_ssu37o5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_4g8ig10(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_26j2l5c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gp81iq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i1us4dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kd24nd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o2o7n1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rpma0gd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_human_size_dq2d7gs(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_last_used_6quhcig(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_mirrors_ptf4sjn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_modified_977m57j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plural_4m5qson(void *env, gm_node *cx, gm_node *inst, uint64_t a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_report_is_empty_ad76r8j(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_size_of_tree_769ctj2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_stale_0kln20t(void * a0, gm_t913bb735pvt5 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stamp_of_li2ggn0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_subdirs_69pr9gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_summary_s3h16om(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_tmp_6ua6orh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_trees_9it6grn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_with_extension_jpfmsok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern void *const gm_hlkic73ff07n[];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0vltqrmpakvk[17];
extern const uint8_t gm_s2c2r23li2if2[8];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2s2f3gkf4c98[13];
extern const uint8_t gm_s36ugqba56le0[18];
extern const uint8_t gm_s3e0g1895m0j9[2];
extern const uint8_t gm_s56ntjkdhhepu[4];
extern const uint8_t gm_s5p7bqbmkba97[13];
extern const uint8_t gm_s7tf2msne28g3[19];
extern const uint8_t gm_s7uuha79fcmn5[18];
extern const uint8_t gm_s8p032eorpf80[3];
extern const uint8_t gm_s9ab1ohnfcjkc[6];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_sesj9tqqcsas0[6];
extern const uint8_t gm_sfinup9etdjip[4];
extern const uint8_t gm_sg8ojjtakanaa[5];
extern const uint8_t gm_sh0274v2fqhs4[18];
extern const uint8_t gm_shauoqbc8hgsg[4];
extern const uint8_t gm_sj7sbm8e8u97n[5];
extern const uint8_t gm_skm5ng1ir37l8[8];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_smu6g4sneo92j[6];
extern const uint8_t gm_sn2furlv6e7jl[4];
extern const uint8_t gm_snj8kbefit27t[14];
extern const uint8_t gm_spc0i9srvnmq5[6];
extern const uint8_t gm_spkgb882eim6u[10];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcdo1hnokt0mq8[];
extern const char gm_srcmj20te78r22f[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srd2ukct5o1qh[6];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stv6f87bn9v0e[7];
extern const uint8_t gm_su2m3cb0nts7p[3];
extern const uint8_t gm_suafpipqeom8q[7];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) uint64_t gm_f_age_aenpk6g(void * a0, gm_t913bb735pvt5 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_age_aenpk6g F;
    void * gm_l0; /* c */
    gm_t913bb735pvt5 gm_l1; /* t */
    int64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_t913bb735pvt5 t2 = gm_l1;
        int64_t t3;
        if (((((uint8_t *)&t2))[8] != 0)) {
            int64_t t4 = GM_LD(int64_t, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            uint8_t t5 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        gm_l2 = t3;
    }
    {
        void * t7 = gm_l0;
        int64_t t8 = GM_LD(int64_t, (((uint8_t *)t7) + 16));
        int64_t t9 = gm_l2;
        uint8_t t10 = (t8 <= t9);
        if (!t10) goto ie6;
        {
            uint8_t t12 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t13 = gm_l0;
        int64_t t14 = GM_LD(int64_t, (((uint8_t *)t13) + 16));
        int64_t t15 = gm_l2;
        int64_t r16; bool o17 = __builtin_sub_overflow(t14, t15, &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcmj20te78r22f, 110, 13);
        int64_t t18 = r16;
        uint64_t t19 = (uint64_t)t18;
        uint8_t t20 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_base_name_6m6nisr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_base_name_6m6nisr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    gm_f_file_name_em7v8lv((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3;
    if (((((uint8_t *)&t2))[16] != 0)) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 0));
        t3 = t4;
    } else {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
        t3 = t5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_exists_1uo1rk0 F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_node *pn4 = gm_find(cx, 2126215853);
            if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 124, 9);
            gm_tih6h4avv5nrb t5;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[5])((void *)&t5, pn4->env, pn4->hctx, pn4, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5j14m8ru132p t6;
            if (!((((uint8_t *)&t5))[0] != 0)) {
                t6 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t5) + 8));
            } else {
                uint8_t t7 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
        }
    }
    {
        uint8_t t8 = 0;
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
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_name_em7v8lv F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 216u);
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
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)&t2) + 16) + 0));
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    uint64_t t6 = (t5 - 1);
    gm_th2lqbo77pvsc t7;
    if (t6 < t5) {
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t4 + t6 * 16));
    gm_th2lqbo77pvsc t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[16] = 1;
        t7 = t9;
    } else {
    gm_th2lqbo77pvsc t10;
    memset(&t10, 0, sizeof t10);
        t7 = t10;
    }
    F.ret = t7;
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
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    uint8_t t5 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t1);
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
    GM_STACK_CHECK(&F, 141u);
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
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t7 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)1ULL)));
        uint8_t t8 = 0;
        F.ret = t7;
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
    GM_STACK_CHECK(&F, 141u);
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
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t7 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plural_4m5qson(void *env, gm_node *cx, gm_node *inst, uint64_t a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_plural_4m5qson F;
    uint64_t gm_l0; /* n */
    gm_b16_8 gm_l1; /* one */
    gm_b16_8 gm_l2; /* many */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf1bjg81u0glv t1;
    uint64_t t2 = gm_l0;
    GM_ST(uint64_t, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3;
    uint64_t t5 = gm_l0;
    uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
    if (!t6) goto ie4;
    gm_b16_8 t8 = gm_l1;
    gm_b16_8 t9 = t8;
    t3 = t9;
    goto ix4;
ie4:;
    gm_b16_8 t11 = gm_l2;
    gm_b16_8 t12 = t11;
    t3 = t12;
ix4:;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 8, t3);
    gm_tabb710d9mq2e t13;
    gm_f_format_o2o7n1t((void *)&t13, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcmj20te78r22f, 34, 68);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
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
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_tree_rp98g1s F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 85u);
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
        uint8_t t6 = ((((uint8_t *)&t5))[0] != 0);
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
inline __attribute__((aligned(4))) uint8_t gm_f_report_is_empty_ad76r8j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_report_is_empty_ad76r8j F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t r5; bool o6 = __builtin_add_overflow(t2, t4, &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 32, 57);
    uint64_t t7 = r5;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 16));
    uint64_t r10; bool o11 = __builtin_add_overflow(t7, t9, &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 32, 57);
    uint64_t t12 = r10;
    uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_stale_0kln20t(void * a0, gm_t913bb735pvt5 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stale_0kln20t F;
    void * gm_l0; /* c */
    gm_t913bb735pvt5 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 8));
    if (!t3) {
        void * t4 = gm_l0;
        gm_t913bb735pvt5 t5 = gm_l1;
        uint64_t t6 = gm_f_age_aenpk6g(t4, t5);
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 0) + 0));
        uint8_t t9 = (t6 >= t8);
        t1 = t9;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stamp_of_li2ggn0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_stamp_of_li2ggn0 F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_gp81iq6((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_srcbq9k9kvdokbf, 410, 54);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
extern __attribute__((aligned(4))) uint64_t gm_f_age_aenpk6g(void * a0, gm_t913bb735pvt5 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_builds_1d6q4v2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_builds_1d6q4v2 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* prefixes */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* f */
    gm_b16_8 gm_l6; /* name */
    uint8_t gm_l7; /* matches */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* p */
    GM_STACK_CHECK(&F, 894u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_trqfvmjtt4sij t4;
            gm_f_subdirs_69pr9gp((void *)&t4, (void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 16));
                    gm_l5 = t17;
                    {
                        gm_b16_8 t19 = gm_l5;
                        gm_b16_8 t20 = gm_f_base_name_6m6nisr((void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l6 = t20;
                    }
                    {
                        gm_l7 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t22 = gm_l2;
                            gm_l8 = t22;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t24 = gm_l9;
                                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t26 = (t24 < t25);
                                if (!t26) goto lb23;
                                {
                                    uint64_t t29 = gm_l9;
                                    uint64_t t30 = (uintptr_t)t29;
                                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t30 * 16));
                                    gm_l10 = t34;
                                    {
                                        gm_b16_8 t37 = gm_l6;
                                        gm_b16_8 t38 = gm_l10;
                                        uint8_t t39 = gm_f_starts_with_6vvt01e(t37, t38);
                                        if (!t39) goto ie36;
                                        {
                                            gm_l7 = ((uint8_t)1);
                                        }
                                        goto ix36;
ie36:;
ix36:;
                                    }
                                    goto me32;
mn33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 195, 9);
me32:;
                                }
lc23:;
                                {
                                    uint64_t t42 = gm_l9;
                                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                                    uint64_t t45 = r43;
                                    gm_l9 = t45;
                                }
                            }
lb23:;
                        }
                    }
                    {
                        uint8_t t47 = gm_l7;
                        uint8_t t48 = (uint8_t)!t47;
                        if (!t48) goto ie46;
                        {
                            goto lc6;
                        }
                        goto ix46;
ie46:;
ix46:;
                    }
                    {
                        gm_b16_8 t51 = gm_l6;
                        gm_b16_8 t52;
                        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_srd2ukct5o1qh));
                        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(5));
                        uint8_t t53 = gm_f_contains_gkki4kq(t51, t52);
                        if (!t53) goto ie50;
                        {
                            uint8_t t56 = 0;
                            void * t57 = gm_l0;
                            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t57) + 0) + 8));
                            if (!t58) {
                                void * t59 = gm_l0;
                                gm_b16_8 t60 = gm_l5;
                                gm_t913bb735pvt5 t61 = gm_f_modified_977m57j((void *)NULL, cx, NULL, t60);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                                uint64_t t62 = gm_f_age_aenpk6g(t59, t61);
                                uint8_t t63 = (t62 >= ((uint64_t)86400000000000ULL));
                                t56 = t63;
                            } else t56 = 1;
                            if (!t56) goto ie55;
                            {
                                {
                                    void * t66 = gm_l0;
                                    gm_b16_8 t67 = gm_l5;
                                    uint8_t t68 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t66, t67);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                            }
                            goto ix55;
ie55:;
ix55:;
                        }
                        {
                            goto lc6;
                        }
                        goto ix50;
ie50:;
ix50:;
                    }
                    {
                        gm_b16_8 t70 = gm_l6;
                        gm_b16_8 t71;
                        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sahim4484f9l7));
                        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(2));
                        uint8_t t72 = gm_f_ends_with_biriu5l(t70, t71);
                        if (!t72) goto ie69;
                        gm_b16_8 t73 = gm_l5;
                        gm_b16_8 t74;
                        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_se0f2jka437t4));
                        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
                        gm_b16_8 t75 = gm_f_with_extension_jpfmsok((void *)NULL, cx, NULL, t73, t74);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        uint8_t t76 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t75);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (!t76) goto ie69;
                        {
                            goto lc6;
                        }
                        goto ix69;
ie69:;
ix69:;
                    }
                    {
                        void * t79 = gm_l0;
                        gm_b16_8 t80 = gm_l5;
                        gm_t913bb735pvt5 t81 = gm_f_modified_977m57j((void *)NULL, cx, NULL, t80);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        uint8_t t82 = gm_f_stale_0kln20t(t79, t81);
                        uint8_t t83 = (uint8_t)!t82;
                        if (!t83) goto ie78;
                        {
                            goto lc6;
                        }
                        goto ix78;
ie78:;
ix78:;
                    }
                    {
                        void * t86 = gm_l0;
                        gm_b16_8 t87 = gm_l5;
                        uint8_t t88 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t86, t87);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        uint8_t t89 = (uint8_t)!t88;
                        if (!t89) goto ie85;
                        {
                            goto lc6;
                        }
                        goto ix85;
ie85:;
ix85:;
                    }
                    {
                        gm_b16_8 t92 = gm_l6;
                        gm_b16_8 t93;
                        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sahim4484f9l7));
                        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(2));
                        uint8_t t94 = gm_f_ends_with_biriu5l(t92, t93);
                        uint8_t t95 = (uint8_t)!t94;
                        if (!t95) goto ie91;
                        {
                            void * t97 = gm_l0;
                            void * t98 = gm_l0;
                            uint64_t t99 = GM_LD(uint64_t, ((((uint8_t *)t98) + 24) + 16));
                            uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                            if (o101) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 207, 39);
                            uint64_t t102 = r100;
                            GM_ST(uint64_t, ((((uint8_t *)t97) + 24) + 16), t102);
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    {
                        gm_b16_8 t104 = gm_l6;
                        gm_b16_8 t105;
                        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_s8p032eorpf80));
                        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(2));
                        uint8_t t106 = gm_f_ends_with_biriu5l(t104, t105);
                        if (!t106) goto ie103;
                        gm_b16_8 t107 = gm_l6;
                        gm_b16_8 t108;
                        GM_ST(void *, (uint8_t *)&t108, (void *)(gm_suafpipqeom8q));
                        GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(6));
                        uint8_t t109 = gm_f_starts_with_6vvt01e(t107, t108);
                        if (!t109) goto ie103;
                        {
                            {
                                void * t112 = gm_l0;
                                gm_b16_8 t113 = gm_l5;
                                gm_b16_8 t114;
                                GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s2gnpj7k4l22n));
                                GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(1));
                                gm_b16_8 t115 = gm_f_with_extension_jpfmsok((void *)NULL, cx, NULL, t113, t114);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                                uint8_t t116 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t112, t115);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                            }
                        }
                        goto ix103;
ie103:;
ix103:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 192, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t118 = gm_l4;
                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                    uint64_t t121 = r119;
                    gm_l4 = t121;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
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
__attribute__((aligned(4))) void gm_f_clean_97pd952(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t553jgmqd1tlr a1) {
    struct gm_fr_clean_97pd952 F;
    gm_b16_8 gm_l0; /* dir */
    gm_t553jgmqd1tlr gm_l1; /* policy */
    gm_tpmdr2hmdhsqr gm_l4; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l5; /* <temporary array> */
    gm_tpmdr2hmdhsqr gm_l6; /* <temporary array> */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* sub */
    gm_b16_8 gm_l12; /* d */
    gm_toe5fmr3vvf4d gm_l13; /* e */
    GM_STACK_CHECK(&F, 1341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_t553jgmqd1tlr t2 = gm_l1;
        GM_ST(gm_t553jgmqd1tlr, (uint8_t *)&F.l2 + 0, t2);
        gm_node *pn3 = gm_find(cx, 1579066750);
        if (!pn3) gm_trap("no handler installed for effect `Clock`", gm_srcmj20te78r22f, 224, 39);
        int64_t t4;
        t4 = ((int64_t (*)(void *, gm_node *, gm_node *))pn3->clauses[1])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(int64_t, (uint8_t *)&F.l2 + 16, t4);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t9j922kfqcdi9 *)((uint8_t *)&F.l2 + 24)) + 0, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t9j922kfqcdi9 *)((uint8_t *)&F.l2 + 24)) + 8, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t9j922kfqcdi9 *)((uint8_t *)&F.l2 + 24)) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t9j922kfqcdi9 *)((uint8_t *)&F.l2 + 24)) + 24, ((uint64_t)0ULL));
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&(*(gm_t9j922kfqcdi9 *)((uint8_t *)&F.l2 + 24)) + 32)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        uint8_t t7 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie5;
        {
            memmove(gm_out, (((uint8_t *)&F.l2) + 24), 72);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t10 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t11 = gm_l0;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
        gm_b16_8 t13 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_spkgb882eim6u));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(9));
        gm_b16_8 t15 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_trees_9it6grn((void *)&F.l3, (void *)NULL, cx, NULL, t10, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t17 = gm_l0;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
        gm_b16_8 t19 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_su2m3cb0nts7p));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
        gm_b16_8 t21 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_b0_1 t24 = gm_f_mirrors_ptf4sjn((void *)NULL, cx, NULL, t16, t21, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t26 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t27 = gm_l0;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s0k6ioi39f878));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
            gm_b16_8 t29 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sa2h9vonan0ma));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(3));
            gm_b16_8 t31 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trqfvmjtt4sij t32;
            gm_f_trees_9it6grn((void *)&t32, (void *)NULL, cx, NULL, t26, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
    }
    {
        void * t33 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t34 = gm_l0;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sn2furlv6e7jl));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(3));
        gm_b16_8 t36 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s9ab1ohnfcjkc));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(5));
            gm_tpmdr2hmdhsqr t39;
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
            gm_l4 = t39;
        }
        void * t40 = ((uint8_t *)&gm_l4);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmj20te78r22f, 229, 34);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t40 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t42 = t41;
        gm_b0_1 t43 = gm_f_builds_1d6q4v2((void *)NULL, cx, NULL, t33, t36, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t44 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t45 = gm_l0;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_stv6f87bn9v0e));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
        gm_b16_8 t47 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sg8ojjtakanaa));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(4));
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sj7sbm8e8u97n));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l5)) + 0, t49);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t0kl141g5m2qf *)((uint8_t *)&gm_l5)) + 16, t50);
        }
        void * t51 = ((uint8_t *)&gm_l5);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcmj20te78r22f, 230, 37);
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t51 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t53 = t52;
        gm_b0_1 t54 = gm_f_builds_1d6q4v2((void *)NULL, cx, NULL, t44, t47, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t55 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t56 = gm_l0;
        {
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_suafpipqeom8q));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(6));
            gm_tpmdr2hmdhsqr t59;
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
            gm_l6 = t59;
        }
        void * t60 = ((uint8_t *)&gm_l6);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmj20te78r22f, 231, 21);
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t60 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t62 = t61;
        gm_b0_1 t63 = gm_f_builds_1d6q4v2((void *)NULL, cx, NULL, t55, t56, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8));
        if (!t65) goto ie64;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 9));
        uint8_t t67 = (uint8_t)!t66;
        if (!t67) goto ie64;
        {
            {
                gm_b16_8 t70;
                GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s0k6ioi39f878));
                GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 0, t70);
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sn2furlv6e7jl));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 16, t71);
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_stv6f87bn9v0e));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 32, t72);
            }
            {
                {
                    {
                        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 0, t75);
                        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 16, t76);
                        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 32));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 32, t77);
                    }
                    void * t78 = ((uint8_t *)&F.l8);
                    if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcmj20te78r22f, 233, 9);
                    gm_b16_8 t79;
                    GM_ST(void *, (uint8_t *)&t79, (void *)(((uint8_t *)t78 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                    gm_b16_8 t80 = t79;
                    gm_l9 = t80;
                }
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t82 = gm_l10;
                        uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint8_t t84 = (t82 < t83);
                        if (!t84) goto lb81;
                        {
                            uint64_t t87 = gm_l10;
                            uint64_t t88 = (uintptr_t)t87;
                            uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t88 * 16));
                            gm_l11 = t92;
                            {
                                gm_b16_8 t94 = gm_l0;
                                gm_b16_8 t95 = gm_l11;
                                gm_b16_8 t96 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t94, t95);
                                if (gm_unw.kind) {
                                    goto gm_ll85;
                                }
                                gm_l12 = t96;
                            }
                            {
                                gm_b16_8 t98 = gm_l12;
                                uint8_t t99 = gm_f_exists_1uo1rk0((void *)NULL, cx, NULL, t98);
                                if (gm_unw.kind) {
                                    goto gm_ll85;
                                }
                                if (!t99) goto ie97;
                                {
                                    gm_b16_8 t101 = gm_l12;
                                    gm_ttln0d0hd70en t102 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t101);
                                    if (gm_unw.kind) {
                                        goto gm_ll85;
                                    }
                                    gm_b0_1 t103;
                                    if (!((((uint8_t *)&t102))[0] != 0)) {
                                        gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 4));
                                        t103 = t104;
                                    } else {
                                        gm_toe5fmr3vvf4d t105 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t102) + 4));
                                        gm_l13 = t105;
                                        void * t107 = (void *)((((uint8_t *)&F.l2) + 24) + 32);
                                        gm_t6j6smj7qoctu t108;
                                        gm_b16_8 t109 = gm_l12;
                                        GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t109);
                                        gm_toe5fmr3vvf4d t110 = gm_l13;
                                        gm_b16_8 t111 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t110);
                                        if (gm_unw.kind) {
                                            goto gm_ll85;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t111);
                                        gm_tabb710d9mq2e t112;
                                        gm_f_format_rpma0gd((void *)&t112, (void *)NULL, cx, NULL, &t108);
                                        if (gm_unw.kind) {
                                            goto gm_ll85;
                                        }
                                        if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcmj20te78r22f, 237, 44);
                                        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
                                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                                        gm_tftb1odc739t5 t115 = gm_f_push_3ef746b(t107, t114);
                                        if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcmj20te78r22f, 237, 21);
                                        gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                                        gm_b0_1 t117 = t116;
                                        t103 = t117;
                                    }
                                }
                                goto ix97;
ie97:;
ix97:;
                            }
                            goto me90;
mn91:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 233, 9);
me90:;
                        }
lc81:;
                        {
                            uint64_t t119 = gm_l10;
                            uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                            uint64_t t122 = r120;
                            gm_l10 = t122;
                        }
                    }
gm_ll85:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
                    }
                    goto gm_lad;
lb81:;
                }
            }
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        memmove(gm_out, (((uint8_t *)&F.l2) + 24), 72);
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
inline __attribute__((aligned(4))) uint8_t gm_f_delete_jp89bug(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_delete_jp89bug F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* path */
    uint64_t gm_l2; /* size */
    gm_ttln0d0hd70en gm_l3; /* res */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    GM_STACK_CHECK(&F, 394u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        uint64_t t3 = gm_f_size_of_tree_769ctj2((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 0) + 9));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie4;
        {
            gm_ttln0d0hd70en t9;
            gm_b16_8 t11 = gm_l1;
            uint8_t t12 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t12) goto ie10;
            gm_b16_8 t14 = gm_l1;
            gm_ttln0d0hd70en t15 = gm_f_remove_tree_rp98g1s((void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttln0d0hd70en t16 = t15;
            t9 = t16;
            goto ix10;
ie10:;
            gm_b16_8 t18 = gm_l1;
            gm_node *pn19 = gm_find(cx, 2126215853);
            if (!pn19) gm_trap("no handler installed for effect `Fs`", gm_srcmj20te78r22f, 120, 61);
            gm_ttln0d0hd70en t20;
            t20 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn19->clauses[6])(pn19->env, pn19->hctx, pn19, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttln0d0hd70en t21 = t20;
            t9 = t21;
ix10:;
            gm_l3 = t9;
        }
        {
            if (!((((uint8_t *)&gm_l3))[0] != 0)) goto mn23;
            gm_toe5fmr3vvf4d t24 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l3) + 4));
            gm_l4 = t24;
            {
                uint8_t t27 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)0ULL)));
                if (!t27) goto ie26;
                {
                    void * t29 = gm_l0;
                    void * t30 = (void *)((((uint8_t *)t29) + 24) + 32);
                    gm_t6j6smj7qoctu t31;
                    gm_b16_8 t32 = gm_l1;
                    GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
                    gm_toe5fmr3vvf4d t33 = gm_l4;
                    gm_b16_8 t34 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t34);
                    gm_tabb710d9mq2e t35;
                    gm_f_format_rpma0gd((void *)&t35, (void *)NULL, cx, NULL, &t31);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcmj20te78r22f, 125, 44);
                    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                    gm_tftb1odc739t5 t38 = gm_f_push_3ef746b(t30, t37);
                    if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcmj20te78r22f, 125, 21);
                    gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                }
                {
                    uint8_t t40 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix26;
ie26:;
ix26:;
            }
            goto me22;
mn23:;
            goto me22;
mn41:;
            gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 121, 9);
me22:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t43 = gm_l0;
        void * t44 = gm_l0;
        uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)t44) + 24) + 24));
        uint64_t t46 = gm_l2;
        uint64_t r47; bool o48 = __builtin_add_overflow(t45, t46, &r47);
        if (o48) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 132, 5);
        uint64_t t49 = r47;
        GM_ST(uint64_t, ((((uint8_t *)t43) + 24) + 24), t49);
    }
    {
        uint8_t t50 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_4g8ig10(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_4g8ig10 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_tibupt4dmcqai t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            t6 = (*(gm_tibupt4dmcqai *)(((uint8_t *)&t5) + 8));
        } else {
            uint8_t t7 = 0;
            ((struct gm_fr_subdirs_69pr9gp *)((&F.h)->up))->ret = (*(gm_trqfvmjtt4sij *)((uint8_t *)&((struct gm_fr_subdirs_69pr9gp *)((&F.h)->up))->l1));
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t7;
            goto gm_lad;
        }
        gm_l2 = t6;
    }
    {
        void * t8 = (void *)((uint8_t *)&((struct gm_fr_subdirs_69pr9gp *)((&F.h)->up))->l1);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_subdirs_69pr9gp *)((&F.h)->up))->l0));
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t11 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t12 = gm_f_push_3ef746b(t8, t11);
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcmj20te78r22f, 100, 9);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 98, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_human_size_dq2d7gs(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_human_size_dq2d7gs F;
    uint64_t gm_l0; /* n */
    double gm_l2; /* v */
    uint64_t gm_l3; /* u */
    GM_STACK_CHECK(&F, 397u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3e0g1895m0j9));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shauoqbc8hgsg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 16, t3);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sfinup9etdjip));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s56ntjkdhhepu));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 48, t5);
    }
    {
        uint64_t t6 = gm_l0;
        double t7 = (double)(t6);
        gm_l2 = t7;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            double t9 = gm_l2;
            uint8_t t10 = (t9 >= ((double)1.024e3));
            if (!t10) goto lb8;
            uint64_t t11 = gm_l3;
            uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 55, 27);
            uint64_t t14 = r12;
            uint8_t t15 = (t14 < ((uint64_t)4ULL));
            if (!t15) goto lb8;
            {
                double t18 = gm_l2;
                double t19 = (t18 / ((double)1.024e3));
                gm_l2 = t19;
            }
            {
                uint64_t t20 = gm_l3;
                uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                if (o22) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 57, 9);
                uint64_t t23 = r21;
                gm_l3 = t23;
            }
lc8:;
        }
lb8:;
    }
    {
        uint64_t t25 = gm_l3;
        uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
        if (!t26) goto ie24;
        {
            uint64_t t28 = gm_l0;
            gm_t4ce0jd3tdro4 t29;
            GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t28);
            gm_tabb710d9mq2e t30;
            gm_f_format_26j2l5c((void *)&t30, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcmj20te78r22f, 59, 24);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_tut1ci3k0af5f t34;
        double t35 = gm_l2;
        GM_ST(double, (uint8_t *)&t34 + 0, t35);
        uint64_t t36 = gm_l3;
        uint64_t t37 = (uintptr_t)t36;
        if (t37 >= (uintptr_t)(4)) gm_trap_index(t37, 4, gm_srcmj20te78r22f, 60, 38);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + t37 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 8, t38);
        gm_tabb710d9mq2e t39;
        gm_f_format_i1us4dk((void *)&t39, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcmj20te78r22f, 60, 12);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        uint8_t t42 = 0;
        F.ret = t41;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_mirrors_ptf4sjn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_mirrors_ptf4sjn F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* db_root */
    gm_b16_8 gm_l2; /* kept */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* mirror */
    uint8_t gm_l6; /* is_kept */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* k */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_trqfvmjtt4sij t4;
            gm_f_subdirs_69pr9gp((void *)&t4, (void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 16));
                    gm_l5 = t17;
                    {
                        uint8_t t20 = 0;
                        void * t21 = gm_l0;
                        gm_b16_8 t22 = gm_l5;
                        uint8_t t23 = gm_f_tmp_6ua6orh((void *)NULL, cx, NULL, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (!t23) {
                            gm_b16_8 t24 = gm_l5;
                            uint8_t t25 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t24);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            uint8_t t26 = (uint8_t)!t25;
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
                        gm_l6 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t29 = gm_l2;
                            gm_l7 = t29;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t31 = gm_l8;
                                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t33 = (t31 < t32);
                                if (!t33) goto lb30;
                                {
                                    uint64_t t36 = gm_l8;
                                    uint64_t t37 = (uintptr_t)t36;
                                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t37 * 16));
                                    gm_l9 = t41;
                                    {
                                        gm_b16_8 t44 = gm_l9;
                                        gm_b16_8 t45 = gm_l5;
                                        gm_b16_8 t46 = gm_f_base_name_6m6nisr((void *)NULL, cx, NULL, t45);
                                        if (gm_unw.kind) {
                                            goto gm_ll34;
                                        }
                                        uint8_t t47 = (gm_eq0iqiubfm7e(((uint8_t *)&t44), ((uint8_t *)&t46)));
                                        if (!t47) goto ie43;
                                        {
                                            gm_l6 = ((uint8_t)1);
                                        }
                                        goto ix43;
ie43:;
ix43:;
                                    }
                                    goto me39;
mn40:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 182, 9);
me39:;
                                }
lc30:;
                                {
                                    uint64_t t50 = gm_l8;
                                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                                    uint64_t t53 = r51;
                                    gm_l8 = t53;
                                }
                            }
gm_ll34:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
                            }
                            goto gm_ll10;
lb30:;
                        }
                    }
                    {
                        uint8_t t55 = 0;
                        void * t56 = gm_l0;
                        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t56) + 0) + 8));
                        if (!t57) {
                            uint8_t t58 = gm_l6;
                            uint8_t t59 = (uint8_t)!t58;
                            t55 = t59;
                        } else t55 = 1;
                        if (!t55) goto ie54;
                        void * t60 = gm_l0;
                        gm_b16_8 t61 = gm_l5;
                        uint8_t t62 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t60, t61);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (!t62) goto ie54;
                        {
                            void * t64 = gm_l0;
                            void * t65 = gm_l0;
                            uint64_t t66 = GM_LD(uint64_t, ((((uint8_t *)t65) + 24) + 8));
                            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                            if (o68) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 185, 66);
                            uint64_t t69 = r67;
                            GM_ST(uint64_t, ((((uint8_t *)t64) + 24) + 8), t69);
                        }
                        goto ix54;
ie54:;
ix54:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 179, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t71 = gm_l4;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l4 = t74;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
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
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_modified_977m57j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_modified_977m57j F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcmj20te78r22f, 91, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[13])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            gm_t913bb735pvt5 t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        int64_t t8 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 16));
        gm_t913bb735pvt5 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
        GM_ST(int64_t, ((uint8_t *)&t9), t8);
        ((uint8_t *)&t9)[8] = 1;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_plural_4m5qson(void *env, gm_node *cx, gm_node *inst, uint64_t a0, gm_b16_8 a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) uint8_t gm_f_report_is_empty_ad76r8j(void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_tree_769ctj2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_size_of_tree_769ctj2 F;
    gm_b16_8 gm_l0; /* path */
    gm_t5j14m8ru132p gm_l1; /* info */
    uint64_t gm_l2; /* total */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* p */
    GM_STACK_CHECK(&F, 405u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcmj20te78r22f, 83, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[13])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t8 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)1ULL)));
        if (!t8) goto ie7;
        {
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t13 = gm_l0;
            gm_trqfvmjtt4sij t14;
            gm_f_subdirs_69pr9gp((void *)&t14, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 0));
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
                        uint64_t t29 = gm_l2;
                        gm_b16_8 t30 = gm_l5;
                        uint64_t t31 = gm_f_size_of_tree_769ctj2((void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        uint64_t r32; bool o33 = __builtin_add_overflow(t29, t31, &r32);
                        if (o33) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 86, 36);
                        uint64_t t34 = r32;
                        gm_l2 = t34;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 86, 5);
me25:;
                }
lc16:;
                {
                    uint64_t t36 = gm_l4;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l4 = t39;
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
        uint64_t t40 = gm_l2;
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
extern __attribute__((aligned(4))) uint8_t gm_f_stale_0kln20t(void * a0, gm_t913bb735pvt5 a1);
inline __attribute__((aligned(4))) void gm_f_subdirs_69pr9gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_subdirs_69pr9gp F;
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 80u);
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
        gm_node wn5 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hlkic73ff07n, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_entries_44o5n9f_ssu37o5((void *)NULL, (&wn5), NULL, t8);
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
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_f_sort_skeree7(t13);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_summary_s3h16om(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_summary_s3h16om F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* dry_run */
    GM_STACK_CHECK(&F, 606u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_report_is_empty_ad76r8j(t3);
        if (!t4) goto ie2;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s7uuha79fcmn5));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(17));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tif3elqk01bdl t8;
        gm_b16_8 t9;
        uint8_t t11 = gm_l1;
        if (!t11) goto ie10;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s5p7bqbmkba97));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(12));
        gm_b16_8 t14 = t13;
        t9 = t14;
        goto ix10;
ie10:;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_skm5ng1ir37l8));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(7));
        gm_b16_8 t17 = t16;
        t9 = t17;
ix10:;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 0));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s0vltqrmpakvk));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(16));
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sh0274v2fqhs4));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(17));
        gm_b16_8 t22 = gm_f_plural_4m5qson((void *)NULL, cx, NULL, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t22);
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s36ugqba56le0));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(17));
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s7tf2msne28g3));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(18));
        gm_b16_8 t27 = gm_f_plural_4m5qson((void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 32, t27);
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s2s2f3gkf4c98));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(12));
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_snj8kbefit27t));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(13));
        gm_b16_8 t32 = gm_f_plural_4m5qson((void *)NULL, cx, NULL, t29, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 48, t32);
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 24));
        gm_b16_8 t35 = gm_f_human_size_dq2d7gs((void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 64, t35);
        gm_b16_8 t36;
        uint8_t t38 = gm_l1;
        if (!t38) goto ie37;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s2c2r23li2if2));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(7));
        gm_b16_8 t41 = t40;
        t36 = t41;
        goto ix37;
ie37:;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_smu6g4sneo92j));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(5));
        gm_b16_8 t44 = t43;
        t36 = t44;
ix37:;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 80, t36);
        gm_tabb710d9mq2e t45;
        gm_f_format_kd24nd8((void *)&t45, (void *)NULL, cx, NULL, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcmj20te78r22f, 39, 12);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        uint8_t t48 = 0;
        F.ret = t47;
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
inline __attribute__((aligned(4))) uint8_t gm_f_tmp_6ua6orh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tmp_6ua6orh F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_f_base_name_6m6nisr((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_srd2ukct5o1qh));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
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
        uint8_t t11 = 0;
        void * t12 = gm_l0;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 0) + 8));
        if (!t13) {
            void * t14 = gm_l0;
            gm_b16_8 t15 = gm_l1;
            gm_t913bb735pvt5 t16 = gm_f_modified_977m57j((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t17 = gm_f_age_aenpk6g(t14, t16);
            uint8_t t18 = (t17 >= ((uint64_t)86400000000000ULL));
            t11 = t18;
        } else t11 = 1;
        if (!t11) goto ie10;
        {
            {
                void * t21 = gm_l0;
                gm_b16_8 t22 = gm_l1;
                uint8_t t23 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint8_t t24 = 0;
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
__attribute__((aligned(4))) void gm_f_trees_9it6grn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_trees_9it6grn F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* repo */
    uint64_t gm_l6; /* left */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* entry */
    gm_b16_8 gm_l10; /* name */
    GM_STACK_CHECK(&F, 979u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_trqfvmjtt4sij t4;
            gm_f_subdirs_69pr9gp((void *)&t4, (void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 16));
                    gm_l5 = t17;
                    {
                        uint8_t t20 = 0;
                        void * t21 = gm_l0;
                        gm_b16_8 t22 = gm_l5;
                        uint8_t t23 = gm_f_tmp_6ua6orh((void *)NULL, cx, NULL, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (!t23) {
                            gm_b16_8 t24 = gm_l5;
                            uint8_t t25 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t24);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            uint8_t t26 = (uint8_t)!t25;
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
                        gm_l6 = ((uint64_t)0ULL);
                    }
                    {
                        {
                            gm_b16_8 t29 = gm_l5;
                            gm_trqfvmjtt4sij t30;
                            gm_f_subdirs_69pr9gp((void *)&t30, (void *)NULL, cx, NULL, t29);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 0));
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
                                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t39 * 16));
                                    gm_l9 = t43;
                                    {
                                        gm_b16_8 t45 = gm_l9;
                                        gm_b16_8 t46 = gm_f_base_name_6m6nisr((void *)NULL, cx, NULL, t45);
                                        if (gm_unw.kind) {
                                            goto gm_ll36;
                                        }
                                        gm_l10 = t46;
                                    }
                                    {
                                        void * t48 = gm_l0;
                                        gm_b16_8 t49 = gm_l9;
                                        uint8_t t50 = gm_f_tmp_6ua6orh((void *)NULL, cx, NULL, t48, t49);
                                        if (gm_unw.kind) {
                                            goto gm_ll36;
                                        }
                                        if (!t50) goto ie47;
                                        {
                                            goto lc32;
                                        }
                                        goto ix47;
ie47:;
ix47:;
                                    }
                                    {
                                        gm_b16_8 t53 = gm_l10;
                                        gm_b16_8 t54;
                                        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sesj9tqqcsas0));
                                        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(5));
                                        uint8_t t55 = gm_f_ends_with_biriu5l(t53, t54);
                                        if (!t55) goto ie52;
                                        {
                                            gm_b16_8 t58 = gm_l5;
                                            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)));
                                            uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            uint64_t r63; bool o64 = __builtin_sub_overflow(t62, ((uint64_t)5ULL), &r63);
                                            if (o64) gm_trap("integer overflow in `-`", gm_srcmj20te78r22f, 155, 50);
                                            uint64_t t65 = r63;
                                            uint64_t t66 = (uintptr_t)t65;
                                            if (t61 > t66 || t66 > (uintptr_t)t59) gm_trap_slice(t61, t66, t59, gm_srcmj20te78r22f, 155, 42);
                                            gm_b16_8 t67;
                                            GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t60 + t61 * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66 - t61));
                                            gm_b16_8 t68 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t58, t67);
                                            if (gm_unw.kind) {
                                                goto gm_ll36;
                                            }
                                            uint8_t t69 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t68);
                                            if (gm_unw.kind) {
                                                goto gm_ll36;
                                            }
                                            uint8_t t70 = (uint8_t)!t69;
                                            if (!t70) goto ie57;
                                            {
                                                {
                                                    void * t73 = gm_l0;
                                                    gm_b16_8 t74 = gm_l9;
                                                    uint8_t t75 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t73, t74);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll36;
                                                    }
                                                }
                                            }
                                            goto ix57;
ie57:;
ix57:;
                                        }
                                        {
                                            goto lc32;
                                        }
                                        goto ix52;
ie52:;
ix52:;
                                    }
                                    {
                                        gm_b16_8 t77 = gm_l9;
                                        uint8_t t78 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t77);
                                        if (gm_unw.kind) {
                                            goto gm_ll36;
                                        }
                                        if (!t78) goto ie76;
                                        void * t79 = gm_l0;
                                        gm_b16_8 t80 = gm_l9;
                                        gm_t913bb735pvt5 t81 = gm_f_last_used_6quhcig((void *)NULL, cx, NULL, t80);
                                        if (gm_unw.kind) {
                                            goto gm_ll36;
                                        }
                                        uint8_t t82 = gm_f_stale_0kln20t(t79, t81);
                                        uint8_t t83 = (uint8_t)!t82;
                                        if (!t83) goto ie76;
                                        {
                                            uint64_t t85 = gm_l6;
                                            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                                            if (o87) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 159, 17);
                                            uint64_t t88 = r86;
                                            gm_l6 = t88;
                                        }
                                        {
                                            goto lc32;
                                        }
                                        goto ix76;
ie76:;
ix76:;
                                    }
                                    {
                                        gm_b16_8 t90 = gm_l9;
                                        uint8_t t91 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t90);
                                        if (gm_unw.kind) {
                                            goto gm_ll36;
                                        }
                                        if (!t91) goto ie89;
                                        {
                                            void * t94 = gm_l0;
                                            gm_b16_8 t95 = gm_l9;
                                            uint8_t t96 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t94, t95);
                                            if (gm_unw.kind) {
                                                goto gm_ll36;
                                            }
                                            if (!t96) goto ie93;
                                            {
                                                void * t98 = gm_l0;
                                                void * t99 = gm_l0;
                                                uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)t99) + 24) + 0));
                                                uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                                                if (o102) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 164, 21);
                                                uint64_t t103 = r101;
                                                GM_ST(uint64_t, ((((uint8_t *)t98) + 24) + 0), t103);
                                            }
                                            {
                                                {
                                                    void * t105 = gm_l0;
                                                    gm_b16_8 t106 = gm_l9;
                                                    gm_b16_8 t107 = gm_f_stamp_of_li2ggn0((void *)NULL, cx, NULL, t106);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll36;
                                                    }
                                                    uint8_t t108 = gm_f_delete_jp89bug((void *)NULL, cx, NULL, t105, t107);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll36;
                                                    }
                                                }
                                            }
                                            goto ix93;
ie93:;
                                            {
                                                uint64_t t110 = gm_l6;
                                                uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                                                if (o112) gm_trap("integer overflow in `+`", gm_srcmj20te78r22f, 167, 21);
                                                uint64_t t113 = r111;
                                                gm_l6 = t113;
                                            }
ix93:;
                                        }
                                        goto ix89;
ie89:;
ix89:;
                                    }
                                    goto me41;
mn42:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 150, 9);
me41:;
                                }
lc32:;
                                {
                                    uint64_t t115 = gm_l8;
                                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                                    uint64_t t118 = r116;
                                    gm_l8 = t118;
                                }
                            }
gm_ll36:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
                            }
                            goto gm_ll10;
lb32:;
                        }
                    }
                    {
                        uint64_t t120 = gm_l6;
                        uint8_t t121 = ((t120 == ((uint64_t)0ULL)));
                        if (!t121) goto ie119;
                        void * t122 = gm_l0;
                        uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t122) + 0) + 9));
                        uint8_t t124 = (uint8_t)!t123;
                        if (!t124) goto ie119;
                        {
                            {
                                gm_b16_8 t127 = gm_l5;
                                gm_node *pn128 = gm_find(cx, 2126215853);
                                if (!pn128) gm_trap("no handler installed for effect `Fs`", gm_srcmj20te78r22f, 172, 53);
                                gm_ttln0d0hd70en t129;
                                t129 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn128->clauses[9])(pn128->env, pn128->hctx, pn128, t127);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                            }
                        }
                        goto ix119;
ie119:;
ix119:;
                    }
                    {
                        uint64_t t131 = gm_l6;
                        uint8_t t132 = (t131 > ((uint64_t)0ULL));
                        if (!t132) goto ie130;
                        {
                            void * t134 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t135 = gm_l5;
                            gm_b16_8 t136 = gm_f_base_name_6m6nisr((void *)NULL, cx, NULL, t135);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_tftb1odc739t5 t137 = gm_f_push_3ef746b(t134, t136);
                            if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srcmj20te78r22f, 173, 23);
                            gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
                        }
                        goto ix130;
ie130:;
ix130:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 147, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t140 = gm_l4;
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    uint64_t t143 = r141;
                    gm_l4 = t143;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_with_extension_jpfmsok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_with_extension_jpfmsok F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* ext */
    gm_tf5srh4mp4b3i gm_l2; /* dot */
    gm_tf5srh4mp4b3i gm_l3; /* slash */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t4 = gm_f_rfind_ai5s2js(t2, t3);
        gm_l2 = t4;
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t7 = gm_f_rfind_ai5s2js(t5, t6);
        gm_l3 = t7;
    }
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie8;
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l4 = t9;
        {
            uint8_t t12 = 0;
            uint8_t t13;
            if (((((uint8_t *)&gm_l3))[8] != 0)) goto mn15;
            t13 = ((uint8_t)1);
            goto me14;
mn15:;
            t13 = ((uint8_t)0);
            goto me14;
mn16:;
            gm_trap("no match arm matched the scrutinee", gm_srcmj20te78r22f, 217, 12);
me14:;
            if (!t13) {
                uint64_t t17 = gm_l4;
                gm_tf5srh4mp4b3i t18 = gm_l3;
                if (!((((uint8_t *)&t18))[8] != 0)) gm_trap("unwrap of null", gm_srcmj20te78r22f, 217, 33);
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                uint8_t t20 = (t17 > t19);
                t12 = t20;
            } else t12 = 1;
            if (!t12) goto ie11;
            {
                gm_t6j6smj7qoctu t22;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t24 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t26 = gm_l4;
                uint64_t t27 = (uintptr_t)t26;
                if (t25 > t27 || t27 > (uintptr_t)t23) gm_trap_slice(t25, t27, t23, gm_srcmj20te78r22f, 217, 69);
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t24 + t25 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t25));
                GM_ST(gm_b16_8, (uint8_t *)&t22 + 0, t28);
                gm_b16_8 t29 = gm_l1;
                GM_ST(gm_b16_8, (uint8_t *)&t22 + 16, t29);
                gm_tabb710d9mq2e t30;
                gm_f_format_ahgk48j((void *)&t30, (void *)NULL, cx, NULL, &t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcmj20te78r22f, 217, 49);
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                uint8_t t33 = 0;
                F.ret = t32;
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
        gm_t6j6smj7qoctu t34;
        gm_b16_8 t35 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
        gm_b16_8 t36 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t36);
        gm_tabb710d9mq2e t37;
        gm_f_format_ahgk48j((void *)&t37, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcmj20te78r22f, 219, 12);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        uint8_t t40 = 0;
        F.ret = t39;
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
