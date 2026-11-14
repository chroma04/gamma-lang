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
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint16_t f0; uint16_t f1; } gm_tuqvtbj0p3v9c;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_tuqvtbj0p3v9c) == 4 && _Alignof(gm_tuqvtbj0p3v9c) == 2 && offsetof(gm_tuqvtbj0p3v9c, f0) == 0 && offsetof(gm_tuqvtbj0p3v9c, f1) == 2, "layout of `(u16, u16)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; uint64_t f6; uint8_t f7; uint8_t f8; uint8_t pe[6]; } gm_t4j35k4k4pvmb;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tiilmfd16v3fc v1; } f1; } gm_t716e5hdn64ba;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_t0mglksrkfa5e f0; uint8_t f1; uint8_t pe[7]; } gm_tnpj2jtv1r8pa;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tbgd14esf0jvi v1; } f1; } gm_ts5t31j0bugq1;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4j35k4k4pvmb) == 152 && _Alignof(gm_t4j35k4k4pvmb) == 8 && offsetof(gm_t4j35k4k4pvmb, f0) == 0 && offsetof(gm_t4j35k4k4pvmb, f1) == 16 && offsetof(gm_t4j35k4k4pvmb, f2) == 32 && offsetof(gm_t4j35k4k4pvmb, f3) == 48 && offsetof(gm_t4j35k4k4pvmb, f4) == 56 && offsetof(gm_t4j35k4k4pvmb, f5) == 96 && offsetof(gm_t4j35k4k4pvmb, f6) == 136 && offsetof(gm_t4j35k4k4pvmb, f7) == 144 && offsetof(gm_t4j35k4k4pvmb, f8) == 145, "layout of `ArgBinder`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t716e5hdn64ba) == 32 && _Alignof(gm_t716e5hdn64ba) == 8 && offsetof(gm_t716e5hdn64ba, f0) == 0 && offsetof(gm_t716e5hdn64ba, f1) == 8, "layout of `EffectNamed`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t716e5hdn64ba f0; uint8_t f1; uint8_t pe[7]; } gm_tlbrdhah49ci5;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnpj2jtv1r8pa) == 48 && _Alignof(gm_tnpj2jtv1r8pa) == 8 && offsetof(gm_tnpj2jtv1r8pa, f0) == 0 && offsetof(gm_tnpj2jtv1r8pa, f1) == 40, "layout of `?FieldDef`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tlbrdhah49ci5) == 40 && _Alignof(gm_tlbrdhah49ci5) == 8 && offsetof(gm_tlbrdhah49ci5, f0) == 0 && offsetof(gm_tlbrdhah49ci5, f1) == 32, "layout of `?EffectNamed`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_arg_value_gavegdu {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* lit */
    gm_trqfvmjtt4sij l4; /* pre */
    void * ret;
};
struct gm_fr_argument_order_jhg33u8 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* order */
    gm_t7te2tdpebbt4 l6; /* a */
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_assert__dllohqd {
    gm_frame h;
    gm_t7te2tdpebbt4 l5; /* a */
    gm_tc3oht4ksth9d l6; /* c */
    gm_tc3oht4ksth9d l8; /* x */
    gm_trqfvmjtt4sij l10; /* both */
    void * ret;
};
struct gm_fr_bind_args_dnih8ih {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* named_here */
    gm_t7te2tdpebbt4 l9; /* a */
    gm_trqfvmjtt4sij l11; /* positional */
    gm_t5v0c8ia6a5mo l15; /* p */
    gm_ts5t31j0bugq1 l18; /* packed */
    gm_trqfvmjtt4sij l22; /* out */
    gm_t4j35k4k4pvmb l28; /* b */
    gm_trqfvmjtt4sij l31; /* stmts */
    gm_trqfvmjtt4sij l32; /* written */
    gm_t7te2tdpebbt4 l35; /* a */
    gm_tc3oht4ksth9d l36; /* e */
    gm_tc3oht4ksth9d l40; /* e */
    gm_tc3oht4ksth9d l43; /* e */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_bind_spread_parts_lmknlmc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_call_decl_sj6f05u {
    gm_frame h;
    void * ret;
};
struct gm_fr_call_fn_2prsqv9 {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_trqfvmjtt4sij l10; /* vals */
    gm_t7te2tdpebbt4 l13; /* a */
    void * ret;
};
struct gm_fr_call_l10ebge {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* a */
    gm_t5vbkdq81qfs7 l8; /* r */
    gm_t0ialelq3ql1k l9; /* v */
    gm_t930q5l38hdap l22; /* b */
    void * ret;
};
struct gm_fr_call_value_t2nv21i {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* f */
    gm_tddtjfggdat0d l6; /* f */
    gm_tddtjfggdat0d l7; /* sig */
    gm_trqfvmjtt4sij l8; /* params */
    gm_trqfvmjtt4sij l14; /* stmts */
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_construct_optional_cffo1ol {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* x */
    void * ret;
};
struct gm_fr_conversion_2qjq323 {
    gm_frame h;
    gm_tpautr4br7h3l l6; /* def */
    gm_tc3oht4ksth9d l10; /* x */
    gm_tnpj2jtv1r8pa l11; /* found */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t0mglksrkfa5e l15; /* f */
    gm_trqfvmjtt4sij l17; /* one */
    gm_tc3oht4ksth9d l26; /* x */
    void * ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_moftl2f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
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
struct gm_fr_expect_eq_iu4ojmi {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* a */
    gm_tc3oht4ksth9d l4; /* b */
    gm_trqfvmjtt4sij l15; /* one */
    gm_trqfvmjtt4sij l17; /* pieces */
    gm_trqfvmjtt4sij l18; /* specs */
    gm_trqfvmjtt4sij l19; /* vals */
    gm_trqfvmjtt4sij l23; /* stop_args */
    gm_trqfvmjtt4sij l25; /* fail_stmts */
    gm_trqfvmjtt4sij l27; /* cond */
    gm_trqfvmjtt4sij l29; /* stmts */
    void * ret;
};
struct gm_fr_fill_defaults_3fq5ut6 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_const_ref_arg_s0f37he {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_literal_tree_om58rum {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_site_8sr7vam {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qtk22f3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_next_positional_t5fspc1 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_one_expr_q6kn0hn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_param_index_78k4iut {
    gm_frame h;
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_print_bhebp9d {
    gm_frame h;
    gm_t7te2tdpebbt4 l6; /* a */
    gm_tc3oht4ksth9d l7; /* template */
    gm_trqfvmjtt4sij l10; /* one */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_recursive_without_ret_48l31rm {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_tir_literal_oh960hv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trap_t21p26s {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* m */
    void * ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_variadic_arg_dfdnlo3 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_argument_order_jhg33u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_assert__dllohqd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_spread_parts_lmknlmc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_call_decl_sj6f05u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_call_fn_2prsqv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_conversion_2qjq323(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_equalize_ni1r5ra(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expect_eq_iu4ojmi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fill_defaults_3fq5ut6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_format_10ctmcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_458q1b3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8fudcr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9ijbt7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b4oiuht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hqkpc5s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ivju6qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jaikcua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kub2tep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mq9dnpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t3u5vlm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_literal_tree_om58rum(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_positional_t5fspc1(void * a0);
__attribute__((aligned(4))) void gm_f_note_fixed_by_ev84og7(void * a0, void * a1, gm_csguankko4lmhd a2, gm_cseahvg8v7p0vs a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_print_bhebp9d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_tir_literal_oh960hv(void * a0);
__attribute__((aligned(4))) void * gm_f_trap_t21p26s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_untyped_literal_pfu1ft4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_variadic_arg_dfdnlo3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_krbc9rueul4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1ekjot5tdpje[67];
extern const uint8_t gm_s2r7keqsravo0[6];
extern const uint8_t gm_s3231a310mran[10];
extern const uint8_t gm_s4g2k7ups3ui6[6];
extern const uint8_t gm_s8vts15kskc6n[11];
extern const uint8_t gm_s98peh781o3et[51];
extern const uint8_t gm_s9ur7sj9r0g63[7];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sbs1lj6q8rfiu[44];
extern const uint8_t gm_scgahod5hue9b[56];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgnvdmoebn4i7[16];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sijukd7vsocnt[68];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_sj4n6nvsgq2jn[43];
extern const uint8_t gm_smadkict0htlf[38];
extern const uint8_t gm_sn937bfucthvg[4];
extern const uint8_t gm_so4gnucc3fl4v[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srch6h8rdicu443[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgp1m0q2ut8m[12];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_ss6g2koqn63bf[5];
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
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1));
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
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_spec_9ntjq2s F;
    GM_STACK_CHECK_SHALLOW(&F, 10u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 5, 0, 3);
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 21, 0, 3);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_t9805qtonbsag t2;
    memset(&t2, 0, sizeof t2);
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 0, ((uint8_t)32ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 1, t1);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 2, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 3, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 4, ((uint8_t)0));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 8, ((uint32_t)0ULL));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 12, t2);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 20, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_moftl2f F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_hoists_13k2a2s(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        void * t10 = gm_f_prefix_operand_3n5jchh(t9);
        void * t11 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (!t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_site_8sr7vam F;
    void * gm_l0; /* p */
    void * gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        void * t8 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t9 != ((uint8_t)49ULL)) goto ie7;
        gm_tet9guhev5r4u t10 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t8) + 8) + 8));
        F.l2 = t10;
        {
            uint8_t t12 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 15) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_sa9e77lq9q04p, 15) == 0));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix7;
ie7:;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix7:;
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qtk22f3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_moftl2f();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)5ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_expr_q6kn0hn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcg82mujgottmf, 140, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_recursive_without_ret_48l31rm F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l2;
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_c8qq9n9((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcl43af23eher3, 280, 20);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_b0_1 t9 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t8);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 133u);
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
        gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_arg_value_gavegdu F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l5; /* v */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 721u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t0mfir12leg2g t6;
        gm_f_iterator_arg_r0if28f((void *)&t6, (void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t6))[88] != 0)) goto ie2;
        F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t6) + 0));
        {
            void * t8 = gm_l0;
            gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 1160));
            F.l4 = t9;
        }
        {
            void * t10 = gm_l0;
            uint8_t *t11 = (((uint8_t *)t10) + 1160);
            gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t11), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t12 = gm_l0;
            void * t13 = (void *)((uint8_t *)&F.l3);
            gm_t88rcucg1b7uk t14 = gm_l2;
            void * t15 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t15;
        }
        {
            void * t16 = gm_l0;
            gm_trqfvmjtt4sij t17 = F.l4;
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t16) + 1160), t17);
        }
        {
            void * t18 = gm_l5;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l1;
        uint8_t t22 = ((GM_LD(uint8_t, (((uint8_t *)t21) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t22) goto ie20;
        {
            void * t25 = gm_l0;
            gm_t88rcucg1b7uk t26 = gm_l2;
            uint8_t t27 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28 = (uint8_t)!t27;
            if (!t28) goto ie24;
            {
                void * t30 = gm_l0;
                void * t31 = gm_l1;
                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
                void * t33 = gm_l0;
                void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
                gm_t88rcucg1b7uk t35 = gm_l2;
                gm_b16_8 t36 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t37 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t30, t32, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix24;
ie24:;
ix24:;
        }
        {
            gm_t88rcucg1b7uk t38 = gm_l2;
            gm_teti411eujvf4 t39;
            memset(&t39, 0, sizeof t39);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)8ULL));
            void * t40 = gm_l1;
            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
            void * t42 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t38, &t39, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t44 = gm_l0;
        uint8_t *t45 = (((uint8_t *)t44) + 472);
        void * t46 = gm_l0;
        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
        void * t48 = gm_l1;
        gm_t88rcucg1b7uk t49 = gm_l2;
        uint8_t t50 = gm_f_is_const_ref_arg_s0f37he((void *)NULL, cx, NULL, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, t45, t50);
    }
    {
        void * t51 = gm_l0;
        void * t52 = gm_l1;
        gm_t88rcucg1b7uk t53 = gm_l2;
        gm_tkn42u7na3t81 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t54), t53);
        ((uint8_t *)&t54)[4] = 1;
        void * t55 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t51, t52, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t55;
    }
    {
        void * t56 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t56) + 472), ((uint8_t)0));
    }
    {
        void * t58 = gm_l0;
        void * t59 = gm_l1;
        void * t60 = gm_l6;
        gm_t88rcucg1b7uk t61 = gm_l2;
        uint8_t t62 = gm_f_iterator_call_as_value_00i5mch((void *)NULL, cx, NULL, t58, t59, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t62) goto ie57;
        {
            void * t64 = gm_l0;
            void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
            void * t66 = gm_l1;
            gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 72));
            void * t68 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t65, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        void * t70 = gm_l0;
        void * t71 = gm_l6;
        gm_t88rcucg1b7uk t72 = gm_l2;
        void * t73 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t70, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t73;
    }
    {
        void * t74 = gm_l0;
        void * t75 = gm_l6;
        gm_t88rcucg1b7uk t76 = gm_l2;
        void * t77 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t74, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t78 = 0;
        F.ret = t77;
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
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_argument_order_jhg33u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_argument_order_jhg33u8 F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* args */
    uint8_t gm_l2; /* has_spread */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* spreading */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    GM_STACK_CHECK(&F, 615u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t7te2tdpebbt4 t15 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 144));
                    F.l6 = t15;
                    {
                        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        switch (t22) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; default: goto ma21; }
ma18:;
                        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t24 != ((uint8_t)0ULL)) goto mn23;
                        void * t25 = (void *)((uint8_t *)&F.l3);
                        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        gm_tftb1odc739t5 t27 = gm_f_push_ucgmlo2(t25, t26);
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srch6h8rdicu443, 289, 31);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        goto me17;
mn23:;
ma19:;
                        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t30 != ((uint8_t)1ULL)) goto mn29;
                        gm_t08olq97rscn3 t31 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l6) + 0) + 8) + 0));
                        gm_l7 = t31;
                        {
                            gm_b16_8 t34 = gm_l0;
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                            gm_tf5srh4mp4b3i t36 = gm_f_param_index_78k4iut(t34, t35);
                            if (!((((uint8_t *)&t36))[8] != 0)) goto ie33;
                            uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&t36));
                            gm_l8 = t37;
                            {
                                void * t39 = (void *)((uint8_t *)&F.l3);
                                uint64_t t40 = gm_l8;
                                gm_tftb1odc739t5 t41 = gm_f_push_ucgmlo2(t39, t40);
                                if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srch6h8rdicu443, 291, 55);
                                gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                            }
                            goto ix33;
ie33:;
ix33:;
                        }
                        goto me17;
mn29:;
ma20:;
                        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t44 != ((uint8_t)2ULL)) goto mn43;
                        {
                            gm_l2 = ((uint8_t)1);
                        }
                        goto me17;
mn43:;
ma21:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 288, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 287, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t47 = gm_l5;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l5 = t50;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t51 = gm_l2;
        gm_l9 = t51;
    }
    {
        {
            gm_l11 = ((uint64_t)1ULL);
        }
        {
            uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            uint64_t t54 = (t53 > ((uint64_t)1ULL) ? t53 : ((uint64_t)1ULL));
            gm_l10 = t54;
        }
        {
            for (;;) {
                uint64_t t56 = gm_l11;
                uint64_t t57 = gm_l10;
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l11;
                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t61));
                    gm_l12 = t64;
                    {
                        uint64_t t67 = gm_l12;
                        uint64_t r68; bool o69 = __builtin_sub_overflow(t67, ((uint64_t)1ULL), &r68);
                        if (o69) gm_trap("integer overflow in `-`", gm_srch6h8rdicu443, 298, 24);
                        uint64_t t70 = r68;
                        uint64_t t71 = (uintptr_t)t70;
                        uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srch6h8rdicu443, 298, 12);
                        uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t71 * 8));
                        uint64_t t74 = gm_l12;
                        uint64_t t75 = (uintptr_t)t74;
                        uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srch6h8rdicu443, 298, 33);
                        uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t75 * 8));
                        uint8_t t78 = (t73 > t77);
                        if (!t78) goto ie66;
                        {
                            gm_l9 = ((uint8_t)1);
                        }
                        goto ix66;
ie66:;
ix66:;
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 297, 5);
me62:;
                }
lc55:;
                {
                    uint64_t t81 = gm_l11;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l11 = t84;
                }
            }
lb55:;
        }
    }
    {
        uint8_t t85 = gm_l2;
        uint8_t t86 = gm_l9;
        gm_tqs5p4n2tq19v t87;
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), t85);
        GM_ST(uint8_t, ((uint8_t *)&t87 + 1), t86);
        uint8_t t88 = 0;
        F.ret = t87;
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
__attribute__((aligned(4))) void * gm_f_assert__dllohqd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_assert__dllohqd F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l7; /* cond */
    void * gm_l9; /* m */
    GM_STACK_CHECK(&F, 1344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    gm_t7te2tdpebbt4 t15 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 144));
                    F.l5 = t15;
                    {
                        uint8_t t18 = 0;
                        uint8_t t19 = 0;
                        uint8_t t20;
                        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 0) + 0));
                        if (t23 != ((uint8_t)0ULL)) goto mn22;
                        t20 = ((uint8_t)0);
                        goto me21;
mn22:;
                        t20 = ((uint8_t)1);
                        goto me21;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 527, 12);
me21:;
                        if (!t20) {
                            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint8_t t26 = (t25 > ((uint64_t)2ULL));
                            t19 = t26;
                        } else t19 = 1;
                        if (!t19) {
                            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                            t18 = t28;
                        } else t18 = 1;
                        if (!t18) goto ie17;
                        {
                            void * t30 = gm_l0;
                            gm_tl874t2atnlh3 t31 = gm_l2;
                            gm_b16_8 t32;
                            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s98peh781o3et));
                            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(50));
                            gm_b0_1 t33 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t30, t31, t32);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t34 = gm_l0;
                            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                            gm_tl874t2atnlh3 t36 = gm_l2;
                            void * t37 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint8_t t38 = 0;
                            F.ret = t37;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 526, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
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
    {
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
        if (!t46) goto ie44;
        {
            void * t48 = gm_l0;
            gm_tl874t2atnlh3 t49 = gm_l2;
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s98peh781o3et));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(50));
            gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t48, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            gm_tl874t2atnlh3 t54 = gm_l2;
            void * t55 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srch6h8rdicu443, 536, 23);
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t58 * 144) + 0) + 0));
        if (t60 != ((uint8_t)0ULL)) goto ie57;
        gm_tc3oht4ksth9d t61 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t58 * 144) + 0) + 8));
        F.l6 = t61;
        {
            void * t63 = gm_l0;
            void * t64 = (void *)((uint8_t *)&F.l6);
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            gm_t88rcucg1b7uk t68 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t69), t68);
            ((uint8_t *)&t69)[4] = 1;
            void * t70 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t63, t64, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t70;
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l7;
            void * t73 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t73;
        }
        {
            void * t74;
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t77 = ((t76 == ((uint64_t)2ULL)));
            if (!t77) goto ie75;
            void * t79;
            uint64_t t81 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_srch6h8rdicu443, 540, 27);
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t81 * 144) + 0) + 0));
            if (t83 != ((uint8_t)0ULL)) goto ie80;
            gm_tc3oht4ksth9d t84 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t81 * 144) + 0) + 8));
            F.l8 = t84;
            void * t86 = gm_l0;
            void * t87 = gm_l0;
            void * t88 = (void *)((uint8_t *)&F.l8);
            void * t89 = gm_l0;
            void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
            void * t91 = (void *)(((uint8_t *)t90) + 8);
            gm_t88rcucg1b7uk t92 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t93;
            GM_ST(uint8_t, (uint8_t *)&t93 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t93 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93), t92);
            ((uint8_t *)&t93)[4] = 1;
            void * t94 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t87, t88, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t95 = gm_l0;
            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
            void * t97 = (void *)(((uint8_t *)t96) + 8);
            gm_t88rcucg1b7uk t98 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t99 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t86, t94, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t100 = t99;
            t79 = t100;
            goto ix80;
ie80:;
            void * t102 = gm_l0;
            void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
            gm_tl874t2atnlh3 t104 = gm_l2;
            void * t105 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t106 = 0;
            F.ret = t105;
            goto gm_fret;
            t79 = ((void *)0);
ix80:;
            void * t107 = t79;
            t74 = t107;
            goto ix75;
ie75:;
            void * t109 = gm_l0;
            void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
            void * t111 = (void *)(((uint8_t *)t110) + 8);
            gm_t88rcucg1b7uk t112 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_spi8n2atkg603));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(16));
            gm_teti411eujvf4 t114;
            memset(&t114, 0, sizeof t114);
            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
            gm_tl874t2atnlh3 t115 = gm_l2;
            void * t116 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t112, &t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t117 = t116;
            t74 = t117;
ix75:;
            gm_l9 = t74;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t118 = (void *)((uint8_t *)&F.l10);
            void * t119 = gm_l7;
            gm_tftb1odc739t5 t120 = gm_f_push_d943vgp_2(t118, &(*(gm_t69m56meqefs8 *)((uint8_t *)t119)));
            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srch6h8rdicu443, 546, 5);
            gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
        }
        {
            void * t122 = (void *)((uint8_t *)&F.l10);
            void * t123 = gm_l9;
            gm_tftb1odc739t5 t124 = gm_f_push_d943vgp_2(t122, &(*(gm_t69m56meqefs8 *)((uint8_t *)t123)));
            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_srch6h8rdicu443, 547, 5);
            gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
        }
        {
            void * t126 = gm_l0;
            void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
            void * t128 = (void *)(((uint8_t *)t127) + 8);
            gm_t88rcucg1b7uk t129 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt8o5kebou62r t130;
            memset((uint8_t *)&t130 + 12, 0, 4);
            gm_tvdg94bk7aa22 t131;
            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)61ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t130 + 0, t131);
            gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t130 + 16, t132);
            gm_teti411eujvf4 t133;
            GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t133 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t133 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t133 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t133 + 8), t130);
            gm_tl874t2atnlh3 t134 = gm_l2;
            void * t135 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t129, &t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t136 = 0;
            F.ret = t135;
            goto gm_fret;
        }
        goto ix57;
ie57:;
        {
            void * t138 = gm_l0;
            void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
            gm_tl874t2atnlh3 t140 = gm_l2;
            void * t141 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t142 = 0;
            F.ret = t141;
            goto gm_fret;
        }
ix57:;
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
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5) {
    struct gm_fr_bind_args_dnih8ih F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* params */
    uint64_t gm_l2; /* callee_module */
    gm_b16_8 gm_l3; /* args0 */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* what */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t08olq97rscn3 gm_l10; /* n */
    uint64_t gm_l12; /* max */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint8_t gm_l16; /* ok */
    gm_b16_8 gm_l17; /* args */
    gm_b16_8 gm_l19; /* a */
    gm_tl874t2atnlh3 gm_l20; /* sp */
    gm_b16_8 gm_l21; /* msg */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_tqs5p4n2tq19v gm_l25; /* order */
    uint8_t gm_l26; /* has_spread */
    uint8_t gm_l27; /* spreading */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l37; /* i */
    void * gm_l38; /* v */
    gm_t08olq97rscn3 gm_l39; /* n */
    uint64_t gm_l41; /* i */
    void * gm_l42; /* v */
    gm_b16_8 gm_l44; /* parts */
    GM_STACK_CHECK(&F, 3231u);
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
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
            gm_l7 = t3;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l8;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l8;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t7te2tdpebbt4 t15 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t11 * 144));
                    F.l9 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 0) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        gm_t08olq97rscn3 t19 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l9) + 0) + 8) + 0));
                        gm_l10 = t19;
                        {
                            void * t21 = (void *)((uint8_t *)&F.l6);
                            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
                            gm_tftb1odc739t5 t23 = gm_f_push_3ef746b(t21, t22);
                            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srch6h8rdicu443, 207, 37);
                            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 206, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l8;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l8 = t29;
                }
            }
lb4:;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l12 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t31 = gm_l1;
            gm_l13 = t31;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t33 = gm_l14;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t35 = (t33 < t34);
                if (!t35) goto lb32;
                {
                    uint64_t t38 = gm_l14;
                    uint64_t t39 = (uintptr_t)t38;
                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_t5v0c8ia6a5mo t43 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t39 * 56));
                    F.l15 = t43;
                    {
                        uint8_t t45 = 0;
                        void * t46 = (void *)((uint8_t *)&F.l15);
                        uint8_t t47 = gm_f_is_site_8sr7vam(t46);
                        uint8_t t48 = (uint8_t)!t47;
                        if (t48) {
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            uint8_t t51 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t50) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t50))) + i * 16, ((uint8_t *)&t49))) { t51 = 1; break; }
                            uint8_t t52 = (uint8_t)(!t51);
                            t45 = t52;
                        } else t45 = 0;
                        gm_l16 = t45;
                    }
                    {
                        void * t53 = (void *)((uint8_t *)&F.l11);
                        uint8_t t54 = gm_l16;
                        gm_tftb1odc739t5 t55 = gm_f_push_vd1290p(t53, t54);
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srch6h8rdicu443, 213, 9);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                    }
                    {
                        uint8_t t58 = gm_l16;
                        if (!t58) goto ie57;
                        {
                            uint64_t t60 = gm_l12;
                            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                            if (o62) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 214, 17);
                            uint64_t t63 = r61;
                            gm_l12 = t63;
                        }
                        goto ix57;
ie57:;
ix57:;
                    }
                    goto me41;
mn42:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 211, 5);
me41:;
                }
lc32:;
                {
                    uint64_t t65 = gm_l14;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l14 = t68;
                }
            }
lb32:;
        }
    }
    {
        gm_b16_8 t69 = gm_l3;
        gm_l17 = t69;
    }
    {
        gm_b16_8 t71 = gm_l1;
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        gm_b16_8 t74 = gm_l3;
        gm_tl874t2atnlh3 t75 = gm_l4;
        gm_tck5nas0nf5v8 t76;
        gm_f_pack_rest_args_cknfrfl((void *)&t76, (void *)NULL, cx, NULL, t71, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t76))[40] != 0)) goto ie70;
        F.l18 = (*(gm_ts5t31j0bugq1 *)(((uint8_t *)&t76) + 0));
        {
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
            if (t80 != ((uint8_t)0ULL)) goto mn79;
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 8));
            gm_l19 = t81;
            {
                gm_b16_8 t83 = gm_l19;
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                gm_l17 = t84;
            }
            goto me78;
mn79:;
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
            if (t86 != ((uint8_t)1ULL)) goto mn85;
            gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l18) + 8) + 0));
            gm_l20 = t87;
            gm_b16_8 t88 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 8) + 16));
            gm_l21 = t88;
            {
                void * t90 = gm_l0;
                gm_tl874t2atnlh3 t91 = gm_l20;
                gm_b16_8 t92 = gm_l21;
                gm_b0_1 t93 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t90, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l22, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t95 = gm_l1;
                    gm_l23 = t95;
                }
                {
                    gm_l24 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t97 = gm_l24;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint8_t t99 = (t97 < t98);
                        if (!t99) goto lb96;
                        {
                            uint64_t t102 = gm_l24;
                            uint64_t t103 = (uintptr_t)t102;
                            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            {
                                void * t108 = (void *)((uint8_t *)&F.l22);
                                void * t109 = gm_l0;
                                void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
                                gm_tl874t2atnlh3 t111 = gm_l4;
                                void * t112 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t110, t111);
                                if (gm_unw.kind) {
                                    goto gm_ll100;
                                }
                                gm_tftb1odc739t5 t113 = gm_f_push_d943vgp_2(t108, &(*(gm_t69m56meqefs8 *)((uint8_t *)t112)));
                                if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srch6h8rdicu443, 223, 35);
                                gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                            }
                            goto me105;
mn106:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 223, 17);
me105:;
                        }
lc96:;
                        {
                            uint64_t t116 = gm_l24;
                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                            uint64_t t119 = r117;
                            gm_l24 = t119;
                        }
                    }
gm_ll100:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb96; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc96; }
                    }
                    goto gm_lad;
lb96:;
                }
            }
            {
                gm_t6j6smj7qoctu t120;
                gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
                gm_b16_8 t122 = gm_f_empty_slice_vj3ckdj_2();
                GM_ST(gm_b16_8, (uint8_t *)&t120 + 16, t122);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
                ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
                GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t120);
                goto gm_fdone;
            }
            goto me78;
mn85:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 218, 9);
me78:;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_b16_8 t123 = gm_l1;
        gm_b16_8 t124 = gm_l17;
        gm_tqs5p4n2tq19v t125 = gm_f_argument_order_jhg33u8((void *)NULL, cx, NULL, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t125;
    }
    {
        uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
        gm_l26 = t126;
    }
    {
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 1));
        gm_l27 = t127;
    }
    {
        memset((uint8_t *)&F.l28 + 146, 0, 6);
        gm_b16_8 t128 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l28 + 0, t128);
        gm_b16_8 t129 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&F.l28 + 16, t129);
        gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
        GM_ST(gm_b16_8, (uint8_t *)&F.l28 + 32, t131);
        uint64_t t132 = gm_l12;
        GM_ST(uint64_t, (uint8_t *)&F.l28 + 48, t132);
        gm_f_list_pmp06kq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l28 + 56)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l28 + 96)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&F.l28 + 136, ((uint64_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l28 + 144, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l28 + 145, ((uint8_t)0));
    }
    {
        {
            gm_b16_8 t134 = gm_l1;
            gm_l29 = t134;
        }
        {
            gm_l30 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t136 = gm_l30;
                uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                uint8_t t138 = (t136 < t137);
                if (!t138) goto lb135;
                {
                    uint64_t t141 = gm_l30;
                    uint64_t t142 = (uintptr_t)t141;
                    uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    {
                        void * t147 = (void *)(((uint8_t *)&F.l28) + 56);
                        gm_tftb1odc739t5 t148 = gm_f_push_r2k6rjf(t147, ((void *)0));
                        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_srch6h8rdicu443, 233, 9);
                        gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
                    }
                    {
                        void * t150 = (void *)(((uint8_t *)&F.l28) + 96);
                        gm_tftb1odc739t5 t151 = gm_f_push_vd1290p(t150, ((uint8_t)0));
                        if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srch6h8rdicu443, 234, 9);
                        gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
                    }
                    goto me144;
mn145:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 232, 5);
me144:;
                }
lc135:;
                {
                    uint64_t t154 = gm_l30;
                    uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                    uint64_t t157 = r155;
                    gm_l30 = t157;
                }
            }
lb135:;
        }
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t159 = gm_l17;
            gm_l33 = t159;
        }
        {
            gm_l34 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t161 = gm_l34;
                uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                uint8_t t163 = (t161 < t162);
                if (!t163) goto lb160;
                {
                    uint64_t t166 = gm_l34;
                    uint64_t t167 = (uintptr_t)t166;
                    uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    gm_t7te2tdpebbt4 t171 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t167 * 144));
                    F.l35 = t171;
                    {
                        uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        switch (t178) { case ((uint8_t)0ULL): goto ma174; case ((uint8_t)1ULL): goto ma175; case ((uint8_t)2ULL): goto ma176; default: goto ma177; }
ma174:;
                        uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        if (t180 != ((uint8_t)0ULL)) goto mn179;
                        gm_tc3oht4ksth9d t181 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l35) + 0) + 8));
                        F.l36 = t181;
                        {
                            uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&F.l28) + 144));
                            if (!t184) goto ie183;
                            {
                                void * t186 = gm_l0;
                                gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                                gm_b16_8 t188;
                                GM_ST(void *, (uint8_t *)&t188, (void *)(gm_s1ekjot5tdpje));
                                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(66));
                                gm_b0_1 t189 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t186, t187, t188);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                            }
                            goto ix183;
ie183:;
ix183:;
                        }
                        {
                            void * t190 = (void *)((uint8_t *)&F.l28);
                            gm_tf5srh4mp4b3i t191 = gm_f_next_positional_t5fspc1(t190);
                            uint64_t t192;
                            if (((((uint8_t *)&t191))[8] != 0)) {
                                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&t191) + 0));
                                t192 = t193;
                            } else {
                                {
                                    void * t195 = gm_l0;
                                    gm_tl874t2atnlh3 t196 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                                    gm_t3877pk7lv748 t197;
                                    gm_b16_8 t198 = gm_l5;
                                    GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t198);
                                    uint64_t t199 = gm_l12;
                                    GM_ST(uint64_t, (uint8_t *)&t197 + 16, t199);
                                    gm_tabb710d9mq2e t200;
                                    gm_f_format_b4oiuht((void *)&t200, (void *)NULL, cx, NULL, &t197);
                                    if (gm_unw.kind) {
                                        goto gm_ll164;
                                    }
                                    if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 8, gm_srch6h8rdicu443, 243, 38);
                                    gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&t200) + 8));
                                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                                    gm_b0_1 t203 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t195, t196, t202);
                                    if (gm_unw.kind) {
                                        goto gm_ll164;
                                    }
                                }
                                {
                                    {
                                        void * t205 = gm_l0;
                                        void * t206 = (void *)((uint8_t *)&F.l36);
                                        gm_tkn42u7na3t81 t207;
                                        memset(&t207, 0, sizeof t207);
                                        void * t208 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t205, t206, t207);
                                        if (gm_unw.kind) {
                                            goto gm_ll164;
                                        }
                                    }
                                }
                                goto lc160;
                            }
                            gm_l37 = t192;
                        }
                        {
                            void * t209 = gm_l0;
                            void * t210 = (void *)((uint8_t *)&F.l36);
                            uint64_t t211 = gm_l37;
                            uint64_t t212 = (uintptr_t)t211;
                            uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t212 >= (uintptr_t)(t213)) gm_trap_index(t212, t213, gm_srch6h8rdicu443, 247, 39);
                            gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t212 * 56) + 16));
                            void * t215 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t209, t210, t214);
                            if (gm_unw.kind) {
                                goto gm_ll164;
                            }
                            gm_l38 = t215;
                        }
                        {
                            uint8_t t217 = gm_l27;
                            if (!t217) goto ie216;
                            {
                                void * t219 = gm_l0;
                                void * t220 = gm_l38;
                                void * t221 = (void *)((uint8_t *)&F.l31);
                                void * t222 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t219, t220, t221);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                                gm_l38 = t222;
                            }
                            goto ix216;
ie216:;
ix216:;
                        }
                        {
                            void * t223 = gm_l0;
                            void * t224 = (void *)(((uint8_t *)&F.l28) + 56);
                            void * t225 = (void *)((uint8_t *)&F.l32);
                            uint64_t t226 = gm_l37;
                            void * t227 = (void *)((uint8_t *)&F.l31);
                            gm_b0_1 t228 = gm_f_place_iter_pre_qn5c5b5((void *)NULL, cx, NULL, t223, t224, t225, t226, t227);
                            if (gm_unw.kind) {
                                goto gm_ll164;
                            }
                        }
                        {
                            uint64_t t229 = gm_l37;
                            uint64_t t230 = (uintptr_t)t229;
                            uint64_t t231 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 56) + 0) + 8);
                            if (t230 >= (uintptr_t)(t231)) gm_trap_index(t230, t231, gm_srch6h8rdicu443, 250, 17);
                            void * t232 = gm_l38;
                            GM_ST(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 56) + 0)) + t230 * 8), t232);
                        }
                        {
                            uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 136));
                            uint64_t r234; bool o235 = __builtin_add_overflow(t233, ((uint64_t)1ULL), &r234);
                            if (o235) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 251, 17);
                            uint64_t t236 = r234;
                            GM_ST(uint64_t, (((uint8_t *)&F.l28) + 136), t236);
                        }
                        goto me173;
mn179:;
ma175:;
                        uint8_t t238 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        if (t238 != ((uint8_t)1ULL)) goto mn237;
                        gm_t08olq97rscn3 t239 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l35) + 0) + 8) + 0));
                        gm_l39 = t239;
                        gm_tc3oht4ksth9d t240 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l35) + 0) + 8) + 32));
                        F.l40 = t240;
                        {
                            gm_b16_8 t242 = gm_l1;
                            gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 0));
                            gm_tf5srh4mp4b3i t244 = gm_f_param_index_78k4iut(t242, t243);
                            uint64_t t245;
                            if (((((uint8_t *)&t244))[8] != 0)) {
                                uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&t244) + 0));
                                t245 = t246;
                            } else {
                                {
                                    void * t248 = gm_l0;
                                    gm_tl874t2atnlh3 t249 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l39) + 16));
                                    gm_t6j6smj7qoctu t250;
                                    gm_b16_8 t251 = gm_l5;
                                    GM_ST(gm_b16_8, (uint8_t *)&t250 + 0, t251);
                                    gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t250 + 16, t252);
                                    gm_tabb710d9mq2e t253;
                                    gm_f_format_ivju6qo((void *)&t253, (void *)NULL, cx, NULL, &t250);
                                    if (gm_unw.kind) {
                                        goto gm_ll164;
                                    }
                                    if (((((uint8_t *)&t253))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t253) + 8, gm_srch6h8rdicu443, 255, 38);
                                    gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&t253) + 8));
                                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                                    gm_b0_1 t256 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t248, t249, t255);
                                    if (gm_unw.kind) {
                                        goto gm_ll164;
                                    }
                                }
                                {
                                    {
                                        void * t258 = gm_l0;
                                        void * t259 = (void *)((uint8_t *)&F.l40);
                                        gm_tkn42u7na3t81 t260;
                                        memset(&t260, 0, sizeof t260);
                                        void * t261 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t258, t259, t260);
                                        if (gm_unw.kind) {
                                            goto gm_ll164;
                                        }
                                    }
                                }
                                goto lc160;
                            }
                            gm_l41 = t245;
                        }
                        {
                            uint8_t t263 = 0;
                            uint64_t t264 = gm_l41;
                            uint64_t t265 = (uintptr_t)t264;
                            uint64_t t266 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 96) + 0) + 8);
                            if (t265 >= (uintptr_t)(t266)) gm_trap_index(t265, t266, gm_srch6h8rdicu443, 259, 20);
                            uint8_t t267 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 96) + 0)) + t265 * 1));
                            if (!t267) {
                                uint8_t t268 = 0;
                                uint64_t t269 = gm_l41;
                                uint64_t t270 = (uintptr_t)t269;
                                uint64_t t271 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 56) + 0) + 8);
                                if (t270 >= (uintptr_t)(t271)) gm_trap_index(t270, t271, gm_srch6h8rdicu443, 259, 44);
                                void * t272 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 56) + 0)) + t270 * 8));
                                uint8_t t273;
                                if ((GM_LD(void *, ((uint8_t *)&t272)) != 0)) goto mn275;
                                t273 = ((uint8_t)0);
                                goto me274;
mn275:;
                                t273 = ((uint8_t)1);
                                goto me274;
mn276:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 259, 44);
me274:;
                                if (t273) {
                                    uint8_t t277 = gm_l26;
                                    uint8_t t278 = (uint8_t)!t277;
                                    t268 = t278;
                                } else t268 = 0;
                                t263 = t268;
                            } else t263 = 1;
                            if (!t263) goto ie262;
                            {
                                void * t280 = gm_l0;
                                gm_tl874t2atnlh3 t281 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l39) + 16));
                                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 0));
                                gm_tnpifv20irnph t283;
                                GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t282);
                                gm_tabb710d9mq2e t284;
                                gm_f_format_hqkpc5s((void *)&t284, (void *)NULL, cx, NULL, t283);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 8, gm_srch6h8rdicu443, 260, 38);
                                gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&t284) + 8));
                                gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                                gm_b0_1 t287 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t280, t281, t286);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                            }
                            goto ix262;
ie262:;
ix262:;
                        }
                        {
                            uint64_t t288 = gm_l41;
                            uint64_t t289 = (uintptr_t)t288;
                            uint64_t t290 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 96) + 0) + 8);
                            if (t289 >= (uintptr_t)(t290)) gm_trap_index(t289, t290, gm_srch6h8rdicu443, 262, 17);
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 96) + 0)) + t289 * 1), ((uint8_t)1));
                        }
                        {
                            void * t291 = gm_l0;
                            void * t292 = (void *)((uint8_t *)&F.l40);
                            uint64_t t293 = gm_l41;
                            uint64_t t294 = (uintptr_t)t293;
                            uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t294 >= (uintptr_t)(t295)) gm_trap_index(t294, t295, gm_srch6h8rdicu443, 264, 39);
                            gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t294 * 56) + 16));
                            void * t297 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t291, t292, t296);
                            if (gm_unw.kind) {
                                goto gm_ll164;
                            }
                            gm_l42 = t297;
                        }
                        {
                            uint8_t t299 = gm_l27;
                            if (!t299) goto ie298;
                            {
                                void * t301 = gm_l0;
                                void * t302 = gm_l42;
                                void * t303 = (void *)((uint8_t *)&F.l31);
                                void * t304 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t301, t302, t303);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                                gm_l42 = t304;
                            }
                            goto ix298;
ie298:;
ix298:;
                        }
                        {
                            void * t305 = gm_l0;
                            void * t306 = (void *)(((uint8_t *)&F.l28) + 56);
                            void * t307 = (void *)((uint8_t *)&F.l32);
                            uint64_t t308 = gm_l41;
                            void * t309 = (void *)((uint8_t *)&F.l31);
                            gm_b0_1 t310 = gm_f_place_iter_pre_qn5c5b5((void *)NULL, cx, NULL, t305, t306, t307, t308, t309);
                            if (gm_unw.kind) {
                                goto gm_ll164;
                            }
                        }
                        {
                            uint64_t t311 = gm_l41;
                            uint64_t t312 = (uintptr_t)t311;
                            uint64_t t313 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 56) + 0) + 8);
                            if (t312 >= (uintptr_t)(t313)) gm_trap_index(t312, t313, gm_srch6h8rdicu443, 267, 17);
                            void * t314 = gm_l42;
                            GM_ST(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 56) + 0)) + t312 * 8), t314);
                        }
                        goto me173;
mn237:;
ma176:;
                        uint8_t t316 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        if (t316 != ((uint8_t)2ULL)) goto mn315;
                        gm_tc3oht4ksth9d t317 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l35) + 0) + 8));
                        F.l43 = t317;
                        {
                            void * t319 = gm_l0;
                            void * t320 = (void *)((uint8_t *)&F.l43);
                            void * t321 = (void *)((uint8_t *)&F.l31);
                            gm_th2lqbo77pvsc t322;
                            gm_f_spread_parts_e62q2l2((void *)&t322, (void *)NULL, cx, NULL, t319, t320, t321);
                            if (gm_unw.kind) {
                                goto gm_ll164;
                            }
                            gm_b16_8 t323;
                            if (((((uint8_t *)&t322))[16] != 0)) {
                                gm_b16_8 t324 = GM_LD(gm_b16_8, (((uint8_t *)&t322) + 0));
                                t323 = t324;
                            } else {
                                {
                                    GM_ST(uint8_t, (((uint8_t *)&F.l28) + 145), ((uint8_t)1));
                                }
                                goto lc160;
                            }
                            gm_l44 = t323;
                        }
                        {
                            void * t326 = gm_l0;
                            void * t327 = (void *)((uint8_t *)&F.l28);
                            gm_b16_8 t328 = gm_l44;
                            gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
                            gm_tl874t2atnlh3 t330 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 72));
                            gm_b0_1 t331 = gm_f_bind_spread_parts_lmknlmc((void *)NULL, cx, NULL, t326, t327, t329, t330);
                            if (gm_unw.kind) {
                                goto gm_ll164;
                            }
                        }
                        goto me173;
mn315:;
ma177:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 239, 9);
me173:;
                    }
                    goto me169;
mn170:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 238, 5);
me169:;
                }
lc160:;
                {
                    uint64_t t333 = gm_l34;
                    uint64_t r334; bool o335 = __builtin_add_overflow(t333, ((uint64_t)1ULL), &r334);
                    uint64_t t336 = r334;
                    gm_l34 = t336;
                }
            }
gm_ll164:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb160; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc160; }
            }
            goto gm_lad;
lb160:;
        }
    }
    {
        void * t337 = gm_l0;
        void * t338 = (void *)((uint8_t *)&F.l28);
        uint64_t t339 = gm_l2;
        gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        gm_tl874t2atnlh3 t341 = gm_l4;
        gm_t6j6smj7qoctu t342;
        gm_f_fill_defaults_3fq5ut6((void *)&t342, (void *)NULL, cx, NULL, t337, t338, t339, t340, t341);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t342);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_spread_parts_lmknlmc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_bind_spread_parts_lmknlmc F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* parts */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l6; /* part */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* n */
    uint64_t gm_l9; /* j */
    uint64_t gm_l10; /* j */
    GM_STACK_CHECK(&F, 1077u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tpouv9r7lrri2 t15 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 32));
                    gm_l6 = t15;
                    {
                        gm_l7 = ((uint64_t)0ULL);
                    }
                    {
                        if (!(((((uint8_t *)&gm_l6) + 0))[16] != 0)) goto ie17;
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                        gm_l8 = t18;
                        {
                            void * t20 = gm_l1;
                            GM_ST(uint8_t, (((uint8_t *)t20) + 144), ((uint8_t)1));
                        }
                        {
                            void * t21 = gm_l1;
                            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
                            gm_b16_8 t23 = gm_l8;
                            gm_tf5srh4mp4b3i t24 = gm_f_param_index_78k4iut(t22, t23);
                            uint64_t t25;
                            if (((((uint8_t *)&t24))[8] != 0)) {
                                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t24) + 0));
                                t25 = t26;
                            } else {
                                {
                                    void * t28 = gm_l0;
                                    gm_tl874t2atnlh3 t29 = gm_l3;
                                    gm_t6j6smj7qoctu t30;
                                    void * t31 = gm_l1;
                                    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t32);
                                    gm_b16_8 t33 = gm_l8;
                                    GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t33);
                                    gm_tabb710d9mq2e t34;
                                    gm_f_format_10ctmcq((void *)&t34, (void *)NULL, cx, NULL, &t30);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                    if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srch6h8rdicu443, 311, 30);
                                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                                    gm_b0_1 t37 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t28, t29, t36);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                }
                                {
                                    void * t38 = gm_l1;
                                    GM_ST(uint8_t, (((uint8_t *)t38) + 145), ((uint8_t)1));
                                }
                                goto lb4;
                            }
                            gm_l9 = t25;
                        }
                        {
                            uint8_t t40 = 0;
                            uint64_t t41 = gm_l9;
                            uint64_t t42 = (uintptr_t)t41;
                            void * t43 = gm_l1;
                            uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 96) + 0) + 8);
                            if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_srch6h8rdicu443, 315, 16);
                            uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 96) + 0)) + t42 * 1));
                            if (!t45) {
                                uint64_t t46 = gm_l9;
                                uint64_t t47 = (uintptr_t)t46;
                                void * t48 = gm_l1;
                                uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 56) + 0) + 8);
                                if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srch6h8rdicu443, 315, 39);
                                void * t50 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t48) + 56) + 0)) + t47 * 8));
                                uint8_t t51;
                                if ((GM_LD(void *, ((uint8_t *)&t50)) != 0)) goto mn53;
                                t51 = ((uint8_t)0);
                                goto me52;
mn53:;
                                t51 = ((uint8_t)1);
                                goto me52;
mn54:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 315, 39);
me52:;
                                t40 = t51;
                            } else t40 = 1;
                            if (!t40) goto ie39;
                            {
                                void * t56 = gm_l0;
                                gm_tl874t2atnlh3 t57 = gm_l3;
                                gm_b16_8 t58 = gm_l8;
                                gm_tnpifv20irnph t59;
                                GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
                                gm_tabb710d9mq2e t60;
                                gm_f_format_hqkpc5s((void *)&t60, (void *)NULL, cx, NULL, t59);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srch6h8rdicu443, 316, 30);
                                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
                                gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                                gm_b0_1 t63 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t56, t57, t62);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            {
                                void * t64 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t64) + 145), ((uint8_t)1));
                            }
                            {
                                goto lb4;
                            }
                            goto ix39;
ie39:;
ix39:;
                        }
                        {
                            uint64_t t65 = gm_l9;
                            gm_l7 = t65;
                        }
                        goto ix17;
ie17:;
                        {
                            void * t68 = gm_l1;
                            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t68) + 144));
                            if (!t69) goto ie67;
                            {
                                void * t71 = gm_l0;
                                gm_tl874t2atnlh3 t72 = gm_l3;
                                gm_b16_8 t73;
                                GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s1ekjot5tdpje));
                                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(66));
                                gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t71, t72, t73);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            {
                                void * t75 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t75) + 145), ((uint8_t)1));
                            }
                            {
                                goto lb4;
                            }
                            goto ix67;
ie67:;
ix67:;
                        }
                        {
                            void * t76 = gm_l1;
                            gm_tf5srh4mp4b3i t77 = gm_f_next_positional_t5fspc1(t76);
                            uint64_t t78;
                            if (((((uint8_t *)&t77))[8] != 0)) {
                                uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&t77) + 0));
                                t78 = t79;
                            } else {
                                {
                                    void * t81 = gm_l0;
                                    gm_tl874t2atnlh3 t82 = gm_l3;
                                    gm_t3877pk7lv748 t83;
                                    void * t84 = gm_l1;
                                    gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)t84) + 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t83 + 0, t85);
                                    void * t86 = gm_l1;
                                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 48));
                                    GM_ST(uint64_t, (uint8_t *)&t83 + 16, t87);
                                    gm_tabb710d9mq2e t88;
                                    gm_f_format_b4oiuht((void *)&t88, (void *)NULL, cx, NULL, &t83);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                    if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_srch6h8rdicu443, 329, 30);
                                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                                    gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                                    gm_b0_1 t91 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t81, t82, t90);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                }
                                {
                                    void * t92 = gm_l1;
                                    GM_ST(uint8_t, (((uint8_t *)t92) + 145), ((uint8_t)1));
                                }
                                goto lb4;
                            }
                            gm_l10 = t78;
                        }
                        {
                            uint64_t t93 = gm_l10;
                            gm_l7 = t93;
                        }
                        {
                            void * t94 = gm_l1;
                            void * t95 = gm_l1;
                            uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)t95) + 136));
                            uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                            if (o98) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 334, 13);
                            uint64_t t99 = r97;
                            GM_ST(uint64_t, (((uint8_t *)t94) + 136), t99);
                        }
ix17:;
                    }
                    {
                        uint64_t t100 = gm_l7;
                        uint64_t t101 = (uintptr_t)t100;
                        void * t102 = gm_l1;
                        uint64_t t103 = GM_LD(uintptr_t, ((((uint8_t *)t102) + 56) + 0) + 8);
                        if (t101 >= (uintptr_t)(t103)) gm_trap_index(t101, t103, gm_srch6h8rdicu443, 336, 9);
                        uint8_t *t104 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t102) + 56) + 0)) + t101 * 8);
                        void * t105 = gm_l0;
                        void * t106 = GM_LD(void *, (((uint8_t *)&gm_l6) + 24));
                        uint64_t t107 = gm_l7;
                        uint64_t t108 = (uintptr_t)t107;
                        void * t109 = gm_l1;
                        uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)t109) + 0) + 8);
                        if (t108 >= (uintptr_t)(t110)) gm_trap_index(t108, t110, gm_srch6h8rdicu443, 336, 50);
                        gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t109) + 0)) + t108 * 56) + 16));
                        void * t112 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t105, t106, t111);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(void *, t104, t112);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 306, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t114 = gm_l5;
                    uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                    uint64_t t117 = r115;
                    gm_l5 = t117;
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
__attribute__((aligned(4))) void * gm_f_call_decl_sj6f05u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_call_decl_sj6f05u F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* callee */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint32_t gm_l6; /* f */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* eff */
    uint32_t gm_l9; /* op */
    GM_STACK_CHECK(&F, 772u);
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
        uint64_t t4 = gm_l1;
        gm_t6qooom8qa2do t5;
        gm_f_resolve_decl_pus042v((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma9; default: goto ma12; }
ma7:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t16 != ((uint8_t)5ULL)) goto mn15;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l6 = t17;
        void * t18 = gm_l0;
        uint32_t t19 = gm_l6;
        gm_b16_8 t20 = gm_l4;
        gm_tl874t2atnlh3 t21 = gm_l5;
        void * t22 = gm_f_call_fn_2prsqv9((void *)NULL, cx, NULL, t18, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me6;
mn15:;
ma8:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t25 != ((uint8_t)0ULL)) goto mn24;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l7 = t26;
        void * t27 = gm_l0;
        gm_t88rcucg1b7uk t28 = gm_l7;
        gm_b16_8 t29 = gm_l4;
        gm_tl874t2atnlh3 t30 = gm_l5;
        void * t31 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t27, t28, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
        goto me6;
mn24:;
ma9:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t34 != ((uint8_t)11ULL)) goto mn33;
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
        gm_tl874t2atnlh3 t37 = gm_l5;
        void * t38 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me6;
mn33:;
ma10:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t41 != ((uint8_t)6ULL)) goto mn40;
        uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l8 = t42;
        uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 4));
        gm_l9 = t43;
        void * t44 = gm_l0;
        uint32_t t45 = gm_l8;
        uint32_t t46 = gm_l9;
        gm_b16_8 t47 = gm_l4;
        gm_tl874t2atnlh3 t48 = gm_l5;
        void * t49 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t44, t45, t46, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
        goto me6;
mn40:;
ma11:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t52 != ((uint8_t)7ULL)) goto mn51;
        void * t53 = gm_l0;
        void * t54 = gm_l0;
        void * t55 = gm_l3;
        gm_tkn42u7na3t81 t56;
        memset(&t56, 0, sizeof t56);
        void * t57 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t58 = gm_l4;
        gm_tl874t2atnlh3 t59 = gm_l5;
        void * t60 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t53, t57, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t61 = 0;
        F.ret = t60;
        goto gm_fret;
        goto me6;
mn51:;
ma12:;
        {
            void * t64 = gm_l0;
            void * t65 = gm_l3;
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
            gm_b16_8 t67 = gm_l2;
            gm_tnpifv20irnph t68;
            GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
            gm_tabb710d9mq2e t69;
            gm_f_format_8fudcr8((void *)&t69, (void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srch6h8rdicu443, 136, 35);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            gm_tl874t2atnlh3 t75 = gm_l5;
            void * t76 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto me6;
mn62:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 129, 5);
me6:;
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
__attribute__((aligned(4))) void * gm_f_call_fn_2prsqv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_call_fn_2prsqv9 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* variadic */
    gm_b16_8 gm_l7; /* fixed */
    gm_t6j6smj7qoctu gm_l8; /* bound */
    gm_b16_8 gm_l9; /* all */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l14; /* call */
    GM_STACK_CHECK(&F, 1442u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srch6h8rdicu443, 144, 12);
        gm_tqqhoivahirr0 t8 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136));
        F.l4 = t8;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 80) + 8);
        uint8_t t11 = (t10 > ((uint64_t)0ULL));
        if (!t11) goto ie9;
        {
            void * t13 = gm_l0;
            uint32_t t14 = gm_l1;
            gm_b16_8 t15 = gm_l2;
            gm_tl874t2atnlh3 t16 = gm_l3;
            void * t17 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t13, t14, ((void *)0), t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 40) + 8);
        gm_l5 = t19;
    }
    {
        uint8_t t20 = 0;
        uint32_t t21 = gm_l1;
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)t23) + 1368) + 0));
        uint8_t t25 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t24) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t24))) + i * 4, ((uint8_t *)&t21))) { t25 = 1; break; }
        uint8_t t26 = (uint8_t)(t25);
        if (t26) {
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t28 = gm_l5;
            uint8_t t29 = (t27 > t28);
            t20 = t29;
        } else t20 = 0;
        gm_l6 = t20;
    }
    {
        gm_b16_8 t30;
        uint8_t t32 = gm_l6;
        if (!t32) goto ie31;
        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t35 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t36 = gm_l5;
        uint64_t t37 = (uintptr_t)t36;
        if ((uintptr_t)0 > t37 || t37 > (uintptr_t)t34) gm_trap_slice((uintptr_t)0, t37, t34, gm_srch6h8rdicu443, 148, 28);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t35 + (uintptr_t)0 * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - (uintptr_t)0));
        gm_b16_8 t39 = t38;
        t30 = t39;
        goto ix31;
ie31:;
        gm_b16_8 t41 = gm_l2;
        gm_b16_8 t42 = t41;
        t30 = t42;
ix31:;
        gm_l7 = t30;
    }
    {
        void * t43 = gm_l0;
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
        gm_b16_8 t47 = gm_l7;
        gm_tl874t2atnlh3 t48 = gm_l3;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sq0dqiq83b924));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(8));
        gm_ttu09lij36f8c t50;
        gm_f_bind_args_dnih8ih((void *)&t50, (void *)NULL, cx, NULL, t43, t45, t46, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t51;
        if (((((uint8_t *)&t50))[32] != 0)) {
            t51 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t50) + 0));
        } else {
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            gm_tl874t2atnlh3 t54 = gm_l3;
            void * t55 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        gm_l8 = t51;
    }
    {
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
        gm_l9 = t57;
    }
    {
        uint8_t t59 = gm_l6;
        if (!t59) goto ie58;
        {
            gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t61 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_tftb1odc739t5 t64 = gm_f_extend_gig5o6g(t61, t63);
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srch6h8rdicu443, 153, 9);
            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
        }
        {
            {
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t68 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t69 = gm_l5;
                uint64_t t70 = (uintptr_t)t69;
                if (t70 > (uintptr_t)t67 || (uintptr_t)t67 > (uintptr_t)t67) gm_trap_slice(t70, (uintptr_t)t67, t67, gm_srch6h8rdicu443, 154, 18);
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t68 + t70 * 144)));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)((uintptr_t)t67 - t70));
                gm_l11 = t71;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t73 = gm_l12;
                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t75 = (t73 < t74);
                    if (!t75) goto lb72;
                    {
                        uint64_t t78 = gm_l12;
                        uint64_t t79 = (uintptr_t)t78;
                        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t7te2tdpebbt4 t83 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t79 * 144));
                        F.l13 = t83;
                        {
                            void * t85 = (void *)((uint8_t *)&F.l10);
                            void * t86 = gm_l0;
                            void * t87 = (void *)((uint8_t *)&F.l13);
                            void * t88 = gm_f_variadic_arg_dfdnlo3((void *)NULL, cx, NULL, t86, t87);
                            if (gm_unw.kind) {
                                goto gm_ll76;
                            }
                            gm_tftb1odc739t5 t89 = gm_f_push_d943vgp_2(t85, &(*(gm_t69m56meqefs8 *)((uint8_t *)t88)));
                            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srch6h8rdicu443, 154, 30);
                            gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                        }
                        goto me81;
mn82:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 154, 9);
me81:;
                    }
lc72:;
                    {
                        uint64_t t92 = gm_l12;
                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                        uint64_t t95 = r93;
                        gm_l12 = t95;
                    }
                }
gm_ll76:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb72; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc72; }
                }
                goto gm_lad;
lb72:;
            }
        }
        {
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_l9 = t96;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        void * t97 = gm_l0;
        void * t98 = (void *)(((uint8_t *)&F.l4) + 64);
        gm_tl874t2atnlh3 t99 = gm_l3;
        gm_b0_1 t100 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t97, t98, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        gm_to76jli3bqddq t102;
        uint32_t t103 = gm_l1;
        gm_tsth9qu1dv0g3 t104;
        memset(&t104, 0, sizeof t104);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t104 + 8), t103);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t102 + 0, t104);
        gm_b16_8 t105 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t102 + 40, t105);
        gm_teti411eujvf4 t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t106 + 8), t102);
        gm_tl874t2atnlh3 t107 = gm_l3;
        void * t108 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t101, &t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t108;
    }
    {
        uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)&gm_l8) + 16) + 8);
        uint8_t t111 = ((t110 == ((uint64_t)0ULL)));
        if (!t111) goto ie109;
        {
            void * t113 = gm_l14;
            uint8_t t114 = 0;
            F.ret = t113;
            goto gm_fret;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        void * t115 = gm_l14;
        gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t115) + 0));
        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
        void * t118 = gm_l14;
        gm_tl874t2atnlh3 t119 = gm_l3;
        void * t120 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t116, t117, t118, t119);
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
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_call_l10ebge F;
    void * gm_l0; /* cx */
    void * gm_l1; /* callee */
    gm_b16_8 gm_l2; /* args */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t08olq97rscn3 gm_l5; /* id */
    gm_t88rcucg1b7uk gm_l7; /* t */
    void * gm_l10; /* c */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    gm_t716e5hdn64ba gm_l13; /* named */
    gm_t08olq97rscn3 gm_l14; /* name */
    gm_t88rcucg1b7uk gm_l15; /* t */
    void * gm_l16; /* base */
    gm_t08olq97rscn3 gm_l17; /* name */
    uint64_t gm_l18; /* m */
    uint64_t gm_l19; /* d */
    gm_t88rcucg1b7uk gm_l20; /* t */
    gm_t08olq97rscn3 gm_l21; /* id */
    uint32_t gm_l23; /* f */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_b16_8 gm_l25; /* n */
    uint64_t gm_l26; /* d */
    gm_t88rcucg1b7uk gm_l27; /* t */
    uint32_t gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* t */
    gm_b16_8 gm_l30; /* es */
    gm_t88rcucg1b7uk gm_l31; /* t */
    GM_STACK_CHECK(&F, 4870u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l5 = t5;
        void * t6 = gm_l0;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        gm_tqvu78rpi6q1h t8;
        gm_f_lookup_local_6f38t7p((void *)&t8, t6, t7);
        uint8_t t9;
        if (((((uint8_t *)&t8))[208] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 6, 38);
me10:;
        if (!t9) goto ie2;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)1ULL)));
        if (!t14) goto ie2;
        uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srch6h8rdicu443, 6, 111);
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 144) + 0) + 0));
        if (t17 != ((uint8_t)0ULL)) goto ie2;
        gm_tc3oht4ksth9d t18 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 144) + 0) + 8));
        F.l6 = t18;
        {
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tkn42u7na3t81 t24 = gm_f_type_param_3av4r8i(t22, t23);
            if (!((((uint8_t *)&t24))[4] != 0)) goto ie20;
            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t24));
            gm_l7 = t25;
            {
                void * t28 = gm_l0;
                void * t29 = (void *)((uint8_t *)&F.l6);
                gm_tkv1vap90vht2 t30;
                gm_f_try_untyped_8j8up35((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t30))[208] != 0)) goto ie27;
                F.l8 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t30) + 0));
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                    if (t33 != ((uint8_t)0ULL)) goto ie32;
                    gm_t0ialelq3ql1k t34 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l8) + 8));
                    F.l9 = t34;
                    {
                        uint8_t t37;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                        if (t40 != ((uint8_t)3ULL)) goto mn39;
                        t37 = ((uint8_t)1);
                        goto me38;
mn39:;
                        t37 = ((uint8_t)0);
                        goto me38;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 10, 20);
me38:;
                        if (!t37) goto ie36;
                        {
                            void * t43 = gm_l0;
                            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                            gm_tnpifv20irnph t46;
                            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                            gm_tabb710d9mq2e t47;
                            gm_f_format_t3u5vlm((void *)&t47, (void *)NULL, cx, NULL, t46);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srch6h8rdicu443, 11, 38);
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            gm_b0_1 t50 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t43, t44, t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t51 = gm_l0;
                            void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
                            gm_tl874t2atnlh3 t53 = gm_l4;
                            void * t54 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t52, t53);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t55 = 0;
                            F.ret = t54;
                            goto gm_fret;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    {
                        void * t56 = gm_l0;
                        void * t57 = gm_l0;
                        void * t58 = (void *)((uint8_t *)&F.l9);
                        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                        void * t60 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t57, t58, t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t61 = gm_l7;
                        void * t62 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t56, t60, t61);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t62;
                    }
                    {
                        void * t63 = gm_l10;
                        gm_tl874t2atnlh3 t64 = gm_l4;
                        GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72), t64);
                    }
                    {
                        void * t65 = gm_l10;
                        uint8_t t66 = 0;
                        F.ret = t65;
                        goto gm_fret;
                    }
                    goto ix32;
ie32:;
                    {
                        void * t68 = gm_l0;
                        void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
                        gm_tl874t2atnlh3 t70 = gm_l4;
                        void * t71 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t69, t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t72 = 0;
                        F.ret = t71;
                        goto gm_fret;
                    }
ix32:;
                }
                goto ix27;
ie27:;
ix27:;
            }
            goto ix20;
ie20:;
ix20:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t74 = gm_l1;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t74) + 8) + 0));
        if (t75 != ((uint8_t)22ULL)) goto ie73;
        void * t76 = GM_LD(void *, (((((uint8_t *)t74) + 8) + 8) + 0));
        gm_l11 = t76;
        gm_t08olq97rscn3 t77 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t74) + 8) + 8) + 8));
        gm_l12 = t77;
        {
            void * t80 = gm_l0;
            void * t81 = gm_l11;
            gm_tlbrdhah49ci5 t82;
            gm_f_effect_named_b8afdbp((void *)&t82, (void *)NULL, cx, NULL, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t82))[32] != 0)) goto ie79;
            gm_l13 = (*(gm_t716e5hdn64ba *)(((uint8_t *)&t82) + 0));
            {
                void * t84 = gm_l0;
                gm_b16_8 t85 = gm_l2;
                gm_tl874t2atnlh3 t86 = gm_l4;
                void * t87 = gm_f_perform_named_h7gr9vi((void *)NULL, cx, NULL, t84, &(*(gm_t716e5hdn64ba *)((uint8_t *)&gm_l13)), &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)), t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t88 = 0;
                F.ret = t87;
                goto gm_fret;
            }
            goto ix79;
ie79:;
ix79:;
        }
        {
            void * t90 = gm_l0;
            void * t91 = gm_l11;
            gm_tf5srh4mp4b3i t92 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t93;
            if (((((uint8_t *)&t92))[8] != 0)) goto mn95;
            t93 = ((uint8_t)1);
            goto me94;
mn95:;
            t93 = ((uint8_t)0);
            goto me94;
mn96:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 23, 12);
me94:;
            if (!t93) goto ie89;
            {
                void * t98 = gm_l0;
                void * t99 = gm_l11;
                gm_b16_8 t100 = gm_l2;
                gm_tl874t2atnlh3 t101 = gm_l4;
                void * t102 = gm_f_method_call_hebo20o((void *)NULL, cx, NULL, t98, t99, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)), t100, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t103 = 0;
                F.ret = t102;
                goto gm_fret;
            }
            goto ix89;
ie89:;
ix89:;
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        void * t104 = gm_l1;
        uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
        switch (t113) { case ((uint8_t)0ULL): goto ma111; case ((uint8_t)1ULL): goto ma111; case ((uint8_t)2ULL): goto ma111; case ((uint8_t)3ULL): goto ma111; case ((uint8_t)4ULL): goto ma111; case ((uint8_t)5ULL): goto ma111; case ((uint8_t)6ULL): goto ma108; case ((uint8_t)7ULL): goto ma111; case ((uint8_t)8ULL): goto ma106; case ((uint8_t)9ULL): goto ma111; case ((uint8_t)10ULL): goto ma110; case ((uint8_t)11ULL): goto ma111; case ((uint8_t)12ULL): goto ma111; case ((uint8_t)13ULL): goto ma111; case ((uint8_t)14ULL): goto ma111; case ((uint8_t)15ULL): goto ma111; case ((uint8_t)16ULL): goto ma111; case ((uint8_t)17ULL): goto ma111; case ((uint8_t)18ULL): goto ma111; case ((uint8_t)19ULL): goto ma109; case ((uint8_t)20ULL): goto ma111; case ((uint8_t)21ULL): goto ma111; case ((uint8_t)22ULL): goto ma107; case ((uint8_t)23ULL): goto ma111; case ((uint8_t)24ULL): goto ma111; case ((uint8_t)25ULL): goto ma111; case ((uint8_t)26ULL): goto ma111; case ((uint8_t)27ULL): goto ma111; case ((uint8_t)28ULL): goto ma111; case ((uint8_t)29ULL): goto ma111; case ((uint8_t)30ULL): goto ma111; case ((uint8_t)31ULL): goto ma111; case ((uint8_t)32ULL): goto ma111; case ((uint8_t)33ULL): goto ma111; case ((uint8_t)34ULL): goto ma111; case ((uint8_t)35ULL): goto ma111; case ((uint8_t)36ULL): goto ma111; case ((uint8_t)37ULL): goto ma111; case ((uint8_t)38ULL): goto ma111; case ((uint8_t)39ULL): goto ma111; case ((uint8_t)40ULL): goto ma111; case ((uint8_t)41ULL): goto ma111; case ((uint8_t)42ULL): goto ma111; case ((uint8_t)43ULL): goto ma111; case ((uint8_t)44ULL): goto ma111; case ((uint8_t)45ULL): goto ma111; case ((uint8_t)46ULL): goto ma111; case ((uint8_t)47ULL): goto ma111; case ((uint8_t)48ULL): goto ma111; case ((uint8_t)49ULL): goto ma111; case ((uint8_t)50ULL): goto ma111; default: goto ma111; }
ma106:;
        uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
        if (t115 != ((uint8_t)8ULL)) goto mn114;
        gm_t08olq97rscn3 t116 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t104) + 8) + 8));
        gm_l14 = t116;
        {
            gm_tkn42u7na3t81 t118 = gm_l3;
            gm_t88rcucg1b7uk t119;
            if (((((uint8_t *)&t118))[4] != 0)) {
                gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t118) + 0));
                t119 = t120;
            } else {
                {
                    void * t122 = gm_l0;
                    void * t123 = gm_l1;
                    gm_tl874t2atnlh3 t124 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t123) + 72));
                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                    gm_tnpifv20irnph t126;
                    GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t125);
                    gm_tabb710d9mq2e t127;
                    gm_f_format_bvitrsu((void *)&t127, (void *)NULL, cx, NULL, t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 8, gm_srch6h8rdicu443, 28, 39);
                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                    gm_b0_1 t130 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t122, t124, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t131 = gm_l0;
                void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
                gm_tl874t2atnlh3 t133 = gm_l4;
                void * t134 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t132, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t135 = 0;
                F.ret = t134;
                goto gm_fret;
            }
            gm_l15 = t119;
        }
        {
            void * t136 = gm_l0;
            void * t137 = gm_l0;
            void * t138 = gm_l0;
            void * t139 = (void *)(((uint8_t *)t138) + 96);
            void * t140 = gm_l0;
            void * t141 = GM_LD(void *, (((uint8_t *)t140) + 0));
            gm_t88rcucg1b7uk t142 = gm_l15;
            gm_t88rcucg1b7uk t143 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t139, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t144 = GM_CONV(gm_t88rcucg1b7uk, gm_f_strip_wrappers_e5thpib(t137, GM_CONV(gm_csguankko4lmhd, t143)));
            gm_b16_8 t145 = gm_l2;
            gm_th2lqbo77pvsc t146;
            GM_ST(uint8_t, (uint8_t *)&t146 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t146 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t146 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t146), t145);
            ((uint8_t *)&t146)[16] = 1;
            gm_tl874t2atnlh3 t147 = gm_l4;
            void * t148 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t136, t144, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l14)), &t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t149 = 0;
            F.ret = t148;
            goto gm_fret;
        }
        goto me105;
mn114:;
ma107:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
        if (t151 != ((uint8_t)22ULL)) goto mn150;
        void * t152 = GM_LD(void *, (((((uint8_t *)t104) + 8) + 8) + 0));
        gm_l16 = t152;
        gm_t08olq97rscn3 t153 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t104) + 8) + 8) + 8));
        gm_l17 = t153;
        {
            void * t156 = gm_l0;
            void * t157 = gm_l16;
            gm_tf5srh4mp4b3i t158 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t158))[8] != 0)) goto ie155;
            uint64_t t159 = GM_LD(uint64_t, ((uint8_t *)&t158));
            gm_l18 = t159;
            {
                void * t161 = gm_l0;
                void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
                uint64_t t163 = gm_l18;
                void * t164 = gm_l0;
                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 8));
                gm_tf5srh4mp4b3i t166 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t162, t163, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)), t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t167;
                if (((((uint8_t *)&t166))[8] != 0)) {
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&t166) + 0));
                    t167 = t168;
                } else {
                    void * t169 = gm_l0;
                    void * t170 = GM_LD(void *, (((uint8_t *)t169) + 0));
                    gm_tl874t2atnlh3 t171 = gm_l4;
                    void * t172 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t173 = 0;
                    F.ret = t172;
                    goto gm_fret;
                }
                gm_l19 = t167;
            }
            {
                void * t174 = gm_l0;
                uint64_t t175 = gm_l19;
                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                void * t177 = gm_l1;
                gm_b16_8 t178 = gm_l2;
                gm_tl874t2atnlh3 t179 = gm_l4;
                void * t180 = gm_f_call_decl_sj6f05u((void *)NULL, cx, NULL, t174, t175, t176, t177, t178, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t181 = 0;
                F.ret = t180;
                goto gm_fret;
            }
            goto ix155;
ie155:;
ix155:;
        }
        {
            void * t183 = gm_l0;
            void * t184 = gm_l16;
            gm_tkn42u7na3t81 t185 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t183, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t185))[4] != 0)) goto ie182;
            gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t185));
            gm_l20 = t186;
            {
                void * t188 = gm_l0;
                gm_t88rcucg1b7uk t189 = gm_l20;
                gm_b16_8 t190 = gm_l2;
                gm_th2lqbo77pvsc t191;
                GM_ST(uint8_t, (uint8_t *)&t191 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t191 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t191 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t191), t190);
                ((uint8_t *)&t191)[16] = 1;
                gm_tl874t2atnlh3 t192 = gm_l4;
                void * t193 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t188, t189, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)), &t191, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t194 = 0;
                F.ret = t193;
                goto gm_fret;
            }
            goto ix182;
ie182:;
ix182:;
        }
        {
            void * t195 = gm_l0;
            gm_tl874t2atnlh3 t196 = gm_l4;
            gm_b16_8 t197;
            GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sgnvdmoebn4i7));
            GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(15));
            void * t198 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t195, t196, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t199 = 0;
            F.ret = t198;
            goto gm_fret;
        }
        goto me105;
mn150:;
ma108:;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
        if (t201 != ((uint8_t)6ULL)) goto mn200;
        gm_t08olq97rscn3 t202 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t104) + 8) + 8));
        gm_l21 = t202;
        {
            void * t205 = gm_l0;
            gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tqvu78rpi6q1h t207;
            gm_f_lookup_local_6f38t7p((void *)&t207, t205, t206);
            if (!((((uint8_t *)&t207))[208] != 0)) goto ie204;
            F.l22 = (*(gm_t930q5l38hdap *)(((uint8_t *)&t207) + 0));
            {
                uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                if (t210 != ((uint8_t)3ULL)) goto ie209;
                uint32_t t211 = GM_LD(uint32_t, (((uint8_t *)&F.l22) + 8));
                gm_l23 = t211;
                {
                    uint32_t t214 = gm_l23;
                    void * t215 = gm_l0;
                    gm_b16_8 t216 = GM_LD(gm_b16_8, ((((uint8_t *)t215) + 760) + 0));
                    uint8_t t217 = 0;
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t216) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t216))) + i * 4, ((uint8_t *)&t214))) { t217 = 1; break; }
                    uint8_t t218 = (uint8_t)(t217);
                    if (!t218) goto ie213;
                    {
                        void * t220 = gm_l0;
                        gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                        gm_tl874t2atnlh3 t222 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                        gm_b0_1 t223 = gm_f_recursive_without_ret_48l31rm((void *)NULL, cx, NULL, t220, t221, t222);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t224 = gm_l0;
                        void * t225 = GM_LD(void *, (((uint8_t *)t224) + 0));
                        gm_tl874t2atnlh3 t226 = gm_l4;
                        void * t227 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t225, t226);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t228 = 0;
                        F.ret = t227;
                        goto gm_fret;
                    }
                    goto ix213;
ie213:;
ix213:;
                }
                {
                    void * t229 = gm_l0;
                    uint32_t t230 = gm_l23;
                    gm_b16_8 t231 = gm_l2;
                    gm_tl874t2atnlh3 t232 = gm_l4;
                    void * t233 = gm_f_call_fn_2prsqv9((void *)NULL, cx, NULL, t229, t230, t231, t232);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t234 = 0;
                    F.ret = t233;
                    goto gm_fret;
                }
                goto ix209;
ie209:;
ix209:;
            }
            {
                uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                if (t236 != ((uint8_t)5ULL)) goto ie235;
                gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l22) + 8));
                gm_l24 = t237;
                {
                    void * t240 = gm_l0;
                    void * t241 = GM_LD(void *, (((uint8_t *)t240) + 0));
                    void * t242 = (void *)(((uint8_t *)t241) + 8);
                    gm_t88rcucg1b7uk t243 = gm_l24;
                    uint8_t t244 = gm_f_is_error_8k27gd1(t242, GM_CONV(gm_csguankko4lmhd, t243));
                    if (!t244) goto ie239;
                    {
                        void * t246 = gm_l0;
                        void * t247 = GM_LD(void *, (((uint8_t *)t246) + 0));
                        gm_tl874t2atnlh3 t248 = gm_l4;
                        void * t249 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t247, t248);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t250 = 0;
                        F.ret = t249;
                        goto gm_fret;
                    }
                    goto ix239;
ie239:;
ix239:;
                }
                {
                    void * t251 = gm_l0;
                    gm_t88rcucg1b7uk t252 = gm_l24;
                    gm_b16_8 t253 = gm_l2;
                    gm_tl874t2atnlh3 t254 = gm_l4;
                    void * t255 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t251, t252, t253, t254);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t256 = 0;
                    F.ret = t255;
                    goto gm_fret;
                }
                goto ix235;
ie235:;
ix235:;
            }
            {
                uint8_t t258 = ((GM_LD(uint8_t, ((uint8_t *)&F.l22) + 0) == ((uint8_t)2ULL)));
                if (!t258) goto ie257;
                {
                    void * t260 = gm_l0;
                    void * t261 = gm_l1;
                    gm_tl874t2atnlh3 t262 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t261) + 72));
                    gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                    gm_tnpifv20irnph t264;
                    GM_ST(gm_b16_8, ((uint8_t *)&t264 + 0), t263);
                    gm_tabb710d9mq2e t265;
                    gm_f_format_q8r21un((void *)&t265, (void *)NULL, cx, NULL, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t265))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t265) + 8, gm_srch6h8rdicu443, 57, 43);
                    gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&t265) + 8));
                    gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&t266));
                    gm_b0_1 t268 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t260, t262, t267);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t269 = gm_l0;
                    void * t270 = GM_LD(void *, (((uint8_t *)t269) + 0));
                    gm_tl874t2atnlh3 t271 = gm_l4;
                    void * t272 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t270, t271);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t273 = 0;
                    F.ret = t272;
                    goto gm_fret;
                }
                goto ix257;
ie257:;
ix257:;
            }
            {
                void * t274 = gm_l0;
                void * t275 = gm_l0;
                void * t276 = gm_l1;
                gm_tkn42u7na3t81 t277;
                memset(&t277, 0, sizeof t277);
                void * t278 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t275, t276, t277);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t279 = gm_l2;
                gm_tl874t2atnlh3 t280 = gm_l4;
                void * t281 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t274, t278, t279, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t282 = 0;
                F.ret = t281;
                goto gm_fret;
            }
            goto ix204;
ie204:;
ix204:;
        }
        {
            void * t284 = gm_l0;
            void * t285 = GM_LD(void *, (((uint8_t *)t284) + 0));
            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tkn42u7na3t81 t287 = gm_f_type_param_3av4r8i(t285, t286);
            uint8_t t288;
            if (((((uint8_t *)&t287))[4] != 0)) goto mn290;
            t288 = ((uint8_t)0);
            goto me289;
mn290:;
            t288 = ((uint8_t)1);
            goto me289;
mn291:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 62, 16);
me289:;
            if (!t288) goto ie283;
            {
                gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                gm_l25 = t293;
            }
            {
                void * t294 = gm_l0;
                void * t295 = gm_l1;
                gm_tl874t2atnlh3 t296 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t295) + 72));
                gm_t0uuhaljsf410 t297;
                gm_b16_8 t298 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t297 + 0, t298);
                gm_b16_8 t299 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t297 + 16, t299);
                gm_b16_8 t300 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t297 + 32, t300);
                gm_b16_8 t301 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t297 + 48, t301);
                gm_tabb710d9mq2e t302;
                gm_f_format_9ijbt7j((void *)&t302, (void *)NULL, cx, NULL, &t297);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 8, gm_srch6h8rdicu443, 64, 39);
                gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&t302) + 8));
                gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
                gm_b0_1 t305 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t294, t296, t304);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t306 = gm_l0;
                void * t307 = GM_LD(void *, (((uint8_t *)t306) + 0));
                gm_tl874t2atnlh3 t308 = gm_l4;
                void * t309 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t307, t308);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t310 = 0;
                F.ret = t309;
                goto gm_fret;
            }
            goto ix283;
ie283:;
ix283:;
        }
        {
            void * t312 = gm_l0;
            void * t313 = GM_LD(void *, (((uint8_t *)t312) + 0));
            void * t314 = gm_l0;
            uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)t314) + 8));
            gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tf5srh4mp4b3i t317 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t313, t315, t316);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t317))[8] != 0)) goto ie311;
            uint64_t t318 = GM_LD(uint64_t, ((uint8_t *)&t317));
            gm_l26 = t318;
            {
                void * t320 = gm_l0;
                void * t321 = GM_LD(void *, (((uint8_t *)t320) + 0));
                uint64_t t322 = gm_l26;
                gm_tl874t2atnlh3 t323 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                gm_b0_1 t324 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t321, t322, t323);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t325 = gm_l0;
                uint64_t t326 = gm_l26;
                gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                void * t328 = gm_l1;
                gm_b16_8 t329 = gm_l2;
                gm_tl874t2atnlh3 t330 = gm_l4;
                void * t331 = gm_f_call_decl_sj6f05u((void *)NULL, cx, NULL, t325, t326, t327, t328, t329, t330);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t332 = 0;
                F.ret = t331;
                goto gm_fret;
            }
            goto ix311;
ie311:;
ix311:;
        }
        {
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s2r7keqsravo0, 5) == 0)) goto mn334;
            void * t335 = gm_l0;
            gm_b16_8 t336 = gm_l2;
            gm_tl874t2atnlh3 t337 = gm_l4;
            void * t338 = gm_f_print_bhebp9d((void *)NULL, cx, NULL, t335, ((uint8_t)1), t336, t337);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t339 = 0;
            F.ret = t338;
            goto gm_fret;
            goto me333;
mn334:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 6) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_so4gnucc3fl4v, 6) == 0)) goto mn340;
            void * t341 = gm_l0;
            gm_b16_8 t342 = gm_l2;
            gm_tl874t2atnlh3 t343 = gm_l4;
            void * t344 = gm_f_print_bhebp9d((void *)NULL, cx, NULL, t341, ((uint8_t)0), t342, t343);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t345 = 0;
            F.ret = t344;
            goto gm_fret;
            goto me333;
mn340:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_sinmpfqtbo9in, 4) == 0)) goto mn346;
            void * t347 = gm_l0;
            gm_b16_8 t348 = gm_l2;
            gm_tl874t2atnlh3 t349 = gm_l4;
            void * t350 = gm_f_trap_t21p26s((void *)NULL, cx, NULL, t347, t348, t349);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t351 = 0;
            F.ret = t350;
            goto gm_fret;
            goto me333;
mn346:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 6) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s9ur7sj9r0g63, 6) == 0)) goto mn352;
            void * t353 = gm_l0;
            gm_b16_8 t354 = gm_l2;
            gm_tl874t2atnlh3 t355 = gm_l4;
            void * t356 = gm_f_assert__dllohqd((void *)NULL, cx, NULL, t353, t354, t355);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t357 = 0;
            F.ret = t356;
            goto gm_fret;
            goto me333;
mn352:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s3231a310mran, 9) == 0)) goto mn358;
            void * t359 = gm_l0;
            gm_b16_8 t360 = gm_l2;
            gm_tl874t2atnlh3 t361 = gm_l4;
            void * t362 = gm_f_expect_eq_iu4ojmi((void *)NULL, cx, NULL, t359, t360, t361);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t363 = 0;
            F.ret = t362;
            goto gm_fret;
            goto me333;
mn358:;
            goto me333;
mn364:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 71, 13);
me333:;
        }
        {
            void * t367 = gm_l0;
            void * t368 = GM_LD(void *, (((uint8_t *)t367) + 0));
            gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tkn42u7na3t81 t370 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t368, t369);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t370))[4] != 0)) goto ie366;
            gm_t88rcucg1b7uk t371 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t370));
            gm_l27 = t371;
            {
                void * t373 = gm_l0;
                gm_t88rcucg1b7uk t374 = gm_l27;
                gm_b16_8 t375 = gm_l2;
                gm_tl874t2atnlh3 t376 = gm_l4;
                void * t377 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t373, t374, t375, t376);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t378 = 0;
                F.ret = t377;
                goto gm_fret;
            }
            goto ix366;
ie366:;
ix366:;
        }
        {
            uint8_t t380 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s4g2k7ups3ui6, 5) == 0));
            if (!t380) goto ie379;
            void * t381 = gm_l0;
            void * t382 = GM_LD(void *, (((uint8_t *)t381) + 0));
            if (!(((((uint8_t *)t382) + 1956))[4] != 0)) goto ie379;
            uint32_t t383 = GM_LD(uint32_t, (((uint8_t *)t382) + 1956));
            gm_l28 = t383;
            {
                void * t385 = gm_l0;
                uint32_t t386 = gm_l28;
                gm_b16_8 t387 = gm_l2;
                gm_tl874t2atnlh3 t388 = gm_l4;
                void * t389 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t385, t386, ((uint32_t)0ULL), t387, t388);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t390 = 0;
                F.ret = t389;
                goto gm_fret;
            }
            goto ix379;
ie379:;
ix379:;
        }
        {
            void * t391 = gm_l0;
            void * t392 = gm_l0;
            void * t393 = gm_l1;
            gm_tkn42u7na3t81 t394;
            memset(&t394, 0, sizeof t394);
            void * t395 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t392, t393, t394);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t396 = gm_l2;
            gm_tl874t2atnlh3 t397 = gm_l4;
            void * t398 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t391, t395, t396, t397);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t399 = 0;
            F.ret = t398;
            goto gm_fret;
        }
        goto me105;
mn200:;
ma109:;
        uint8_t t401 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
        if (t401 != ((uint8_t)19ULL)) goto mn400;
        {
            void * t404 = gm_l0;
            void * t405 = gm_l1;
            gm_tkn42u7na3t81 t406 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t404, t405);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t406))[4] != 0)) goto ie403;
            gm_t88rcucg1b7uk t407 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t406));
            gm_l29 = t407;
            {
                void * t409 = gm_l0;
                gm_t88rcucg1b7uk t410 = gm_l29;
                gm_b16_8 t411 = gm_l2;
                gm_tl874t2atnlh3 t412 = gm_l4;
                void * t413 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t409, t410, t411, t412);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t414 = 0;
                F.ret = t413;
                goto gm_fret;
            }
            goto ix403;
ie403:;
ix403:;
        }
        {
            void * t416 = gm_l0;
            void * t417 = GM_LD(void *, (((uint8_t *)t416) + 0));
            void * t418 = gm_l0;
            uint64_t t419 = GM_LD(uint64_t, (((uint8_t *)t418) + 8));
            void * t420 = gm_l1;
            uint8_t t421 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t417, t419, t420);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t421) goto ie415;
            void * t422 = gm_l0;
            void * t423 = GM_LD(void *, (((uint8_t *)t422) + 0));
            void * t424 = (void *)(((uint8_t *)t423) + 8);
            void * t425 = gm_l0;
            void * t426 = gm_l1;
            gm_t88rcucg1b7uk t427 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t425, t426);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t428 = gm_f_is_error_8k27gd1(t424, GM_CONV(gm_csguankko4lmhd, t427));
            if (!t428) goto ie415;
            {
                void * t430 = gm_l0;
                void * t431 = GM_LD(void *, (((uint8_t *)t430) + 0));
                gm_tl874t2atnlh3 t432 = gm_l4;
                void * t433 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t431, t432);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t434 = 0;
                F.ret = t433;
                goto gm_fret;
            }
            goto ix415;
ie415:;
ix415:;
        }
        {
            void * t435 = gm_l0;
            void * t436 = gm_l0;
            void * t437 = gm_l1;
            gm_tkn42u7na3t81 t438;
            memset(&t438, 0, sizeof t438);
            void * t439 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t436, t437, t438);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t440 = gm_l2;
            gm_tl874t2atnlh3 t441 = gm_l4;
            void * t442 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t435, t439, t440, t441);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t443 = 0;
            F.ret = t442;
            goto gm_fret;
        }
        goto me105;
mn400:;
ma110:;
        uint8_t t445 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
        if (t445 != ((uint8_t)10ULL)) goto mn444;
        gm_b16_8 t446 = GM_LD(gm_b16_8, ((((uint8_t *)t104) + 8) + 8));
        gm_l30 = t446;
        {
            uint64_t t449 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
            uint8_t t450 = (t449 > ((uint64_t)0ULL));
            if (!t450) goto ie448;
            void * t451 = gm_l0;
            void * t452 = GM_LD(void *, (((uint8_t *)t451) + 0));
            void * t453 = gm_l0;
            uint64_t t454 = GM_LD(uint64_t, (((uint8_t *)t453) + 8));
            void * t455 = gm_l1;
            uint8_t t456 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t452, t454, t455);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t456) goto ie448;
            {
                void * t458 = gm_l0;
                void * t459 = gm_l1;
                gm_t88rcucg1b7uk t460 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t458, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l31 = t460;
            }
            {
                void * t462 = gm_l0;
                void * t463 = GM_LD(void *, (((uint8_t *)t462) + 0));
                void * t464 = (void *)(((uint8_t *)t463) + 8);
                gm_t88rcucg1b7uk t465 = gm_l31;
                uint8_t t466 = gm_f_is_error_8k27gd1(t464, GM_CONV(gm_csguankko4lmhd, t465));
                if (!t466) goto ie461;
                {
                    void * t468 = gm_l0;
                    void * t469 = GM_LD(void *, (((uint8_t *)t468) + 0));
                    gm_tl874t2atnlh3 t470 = gm_l4;
                    void * t471 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t469, t470);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t472 = 0;
                    F.ret = t471;
                    goto gm_fret;
                }
                goto ix461;
ie461:;
ix461:;
            }
            {
                void * t473 = gm_l0;
                gm_t88rcucg1b7uk t474 = gm_l31;
                gm_b16_8 t475 = gm_l2;
                gm_tl874t2atnlh3 t476 = gm_l4;
                void * t477 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t473, t474, t475, t476);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t478 = 0;
                F.ret = t477;
                goto gm_fret;
            }
            goto ix448;
ie448:;
ix448:;
        }
        {
            void * t479 = gm_l0;
            void * t480 = gm_l0;
            void * t481 = gm_l1;
            gm_tkn42u7na3t81 t482;
            memset(&t482, 0, sizeof t482);
            void * t483 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t480, t481, t482);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t484 = gm_l2;
            gm_tl874t2atnlh3 t485 = gm_l4;
            void * t486 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t479, t483, t484, t485);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t487 = 0;
            F.ret = t486;
            goto gm_fret;
        }
        goto me105;
mn444:;
ma111:;
        void * t489 = gm_l0;
        void * t490 = gm_l0;
        void * t491 = gm_l1;
        gm_tkn42u7na3t81 t492;
        memset(&t492, 0, sizeof t492);
        void * t493 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t490, t491, t492);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t494 = gm_l2;
        gm_tl874t2atnlh3 t495 = gm_l4;
        void * t496 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t489, t493, t494, t495);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t497 = 0;
        F.ret = t496;
        goto gm_fret;
        goto me105;
mn488:;
ma112:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 25, 5);
me105:;
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
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_call_value_t2nv21i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t88rcucg1b7uk gm_l11; /* p */
    gm_t6j6smj7qoctu gm_l12; /* bound */
    void * gm_l13; /* callee */
    void * gm_l15; /* call */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_trcmmd12cefb1 gm_l18; /* st */
    GM_STACK_CHECK(&F, 1488u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 96);
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        gm_t88rcucg1b7uk t8 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t3, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        gm_t88rcucg1b7uk t12 = gm_l4;
        gm_t53at80pfdus3 t13;
        gm_f_kind_jcnt4hm((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
        gm_tddtjfggdat0d t14;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t19 != ((uint8_t)16ULL)) goto pa18;
        F.l5 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t13) + 8));
        goto po17;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t21 != ((uint8_t)15ULL)) goto pa20;
        F.l5 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t13) + 8));
        goto po17;
pa20:;
        goto mn16;
po17:;
        gm_tddtjfggdat0d t22 = F.l5;
        t14 = t22;
        goto me15;
mn16:;
        {
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            void * t28 = (void *)(((uint8_t *)t27) + 8);
            gm_t88rcucg1b7uk t29 = gm_l4;
            uint8_t t30 = gm_f_is_error_8k27gd1(t28, GM_CONV(gm_csguankko4lmhd, t29));
            uint8_t t31 = (uint8_t)!t30;
            if (!t31) goto ie25;
            {
                void * t33 = gm_l0;
                void * t34 = gm_l1;
                gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
                void * t36 = gm_l0;
                gm_t88rcucg1b7uk t37 = gm_l4;
                gm_b16_8 t38 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t39;
                GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
                gm_tabb710d9mq2e t40;
                gm_f_format_jaikcua((void *)&t40, (void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srch6h8rdicu443, 110, 60);
                gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                gm_b0_1 t43 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t33, t35, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix25;
ie25:;
ix25:;
        }
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        gm_tl874t2atnlh3 t46 = gm_l3;
        void * t47 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
        goto me15;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 107, 12);
me15:;
        F.l7 = t14;
    }
    {
        gm_f_list_af74s0q((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_l9 = t50;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t52 = gm_l10;
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t54 = (t52 < t53);
                if (!t54) goto lb51;
                {
                    uint64_t t57 = gm_l10;
                    uint64_t t58 = (uintptr_t)t57;
                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t58 * 4));
                    gm_l11 = t62;
                    {
                        void * t64 = (void *)((uint8_t *)&F.l8);
                        gm_t5v0c8ia6a5mo t65;
                        memset((uint8_t *)&t65 + 49, 0, 7);
                        gm_b16_8 t66;
                        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sfdbfrv46lihd));
                        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(1));
                        GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t66);
                        gm_t88rcucg1b7uk t67 = gm_l11;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t65 + 16, t67);
                        gm_tl874t2atnlh3 t68 = gm_l3;
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t65 + 20, t68);
                        GM_ST(void *, (uint8_t *)&t65 + 32, ((void *)0));
                        gm_t9805qtonbsag t69;
                        memset(&t69, 0, sizeof t69);
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t65 + 40, t69);
                        GM_ST(uint8_t, (uint8_t *)&t65 + 48, ((uint8_t)0));
                        gm_tftb1odc739t5 t70 = gm_f_push_o8u01sh(t64, &t65);
                        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srch6h8rdicu443, 115, 27);
                        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                    }
                    goto me60;
mn61:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 115, 5);
me60:;
                }
lc51:;
                {
                    uint64_t t73 = gm_l10;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l10 = t76;
                }
            }
lb51:;
        }
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
        void * t80 = gm_l0;
        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)t80) + 8));
        gm_b16_8 t82 = gm_l2;
        gm_tl874t2atnlh3 t83 = gm_l3;
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sq0dqiq83b924));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(8));
        gm_ttu09lij36f8c t85;
        gm_f_bind_args_dnih8ih((void *)&t85, (void *)NULL, cx, NULL, t77, t79, t81, t82, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t86;
        if (((((uint8_t *)&t85))[32] != 0)) {
            t86 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t85) + 0));
        } else {
            void * t87 = gm_l0;
            void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
            gm_tl874t2atnlh3 t89 = gm_l3;
            void * t90 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t91 = 0;
            F.ret = t90;
            goto gm_fret;
        }
        gm_l12 = t86;
    }
    {
        void * t92 = gm_l0;
        void * t93 = (void *)(((uint8_t *)&F.l7) + 24);
        gm_tl874t2atnlh3 t94 = gm_l3;
        gm_b0_1 t95 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t92, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t96 = gm_l1;
        gm_l13 = t96;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 16) + 8);
        uint8_t t99 = (t98 > ((uint64_t)0ULL));
        if (!t99) goto ie97;
        {
            void * t101 = gm_l0;
            void * t102 = gm_l1;
            void * t103 = (void *)((uint8_t *)&F.l14);
            void * t104 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t101, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t104;
        }
        goto ix97;
ie97:;
ix97:;
    }
    {
        gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
        gm_to76jli3bqddq t106;
        void * t107 = gm_l13;
        gm_tsth9qu1dv0g3 t108;
        GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t108 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t108 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t108 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&t108 + 8), t107);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t106 + 0, t108);
        gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t106 + 40, t109);
        gm_teti411eujvf4 t110;
        GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t110 + 8), t106);
        gm_tl874t2atnlh3 t111 = gm_l3;
        void * t112 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t105, &t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t112;
    }
    {
        uint64_t t114 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 16) + 8);
        uint8_t t115 = ((t114 == ((uint64_t)0ULL)));
        if (!t115) goto ie113;
        {
            void * t117 = gm_l15;
            uint8_t t118 = 0;
            F.ret = t117;
            goto gm_fret;
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        {
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
            gm_l16 = t120;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t122 = gm_l17;
                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t124 = (t122 < t123);
                if (!t124) goto lb121;
                {
                    uint64_t t127 = gm_l17;
                    uint64_t t128 = (uintptr_t)t127;
                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_trcmmd12cefb1 t132 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t128 * 32));
                    gm_l18 = t132;
                    {
                        void * t134 = (void *)((uint8_t *)&F.l14);
                        gm_tftb1odc739t5 t135 = gm_f_push_d6c76ud_2(t134, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l18)));
                        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srch6h8rdicu443, 123, 27);
                        gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                    }
                    goto me130;
mn131:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 123, 5);
me130:;
                }
lc121:;
                {
                    uint64_t t138 = gm_l17;
                    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                    uint64_t t141 = r139;
                    gm_l17 = t141;
                }
            }
lb121:;
        }
    }
    {
        void * t142 = gm_l15;
        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + 0));
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t145 = gm_l15;
        gm_tl874t2atnlh3 t146 = gm_l3;
        void * t147 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t143, t144, t145, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t148 = 0;
        F.ret = t147;
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
inline __attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_optional_cffo1ol F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 662u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t5 = (!(t4 == ((uint64_t)1ULL)));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srch6h8rdicu443, 483, 29);
            uint8_t t8;
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t6 * 144) + 0) + 0));
            if (t11 != ((uint8_t)0ULL)) goto mn10;
            t8 = ((uint8_t)0);
            goto me9;
mn10:;
            t8 = ((uint8_t)1);
            goto me9;
mn12:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 483, 29);
me9:;
            t3 = t8;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l4;
            void * t16 = gm_l0;
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_tabb710d9mq2e t20;
            gm_f_format_mq9dnpb((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srch6h8rdicu443, 484, 24);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t22);
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
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srch6h8rdicu443, 487, 23);
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 144) + 0) + 0));
        if (t32 != ((uint8_t)0ULL)) goto ie29;
        gm_tc3oht4ksth9d t33 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 144) + 0) + 8));
        F.l5 = t33;
        {
            uint8_t t36 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 8) + 0) == ((uint8_t)4ULL)));
            if (!t36) goto ie35;
            {
                gm_t88rcucg1b7uk t38 = gm_l1;
                gm_teti411eujvf4 t39;
                memset(&t39, 0, sizeof t39);
                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)7ULL));
                gm_tl874t2atnlh3 t40 = gm_l4;
                void * t41 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t38, &t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            void * t43 = gm_l0;
            void * t44 = gm_l0;
            void * t45 = (void *)((uint8_t *)&F.l5);
            gm_t88rcucg1b7uk t46 = gm_l2;
            gm_tkn42u7na3t81 t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47), t46);
            ((uint8_t *)&t47)[4] = 1;
            void * t48 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t44, t45, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t49 = gm_l2;
            void * t50 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t43, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t50;
        }
        {
            gm_t88rcucg1b7uk t51 = gm_l1;
            gm_tmn7n0n9raou5 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)2ULL));
            void * t53 = gm_l6;
            gm_t8jsc4n9vp2ib t54;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t54 + 0), t52);
            GM_ST(void *, ((uint8_t *)&t54 + 8), t53);
            gm_teti411eujvf4 t55;
            memset(&t55, 0, sizeof t55);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t55 + 8), t54);
            gm_tl874t2atnlh3 t56 = gm_l4;
            void * t57 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t51, &t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix29;
ie29:;
        {
            void * t60 = gm_l0;
            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 0));
            gm_tl874t2atnlh3 t62 = gm_l4;
            void * t63 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t64 = 0;
            F.ret = t63;
            goto gm_fret;
        }
ix29:;
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
__attribute__((aligned(4))) void * gm_f_conversion_2qjq323(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_conversion_2qjq323 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    uint32_t gm_l5; /* id */
    uint8_t gm_l7; /* one_positional */
    gm_b16_8 gm_l8; /* fields */
    gm_t08olq97rscn3 gm_l9; /* n */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l16; /* v */
    gm_b16_8 gm_l18; /* elems */
    gm_b16_8 gm_l19; /* names */
    uint64_t gm_l20; /* len */
    gm_t88rcucg1b7uk gm_l21; /* elem */
    gm_tnnjeerolos0a gm_l22; /* kind */
    gm_t88rcucg1b7uk gm_l23; /* index */
    gm_t88rcucg1b7uk gm_l24; /* inner */
    gm_t88rcucg1b7uk gm_l25; /* elem */
    gm_t88rcucg1b7uk gm_l27; /* u */
    void * gm_l28; /* v */
    void * gm_l29; /* v */
    gm_t88rcucg1b7uk gm_l30; /* vt */
    GM_STACK_CHECK(&F, 4082u);
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
        void * t6 = gm_l4;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)17ULL)) goto ie5;
        uint32_t t10 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l5 = t10;
        {
            uint32_t t12 = gm_l5;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srch6h8rdicu443, 406, 15);
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 0));
            switch (t23) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma20; default: goto ma21; }
ma19:;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t25 != ((uint8_t)0ULL)) goto mn24;
            gm_tpautr4br7h3l t26 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 8));
            F.l6 = t26;
            {
                uint8_t t28 = 0;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t30 = ((t29 == ((uint64_t)1ULL)));
                if (t30) {
                    uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srch6h8rdicu443, 409, 53);
                    uint8_t t33;
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t31 * 144) + 0) + 0));
                    if (t36 != ((uint8_t)0ULL)) goto mn35;
                    t33 = ((uint8_t)1);
                    goto me34;
mn35:;
                    t33 = ((uint8_t)0);
                    goto me34;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 409, 53);
me34:;
                    t28 = t33;
                } else t28 = 0;
                gm_l7 = t28;
            }
            {
                uint8_t t39 = 0;
                uint8_t t40 = gm_l7;
                if (t40) {
                    uint8_t t41;
                    if ((((((uint8_t *)&F.l6) + 40))[4] != 0)) goto mn43;
                    t41 = ((uint8_t)0);
                    goto me42;
mn43:;
                    t41 = ((uint8_t)1);
                    goto me42;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 410, 44);
me42:;
                    t39 = t41;
                } else t39 = 0;
                uint8_t t45 = (uint8_t)!t39;
                if (!t45) goto ie38;
                {
                    void * t47 = gm_l0;
                    gm_t88rcucg1b7uk t48 = gm_l1;
                    gm_b16_8 t49 = gm_l2;
                    gm_tl874t2atnlh3 t50 = gm_l3;
                    void * t51 = gm_f_construct_struct_ve6tb88((void *)NULL, cx, NULL, t47, t48, t49, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t52 = 0;
                    F.ret = t51;
                    goto gm_fret;
                }
                goto ix38;
ie38:;
ix38:;
            }
            goto me18;
mn24:;
ma20:;
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t54 != ((uint8_t)2ULL)) goto mn53;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 8));
            gm_l8 = t55;
            {
                uint8_t t58 = 0;
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t60 = (!(t59 == ((uint64_t)1ULL)));
                if (!t60) {
                    uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t61 >= (uintptr_t)(t62)) gm_trap_index(t61, t62, gm_srch6h8rdicu443, 414, 41);
                    uint8_t t63;
                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t61 * 144) + 0) + 0));
                    if (t66 != ((uint8_t)1ULL)) goto mn65;
                    t63 = ((uint8_t)0);
                    goto me64;
mn65:;
                    t63 = ((uint8_t)1);
                    goto me64;
mn67:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 414, 41);
me64:;
                    t58 = t63;
                } else t58 = 1;
                if (!t58) goto ie57;
                {
                    void * t69 = gm_l0;
                    gm_tl874t2atnlh3 t70 = gm_l3;
                    void * t71 = gm_l0;
                    gm_t88rcucg1b7uk t72 = gm_l1;
                    gm_b16_8 t73 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t71, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t74;
                    GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
                    gm_tabb710d9mq2e t75;
                    gm_f_format_cqv4884((void *)&t75, (void *)NULL, cx, NULL, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srch6h8rdicu443, 415, 36);
                    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                    gm_b0_1 t78 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t69, t70, t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t79 = gm_l0;
                    void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                    gm_tl874t2atnlh3 t81 = gm_l3;
                    void * t82 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t80, t81);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t83 = 0;
                    F.ret = t82;
                    goto gm_fret;
                }
                goto ix57;
ie57:;
ix57:;
            }
            {
                uint64_t t85 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srch6h8rdicu443, 418, 33);
                uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t85 * 144) + 0) + 0));
                if (t87 != ((uint8_t)1ULL)) goto ie84;
                gm_t08olq97rscn3 t88 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t85 * 144) + 0) + 8) + 0));
                gm_l9 = t88;
                gm_tc3oht4ksth9d t89 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t85 * 144) + 0) + 8) + 32));
                F.l10 = t89;
                {
                    gm_tnpj2jtv1r8pa t91;
                    memset(&t91, 0, sizeof t91);
                    F.l11 = t91;
                }
                {
                    {
                        gm_b16_8 t93 = gm_l8;
                        gm_l12 = t93;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t95 = gm_l13;
                            uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            uint8_t t97 = (t95 < t96);
                            if (!t97) goto lb94;
                            {
                                uint64_t t100 = gm_l13;
                                uint64_t t101 = (uintptr_t)t100;
                                uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                gm_t0mglksrkfa5e t105 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t101 * 40));
                                F.l14 = t105;
                                {
                                    uint8_t t108;
                                    if (((((uint8_t *)&F.l11))[40] != 0)) goto mn110;
                                    t108 = ((uint8_t)1);
                                    goto me109;
mn110:;
                                    t108 = ((uint8_t)0);
                                    goto me109;
mn111:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 421, 24);
me109:;
                                    if (!t108) goto ie107;
                                    gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                    gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                                    uint8_t t114 = (gm_eq0iqiubfm7e(((uint8_t *)&t112), ((uint8_t *)&t113)));
                                    if (!t114) goto ie107;
                                    {
                                        const uint8_t *t116 = ((uint8_t *)&F.l14);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tnpj2jtv1r8pa *)((uint8_t *)&F.l11)) + 41, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tnpj2jtv1r8pa *)((uint8_t *)&F.l11)) + 42, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tnpj2jtv1r8pa *)((uint8_t *)&F.l11)) + 44, 0);
                                        ((uint8_t *)&(*(gm_tnpj2jtv1r8pa *)((uint8_t *)&F.l11)))[40] = 1;
                                        memmove((uint8_t *)&(*(gm_tnpj2jtv1r8pa *)((uint8_t *)&F.l11)) + 0, t116, 40);
                                    }
                                    goto ix107;
ie107:;
ix107:;
                                }
                                goto me103;
mn104:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 420, 17);
me103:;
                            }
lc94:;
                            {
                                uint64_t t118 = gm_l13;
                                uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                uint64_t t121 = r119;
                                gm_l13 = t121;
                            }
                        }
lb94:;
                    }
                }
                {
                    gm_tnpj2jtv1r8pa t122 = F.l11;
                    gm_t0mglksrkfa5e t123;
                    if (((((uint8_t *)&t122))[40] != 0)) {
                        t123 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t122) + 0));
                    } else {
                        {
                            void * t125 = gm_l0;
                            gm_tl874t2atnlh3 t126 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
                            gm_t6j6smj7qoctu t127;
                            void * t128 = gm_l0;
                            gm_t88rcucg1b7uk t129 = gm_l1;
                            gm_b16_8 t130 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t128, t129);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t127 + 0, t130);
                            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t127 + 16, t131);
                            gm_tabb710d9mq2e t132;
                            gm_f_format_lc07prd((void *)&t132, (void *)NULL, cx, NULL, &t127);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 8, gm_srch6h8rdicu443, 424, 38);
                            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                            gm_b0_1 t135 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t125, t126, t134);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t136 = gm_l0;
                        void * t137 = GM_LD(void *, (((uint8_t *)t136) + 0));
                        gm_tl874t2atnlh3 t138 = gm_l3;
                        void * t139 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t137, t138);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t140 = 0;
                        F.ret = t139;
                        goto gm_fret;
                    }
                    F.l15 = t123;
                }
                {
                    void * t141 = gm_l0;
                    void * t142 = gm_l0;
                    void * t143 = (void *)((uint8_t *)&F.l10);
                    gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                    gm_tkn42u7na3t81 t145;
                    GM_ST(uint8_t, (uint8_t *)&t145 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t145 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t145), t144);
                    ((uint8_t *)&t145)[4] = 1;
                    void * t146 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t142, t143, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                    void * t148 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t141, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t148;
                }
                {
                    gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t149 = (void *)((uint8_t *)&F.l17);
                    void * t150 = gm_l16;
                    gm_tftb1odc739t5 t151 = gm_f_push_d943vgp_2(t149, &(*(gm_t69m56meqefs8 *)((uint8_t *)t150)));
                    if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srch6h8rdicu443, 429, 17);
                    gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
                }
                {
                    gm_t88rcucg1b7uk t153 = gm_l1;
                    gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                    gm_teti411eujvf4 t155;
                    memset(&t155, 0, sizeof t155);
                    GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t155 + 8), t154);
                    gm_tl874t2atnlh3 t156 = gm_l3;
                    void * t157 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t153, &t155, t156);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t158 = 0;
                    F.ret = t157;
                    goto gm_fret;
                }
                goto ix84;
ie84:;
                {
                    void * t160 = gm_l0;
                    void * t161 = GM_LD(void *, (((uint8_t *)t160) + 0));
                    gm_tl874t2atnlh3 t162 = gm_l3;
                    void * t163 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t161, t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t164 = 0;
                    F.ret = t163;
                    goto gm_fret;
                }
ix84:;
            }
            goto me18;
mn53:;
ma21:;
            goto me18;
mn165:;
ma22:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 406, 9);
me18:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t167 = gm_l4;
        gm_t88rcucg1b7uk t168 = gm_l1;
        gm_t53at80pfdus3 t169;
        gm_f_kind_jcnt4hm((void *)&t169, t167, GM_CONV(gm_csguankko4lmhd, t168));
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&t169) + 0));
        switch (t177) { case ((uint8_t)0ULL): goto ma175; case ((uint8_t)1ULL): goto ma175; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; case ((uint8_t)7ULL): goto ma175; case ((uint8_t)8ULL): goto ma175; case ((uint8_t)9ULL): goto ma175; case ((uint8_t)10ULL): goto ma175; case ((uint8_t)11ULL): goto ma172; case ((uint8_t)12ULL): goto ma171; case ((uint8_t)13ULL): goto ma173; case ((uint8_t)14ULL): goto ma175; case ((uint8_t)15ULL): goto ma175; case ((uint8_t)16ULL): goto ma175; case ((uint8_t)17ULL): goto ma175; case ((uint8_t)18ULL): goto ma175; case ((uint8_t)19ULL): goto ma175; case ((uint8_t)20ULL): goto ma174; case ((uint8_t)21ULL): goto ma175; case ((uint8_t)22ULL): goto ma175; case ((uint8_t)23ULL): goto ma175; case ((uint8_t)24ULL): goto ma175; case ((uint8_t)25ULL): goto ma175; case ((uint8_t)26ULL): goto ma175; default: goto ma175; }
ma171:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&t169) + 0));
        if (t179 != ((uint8_t)12ULL)) goto mn178;
        gm_b16_8 t180 = GM_LD(gm_b16_8, ((((uint8_t *)&t169) + 8) + 0));
        gm_l18 = t180;
        gm_b16_8 t181 = GM_LD(gm_b16_8, ((((uint8_t *)&t169) + 8) + 16));
        gm_l19 = t181;
        void * t182 = gm_l0;
        gm_t88rcucg1b7uk t183 = gm_l1;
        gm_b16_8 t184 = gm_l18;
        gm_b16_8 t185 = gm_l19;
        gm_b16_8 t186 = gm_l2;
        gm_tl874t2atnlh3 t187 = gm_l3;
        void * t188 = gm_f_construct_tuple_6ee5ejf((void *)NULL, cx, NULL, t182, t183, t184, t185, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t189 = 0;
        F.ret = t188;
        goto gm_fret;
        goto me170;
mn178:;
ma172:;
        uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&t169) + 0));
        if (t191 != ((uint8_t)11ULL)) goto mn190;
        uint64_t t192 = GM_LD(uint64_t, ((((uint8_t *)&t169) + 8) + 0));
        gm_l20 = t192;
        gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t169) + 8) + 8));
        gm_l21 = t193;
        gm_tnnjeerolos0a t194 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t169) + 8) + 12));
        gm_l22 = t194;
        {
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t197 != ((uint8_t)1ULL)) goto ie196;
            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 4));
            gm_l23 = t198;
            {
                void * t200 = gm_l0;
                gm_t88rcucg1b7uk t201 = gm_l1;
                gm_t88rcucg1b7uk t202 = gm_l23;
                gm_t88rcucg1b7uk t203 = gm_l21;
                gm_b16_8 t204 = gm_l2;
                gm_tl874t2atnlh3 t205 = gm_l3;
                void * t206 = gm_f_construct_enum_array_d7cisuu((void *)NULL, cx, NULL, t200, t201, t202, t203, t204, t205);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t207 = 0;
                F.ret = t206;
                goto gm_fret;
            }
            goto ix196;
ie196:;
ix196:;
        }
        {
            void * t208 = gm_l0;
            gm_t88rcucg1b7uk t209 = gm_l1;
            gm_tkn42u7na3t81 t210;
            GM_ST(uint8_t, (uint8_t *)&t210 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t210 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t210), t209);
            ((uint8_t *)&t210)[4] = 1;
            uint64_t t211 = gm_l20;
            gm_tf5srh4mp4b3i t212;
            GM_ST(uint8_t, (uint8_t *)&t212 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t212 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t212 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t212), t211);
            ((uint8_t *)&t212)[8] = 1;
            gm_t88rcucg1b7uk t213 = gm_l21;
            gm_b16_8 t214 = gm_l2;
            gm_tl874t2atnlh3 t215 = gm_l3;
            void * t216 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t208, t210, t212, t213, t214, t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t217 = 0;
            F.ret = t216;
            goto gm_fret;
        }
        goto me170;
mn190:;
ma173:;
        uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&t169) + 0));
        if (t219 != ((uint8_t)13ULL)) goto mn218;
        gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t169) + 8));
        gm_l24 = t220;
        void * t221 = gm_l0;
        gm_t88rcucg1b7uk t222 = gm_l1;
        gm_t88rcucg1b7uk t223 = gm_l24;
        gm_b16_8 t224 = gm_l2;
        gm_tl874t2atnlh3 t225 = gm_l3;
        void * t226 = gm_f_construct_optional_cffo1ol((void *)NULL, cx, NULL, t221, t222, t223, t224, t225);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t227 = 0;
        F.ret = t226;
        goto gm_fret;
        goto me170;
mn218:;
ma174:;
        uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&t169) + 0));
        if (t229 != ((uint8_t)20ULL)) goto mn228;
        gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t169) + 8) + 4));
        gm_l25 = t230;
        void * t231 = gm_l0;
        gm_t88rcucg1b7uk t232 = gm_l1;
        gm_t88rcucg1b7uk t233 = gm_l25;
        gm_b16_8 t234 = gm_l2;
        gm_tl874t2atnlh3 t235 = gm_l3;
        void * t236 = gm_f_construct_param_array_btk60nl((void *)NULL, cx, NULL, t231, t232, t233, t234, t235);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t237 = 0;
        F.ret = t236;
        goto gm_fret;
        goto me170;
mn228:;
ma175:;
        goto me170;
mn238:;
ma176:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 435, 5);
me170:;
    }
    {
        uint8_t t241 = 0;
        uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t243 = (!(t242 == ((uint64_t)1ULL)));
        if (!t243) {
            uint64_t t244 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t244 >= (uintptr_t)(t245)) gm_trap_index(t244, t245, gm_srch6h8rdicu443, 445, 29);
            uint8_t t246;
            uint8_t t249 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t244 * 144) + 0) + 0));
            if (t249 != ((uint8_t)0ULL)) goto mn248;
            t246 = ((uint8_t)0);
            goto me247;
mn248:;
            t246 = ((uint8_t)1);
            goto me247;
mn250:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 445, 29);
me247:;
            t241 = t246;
        } else t241 = 1;
        if (!t241) goto ie240;
        {
            void * t252 = gm_l0;
            gm_tl874t2atnlh3 t253 = gm_l3;
            void * t254 = gm_l0;
            gm_t88rcucg1b7uk t255 = gm_l1;
            gm_b16_8 t256 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t254, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t257;
            GM_ST(gm_b16_8, ((uint8_t *)&t257 + 0), t256);
            gm_tabb710d9mq2e t258;
            gm_f_format_458q1b3((void *)&t258, (void *)NULL, cx, NULL, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 8, gm_srch6h8rdicu443, 446, 24);
            gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&t258) + 8));
            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
            gm_b0_1 t261 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t252, t253, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t262 = gm_l0;
            void * t263 = GM_LD(void *, (((uint8_t *)t262) + 0));
            gm_tl874t2atnlh3 t264 = gm_l3;
            void * t265 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t266 = 0;
            F.ret = t265;
            goto gm_fret;
        }
        goto ix240;
ie240:;
ix240:;
    }
    {
        uint64_t t268 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t268 >= (uintptr_t)(t269)) gm_trap_index(t268, t269, gm_srch6h8rdicu443, 449, 23);
        uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t268 * 144) + 0) + 0));
        if (t270 != ((uint8_t)0ULL)) goto ie267;
        gm_tc3oht4ksth9d t271 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t268 * 144) + 0) + 8));
        F.l26 = t271;
        {
            void * t274 = gm_l0;
            void * t275 = GM_LD(void *, (((uint8_t *)t274) + 0));
            gm_t88rcucg1b7uk t276 = gm_l1;
            gm_tkn42u7na3t81 t277 = gm_f_bits_struct_ty_l1kjtap(t275, GM_CONV(gm_csguankko4lmhd, t276));
            if (!((((uint8_t *)&t277))[4] != 0)) goto ie273;
            gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t277));
            gm_l27 = t278;
            {
                void * t280 = gm_l0;
                void * t281 = (void *)((uint8_t *)&F.l26);
                gm_t88rcucg1b7uk t282 = gm_l27;
                gm_tkn42u7na3t81 t283;
                GM_ST(uint8_t, (uint8_t *)&t283 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t283 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t283), t282);
                ((uint8_t *)&t283)[4] = 1;
                void * t284 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t280, t281, t283);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t284;
            }
            {
                void * t286 = gm_l0;
                void * t287 = (void *)(((uint8_t *)t286) + 96);
                void * t288 = gm_l4;
                void * t289 = gm_l28;
                gm_t88rcucg1b7uk t290 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t289) + 0));
                gm_taari1s0m51a7 t291 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t287, t288, t290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t292;
                if (((((uint8_t *)&t291))[1] != 0)) goto mn294;
                t292 = ((uint8_t)0);
                goto me293;
mn294:;
                t292 = ((uint8_t)1);
                goto me293;
mn295:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 453, 12);
me293:;
                if (!t292) goto ie285;
                {
                    void * t297 = gm_l0;
                    void * t298 = gm_l28;
                    gm_t88rcucg1b7uk t299 = gm_l27;
                    void * t300 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t297, t298, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t300;
                }
                goto ix285;
ie285:;
ix285:;
            }
            {
                void * t301 = gm_l0;
                void * t302 = (void *)(((uint8_t *)t301) + 320);
                gm_thsh8t5u55gps t303;
                void * t304 = gm_l28;
                gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 0));
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t303 + 0, t305);
                gm_t88rcucg1b7uk t306 = gm_l1;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t303 + 4, t306);
                gm_tl874t2atnlh3 t307 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t303 + 8, t307);
                gm_tpm220j5qhjke t308;
                GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t308 + 28, 0);
                GM_ST(uint64_t, (uint8_t *)&t308 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t308 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)1ULL));
                GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t308 + 8), t303);
                gm_tftb1odc739t5 t309 = gm_f_push_7koe9lk(t302, &t308);
                if (((((uint8_t *)&t309))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t309) + 1, gm_srch6h8rdicu443, 455, 9);
                gm_b0_1 t310 = GM_LD(gm_b0_1, (((uint8_t *)&t309) + 1));
            }
            {
                gm_t88rcucg1b7uk t311 = gm_l1;
                gm_tt8o5kebou62r t312;
                memset((uint8_t *)&t312 + 12, 0, 4);
                gm_tvdg94bk7aa22 t313;
                GM_ST(uint8_t, (uint8_t *)&t313 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t313 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t313 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t313 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t312 + 0, t313);
                void * t314 = gm_l28;
                gm_b16_8 t315 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t312 + 16, t315);
                gm_teti411eujvf4 t316;
                GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t316 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t316 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t316 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)33ULL));
                GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t316 + 8), t312);
                gm_tl874t2atnlh3 t317 = gm_l3;
                void * t318 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t311, &t316, t317);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t319 = 0;
                F.ret = t318;
                goto gm_fret;
            }
            goto ix273;
ie273:;
ix273:;
        }
        {
            void * t320 = gm_l0;
            void * t321 = (void *)((uint8_t *)&F.l26);
            gm_tkn42u7na3t81 t322;
            void * t324 = (void *)((uint8_t *)&F.l26);
            uint8_t t325 = gm_f_is_literal_tree_om58rum(t324);
            if (!t325) goto ie323;
            gm_t88rcucg1b7uk t327 = gm_l1;
            gm_t88rcucg1b7uk t328 = t327;
            gm_tkn42u7na3t81 t329;
            GM_ST(uint8_t, (uint8_t *)&t329 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t329 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t329), t328);
            ((uint8_t *)&t329)[4] = 1;
            t322 = t329;
            goto ix323;
ie323:;
            gm_tkn42u7na3t81 t331;
            memset(&t331, 0, sizeof t331);
            gm_tkn42u7na3t81 t332 = t331;
            t322 = t332;
ix323:;
            void * t333 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t320, t321, t322);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t333;
        }
        {
            void * t335 = gm_l0;
            void * t336 = (void *)(((uint8_t *)t335) + 96);
            void * t337 = gm_l4;
            void * t338 = gm_l29;
            gm_t88rcucg1b7uk t339 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t338) + 0));
            gm_taari1s0m51a7 t340 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t336, t337, t339);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t341;
            if (((((uint8_t *)&t340))[1] != 0)) goto mn343;
            t341 = ((uint8_t)0);
            goto me342;
mn343:;
            t341 = ((uint8_t)1);
            goto me342;
mn344:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 460, 8);
me342:;
            if (!t341) goto ie334;
            uint8_t t345 = 0;
            void * t346 = gm_l4;
            gm_t88rcucg1b7uk t347 = gm_l1;
            uint8_t t348 = gm_f_is_integer_8d4f9ek(t346, GM_CONV(gm_csguankko4lmhd, t347));
            if (!t348) {
                uint8_t t349 = 0;
                void * t350 = gm_l4;
                gm_t88rcucg1b7uk t351 = gm_l1;
                gm_t53at80pfdus3 t352;
                gm_f_kind_jcnt4hm((void *)&t352, t350, GM_CONV(gm_csguankko4lmhd, t351));
                uint8_t t353;
                uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&t352) + 0));
                if (t356 != ((uint8_t)3ULL)) goto mn355;
                t353 = ((uint8_t)1);
                goto me354;
mn355:;
                t353 = ((uint8_t)0);
                goto me354;
mn357:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 460, 76);
me354:;
                if (t353) {
                    void * t358 = gm_l29;
                    uint8_t t359 = gm_f_tir_literal_oh960hv(t358);
                    t349 = t359;
                } else t349 = 0;
                t345 = t349;
            } else t345 = 1;
            if (!t345) goto ie334;
            {
                void * t362 = gm_l0;
                void * t363 = (void *)(((uint8_t *)t362) + 96);
                void * t364 = gm_l4;
                void * t365 = gm_l29;
                gm_t88rcucg1b7uk t366 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t365) + 0));
                gm_t88rcucg1b7uk t367 = gm_l1;
                gm_t9qkh0rsooc8d t368 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t363, t364, t366, t367);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t369;
                if (!((((uint8_t *)&t368))[0] != 0)) goto mn371;
                t369 = ((uint8_t)0);
                goto me370;
mn371:;
                t369 = ((uint8_t)1);
                goto me370;
mn372:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 461, 12);
me370:;
                if (!t369) goto ie361;
                {
                    void * t375 = gm_l29;
                    uint8_t t376 = gm_f_tir_literal_oh960hv(t375);
                    uint8_t t377 = (uint8_t)!t376;
                    if (!t377) goto ie374;
                    {
                        void * t379 = gm_l0;
                        void * t380 = (void *)(((uint8_t *)t379) + 96);
                        void * t381 = gm_l4;
                        void * t382 = gm_l29;
                        gm_t88rcucg1b7uk t383 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t382) + 0));
                        gm_tl874t2atnlh3 t384 = gm_l3;
                        gm_f_note_fixed_by_ev84og7(t380, t381, GM_CONV(gm_csguankko4lmhd, t383), GM_CONV(gm_cseahvg8v7p0vs, t384));
                    }
                    goto ix374;
ie374:;
ix374:;
                }
                {
                    void * t385 = gm_l29;
                    uint8_t t386 = 0;
                    F.ret = t385;
                    goto gm_fret;
                }
                goto ix361;
ie361:;
ix361:;
            }
            {
                uint8_t t388 = 0;
                void * t389 = gm_l29;
                uint8_t t390 = gm_f_tir_literal_oh960hv(t389);
                uint8_t t391 = (uint8_t)!t390;
                if (!t391) {
                    void * t392 = gm_l0;
                    void * t393 = (void *)(((uint8_t *)t392) + 96);
                    void * t394 = gm_l4;
                    void * t395 = gm_l29;
                    gm_t88rcucg1b7uk t396 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t395) + 0));
                    gm_taari1s0m51a7 t397 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t393, t394, t396);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9qkh0rsooc8d t398;
                    GM_ST(uint8_t, ((uint8_t *)&t398 + 0), ((uint8_t)1ULL));
                    gm_taari1s0m51a7 t399;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t399), t398);
                    ((uint8_t *)&t399)[1] = 1;
                    uint8_t t400 = (!gm_eq5307j5k9ms(((uint8_t *)&t397), ((uint8_t *)&t399)));
                    t388 = t400;
                } else t388 = 1;
                if (!t388) goto ie387;
                {
                    void * t402 = gm_l0;
                    void * t403 = (void *)(((uint8_t *)t402) + 320);
                    gm_thsh8t5u55gps t404;
                    void * t405 = gm_l29;
                    gm_t88rcucg1b7uk t406 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t405) + 0));
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t404 + 0, t406);
                    gm_t88rcucg1b7uk t407 = gm_l1;
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t404 + 4, t407);
                    gm_tl874t2atnlh3 t408 = gm_l3;
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t404 + 8, t408);
                    gm_tpm220j5qhjke t409;
                    GM_ST(uint8_t, (uint8_t *)&t409 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t409 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t409 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t409 + 28, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t409 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t409 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t409 + 8), t404);
                    gm_tftb1odc739t5 t410 = gm_f_push_7koe9lk(t403, &t409);
                    if (((((uint8_t *)&t410))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t410) + 1, gm_srch6h8rdicu443, 466, 13);
                    gm_b0_1 t411 = GM_LD(gm_b0_1, (((uint8_t *)&t410) + 1));
                }
                {
                    gm_t88rcucg1b7uk t412 = gm_l1;
                    gm_tt8o5kebou62r t413;
                    memset((uint8_t *)&t413 + 12, 0, 4);
                    gm_tvdg94bk7aa22 t414;
                    GM_ST(uint8_t, (uint8_t *)&t414 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t414 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t414 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t414 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t414 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t413 + 0, t414);
                    void * t415 = gm_l29;
                    gm_b16_8 t416 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t415);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t413 + 16, t416);
                    gm_teti411eujvf4 t417;
                    GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t417 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t417 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t417 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)33ULL));
                    GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t417 + 8), t413);
                    gm_tl874t2atnlh3 t418 = gm_l3;
                    void * t419 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t412, &t417, t418);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t420 = 0;
                    F.ret = t419;
                    goto gm_fret;
                }
                goto ix387;
ie387:;
ix387:;
            }
            {
                void * t421 = gm_l0;
                gm_tl874t2atnlh3 t422 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                void * t423 = gm_l0;
                gm_t88rcucg1b7uk t424 = gm_l1;
                gm_b16_8 t425 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t423, t424);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t426;
                GM_ST(gm_b16_8, ((uint8_t *)&t426 + 0), t425);
                gm_tabb710d9mq2e t427;
                gm_f_format_i1v5tqv((void *)&t427, (void *)NULL, cx, NULL, t426);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t427))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t427) + 8, gm_srch6h8rdicu443, 469, 26);
                gm_b16_8 t428 = GM_LD(gm_b16_8, (((uint8_t *)&t427) + 8));
                gm_b16_8 t429 = GM_LD(gm_b16_8, ((uint8_t *)&t428));
                gm_b0_1 t430 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t421, t422, t429);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t431 = gm_l29;
                uint8_t t432 = 0;
                F.ret = t431;
                goto gm_fret;
            }
            goto ix334;
ie334:;
ix334:;
        }
        {
            void * t433 = gm_l0;
            void * t434 = (void *)(((uint8_t *)t433) + 96);
            void * t435 = gm_l0;
            void * t436 = GM_LD(void *, (((uint8_t *)t435) + 0));
            void * t437 = gm_l29;
            gm_t88rcucg1b7uk t438 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t437) + 0));
            gm_t88rcucg1b7uk t439 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t434, t436, t438);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t439;
        }
        {
            void * t441 = gm_l4;
            gm_t88rcucg1b7uk t442 = gm_l30;
            uint8_t t443 = gm_f_has_infer_3o82a0b(t441, GM_CONV(gm_csguankko4lmhd, t442));
            uint8_t t444 = (uint8_t)!t443;
            if (!t444) goto ie440;
            gm_t88rcucg1b7uk t445 = gm_l30;
            gm_t88rcucg1b7uk t446 = gm_l1;
            uint8_t t447 = (!gm_eq9m6mvtmgde(((uint8_t *)&t445), ((uint8_t *)&t446)));
            if (!t447) goto ie440;
            void * t448 = gm_l0;
            gm_t88rcucg1b7uk t449 = gm_l30;
            gm_t88rcucg1b7uk t450 = gm_l1;
            gm_th2lqbo77pvsc t451;
            gm_f_coercion_steps_ibqvhta((void *)&t451, (void *)NULL, cx, NULL, t448, t449, t450);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t452;
            if (((((uint8_t *)&t451))[16] != 0)) goto mn454;
            t452 = ((uint8_t)0);
            goto me453;
mn454:;
            t452 = ((uint8_t)1);
            goto me453;
mn455:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 474, 50);
me453:;
            if (!t452) goto ie440;
            {
                void * t457 = gm_l0;
                void * t458 = gm_l29;
                gm_t88rcucg1b7uk t459 = gm_l1;
                void * t460 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t457, t458, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t461 = 0;
                F.ret = t460;
                goto gm_fret;
            }
            goto ix440;
ie440:;
ix440:;
        }
        {
            void * t463 = gm_l4;
            gm_t88rcucg1b7uk t464 = gm_l30;
            uint8_t t465 = gm_f_has_infer_3o82a0b(t463, GM_CONV(gm_csguankko4lmhd, t464));
            uint8_t t466 = (uint8_t)!t465;
            if (!t466) goto ie462;
            gm_t88rcucg1b7uk t467 = gm_l30;
            gm_t88rcucg1b7uk t468 = gm_l1;
            uint8_t t469 = (gm_eq9m6mvtmgde(((uint8_t *)&t467), ((uint8_t *)&t468)));
            if (!t469) goto ie462;
            void * t470 = gm_l4;
            gm_t88rcucg1b7uk t471 = gm_l30;
            uint8_t t472 = gm_f_is_error_8k27gd1(t470, GM_CONV(gm_csguankko4lmhd, t471));
            uint8_t t473 = (uint8_t)!t472;
            if (!t473) goto ie462;
            {
                gm_t88rcucg1b7uk t475 = gm_l1;
                gm_tmn7n0n9raou5 t476;
                GM_ST(uint8_t, (uint8_t *)&t476 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t476 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t476 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)8ULL));
                void * t477 = gm_l29;
                gm_t8jsc4n9vp2ib t478;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t478 + 0), t476);
                GM_ST(void *, ((uint8_t *)&t478 + 8), t477);
                gm_teti411eujvf4 t479;
                memset(&t479, 0, sizeof t479);
                GM_ST(uint8_t, ((uint8_t *)&t479 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t479 + 8), t478);
                gm_tl874t2atnlh3 t480 = gm_l3;
                void * t481 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t475, &t479, t480);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t482 = 0;
                F.ret = t481;
                goto gm_fret;
            }
            goto ix462;
ie462:;
ix462:;
        }
        {
            void * t483 = gm_l0;
            void * t484 = (void *)(((uint8_t *)t483) + 320);
            gm_thsh8t5u55gps t485;
            void * t486 = gm_l29;
            gm_t88rcucg1b7uk t487 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t486) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t485 + 0, t487);
            gm_t88rcucg1b7uk t488 = gm_l1;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t485 + 4, t488);
            gm_tl874t2atnlh3 t489 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t485 + 8, t489);
            gm_tpm220j5qhjke t490;
            GM_ST(uint8_t, (uint8_t *)&t490 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t490 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t490 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t490 + 28, 0);
            GM_ST(uint64_t, (uint8_t *)&t490 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t490 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t490 + 0), ((uint8_t)1ULL));
            GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t490 + 8), t485);
            gm_tftb1odc739t5 t491 = gm_f_push_7koe9lk(t484, &t490);
            if (((((uint8_t *)&t491))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t491) + 1, gm_srch6h8rdicu443, 477, 5);
            gm_b0_1 t492 = GM_LD(gm_b0_1, (((uint8_t *)&t491) + 1));
        }
        {
            gm_t88rcucg1b7uk t493 = gm_l1;
            gm_tt8o5kebou62r t494;
            memset((uint8_t *)&t494 + 12, 0, 4);
            gm_tvdg94bk7aa22 t495;
            GM_ST(uint8_t, (uint8_t *)&t495 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t495 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t495 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t495 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t495 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t494 + 0, t495);
            void * t496 = gm_l29;
            gm_b16_8 t497 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t496);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t494 + 16, t497);
            gm_teti411eujvf4 t498;
            GM_ST(uint8_t, (uint8_t *)&t498 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t498 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t498 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t498 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t498 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t498 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t498 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t498 + 8), t494);
            gm_tl874t2atnlh3 t499 = gm_l3;
            void * t500 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t493, &t498, t499);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t501 = 0;
            F.ret = t500;
            goto gm_fret;
        }
        goto ix267;
ie267:;
        {
            void * t503 = gm_l0;
            void * t504 = GM_LD(void *, (((uint8_t *)t503) + 0));
            gm_tl874t2atnlh3 t505 = gm_l3;
            void * t506 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t504, t505);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t507 = 0;
            F.ret = t506;
            goto gm_fret;
        }
ix267:;
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
__attribute__((aligned(4))) void * gm_f_expect_eq_iu4ojmi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_expect_eq_iu4ojmi F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tiilmfd16v3fc gm_l5; /* eref */
    void * gm_l6; /* tys */
    void * gm_l7; /* l0 */
    void * gm_l8; /* r0 */
    gm_t3mf58gv9kf3n gm_l9; /* eq */
    gm_t88rcucg1b7uk gm_l10; /* lt */
    gm_t88rcucg1b7uk gm_l11; /* rt */
    uint32_t gm_l12; /* x */
    uint32_t gm_l13; /* y */
    void * gm_l14; /* differ */
    gm_tnpifv20irnph gm_l16; /* set */
    void * gm_l20; /* write */
    void * gm_l21; /* msg */
    void * gm_l22; /* loc */
    void * gm_l24; /* stop */
    void * gm_l26; /* fail */
    void * gm_l28; /* check */
    GM_STACK_CHECK(&F, 3072u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t6 = (!(t5 == ((uint64_t)2ULL)));
        if (!t6) {
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srch6h8rdicu443, 554, 29);
            uint8_t t9;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t7 * 144) + 0) + 0));
            if (t12 != ((uint8_t)0ULL)) goto mn11;
            t9 = ((uint8_t)0);
            goto me10;
mn11:;
            t9 = ((uint8_t)1);
            goto me10;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 554, 29);
me10:;
            t4 = t9;
        } else t4 = 1;
        if (!t4) {
            uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srch6h8rdicu443, 554, 67);
            uint8_t t16;
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 144) + 0) + 0));
            if (t19 != ((uint8_t)0ULL)) goto mn18;
            t16 = ((uint8_t)0);
            goto me17;
mn18:;
            t16 = ((uint8_t)1);
            goto me17;
mn20:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 554, 67);
me17:;
            t3 = t16;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t22 = gm_l0;
            gm_tl874t2atnlh3 t23 = gm_l2;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_scgahod5hue9b));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(55));
            gm_b0_1 t25 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            gm_tl874t2atnlh3 t28 = gm_l2;
            void * t29 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srch6h8rdicu443, 558, 23);
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 144) + 0) + 0));
        if (t34 != ((uint8_t)0ULL)) goto ie31;
        gm_tc3oht4ksth9d t35 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 144) + 0) + 8));
        F.l3 = t35;
        {
            uint64_t t38 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srch6h8rdicu443, 559, 23);
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 144) + 0) + 0));
            if (t40 != ((uint8_t)0ULL)) goto ie37;
            gm_tc3oht4ksth9d t41 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 144) + 0) + 8));
            F.l4 = t41;
            {
                void * t43 = gm_l0;
                void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_ss6g2koqn63bf));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(4));
                gm_tqu3m2l3n8mnc t46;
                gm_f_prelude_effect_gilrmm5((void *)&t46, (void *)NULL, cx, NULL, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tiilmfd16v3fc t47;
                if (((((uint8_t *)&t46))[24] != 0)) {
                    t47 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t46) + 0));
                } else {
                    void * t48 = gm_l0;
                    void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                    gm_tl874t2atnlh3 t50 = gm_l2;
                    void * t51 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t49, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t52 = 0;
                    F.ret = t51;
                    goto gm_fret;
                }
                gm_l5 = t47;
            }
            {
                void * t53 = gm_l0;
                void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
                void * t55 = (void *)(((uint8_t *)t54) + 8);
                gm_l6 = t55;
            }
            {
                void * t56 = gm_l0;
                void * t57 = (void *)((uint8_t *)&F.l3);
                gm_tkn42u7na3t81 t58;
                memset(&t58, 0, sizeof t58);
                void * t59 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t56, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t59;
            }
            {
                void * t60 = gm_l0;
                void * t61 = (void *)((uint8_t *)&F.l4);
                void * t62 = gm_l7;
                gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t62) + 0));
                gm_tkn42u7na3t81 t64;
                GM_ST(uint8_t, (uint8_t *)&t64 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t64 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t64), t63);
                ((uint8_t *)&t64)[4] = 1;
                void * t65 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t60, t61, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t65;
            }
            {
                void * t66 = gm_l0;
                void * t67 = gm_l7;
                void * t68 = gm_l8;
                gm_tl874t2atnlh3 t69 = gm_l2;
                gm_t3mf58gv9kf3n t70 = gm_f_equalize_ni1r5ra((void *)NULL, cx, NULL, t66, t67, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t70;
            }
            {
                void * t71 = gm_l0;
                void * t72 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
                gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 0));
                gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
                gm_t88rcucg1b7uk t75 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t71, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t75;
            }
            {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t77) + 0));
                gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
                gm_t88rcucg1b7uk t80 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t76, t78, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t80;
            }
            {
                void * t81 = gm_l0;
                gm_t88rcucg1b7uk t82 = gm_l10;
                gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
                uint32_t t84 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t84;
            }
            {
                void * t85 = gm_l0;
                gm_t88rcucg1b7uk t86 = gm_l11;
                gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
                uint32_t t88 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t85, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t88;
            }
            {
                void * t89 = gm_l6;
                gm_t88rcucg1b7uk t90 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tlcl4u558l6p5 t91;
                memset((uint8_t *)&t91 + 17, 0, 7);
                gm_t88rcucg1b7uk t92 = gm_l10;
                uint32_t t93 = gm_l12;
                gm_teti411eujvf4 t94;
                memset(&t94, 0, sizeof t94);
                GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t94 + 8), t93);
                gm_tl874t2atnlh3 t95 = gm_l2;
                void * t96 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t92, &t94, t95);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, (uint8_t *)&t91 + 0, t96);
                gm_t88rcucg1b7uk t97 = gm_l11;
                uint32_t t98 = gm_l13;
                gm_teti411eujvf4 t99;
                memset(&t99, 0, sizeof t99);
                GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t99 + 8), t98);
                gm_tl874t2atnlh3 t100 = gm_l2;
                void * t101 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t97, &t99, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, (uint8_t *)&t91 + 8, t101);
                GM_ST(uint8_t, (uint8_t *)&t91 + 16, ((uint8_t)1));
                gm_teti411eujvf4 t102;
                memset(&t102, 0, sizeof t102);
                GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)43ULL));
                GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t102 + 8), t91);
                gm_tl874t2atnlh3 t103 = gm_l2;
                void * t104 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t90, &t102, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t104;
            }
            {
                gm_f_list_084fcd6((void *)&F.l15, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t105 = (void *)((uint8_t *)&F.l15);
                gm_tftb1odc739t5 t106 = gm_f_push_d1r4h19(t105, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l5)));
                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srch6h8rdicu443, 571, 5);
                gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
            }
            {
                gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                gm_tnpifv20irnph t110 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t110;
            }
            {
                void * t111 = gm_l0;
                void * t112 = (void *)((uint8_t *)&gm_l16);
                gm_tl874t2atnlh3 t113 = gm_l2;
                gm_b0_1 t114 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t111, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_epvducg((void *)&F.l17, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t115 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t116;
                GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s8vts15kskc6n));
                GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(10));
                gm_tftb1odc739t5 t117 = gm_f_push_3ef746b(t115, t116);
                if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_srch6h8rdicu443, 576, 5);
                gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
            }
            {
                void * t119 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t120;
                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_srgp1m0q2ut8m));
                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(11));
                gm_tftb1odc739t5 t121 = gm_f_push_3ef746b(t119, t120);
                if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srch6h8rdicu443, 577, 5);
                gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
            }
            {
                void * t123 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t124;
                GM_ST(void *, (uint8_t *)&t124, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(1));
                gm_tftb1odc739t5 t125 = gm_f_push_3ef746b(t123, t124);
                if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srch6h8rdicu443, 578, 5);
                gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
            }
            {
                gm_f_list_qtk22f3((void *)&F.l18, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t127 = (void *)((uint8_t *)&F.l18);
                gm_t7g88i52rrtm5 t128;
                gm_f_default_spec_9ntjq2s((void *)&t128);
                gm_tftb1odc739t5 t129 = gm_f_push_nbmosph(t127, &t128);
                if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srch6h8rdicu443, 580, 5);
                gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
            }
            {
                void * t131 = (void *)((uint8_t *)&F.l18);
                gm_t7g88i52rrtm5 t132;
                gm_f_default_spec_9ntjq2s((void *)&t132);
                gm_tftb1odc739t5 t133 = gm_f_push_nbmosph(t131, &t132);
                if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_srch6h8rdicu443, 581, 5);
                gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l19, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t135 = (void *)((uint8_t *)&F.l19);
                gm_t88rcucg1b7uk t136 = gm_l11;
                uint32_t t137 = gm_l13;
                gm_teti411eujvf4 t138;
                memset(&t138, 0, sizeof t138);
                GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t138 + 8), t137);
                gm_tl874t2atnlh3 t139 = gm_l2;
                void * t140 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t136, &t138, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t141 = gm_f_push_d943vgp_2(t135, &(*(gm_t69m56meqefs8 *)((uint8_t *)t140)));
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srch6h8rdicu443, 583, 5);
                gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
            }
            {
                void * t143 = (void *)((uint8_t *)&F.l19);
                gm_t88rcucg1b7uk t144 = gm_l10;
                uint32_t t145 = gm_l12;
                gm_teti411eujvf4 t146;
                memset(&t146, 0, sizeof t146);
                GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t146 + 8), t145);
                gm_tl874t2atnlh3 t147 = gm_l2;
                void * t148 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t144, &t146, t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t149 = gm_f_push_d943vgp_2(t143, &(*(gm_t69m56meqefs8 *)((uint8_t *)t148)));
                if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_srch6h8rdicu443, 584, 5);
                gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
            }
            {
                void * t151 = gm_l6;
                gm_t88rcucg1b7uk t152 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tntn2tf15puib t153;
                gm_t419mo45g7j0a t154;
                memset((uint8_t *)&t154 + 28, 0, 4);
                memcpy((uint8_t *)&t154 + 0, ((uint8_t *)&gm_l5), 24);
                GM_ST(uint32_t, (uint8_t *)&t154 + 24, ((uint32_t)0ULL));
                gm_th1g69d7vpipg t155;
                GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t155 + 8), t154);
                GM_ST(gm_th1g69d7vpipg, (uint8_t *)&t153 + 0, t155);
                gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                GM_ST(gm_b16_8, (uint8_t *)&t153 + 40, t157);
                gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                GM_ST(gm_b16_8, (uint8_t *)&t153 + 56, t159);
                gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t153 + 72, t160);
                gm_tq7s98ihonkju t161 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t153, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krbc9rueul4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 8, gm_srch6h8rdicu443, 585, 40);
                void * t162 = GM_LD(void *, (((uint8_t *)&t161) + 8));
                gm_teti411eujvf4 t163;
                memset(&t163, 0, sizeof t163);
                GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)55ULL));
                GM_ST(void *, ((uint8_t *)&t163 + 8), t162);
                gm_tl874t2atnlh3 t164 = gm_l2;
                void * t165 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t152, &t163, t164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t165;
            }
            {
                void * t166 = gm_l6;
                gm_t88rcucg1b7uk t167 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t168;
                GM_ST(void *, (uint8_t *)&t168, (void *)(gm_smadkict0htlf));
                GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(37));
                gm_teti411eujvf4 t169;
                memset(&t169, 0, sizeof t169);
                GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t169 + 8), t168);
                gm_tl874t2atnlh3 t170 = gm_l2;
                void * t171 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t167, &t169, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t171;
            }
            {
                void * t172 = gm_l0;
                gm_tl874t2atnlh3 t173 = gm_l2;
                void * t174 = gm_f_source_location_nc1oola((void *)NULL, cx, NULL, t172, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t174;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l23, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t175 = (void *)((uint8_t *)&F.l23);
                void * t176 = gm_l21;
                gm_tftb1odc739t5 t177 = gm_f_push_d943vgp_2(t175, &(*(gm_t69m56meqefs8 *)((uint8_t *)t176)));
                if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 1, gm_srch6h8rdicu443, 589, 5);
                gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 1));
            }
            {
                void * t179 = (void *)((uint8_t *)&F.l23);
                void * t180 = gm_l22;
                gm_tftb1odc739t5 t181 = gm_f_push_d943vgp_2(t179, &(*(gm_t69m56meqefs8 *)((uint8_t *)t180)));
                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srch6h8rdicu443, 590, 5);
                gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
            }
            {
                void * t183 = gm_l6;
                gm_t88rcucg1b7uk t184 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to76jli3bqddq t185;
                gm_t419mo45g7j0a t186;
                memset((uint8_t *)&t186 + 28, 0, 4);
                memcpy((uint8_t *)&t186 + 0, ((uint8_t *)&gm_l5), 24);
                GM_ST(uint32_t, (uint8_t *)&t186 + 24, ((uint32_t)1ULL));
                gm_tsth9qu1dv0g3 t187;
                GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t187 + 8), t186);
                GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t185 + 0, t187);
                gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t185 + 40, t188);
                gm_teti411eujvf4 t189;
                GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)34ULL));
                GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t189 + 8), t185);
                gm_tl874t2atnlh3 t190 = gm_l2;
                void * t191 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t184, &t189, t190);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t191;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l25, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t192 = (void *)((uint8_t *)&F.l25);
                void * t193 = gm_l20;
                gm_trcmmd12cefb1 t194;
                GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t194 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t194 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t194 + 8), t193);
                gm_tftb1odc739t5 t195 = gm_f_push_d6c76ud_2(t192, &t194);
                if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_srch6h8rdicu443, 593, 5);
                gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
            }
            {
                void * t197 = gm_l6;
                gm_t88rcucg1b7uk t198 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                void * t200 = gm_l24;
                gm_tl874t2atnlh3 t201 = gm_l2;
                void * t202 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t198, t199, t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t202;
            }
            {
                gm_f_list_v5d1o9b_2((void *)&F.l27, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t203 = (void *)((uint8_t *)&F.l27);
                void * t204 = gm_l14;
                gm_tlr9dp305htm7 t205;
                GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t205 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)0ULL));
                GM_ST(void *, ((uint8_t *)&t205 + 8), t204);
                gm_tftb1odc739t5 t206 = gm_f_push_eb7kbhp_2(t203, &t205);
                if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 1, gm_srch6h8rdicu443, 596, 5);
                gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t206) + 1));
            }
            {
                void * t208 = gm_l6;
                gm_t88rcucg1b7uk t209 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trl2khmuphncd t210;
                gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t210 + 0, t211);
                void * t212 = gm_l26;
                GM_ST(void *, (uint8_t *)&t210 + 16, t212);
                GM_ST(void *, (uint8_t *)&t210 + 24, ((void *)0));
                gm_teti411eujvf4 t213;
                GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t213 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t213 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t213 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)47ULL));
                GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t213 + 8), t210);
                gm_tl874t2atnlh3 t214 = gm_l2;
                void * t215 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t209, &t213, t214);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t215;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l29, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t216 = (void *)((uint8_t *)&F.l29);
                uint32_t t217 = gm_l12;
                void * t218 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
                gm_ttggq7ng4kp3p t219;
                GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t219 + 0), t217);
                GM_ST(void *, ((uint8_t *)&t219 + 8), t218);
                gm_trcmmd12cefb1 t220;
                GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t220 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t220 + 8), t219);
                gm_tftb1odc739t5 t221 = gm_f_push_d6c76ud_2(t216, &t220);
                if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_srch6h8rdicu443, 599, 5);
                gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
            }
            {
                void * t223 = (void *)((uint8_t *)&F.l29);
                uint32_t t224 = gm_l13;
                void * t225 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                gm_ttggq7ng4kp3p t226;
                GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t226 + 0), t224);
                GM_ST(void *, ((uint8_t *)&t226 + 8), t225);
                gm_trcmmd12cefb1 t227;
                GM_ST(uint8_t, (uint8_t *)&t227 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t227 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t227 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t227 + 8), t226);
                gm_tftb1odc739t5 t228 = gm_f_push_d6c76ud_2(t223, &t227);
                if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_srch6h8rdicu443, 600, 5);
                gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
            }
            {
                void * t230 = (void *)((uint8_t *)&F.l29);
                void * t231 = gm_l28;
                gm_trcmmd12cefb1 t232;
                GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t232 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t232 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t232 + 8), t231);
                gm_tftb1odc739t5 t233 = gm_f_push_d6c76ud_2(t230, &t232);
                if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 1, gm_srch6h8rdicu443, 601, 5);
                gm_b0_1 t234 = GM_LD(gm_b0_1, (((uint8_t *)&t233) + 1));
            }
            {
                void * t235 = gm_l0;
                void * t236 = GM_LD(void *, (((uint8_t *)t235) + 0));
                gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                gm_tl874t2atnlh3 t238 = gm_l2;
                void * t239 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t236, t237, t238);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t240 = 0;
                F.ret = t239;
                goto gm_fret;
            }
            goto ix37;
ie37:;
            {
                void * t242 = gm_l0;
                void * t243 = GM_LD(void *, (((uint8_t *)t242) + 0));
                gm_tl874t2atnlh3 t244 = gm_l2;
                void * t245 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t243, t244);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t246 = 0;
                F.ret = t245;
                goto gm_fret;
            }
ix37:;
        }
        goto ix31;
ie31:;
        {
            void * t248 = gm_l0;
            void * t249 = GM_LD(void *, (((uint8_t *)t248) + 0));
            gm_tl874t2atnlh3 t250 = gm_l2;
            void * t251 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t249, t250);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t252 = 0;
            F.ret = t251;
            goto gm_fret;
        }
ix31:;
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
__attribute__((aligned(4))) void gm_f_fill_defaults_3fq5ut6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_fill_defaults_3fq5ut6 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* callee_module */
    gm_b16_8 gm_l3; /* pre */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* params */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* slot */
    void * gm_l11; /* v */
    void * gm_l12; /* p */
    void * gm_l13; /* d */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t5 = gm_l1;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 56) + 0));
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
            gm_l7 = t7;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l8;
                    uint64_t t15 = gm_l8;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t18 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t16 * 8));
                    gm_t22qobfhrnoif t19;
                    GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t14);
                    GM_ST(void *, ((uint8_t *)&t19 + 8), t18);
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                    gm_l9 = t22;
                    void * t23 = GM_LD(void *, (((uint8_t *)&t19) + 8));
                    gm_l10 = t23;
                    {
                        void * t26 = gm_l10;
                        if (!(GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto ie25;
                        void * t27 = GM_LD(void *, ((uint8_t *)&t26));
                        gm_l11 = t27;
                        {
                            void * t29 = (void *)((uint8_t *)&F.l6);
                            void * t30 = gm_l11;
                            gm_tftb1odc739t5 t31 = gm_f_push_d943vgp_2(t29, &(*(gm_t69m56meqefs8 *)((uint8_t *)t30)));
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srch6h8rdicu443, 347, 13);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        {
                            goto lc8;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    {
                        uint64_t t33 = gm_l9;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srch6h8rdicu443, 350, 15);
                        void * t36 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t34 * 56);
                        gm_l12 = t36;
                    }
                    {
                        void * t38 = gm_l12;
                        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 32));
                        if (!(GM_LD(void *, ((uint8_t *)&t39)) != 0)) goto ie37;
                        void * t40 = GM_LD(void *, ((uint8_t *)&t39));
                        gm_l13 = t40;
                        {
                            void * t42 = (void *)((uint8_t *)&F.l6);
                            void * t43 = gm_l0;
                            void * t44 = gm_l12;
                            uint64_t t45 = gm_l2;
                            void * t46 = gm_l13;
                            void * t47 = gm_l12;
                            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 16));
                            gm_tl874t2atnlh3 t49 = gm_l4;
                            void * t50 = gm_f_param_default_h93ci1m((void *)NULL, cx, NULL, t43, t44, t45, t46, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            gm_tftb1odc739t5 t51 = gm_f_push_d943vgp_2(t42, &(*(gm_t69m56meqefs8 *)((uint8_t *)t50)));
                            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srch6h8rdicu443, 352, 13);
                            gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                        }
                        {
                            goto lc8;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        void * t54 = gm_l1;
                        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)t54) + 145));
                        uint8_t t56 = (uint8_t)!t55;
                        if (!t56) goto ie53;
                        {
                            void * t59 = gm_l12;
                            uint8_t t60 = (((GM_LD(uintptr_t, (((uint8_t *)t59) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)t59) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                            if (!t60) goto ie58;
                            {
                                void * t62 = gm_l0;
                                gm_tl874t2atnlh3 t63 = gm_l4;
                                gm_t3877pk7lv748 t64;
                                void * t65 = gm_l1;
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)t65) + 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t66);
                                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                GM_ST(uint64_t, (uint8_t *)&t64 + 16, t67);
                                gm_tabb710d9mq2e t68;
                                gm_f_format_qjdh7n5((void *)&t68, (void *)NULL, cx, NULL, &t64);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srch6h8rdicu443, 357, 32);
                                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                                gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t63, t70);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
                            goto ix58;
ie58:;
                            {
                                void * t73 = gm_l0;
                                gm_tl874t2atnlh3 t74 = gm_l4;
                                void * t75 = gm_l12;
                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t75) + 0));
                                gm_tnpifv20irnph t77;
                                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                                gm_tabb710d9mq2e t78;
                                gm_f_format_qk20moi((void *)&t78, (void *)NULL, cx, NULL, t77);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srch6h8rdicu443, 359, 32);
                                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
                                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                                gm_b0_1 t81 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t73, t74, t80);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
ix58:;
                        }
                        goto ix53;
ie53:;
ix53:;
                    }
                    {
                        void * t82 = (void *)((uint8_t *)&F.l6);
                        void * t83 = gm_l0;
                        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 0));
                        gm_tl874t2atnlh3 t85 = gm_l4;
                        void * t86 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t84, t85);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_tftb1odc739t5 t87 = gm_f_push_d943vgp_2(t82, &(*(gm_t69m56meqefs8 *)((uint8_t *)t86)));
                        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srch6h8rdicu443, 362, 9);
                        gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 345, 5);
me20:;
                }
lc8:;
                {
                    uint64_t t90 = gm_l8;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l8 = t93;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
        }
    }
    {
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 0, t94);
        gm_b16_8 t95 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 16, t95);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_is_const_ref_arg_s0f37he F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* x */
    void * gm_l4; /* inner */
    void * gm_l5; /* h */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l3;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            if (t5 != ((uint8_t)9ULL)) goto lb3;
            void * t6 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l4 = t6;
            {
                void * t9 = gm_l4;
                gm_l3 = t9;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t11 = gm_l3;
        void * t12 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!(GM_LD(void *, ((uint8_t *)&t12)) != 0)) goto ie10;
        void * t13 = GM_LD(void *, ((uint8_t *)&t12));
        gm_l5 = t13;
        {
            void * t15 = gm_l5;
            gm_l3 = t15;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t17 = gm_l3;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
        if (t18 != ((uint8_t)12ULL)) goto ie16;
        uint8_t t19 = GM_LD(uint8_t, ((((((uint8_t *)t17) + 8) + 8) + 0) + 0));
        if (t19 != ((uint8_t)2ULL)) goto ie16;
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            gm_t88rcucg1b7uk t24 = gm_l2;
            gm_t53at80pfdus3 t25;
            gm_f_kind_jcnt4hm((void *)&t25, t23, GM_CONV(gm_csguankko4lmhd, t24));
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t26 != ((uint8_t)8ULL)) goto ie21;
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&t25) + 8) + 0));
            gm_l6 = t27;
            {
                uint8_t t29 = gm_l6;
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint8_t t31 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_literal_tree_om58rum(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_literal_tree_om58rum F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    void * gm_l2; /* operand */
    void * gm_l3; /* lhs */
    void * gm_l4; /* rhs */
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t14 != ((uint8_t)0ULL)) goto pa13;
    goto po12;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)1ULL)) goto pa15;
    goto po12;
pa15:;
    goto mn11;
po12:;
    t2 = ((uint8_t)1);
    goto me3;
mn11:;
ma5:;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t18 != ((uint8_t)9ULL)) goto mn17;
    void * t19 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t19;
    void * t20 = gm_l1;
    uint8_t t21 = gm_f_is_literal_tree_om58rum(t20);
    t2 = t21;
    goto me3;
mn17:;
ma6:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)12ULL)) goto mn22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l2 = t24;
    void * t25 = gm_l2;
    uint8_t t26 = gm_f_is_literal_tree_om58rum(t25);
    t2 = t26;
    goto me3;
mn22:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t28 != ((uint8_t)13ULL)) goto mn27;
    void * t29 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t29;
    void * t30 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 16));
    gm_l4 = t30;
    uint8_t t31 = 0;
    void * t32 = gm_l3;
    uint8_t t33 = gm_f_is_literal_tree_om58rum(t32);
    if (t33) {
        void * t34 = gm_l4;
        uint8_t t35 = gm_f_is_literal_tree_om58rum(t34);
        t31 = t35;
    } else t31 = 0;
    t2 = t31;
    goto me3;
mn27:;
ma8:;
    t2 = ((uint8_t)0);
    goto me3;
mn36:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 494, 54);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_positional_t5fspc1(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_positional_t5fspc1 F;
    void * gm_l0; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
            uint8_t t7 = (t4 < t6);
            if (!t7) goto lb2;
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 136));
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 32) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srch6h8rdicu443, 196, 41);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 32)) + t10 * 1));
            uint8_t t14 = (uint8_t)!t13;
            if (!t14) goto lb2;
            {
                void * t17 = gm_l0;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 136));
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 196, 64);
                uint64_t t22 = r20;
                GM_ST(uint64_t, (((uint8_t *)t17) + 136), t22);
            }
lc2:;
        }
lb2:;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 136));
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
        uint8_t t28 = (t25 >= t27);
        if (!t28) goto ie23;
        {
            gm_tf5srh4mp4b3i t30;
            memset(&t30, 0, sizeof t30);
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t32 = gm_l0;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 136));
        gm_tf5srh4mp4b3i t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[8] = 1;
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_index_78k4iut F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_tgsvmddj3e85e t10;
                    uint64_t t11 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 56), 56);
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t10) + 0));
                    gm_l4 = t17;
                    F.l5 = (*(gm_t5v0c8ia6a5mo *)(((uint8_t *)&t10) + 8));
                    {
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t21 = gm_l1;
                        uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (!t22) goto ie19;
                        {
                            uint64_t t24 = gm_l4;
                            gm_tf5srh4mp4b3i t25;
                            GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t25), t24);
                            ((uint8_t *)&t25)[8] = 1;
                            uint8_t t26 = 0;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 171, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb4:;
        }
    }
    {
        gm_tf5srh4mp4b3i t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_print_bhebp9d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_print_bhebp9d F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* to_out */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* effect_name */
    gm_tiilmfd16v3fc gm_l9; /* eref */
    gm_tnpifv20irnph gm_l11; /* set */
    GM_STACK_CHECK(&F, 1109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srch6h8rdicu443, 607, 25);
            uint8_t t8;
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 144) + 0) + 0));
            if (t11 != ((uint8_t)0ULL)) goto mn10;
            t8 = ((uint8_t)0);
            goto me9;
mn10:;
            t8 = ((uint8_t)1);
            goto me9;
mn12:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 607, 25);
me9:;
            t3 = t8;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l3;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sijukd7vsocnt));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(67));
            gm_b0_1 t17 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_tl874t2atnlh3 t20 = gm_l3;
            void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t24 = gm_l2;
            gm_l4 = t24;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t26 = gm_l5;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t28 = (t26 < t27);
                if (!t28) goto lb25;
                {
                    uint64_t t31 = gm_l5;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t7te2tdpebbt4 t36 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 144));
                    F.l6 = t36;
                    {
                        uint8_t t39;
                        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t42 != ((uint8_t)1ULL)) goto mn41;
                        t39 = ((uint8_t)1);
                        goto me40;
mn41:;
                        t39 = ((uint8_t)0);
                        goto me40;
mn43:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 612, 12);
me40:;
                        if (!t39) goto ie38;
                        {
                            void * t45 = gm_l0;
                            gm_tl874t2atnlh3 t46 = gm_l3;
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sijukd7vsocnt));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(67));
                            gm_b0_1 t48 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t45, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                        }
                        {
                            void * t49 = gm_l0;
                            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                            gm_tl874t2atnlh3 t51 = gm_l3;
                            void * t52 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            uint8_t t53 = 0;
                            F.ret = t52;
                            goto gm_fret;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 611, 5);
me34:;
                }
lc25:;
                {
                    uint64_t t55 = gm_l5;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l5 = t58;
                }
            }
gm_ll29:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
            }
            goto gm_lad;
lb25:;
        }
    }
    {
        uint64_t t60 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srch6h8rdicu443, 617, 30);
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t60 * 144) + 0) + 0));
        if (t62 != ((uint8_t)0ULL)) goto ie59;
        gm_tc3oht4ksth9d t63 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t60 * 144) + 0) + 8));
        F.l7 = t63;
        {
            gm_b16_8 t65;
            uint8_t t67 = gm_l1;
            if (!t67) goto ie66;
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sn937bfucthvg));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(3));
            gm_b16_8 t70 = t69;
            t65 = t70;
            goto ix66;
ie66:;
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_srqehfttj6ngk));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(3));
            gm_b16_8 t73 = t72;
            t65 = t73;
ix66:;
            gm_l8 = t65;
        }
        {
            void * t74 = gm_l0;
            void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
            gm_b16_8 t76 = gm_l8;
            gm_tqu3m2l3n8mnc t77;
            gm_f_prelude_effect_gilrmm5((void *)&t77, (void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tiilmfd16v3fc t78;
            if (((((uint8_t *)&t77))[24] != 0)) {
                t78 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t77) + 0));
            } else {
                void * t79 = gm_l0;
                void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                gm_tl874t2atnlh3 t81 = gm_l3;
                void * t82 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t83 = 0;
                F.ret = t82;
                goto gm_fret;
            }
            gm_l9 = t78;
        }
        {
            gm_f_list_084fcd6((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t84 = (void *)((uint8_t *)&F.l10);
            gm_tftb1odc739t5 t85 = gm_f_push_d1r4h19(t84, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l9)));
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srch6h8rdicu443, 621, 5);
            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
        }
        {
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
            gm_tnpifv20irnph t89 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t89;
        }
        {
            void * t90 = gm_l0;
            void * t91 = (void *)((uint8_t *)&gm_l11);
            gm_tl874t2atnlh3 t92 = gm_l3;
            gm_b0_1 t93 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t94 = gm_l0;
            gm_t419mo45g7j0a t95;
            memset((uint8_t *)&t95 + 28, 0, 4);
            memcpy((uint8_t *)&t95 + 0, ((uint8_t *)&gm_l9), 24);
            GM_ST(uint32_t, (uint8_t *)&t95 + 24, ((uint32_t)0ULL));
            gm_th1g69d7vpipg t96;
            GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t96 + 8), t95);
            void * t97 = (void *)((uint8_t *)&F.l7);
            uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t99 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t100 = (uintptr_t)((uint64_t)1ULL);
            if (t100 > (uintptr_t)t98 || (uintptr_t)t98 > (uintptr_t)t98) gm_trap_slice(t100, (uintptr_t)t98, t98, gm_srch6h8rdicu443, 624, 57);
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(((uint8_t *)t99 + t100 * 144)));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)((uintptr_t)t98 - t100));
            gm_tl874t2atnlh3 t102 = gm_l3;
            void * t103 = gm_f_build_write_k6dcf8r((void *)NULL, cx, NULL, t94, &t96, t97, t101, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t104 = 0;
            F.ret = t103;
            goto gm_fret;
        }
        goto ix59;
ie59:;
        {
            void * t106 = gm_l0;
            void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
            gm_tl874t2atnlh3 t108 = gm_l3;
            void * t109 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
        }
ix59:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_tir_literal_oh960hv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tir_literal_oh960hv F;
    void * gm_l0; /* e */
    void * gm_l1; /* operand */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t13 != ((uint8_t)0ULL)) goto pa12;
    goto po11;
pa12:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)1ULL)) goto pa14;
    goto po11;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    goto po11;
pa16:;
    goto mn10;
po11:;
    t2 = ((uint8_t)1);
    goto me3;
mn10:;
ma5:;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t19 != ((uint8_t)29ULL)) goto mn18;
    void * t20 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l1 = t20;
    void * t21 = gm_l1;
    uint8_t t22 = gm_f_tir_literal_oh960hv(t21);
    t2 = t22;
    goto me3;
mn18:;
ma6:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t24 != ((uint8_t)30ULL)) goto mn23;
    void * t25 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l2 = t25;
    void * t26 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 16));
    gm_l3 = t26;
    uint8_t t27 = 0;
    void * t28 = gm_l2;
    uint8_t t29 = gm_f_tir_literal_oh960hv(t28);
    if (t29) {
        void * t30 = gm_l3;
        uint8_t t31 = gm_f_tir_literal_oh960hv(t30);
        t27 = t31;
    } else t27 = 0;
    t2 = t27;
    goto me3;
mn23:;
ma7:;
    t2 = ((uint8_t)0);
    goto me3;
mn32:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 503, 40);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_trap_t21p26s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_trap_t21p26s F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l4; /* msg */
    GM_STACK_CHECK(&F, 712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie3;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sinmpfqtbo9in));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_teti411eujvf4 t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 8), t11);
        gm_tl874t2atnlh3 t13 = gm_l2;
        void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t15 = t14;
        t2 = t15;
        goto ix3;
ie3:;
        void * t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t19 = ((t18 == ((uint64_t)1ULL)));
        if (!t19) goto ie17;
        uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srch6h8rdicu443, 514, 33);
        uint8_t t22;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 144) + 0) + 0));
        if (t25 != ((uint8_t)0ULL)) goto mn24;
        t22 = ((uint8_t)1);
        goto me23;
mn24:;
        t22 = ((uint8_t)0);
        goto me23;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 514, 33);
me23:;
        if (!t22) goto ie17;
        void * t28;
        uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srch6h8rdicu443, 515, 27);
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t30 * 144) + 0) + 0));
        if (t32 != ((uint8_t)0ULL)) goto ie29;
        gm_tc3oht4ksth9d t33 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t30 * 144) + 0) + 8));
        F.l3 = t33;
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = (void *)((uint8_t *)&F.l3);
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_t88rcucg1b7uk t41 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
        ((uint8_t *)&t42)[4] = 1;
        void * t43 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t36, t37, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        gm_t88rcucg1b7uk t47 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t48 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t35, t43, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t49 = t48;
        t28 = t49;
        goto ix29;
ie29:;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        gm_tl874t2atnlh3 t53 = gm_l2;
        void * t54 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t55 = 0;
        F.ret = t54;
        goto gm_fret;
        t28 = ((void *)0);
ix29:;
        void * t56 = t28;
        t16 = t56;
        goto ix17;
ie17:;
        {
            void * t58 = gm_l0;
            gm_tl874t2atnlh3 t59 = gm_l2;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sj4n6nvsgq2jn));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(42));
            gm_b0_1 t61 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t58, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        gm_tl874t2atnlh3 t64 = gm_l2;
        void * t65 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t66 = 0;
        F.ret = t65;
        goto gm_fret;
        t16 = ((void *)0);
ix17:;
        t2 = t16;
ix3:;
        gm_l4 = t2;
    }
    {
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
        void * t69 = (void *)(((uint8_t *)t68) + 8);
        gm_t88rcucg1b7uk t70 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t71 = gm_l4;
        gm_teti411eujvf4 t72;
        memset(&t72, 0, sizeof t72);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)52ULL));
        GM_ST(void *, ((uint8_t *)&t72 + 8), t71);
        gm_tl874t2atnlh3 t73 = gm_l2;
        void * t74 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t70, &t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t75 = 0;
        F.ret = t74;
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
__attribute__((aligned(4))) void * gm_f_variadic_arg_dfdnlo3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_variadic_arg_dfdnlo3 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* a */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint16_t gm_l5; /* bits */
    uint16_t gm_l6; /* bits */
    gm_t88rcucg1b7uk gm_l7; /* inner */
    uint8_t gm_l8; /* ok */
    GM_STACK_CHECK(&F, 586u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
        if (t4 != ((uint8_t)0ULL)) goto ie2;
        gm_tc3oht4ksth9d t5 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t3) + 0) + 8));
        F.l2 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)((uint8_t *)&F.l2);
            gm_tkn42u7na3t81 t9;
            memset(&t9, 0, sizeof t9);
            void * t10 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t10;
        }
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 96);
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = gm_l3;
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 0));
            gm_t88rcucg1b7uk t17 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t12, t14, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t17;
        }
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            void * t20 = (void *)(((uint8_t *)t19) + 8);
            gm_t88rcucg1b7uk t21 = gm_l4;
            gm_t53at80pfdus3 t22;
            gm_f_kind_jcnt4hm((void *)&t22, t20, GM_CONV(gm_csguankko4lmhd, t21));
            uint8_t t23;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            switch (t31) { case ((uint8_t)0ULL): goto ma25; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma29; case ((uint8_t)5ULL): goto ma29; case ((uint8_t)6ULL): goto ma29; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma29; case ((uint8_t)11ULL): goto ma29; case ((uint8_t)12ULL): goto ma29; case ((uint8_t)13ULL): goto ma28; case ((uint8_t)14ULL): goto ma29; case ((uint8_t)15ULL): goto ma29; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma29; case ((uint8_t)18ULL): goto ma29; case ((uint8_t)19ULL): goto ma29; case ((uint8_t)20ULL): goto ma29; case ((uint8_t)21ULL): goto ma29; case ((uint8_t)22ULL): goto ma29; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma29; case ((uint8_t)25ULL): goto ma29; case ((uint8_t)26ULL): goto ma26; default: goto ma29; }
ma25:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t33 != ((uint8_t)0ULL)) goto mn32;
            uint16_t t34 = GM_LD(uint16_t, ((((uint8_t *)&t22) + 8) + 2));
            gm_l5 = t34;
            uint16_t t35 = gm_l5;
            gm_tuqvtbj0p3v9c t36;
            GM_ST(uint16_t, ((uint8_t *)&t36 + 0), ((uint16_t)32ULL));
            GM_ST(uint16_t, ((uint8_t *)&t36 + 2), ((uint16_t)64ULL));
            uint8_t t37 = 0;
            if (gm_eq9oqmmiu8qc(((uint8_t *)&t36) + 0, ((uint8_t *)&t35))) t37 = 1;
            if (gm_eq9oqmmiu8qc(((uint8_t *)&t36) + 2, ((uint8_t *)&t35))) t37 = 1;
            uint8_t t38 = (uint8_t)(t37);
            t23 = t38;
            goto me24;
mn32:;
ma26:;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t42 != ((uint8_t)1ULL)) goto pa41;
            goto po40;
pa41:;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t44 != ((uint8_t)2ULL)) goto pa43;
            goto po40;
pa43:;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t46 != ((uint8_t)7ULL)) goto pa45;
            goto po40;
pa45:;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t48 != ((uint8_t)8ULL)) goto pa47;
            goto po40;
pa47:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t50 != ((uint8_t)9ULL)) goto pa49;
            goto po40;
pa49:;
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t52 != ((uint8_t)23ULL)) goto pa51;
            goto po40;
pa51:;
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t54 != ((uint8_t)26ULL)) goto pa53;
            goto po40;
pa53:;
            goto mn39;
po40:;
            t23 = ((uint8_t)1);
            goto me24;
mn39:;
ma27:;
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t56 != ((uint8_t)3ULL)) goto mn55;
            uint16_t t57 = GM_LD(uint16_t, (((uint8_t *)&t22) + 8));
            gm_l6 = t57;
            uint16_t t58 = gm_l6;
            uint8_t t59 = ((t58 == ((uint16_t)64ULL)));
            t23 = t59;
            goto me24;
mn55:;
ma28:;
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t61 != ((uint8_t)13ULL)) goto mn60;
            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t22) + 8));
            gm_l7 = t62;
            void * t63 = gm_l0;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
            gm_t88rcucg1b7uk t65 = gm_l7;
            uint8_t t66 = gm_f_is_nullable_pointer_nkie06s(t64, GM_CONV(gm_csguankko4lmhd, t65));
            t23 = t66;
            goto me24;
mn60:;
ma29:;
            t23 = ((uint8_t)0);
            goto me24;
mn67:;
ma30:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 636, 11);
me24:;
            gm_l8 = t23;
        }
        {
            uint8_t t69 = gm_l8;
            uint8_t t70 = (uint8_t)!t69;
            if (!t70) goto ie68;
            {
                void * t72 = gm_l0;
                gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
                void * t74 = gm_l0;
                gm_t88rcucg1b7uk t75 = gm_l4;
                gm_b16_8 t76 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t77;
                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                gm_tabb710d9mq2e t78;
                gm_f_format_kub2tep((void *)&t78, (void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srch6h8rdicu443, 643, 34);
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                gm_b0_1 t81 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t72, t73, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix68;
ie68:;
ix68:;
        }
        {
            void * t82 = gm_l3;
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            {
                void * t86 = gm_l0;
                void * t87 = gm_l1;
                gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 128));
                gm_b16_8 t89;
                GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sbs1lj6q8rfiu));
                GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(43));
                gm_b0_1 t90 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t91 = gm_l0;
                void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
                void * t93 = gm_l1;
                gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 128));
                void * t95 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t92, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t96 = 0;
                F.ret = t95;
                goto gm_fret;
            }
        }
ix2:;
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
