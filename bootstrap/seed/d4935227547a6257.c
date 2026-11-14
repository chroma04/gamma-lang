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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { gm_b16_8 f0; gm_ttu09lij36f8c f1; gm_th2lqbo77pvsc f2; } gm_t03ccdabio1fj;
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_t03ccdabio1fj) == 80 && _Alignof(gm_t03ccdabio1fj) == 8 && offsetof(gm_t03ccdabio1fj, f0) == 0 && offsetof(gm_t03ccdabio1fj, f1) == 16 && offsetof(gm_t03ccdabio1fj, f2) == 56, "layout of `(url: string, reference: ?(key: string, value: string), subdir: ?string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t03ccdabio1fj v1; gm_tt008mgut8glb v2; } f1; } gm_t2jeshnsn30ep;
_Static_assert(sizeof(gm_t2jeshnsn30ep) == 88 && _Alignof(gm_t2jeshnsn30ep) == 8 && offsetof(gm_t2jeshnsn30ep, f0) == 0 && offsetof(gm_t2jeshnsn30ep, f1) == 8, "layout of `NewDep`");
struct gm_fr_add_dependency_jmjffrp {
    gm_frame h;
    gm_t2jeshnsn30ep l2; /* dep */
    gm_trqfvmjtt4sij l9; /* lines */
    gm_trqfvmjtt4sij l10; /* out */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entry_lines_3jhl7a4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_header_of_2u6atoc {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_inline_value_0impjcn {
    gm_frame h;
    gm_t2jeshnsn30ep l0; /* dep */
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt008mgut8glb l3; /* u */
    gm_t03ccdabio1fj l6; /* g */
    gm_b16_8 ret;
};
struct gm_fr_is_blank_or_comment_lubpopi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_header_uakq83c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_without_ej3mkbe {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_key_of_99iu4dv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_normalize_newlines_183t7r2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_remove_dependency_4ju7b0h {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* lines */
    gm_trqfvmjtt4sij l6; /* keep */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_split_lines_jecth9m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* lines */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_table_range_09bngqh {
    gm_frame h;
    gm_tc70a9dbbmgkg ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_entry_lines_3jhl7a4(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_7plp021(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f1281l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fqec6j2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_m89t2jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rkrg0f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_header_of_2u6atoc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_inline_value_0impjcn(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_without_ej3mkbe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_key_of_99iu4dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_newlines_183t7r2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_split_lines_jecth9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_table_range_09bngqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_toml_string_cagbrmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5jambfs(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ctdpla1(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gle9guv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ivc38em(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r83e4t6(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rbbkc88(void * a0, gm_tnpifv20irnph a1);
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_sajfarek3i4jp[44];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_shv5ofgc65v68[3];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_snomh93h7tumv[14];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srclt70oog8j9io[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sv9j89ufns35b[16];
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_blank_or_comment_lubpopi F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) {
            gm_b16_8 t7 = gm_l1;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_seccpktgmrevt));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
            uint8_t t9 = gm_f_starts_with_6vvt01e(t7, t8);
            t4 = t9;
        } else t4 = 1;
        uint8_t t10 = 0;
        F.ret = t4;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_header_uakq83c F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sk754mj3p1el6));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
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
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_add_dependency_jmjffrp F;
    gm_b16_8 gm_l0; /* original */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l3; /* text */
    gm_tob1snuii5bme gm_l4; /* e */
    void * gm_l5; /* doc */
    void * gm_l6; /* d */
    void * gm_l7; /* t */
    gm_b16_8 gm_l8; /* entry */
    gm_t2lqu8jsj511d gm_l11; /* r */
    uint64_t gm_l12; /* last */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l15; /* l */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* l */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* l */
    gm_b16_8 gm_l23; /* h */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l26; /* x */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* x */
    GM_STACK_CHECK(&F, 3168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_normalize_newlines_183t7r2((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t3;
    }
    {
        gm_b16_8 t4 = gm_l3;
        gm_trssmtuv8jr0v t5;
        gm_f_parse_toml_gjtmf79((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trssmtuv8jr0v t6 = t5;
        void * t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            void * t8 = GM_LD(void *, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_tob1snuii5bme t9 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t6) + 8));
            gm_l4 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_tnpifv20irnph t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
            gm_tabb710d9mq2e t13;
            gm_f_format_f1281l5((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srclt70oog8j9io, 138, 54);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
            gm_b16_8 t16 = t15;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_t3ujhu6qph7de t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
            gm_t3ujhu6qph7de t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        gm_l5 = t7;
    }
    {
        void * t22 = gm_l5;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(12));
        void * t24 = gm_f_get_9rme0gb(t22, t23);
        void * t25 = t24;
        if (!(GM_LD(void *, ((uint8_t *)&t25)) != 0)) goto ie21;
        void * t26 = GM_LD(void *, ((uint8_t *)&t25));
        gm_l6 = t26;
        {
            void * t28 = gm_l6;
            void * t29 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t28) + 16)));
            void * t30 = t29;
            void * t31;
            if ((GM_LD(void *, ((uint8_t *)&t30)) != 0)) {
                void * t32 = GM_LD(void *, (((uint8_t *)&t30) + 0));
                t31 = t32;
            } else {
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sajfarek3i4jp));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(43));
                gm_t3ujhu6qph7de t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                ((uint8_t *)&t34)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t34 + 8), t33);
                gm_t3ujhu6qph7de t35 = t34;
                uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
                F.ret = t34;
                goto gm_fret;
            }
            gm_l7 = t31;
        }
        {
            void * t38 = gm_l7;
            gm_b16_8 t39 = gm_l1;
            void * t40 = gm_f_get_9rme0gb(t38, t39);
            void * t41 = t40;
            uint8_t t42;
            if ((GM_LD(void *, ((uint8_t *)&t41)) != 0)) goto mn44;
            t42 = ((uint8_t)0);
            goto me43;
mn44:;
            t42 = ((uint8_t)1);
            goto me43;
mn45:;
            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 141, 12);
me43:;
            if (!t42) goto ie37;
            {
                gm_b16_8 t47 = gm_l1;
                gm_tnpifv20irnph t48;
                GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                gm_tabb710d9mq2e t49;
                gm_f_format_m89t2jt((void *)&t49, (void *)NULL, cx, NULL, t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t50 = t49;
                if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srclt70oog8j9io, 141, 46);
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                gm_b16_8 t52 = t51;
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                gm_t3ujhu6qph7de t54;
                GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                ((uint8_t *)&t54)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
                gm_t3ujhu6qph7de t55 = t54;
                uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
                F.ret = t54;
                goto gm_fret;
            }
            goto ix37;
ie37:;
ix37:;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_t6j6smj7qoctu t57;
        gm_b16_8 t58 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t58);
        gm_b16_8 t59 = gm_f_inline_value_0impjcn((void *)NULL, cx, NULL, &(*(gm_t2jeshnsn30ep *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t59);
        gm_tabb710d9mq2e t60;
        gm_f_format_fqec6j2((void *)&t60, (void *)NULL, cx, NULL, &t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t61 = t60;
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srclt70oog8j9io, 143, 14);
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
        gm_l8 = t62;
    }
    {
        gm_b16_8 t63 = gm_l3;
        gm_f_split_lines_jecth9m((void *)&F.l9, (void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t66 = t65;
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(12));
        gm_tc70a9dbbmgkg t69;
        gm_f_table_range_09bngqh((void *)&t69, (void *)NULL, cx, NULL, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tc70a9dbbmgkg t70 = t69;
        if (!((((uint8_t *)&t70))[16] != 0)) goto ie64;
        gm_t2lqu8jsj511d t71 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t70));
        gm_l11 = t71;
        {
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            gm_l12 = t73;
        }
        {
            for (;;) {
                uint64_t t75 = gm_l12;
                uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 0));
                uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                if (o78) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 149, 22);
                uint64_t t79 = r77;
                uint8_t t80 = (t75 > t79);
                if (!t80) goto lb74;
                uint64_t t81 = gm_l12;
                uint64_t r82; bool o83 = __builtin_sub_overflow(t81, ((uint64_t)1ULL), &r82);
                if (o83) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 149, 71);
                uint64_t t84 = r82;
                uint64_t t85 = (uintptr_t)t84;
                uint64_t t86 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srclt70oog8j9io, 149, 59);
                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t85 * 16));
                uint8_t t88 = gm_f_is_blank_or_comment_lubpopi(t87);
                if (!t88) goto lb74;
                {
                    uint64_t t91 = gm_l12;
                    uint64_t r92; bool o93 = __builtin_sub_overflow(t91, ((uint64_t)1ULL), &r92);
                    if (o93) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 149, 84);
                    uint64_t t94 = r92;
                    gm_l12 = t94;
                }
lc74:;
            }
lb74:;
        }
        {
            {
                uint64_t t96 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                void * t97 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                uint64_t t98 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t99 = gm_l12;
                uint64_t t100 = (uintptr_t)t99;
                if (t98 > t100 || t100 > (uintptr_t)t96) gm_trap_slice(t98, t100, t96, gm_srclt70oog8j9io, 150, 18);
                gm_b16_8 t101;
                GM_ST(void *, (uint8_t *)&t101, (void *)(((uint8_t *)t97 + t98 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(t100 - t98));
                gm_l13 = t101;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t103 = gm_l14;
                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t105 = (t103 < t104);
                    if (!t105) goto lb102;
                    {
                        uint64_t t108 = gm_l14;
                        uint64_t t109 = (uintptr_t)t108;
                        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t109 * 16));
                        gm_l15 = t113;
                        {
                            void * t115 = (void *)((uint8_t *)&F.l10);
                            gm_b16_8 t116 = gm_l15;
                            gm_tftb1odc739t5 t117 = gm_f_extend_0cs85gs(t115, t116);
                            gm_tftb1odc739t5 t118 = t117;
                            if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srclt70oog8j9io, 150, 41);
                            gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                        }
                        goto me111;
mn112:;
                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 150, 9);
me111:;
                    }
lc102:;
                    {
                        uint64_t t121 = gm_l14;
                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                        uint64_t t124 = r122;
                        gm_l14 = t124;
                    }
                }
lb102:;
            }
        }
        {
            uint64_t t126 = gm_l12;
            uint8_t t127 = (t126 > ((uint64_t)0ULL));
            if (!t127) goto ie125;
            uint64_t t128 = gm_l12;
            uint64_t r129; bool o130 = __builtin_sub_overflow(t128, ((uint64_t)1ULL), &r129);
            if (o130) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 151, 41);
            uint64_t t131 = r129;
            uint64_t t132 = (uintptr_t)t131;
            uint64_t t133 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
            if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srclt70oog8j9io, 151, 29);
            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t132 * 16));
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(1));
            uint8_t t136 = gm_f_ends_with_biriu5l(t134, t135);
            uint8_t t137 = (uint8_t)!t136;
            if (!t137) goto ie125;
            {
                void * t139 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t140 = gm_f_push_t83rg2m(t139, ((uint8_t)10ULL));
                gm_tftb1odc739t5 t141 = t140;
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srclt70oog8j9io, 151, 69);
                gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
            }
            goto ix125;
ie125:;
ix125:;
        }
        {
            void * t143 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t144 = gm_l8;
            gm_b16_8 t145 = t144;
            gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
            gm_tftb1odc739t5 t147 = gm_f_extend_0cs85gs(t143, t146);
            gm_tftb1odc739t5 t148 = t147;
            if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_srclt70oog8j9io, 152, 9);
            gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
        }
        {
            {
                uint64_t t151 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                void * t152 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                uint64_t t153 = gm_l12;
                uint64_t t154 = (uintptr_t)t153;
                if (t154 > (uintptr_t)t151 || (uintptr_t)t151 > (uintptr_t)t151) gm_trap_slice(t154, (uintptr_t)t151, t151, gm_srclt70oog8j9io, 153, 18);
                gm_b16_8 t155;
                GM_ST(void *, (uint8_t *)&t155, (void *)(((uint8_t *)t152 + t154 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)((uintptr_t)t151 - t154));
                gm_l16 = t155;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t157 = gm_l17;
                    uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t159 = (t157 < t158);
                    if (!t159) goto lb156;
                    {
                        uint64_t t162 = gm_l17;
                        uint64_t t163 = (uintptr_t)t162;
                        uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t163 * 16));
                        gm_l18 = t167;
                        {
                            void * t169 = (void *)((uint8_t *)&F.l10);
                            gm_b16_8 t170 = gm_l18;
                            gm_tftb1odc739t5 t171 = gm_f_extend_0cs85gs(t169, t170);
                            gm_tftb1odc739t5 t172 = t171;
                            if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srclt70oog8j9io, 153, 40);
                            gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                        }
                        goto me165;
mn166:;
                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 153, 9);
me165:;
                    }
lc156:;
                    {
                        uint64_t t175 = gm_l17;
                        uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                        uint64_t t178 = r176;
                        gm_l17 = t178;
                    }
                }
lb156:;
            }
        }
        {
            gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t180 = t179;
            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
            gm_t3ujhu6qph7de t182;
            GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
            ((uint8_t *)&t182)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t182 + 8), t181);
            gm_t3ujhu6qph7de t183 = t182;
            uint8_t t184 = ((((uint8_t *)&t183))[0] != 0);
            F.ret = t182;
            goto gm_fret;
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        {
            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t187 = t186;
            gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
            gm_l19 = t188;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t190 = gm_l20;
                uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t192 = (t190 < t191);
                if (!t192) goto lb189;
                {
                    gm_tf1bjg81u0glv t195;
                    uint64_t t196 = gm_l20;
                    GM_ST(uint64_t, (uint8_t *)&t195 + 0, t196);
                    uint64_t t197 = gm_l20;
                    uint64_t t198 = (uintptr_t)t197;
                    uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t198 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t195 + 8, t200);
                    gm_tf1bjg81u0glv t201 = t195;
                    uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&t201) + 0));
                    gm_l21 = t204;
                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&t201) + 8));
                    gm_l22 = t205;
                    {
                        gm_b16_8 t208 = gm_l22;
                        gm_th2lqbo77pvsc t209;
                        gm_f_header_of_2u6atoc((void *)&t209, (void *)NULL, cx, NULL, t208);
                        if (gm_unw.kind) {
                            goto gm_ll193;
                        }
                        gm_th2lqbo77pvsc t210 = t209;
                        if (!((((uint8_t *)&t210))[16] != 0)) goto ie207;
                        gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&t210));
                        gm_l23 = t211;
                        {
                            gm_b16_8 t214 = gm_l23;
                            gm_b16_8 t215;
                            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_snomh93h7tumv));
                            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(13));
                            uint8_t t216 = gm_f_starts_with_6vvt01e(t214, t215);
                            if (!t216) goto ie213;
                            {
                                {
                                    uint64_t t219 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                    void * t220 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                                    uint64_t t221 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t222 = gm_l21;
                                    uint64_t t223 = (uintptr_t)t222;
                                    if (t221 > t223 || t223 > (uintptr_t)t219) gm_trap_slice(t221, t223, t219, gm_srclt70oog8j9io, 160, 26);
                                    gm_b16_8 t224;
                                    GM_ST(void *, (uint8_t *)&t224, (void *)(((uint8_t *)t220 + t221 * 16)));
                                    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(t223 - t221));
                                    gm_l24 = t224;
                                }
                                {
                                    gm_l25 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t226 = gm_l25;
                                        uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                        uint8_t t228 = (t226 < t227);
                                        if (!t228) goto lb225;
                                        {
                                            uint64_t t231 = gm_l25;
                                            uint64_t t232 = (uintptr_t)t231;
                                            uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t232 * 16));
                                            gm_l26 = t236;
                                            {
                                                void * t238 = (void *)((uint8_t *)&F.l10);
                                                gm_b16_8 t239 = gm_l26;
                                                gm_tftb1odc739t5 t240 = gm_f_extend_0cs85gs(t238, t239);
                                                gm_tftb1odc739t5 t241 = t240;
                                                if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_srclt70oog8j9io, 160, 46);
                                                gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
                                            }
                                            goto me234;
mn235:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 160, 17);
me234:;
                                        }
lc225:;
                                        {
                                            uint64_t t244 = gm_l25;
                                            uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                                            uint64_t t247 = r245;
                                            gm_l25 = t247;
                                        }
                                    }
lb225:;
                                }
                            }
                            {
                                uint64_t t249 = gm_l21;
                                uint8_t t250 = (t249 > ((uint64_t)0ULL));
                                if (!t250) goto ie248;
                                uint64_t t251 = gm_l21;
                                uint64_t r252; bool o253 = __builtin_sub_overflow(t251, ((uint64_t)1ULL), &r252);
                                if (o253) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 161, 66);
                                uint64_t t254 = r252;
                                uint64_t t255 = (uintptr_t)t254;
                                uint64_t t256 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                if (t255 >= (uintptr_t)(t256)) gm_trap_index(t255, t256, gm_srclt70oog8j9io, 161, 54);
                                gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t255 * 16));
                                uint8_t t258 = gm_f_is_blank_or_comment_lubpopi(t257);
                                uint8_t t259 = (uint8_t)!t258;
                                if (!t259) goto ie248;
                                {
                                    void * t261 = (void *)((uint8_t *)&F.l10);
                                    gm_tftb1odc739t5 t262 = gm_f_push_t83rg2m(t261, ((uint8_t)10ULL));
                                    gm_tftb1odc739t5 t263 = t262;
                                    if (((((uint8_t *)&t263))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t263) + 1, gm_srclt70oog8j9io, 161, 76);
                                    gm_b0_1 t264 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 1));
                                }
                                goto ix248;
ie248:;
ix248:;
                            }
                            {
                                void * t265 = (void *)((uint8_t *)&F.l10);
                                gm_b16_8 t266;
                                GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sv9j89ufns35b));
                                GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(15));
                                gm_tftb1odc739t5 t267 = gm_f_extend_0cs85gs(t265, t266);
                                gm_tftb1odc739t5 t268 = t267;
                                if (((((uint8_t *)&t268))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t268) + 1, gm_srclt70oog8j9io, 162, 17);
                                gm_b0_1 t269 = GM_LD(gm_b0_1, (((uint8_t *)&t268) + 1));
                            }
                            {
                                void * t270 = (void *)((uint8_t *)&F.l10);
                                gm_b16_8 t271 = gm_l8;
                                gm_b16_8 t272 = t271;
                                gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
                                gm_tftb1odc739t5 t274 = gm_f_extend_0cs85gs(t270, t273);
                                gm_tftb1odc739t5 t275 = t274;
                                if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 1, gm_srclt70oog8j9io, 163, 17);
                                gm_b0_1 t276 = GM_LD(gm_b0_1, (((uint8_t *)&t275) + 1));
                            }
                            {
                                {
                                    uint64_t t278 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                    void * t279 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                                    uint64_t t280 = gm_l21;
                                    uint64_t t281 = (uintptr_t)t280;
                                    if (t281 > (uintptr_t)t278 || (uintptr_t)t278 > (uintptr_t)t278) gm_trap_slice(t281, (uintptr_t)t278, t278, gm_srclt70oog8j9io, 164, 26);
                                    gm_b16_8 t282;
                                    GM_ST(void *, (uint8_t *)&t282, (void *)(((uint8_t *)t279 + t281 * 16)));
                                    GM_ST(uintptr_t, (uint8_t *)&t282 + 8, (uintptr_t)((uintptr_t)t278 - t281));
                                    gm_l27 = t282;
                                }
                                {
                                    gm_l28 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t284 = gm_l28;
                                        uint64_t t285 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                                        uint8_t t286 = (t284 < t285);
                                        if (!t286) goto lb283;
                                        {
                                            uint64_t t289 = gm_l28;
                                            uint64_t t290 = (uintptr_t)t289;
                                            uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                                            gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t290 * 16));
                                            gm_l29 = t294;
                                            {
                                                void * t296 = (void *)((uint8_t *)&F.l10);
                                                gm_b16_8 t297 = gm_l29;
                                                gm_tftb1odc739t5 t298 = gm_f_extend_0cs85gs(t296, t297);
                                                gm_tftb1odc739t5 t299 = t298;
                                                if (((((uint8_t *)&t299))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t299) + 1, gm_srclt70oog8j9io, 164, 45);
                                                gm_b0_1 t300 = GM_LD(gm_b0_1, (((uint8_t *)&t299) + 1));
                                            }
                                            goto me292;
mn293:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 164, 17);
me292:;
                                        }
lc283:;
                                        {
                                            uint64_t t302 = gm_l28;
                                            uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                                            uint64_t t305 = r303;
                                            gm_l28 = t305;
                                        }
                                    }
lb283:;
                                }
                            }
                            {
                                gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_b16_8 t307 = t306;
                                gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&t307));
                                gm_t3ujhu6qph7de t309;
                                GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
                                ((uint8_t *)&t309)[0] = 0;
                                GM_ST(gm_b16_8, ((uint8_t *)&t309 + 8), t308);
                                gm_t3ujhu6qph7de t310 = t309;
                                uint8_t t311 = ((((uint8_t *)&t310))[0] != 0);
                                F.ret = t309;
                                goto gm_fret;
                            }
                            goto ix213;
ie213:;
ix213:;
                        }
                        goto ix207;
ie207:;
ix207:;
                    }
                    goto me202;
mn203:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 157, 5);
me202:;
                }
lc189:;
                {
                    uint64_t t313 = gm_l20;
                    uint64_t r314; bool o315 = __builtin_add_overflow(t313, ((uint64_t)1ULL), &r314);
                    uint64_t t316 = r314;
                    gm_l20 = t316;
                }
            }
gm_ll193:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb189; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc189; }
            }
            goto gm_lad;
lb189:;
        }
    }
    {
        void * t317 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t318 = gm_l3;
        gm_tftb1odc739t5 t319 = gm_f_extend_0cs85gs(t317, t318);
        gm_tftb1odc739t5 t320 = t319;
        if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 1, gm_srclt70oog8j9io, 169, 5);
        gm_b0_1 t321 = GM_LD(gm_b0_1, (((uint8_t *)&t320) + 1));
    }
    {
        uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t324 = (t323 > ((uint64_t)0ULL));
        if (!t324) goto ie322;
        {
            gm_b16_8 t327 = gm_l3;
            gm_b16_8 t328;
            GM_ST(void *, (uint8_t *)&t328, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(1));
            uint8_t t329 = gm_f_ends_with_biriu5l(t327, t328);
            uint8_t t330 = (uint8_t)!t329;
            if (!t330) goto ie326;
            {
                void * t332 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t333 = gm_f_push_t83rg2m(t332, ((uint8_t)10ULL));
                gm_tftb1odc739t5 t334 = t333;
                if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 1, gm_srclt70oog8j9io, 171, 39);
                gm_b0_1 t335 = GM_LD(gm_b0_1, (((uint8_t *)&t334) + 1));
            }
            goto ix326;
ie326:;
ix326:;
        }
        {
            void * t336 = (void *)((uint8_t *)&F.l10);
            gm_tftb1odc739t5 t337 = gm_f_push_t83rg2m(t336, ((uint8_t)10ULL));
            gm_tftb1odc739t5 t338 = t337;
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srclt70oog8j9io, 172, 9);
            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
        }
        goto ix322;
ie322:;
ix322:;
    }
    {
        void * t340 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t341;
        GM_ST(void *, (uint8_t *)&t341, (void *)(gm_sv9j89ufns35b));
        GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(15));
        gm_tftb1odc739t5 t342 = gm_f_extend_0cs85gs(t340, t341);
        gm_tftb1odc739t5 t343 = t342;
        if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 1, gm_srclt70oog8j9io, 174, 5);
        gm_b0_1 t344 = GM_LD(gm_b0_1, (((uint8_t *)&t343) + 1));
    }
    {
        void * t345 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t346 = gm_l8;
        gm_b16_8 t347 = t346;
        gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&t347));
        gm_tftb1odc739t5 t349 = gm_f_extend_0cs85gs(t345, t348);
        gm_tftb1odc739t5 t350 = t349;
        if (((((uint8_t *)&t350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t350) + 1, gm_srclt70oog8j9io, 175, 5);
        gm_b0_1 t351 = GM_LD(gm_b0_1, (((uint8_t *)&t350) + 1));
    }
    {
        gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t353 = t352;
        gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
        gm_t3ujhu6qph7de t355;
        GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
        ((uint8_t *)&t355)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t355 + 8), t354);
        gm_t3ujhu6qph7de t356 = t355;
        uint8_t t357 = ((((uint8_t *)&t356))[0] != 0);
        F.ret = t355;
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
__attribute__((aligned(4))) uint64_t gm_f_entry_lines_3jhl7a4(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_entry_lines_3jhl7a4 F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* at */
    int32_t gm_l2; /* depth */
    uint8_t gm_l3; /* quote */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((int32_t)0LL);
    }
    {
        gm_l3 = ((uint8_t)0ULL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        uint64_t t2 = gm_l1;
        gm_l5 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l5;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            {
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srclt70oog8j9io, 79, 18);
                    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t11 * 16));
                    gm_l6 = t13;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t15 = gm_l7;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t17 = (t15 < t16);
                        if (!t17) goto lb14;
                        {
                            uint64_t t20 = gm_l7;
                            uint64_t t21 = (uintptr_t)t20;
                            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t21 * 1));
                            uint8_t t24 = t23;
                            uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t24));
                            gm_l8 = t27;
                            {
                                uint8_t t30 = gm_l3;
                                uint8_t t31 = (!(t30 == ((uint8_t)0ULL)));
                                if (!t31) goto ie29;
                                {
                                    uint8_t t34 = gm_l8;
                                    uint8_t t35 = gm_l3;
                                    uint8_t t36 = ((t34 == t35));
                                    if (!t36) goto ie33;
                                    {
                                        gm_l3 = ((uint8_t)0ULL);
                                    }
                                    goto ix33;
ie33:;
ix33:;
                                }
                                goto ix29;
ie29:;
                                uint8_t t39 = 0;
                                uint8_t t40 = gm_l8;
                                uint8_t t41 = ((t40 == ((uint8_t)34ULL)));
                                if (!t41) {
                                    uint8_t t42 = gm_l8;
                                    uint8_t t43 = ((t42 == ((uint8_t)39ULL)));
                                    t39 = t43;
                                } else t39 = 1;
                                if (!t39) goto ie38;
                                {
                                    uint8_t t45 = gm_l8;
                                    gm_l3 = t45;
                                }
                                goto ix38;
ie38:;
                                uint8_t t47 = gm_l8;
                                uint8_t t48 = ((t47 == ((uint8_t)35ULL)));
                                if (!t48) goto ie46;
                                {
                                    goto lb14;
                                }
                                goto ix46;
ie46:;
                                uint8_t t51 = gm_l8;
                                gm_tqs5p4n2tq19v t52;
                                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)91ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t52 + 1), ((uint8_t)123ULL));
                                gm_tqs5p4n2tq19v t53 = t52;
                                uint8_t t54 = 0;
                                uint8_t t55 = t51;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t53) + 0, ((uint8_t *)&t55))) t54 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t53) + 1, ((uint8_t *)&t55))) t54 = 1;
                                uint8_t t56 = (uint8_t)(t54);
                                if (!t56) goto ie50;
                                {
                                    int32_t t58 = gm_l2;
                                    int32_t r59; bool o60 = __builtin_add_overflow(t58, ((int32_t)1LL), &r59);
                                    if (o60) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 87, 17);
                                    int32_t t61 = r59;
                                    gm_l2 = t61;
                                }
                                goto ix50;
ie50:;
                                uint8_t t63 = gm_l8;
                                gm_tqs5p4n2tq19v t64;
                                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)93ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t64 + 1), ((uint8_t)125ULL));
                                gm_tqs5p4n2tq19v t65 = t64;
                                uint8_t t66 = 0;
                                uint8_t t67 = t63;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t65) + 0, ((uint8_t *)&t67))) t66 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t65) + 1, ((uint8_t *)&t67))) t66 = 1;
                                uint8_t t68 = (uint8_t)(t66);
                                if (!t68) goto ie62;
                                {
                                    int32_t t70 = gm_l2;
                                    int32_t r71; bool o72 = __builtin_sub_overflow(t70, ((int32_t)1LL), &r71);
                                    if (o72) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 89, 17);
                                    int32_t t73 = r71;
                                    gm_l2 = t73;
                                }
                                goto ix62;
ie62:;
ix62:;
ix50:;
ix46:;
ix38:;
ix29:;
                            }
                            goto me25;
mn26:;
                            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 79, 9);
me25:;
                        }
lc14:;
                        {
                            uint64_t t75 = gm_l7;
                            uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                            uint64_t t78 = r76;
                            gm_l7 = t78;
                        }
                    }
lb14:;
                }
            }
            {
                gm_l3 = ((uint8_t)0ULL);
            }
            {
                uint64_t t79 = gm_l4;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                if (o81) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 93, 9);
                uint64_t t82 = r80;
                gm_l4 = t82;
            }
            {
                uint64_t t83 = gm_l5;
                uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                if (o85) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 94, 9);
                uint64_t t86 = r84;
                gm_l5 = t86;
            }
            {
                int32_t t88 = gm_l2;
                uint8_t t89 = (t88 <= ((int32_t)0LL));
                if (!t89) goto ie87;
                {
                    goto lb3;
                }
                goto ix87;
ie87:;
ix87:;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t91 = gm_l4;
        uint8_t t92 = 0;
        F.ret = t91;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_header_of_2u6atoc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_header_of_2u6atoc F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 585u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        uint8_t t5 = 0;
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sk754mj3p1el6));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) {
            gm_b16_8 t10 = gm_l1;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_shv5ofgc65v68));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(2));
            uint8_t t12 = gm_f_starts_with_6vvt01e(t10, t11);
            t5 = t12;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            gm_th2lqbo77pvsc t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        gm_b16_8 t16 = gm_l1;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2053hhvf16fm));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t18 = gm_f_find_nab29co(t16, t17);
        gm_tf5srh4mp4b3i t19 = t18;
        uint64_t t20;
        if (((((uint8_t *)&t19))[8] != 0)) {
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
            t20 = t21;
        } else {
            gm_th2lqbo77pvsc t22;
            memset(&t22, 0, sizeof t22);
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        gm_l2 = t20;
    }
    {
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t26 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t27 = gm_l2;
        uint64_t t28 = (uintptr_t)t27;
        if (t26 > t28 || t28 > (uintptr_t)t24) gm_trap_slice(t26, t28, t24, gm_srclt70oog8j9io, 40, 30);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t26));
        gm_b16_8 t30 = gm_f_trim_ascii_c9j36d1(t29);
        gm_l3 = t30;
    }
    {
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t33 = (t32 >= ((uint64_t)2ULL));
        if (!t33) goto ie31;
        uint8_t t34 = 0;
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srclt70oog8j9io, 41, 28);
        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t35 * 1));
        uint8_t t38 = ((t37 == ((uint8_t)34ULL)));
        if (!t38) {
            uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_srclt70oog8j9io, 41, 47);
            uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t39 * 1));
            uint8_t t42 = ((t41 == ((uint8_t)39ULL)));
            t34 = t42;
        } else t34 = 1;
        if (!t34) goto ie31;
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r44; bool o45 = __builtin_sub_overflow(t43, ((uint64_t)1ULL), &r44);
        if (o45) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 41, 75);
        uint64_t t46 = r44;
        uint64_t t47 = (uintptr_t)t46;
        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t47 >= (uintptr_t)(t48)) gm_trap_index(t47, t48, gm_srclt70oog8j9io, 41, 69);
        uint8_t t49 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t47 * 1));
        uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srclt70oog8j9io, 41, 93);
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t50 * 1));
        uint8_t t53 = ((t49 == t52));
        if (!t53) goto ie31;
        {
            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t56 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t57 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t r59; bool o60 = __builtin_sub_overflow(t58, ((uint64_t)1ULL), &r59);
            if (o60) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 42, 25);
            uint64_t t61 = r59;
            uint64_t t62 = (uintptr_t)t61;
            if (t57 > t62 || t62 > (uintptr_t)t55) gm_trap_slice(t57, t62, t55, gm_srclt70oog8j9io, 42, 16);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t56 + t57 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62 - t57));
            gm_th2lqbo77pvsc t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t64), t63);
            ((uint8_t *)&t64)[16] = 1;
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        gm_b16_8 t66 = gm_l3;
        gm_th2lqbo77pvsc t67;
        GM_ST(uint8_t, (uint8_t *)&t67 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t67 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t67 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t67), t66);
        ((uint8_t *)&t67)[16] = 1;
        uint8_t t68 = 0;
        F.ret = t67;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_inline_value_0impjcn(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_inline_value_0impjcn F;
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l4; /* h */
    gm_b16_8 gm_l5; /* s */
    gm_t6j6smj7qoctu gm_l7; /* r */
    gm_b16_8 gm_l8; /* s */
    GM_STACK_CHECK(&F, 929u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7mfvroqmll1f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srclt70oog8j9io, 117, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma9; default: goto ma11; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l2 = t15;
        {
            void * t17 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t18 = gm_l2;
            gm_b16_8 t19 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_tftb1odc739t5 t21 = gm_f_write_r83e4t6(t17, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srclt70oog8j9io, 119, 23);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t25 != ((uint8_t)2ULL)) goto mn24;
        gm_tt008mgut8glb t26 = GM_LD(gm_tt008mgut8glb, (((uint8_t *)a0) + 8));
        F.l3 = t26;
        {
            void * t28 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t30 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t31;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
            gm_tftb1odc739t5 t32 = gm_f_write_ctdpla1(t28, t31);
            gm_tftb1odc739t5 t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srclt70oog8j9io, 121, 13);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l3) + 16))[16] != 0)) goto ie35;
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_l4 = t36;
            {
                void * t38 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t39 = gm_l4;
                gm_b16_8 t40 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t41;
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                gm_tftb1odc739t5 t42 = gm_f_write_rbbkc88(t38, t41);
                gm_tftb1odc739t5 t43 = t42;
                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srclt70oog8j9io, 122, 32);
                gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            if (!(((((uint8_t *)&F.l3) + 40))[16] != 0)) goto ie45;
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_l5 = t46;
            {
                void * t48 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t49 = gm_l5;
                gm_b16_8 t50 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t51;
                GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t50);
                gm_tftb1odc739t5 t52 = gm_f_write_5jambfs(t48, t51);
                gm_tftb1odc739t5 t53 = t52;
                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srclt70oog8j9io, 123, 32);
                gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
            }
            goto ix45;
ie45:;
ix45:;
        }
        goto me7;
mn24:;
ma10:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t56 != ((uint8_t)1ULL)) goto mn55;
        gm_t03ccdabio1fj t57 = GM_LD(gm_t03ccdabio1fj, (((uint8_t *)a0) + 8));
        F.l6 = t57;
        {
            void * t59 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t61 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_tftb1odc739t5 t63 = gm_f_write_ivc38em(t59, t62);
            gm_tftb1odc739t5 t64 = t63;
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srclt70oog8j9io, 126, 13);
            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l6) + 16))[32] != 0)) goto ie66;
            gm_t6j6smj7qoctu t67 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&F.l6) + 16));
            gm_l7 = t67;
            {
                void * t69 = (void *)((uint8_t *)&F.l1);
                gm_t6j6smj7qoctu t70;
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t71);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 16));
                gm_b16_8 t73 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t73);
                gm_tftb1odc739t5 t74 = gm_f_write_gle9guv(t69, &t70);
                gm_tftb1odc739t5 t75 = t74;
                if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srclt70oog8j9io, 127, 35);
                gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
            }
            goto ix66;
ie66:;
ix66:;
        }
        {
            if (!(((((uint8_t *)&F.l6) + 56))[16] != 0)) goto ie77;
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 56));
            gm_l8 = t78;
            {
                void * t80 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t81 = gm_l8;
                gm_b16_8 t82 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t83;
                GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
                gm_tftb1odc739t5 t84 = gm_f_write_5jambfs(t80, t83);
                gm_tftb1odc739t5 t85 = t84;
                if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srclt70oog8j9io, 128, 32);
                gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
            }
            goto ix77;
ie77:;
ix77:;
        }
        goto me7;
mn55:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 118, 5);
me7:;
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s5m9qimbv7e8k));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t89 = gm_f_extend_0cs85gs(t87, t88);
        gm_tftb1odc739t5 t90 = t89;
        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srclt70oog8j9io, 131, 5);
        gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
    }
    {
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t93 = t92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        uint8_t t95 = 0;
        F.ret = t94;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_without_ej3mkbe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_join_without_ej3mkbe F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* to */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* l */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* l */
    GM_STACK_CHECK(&F, 499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            if (t5 > t7 || t7 > (uintptr_t)t3) gm_trap_slice(t5, t7, t3, gm_srclt70oog8j9io, 221, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t4 + t5 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t5));
            gm_l4 = t8;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l5;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 16));
                    gm_l6 = t20;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t23 = gm_l6;
                        gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
                        gm_tftb1odc739t5 t25 = t24;
                        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srclt70oog8j9io, 221, 31);
                        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 221, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t28 = gm_l5;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l5 = t31;
                }
            }
lb9:;
        }
    }
    {
        {
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t35 = gm_l2;
            uint64_t t36 = (uintptr_t)t35;
            if (t36 > (uintptr_t)t33 || (uintptr_t)t33 > (uintptr_t)t33) gm_trap_slice(t36, (uintptr_t)t33, t33, gm_srclt70oog8j9io, 222, 14);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t34 + t36 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)((uintptr_t)t33 - t36));
            gm_l7 = t37;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t39 = gm_l8;
                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t41 = (t39 < t40);
                if (!t41) goto lb38;
                {
                    uint64_t t44 = gm_l8;
                    uint64_t t45 = (uintptr_t)t44;
                    uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t45 * 16));
                    gm_l9 = t49;
                    {
                        void * t51 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t52 = gm_l9;
                        gm_tftb1odc739t5 t53 = gm_f_extend_0cs85gs(t51, t52);
                        gm_tftb1odc739t5 t54 = t53;
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srclt70oog8j9io, 222, 28);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                    }
                    goto me47;
mn48:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 222, 5);
me47:;
                }
lc38:;
                {
                    uint64_t t57 = gm_l8;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l8 = t60;
                }
            }
lb38:;
        }
    }
    {
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t62 = t61;
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        uint8_t t64 = 0;
        F.ret = t63;
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
__attribute__((aligned(4))) void gm_f_key_of_99iu4dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_key_of_99iu4dv F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* eq */
    gm_b16_8 gm_l3; /* k */
    uint64_t gm_l4; /* dot */
    GM_STACK_CHECK_SHALLOW(&F, 714u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sh0j2ggs1vvoa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t6 = gm_f_find_nab29co(t4, t5);
        gm_tf5srh4mp4b3i t7 = t6;
        uint64_t t8;
        if (((((uint8_t *)&t7))[8] != 0)) {
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_th2lqbo77pvsc t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l2 = t8;
    }
    {
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t13 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t15 = gm_l2;
        uint64_t t16 = (uintptr_t)t15;
        if (t14 > t16 || t16 > (uintptr_t)t12) gm_trap_slice(t14, t16, t12, gm_srclt70oog8j9io, 62, 26);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + t14 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16 - t14));
        gm_b16_8 t18 = gm_f_trim_ascii_c9j36d1(t17);
        gm_l3 = t18;
    }
    {
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t21 = (t20 >= ((uint64_t)2ULL));
        if (!t21) goto ie19;
        uint8_t t22 = 0;
        uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srclt70oog8j9io, 63, 24);
        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t23 * 1));
        uint8_t t26 = ((t25 == ((uint8_t)34ULL)));
        if (!t26) {
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srclt70oog8j9io, 63, 39);
            uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t27 * 1));
            uint8_t t30 = ((t29 == ((uint8_t)39ULL)));
            t22 = t30;
        } else t22 = 1;
        if (!t22) goto ie19;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r32; bool o33 = __builtin_sub_overflow(t31, ((uint64_t)1ULL), &r32);
        if (o33) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 63, 59);
        uint64_t t34 = r32;
        uint64_t t35 = (uintptr_t)t34;
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srclt70oog8j9io, 63, 57);
        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t35 * 1));
        uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srclt70oog8j9io, 63, 73);
        uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t38 * 1));
        uint8_t t41 = ((t37 == t40));
        if (!t41) goto ie19;
        {
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t44 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t45 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 63, 92);
            uint64_t t49 = r47;
            uint64_t t50 = (uintptr_t)t49;
            if (t45 > t50 || t50 > (uintptr_t)t43) gm_trap_slice(t45, t50, t43, gm_srclt70oog8j9io, 63, 87);
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t44 + t45 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50 - t45));
            gm_th2lqbo77pvsc t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[16] = 1;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t56 = (t55 > ((uint64_t)0ULL));
        if (!t56) goto ie54;
        uint64_t t57 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_srclt70oog8j9io, 65, 22);
        uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t57 * 1));
        uint8_t t60 = (!(t59 == ((uint8_t)34ULL)));
        if (!t60) goto ie54;
        uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t61 >= (uintptr_t)(t62)) gm_trap_index(t61, t62, gm_srclt70oog8j9io, 65, 42);
        uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t61 * 1));
        uint8_t t64 = (!(t63 == ((uint8_t)39ULL)));
        if (!t64) goto ie54;
        {
            gm_b16_8 t67 = gm_l3;
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
            gm_tf5srh4mp4b3i t69 = gm_f_find_nab29co(t67, t68);
            gm_tf5srh4mp4b3i t70 = t69;
            if (!((((uint8_t *)&t70))[8] != 0)) goto ie66;
            uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)&t70));
            gm_l4 = t71;
            {
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t74 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t75 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t76 = gm_l4;
                uint64_t t77 = (uintptr_t)t76;
                if (t75 > t77 || t77 > (uintptr_t)t73) gm_trap_slice(t75, t77, t73, gm_srclt70oog8j9io, 66, 61);
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t74 + t75 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(t77 - t75));
                gm_b16_8 t79 = gm_f_trim_ascii_c9j36d1(t78);
                gm_th2lqbo77pvsc t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t80), t79);
                ((uint8_t *)&t80)[16] = 1;
                uint8_t t81 = 0;
                F.ret = t80;
                goto gm_fret;
            }
            goto ix66;
ie66:;
ix66:;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        gm_b16_8 t82 = gm_l3;
        gm_th2lqbo77pvsc t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t83), t82);
        ((uint8_t *)&t83)[16] = 1;
        uint8_t t84 = 0;
        F.ret = t83;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_newlines_183t7r2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_normalize_newlines_183t7r2 F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 275u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_skmt2kt2p7v0u));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
        gm_tf5srh4mp4b3i t5 = gm_f_find_nab29co(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        uint8_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 16, 8);
me8:;
        if (!t7) goto ie2;
        {
            gm_b16_8 t12 = gm_l0;
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
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_skmt2kt2p7v0u));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(2));
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t17;
        gm_f_replace_alloc_t7hndlq((void *)&t17, (void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srclt70oog8j9io, 17, 12);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        uint8_t t22 = 0;
        F.ret = t21;
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
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_remove_dependency_4ju7b0h F;
    gm_b16_8 gm_l0; /* original */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* text */
    gm_b16_8 gm_l4; /* missing */
    gm_t2lqu8jsj511d gm_l5; /* r */
    uint8_t gm_l7; /* found */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* n */
    gm_b16_8 gm_l10; /* k */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* j */
    gm_b16_8 gm_l14; /* wanted */
    gm_t2lqu8jsj511d gm_l15; /* r */
    uint64_t gm_l16; /* first */
    GM_STACK_CHECK(&F, 1840u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_normalize_newlines_183t7r2((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        gm_b16_8 t4 = gm_l2;
        gm_f_split_lines_jecth9m((void *)&F.l3, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_tnpifv20irnph t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        gm_tabb710d9mq2e t7;
        gm_f_format_rkrg0f2((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srclt70oog8j9io, 183, 16);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l4 = t9;
    }
    {
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(12));
        gm_tc70a9dbbmgkg t15;
        gm_f_table_range_09bngqh((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tc70a9dbbmgkg t16 = t15;
        if (!((((uint8_t *)&t16))[16] != 0)) goto ie10;
        gm_t2lqu8jsj511d t17 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t16));
        gm_l5 = t17;
        {
            gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l7 = ((uint8_t)0);
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t20 = gm_l8;
                uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t26 = gm_l8;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t28 = (t26 > t27);
                    if (!t28) goto ie25;
                    uint64_t t29 = gm_l8;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto ie25;
                    uint64_t t32 = gm_l8;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srclt70oog8j9io, 190, 71);
                    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t33 * 16));
                    uint8_t t36 = gm_f_is_blank_or_comment_lubpopi(t35);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) goto ie25;
                    {
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t40 = t39;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        uint64_t t42 = gm_l8;
                        uint64_t t43 = gm_f_entry_lines_3jhl7a4(t41, t42);
                        gm_l9 = t43;
                    }
                    {
                        uint64_t t44 = gm_l8;
                        uint64_t t45 = (uintptr_t)t44;
                        uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srclt70oog8j9io, 192, 29);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t45 * 16));
                        gm_th2lqbo77pvsc t48;
                        gm_f_key_of_99iu4dv((void *)&t48, (void *)NULL, cx, NULL, t47);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                        gm_th2lqbo77pvsc t49 = t48;
                        gm_b16_8 t50;
                        if (((((uint8_t *)&t49))[16] != 0)) {
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 0));
                            t50 = t51;
                        } else {
                            gm_b16_8 t52;
                            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(0));
                            t50 = t52;
                        }
                        gm_l10 = t50;
                    }
                    {
                        gm_b16_8 t54 = gm_l10;
                        gm_b16_8 t55 = gm_l1;
                        gm_b16_8 t56 = t54;
                        gm_b16_8 t57 = t55;
                        uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                        if (!t58) goto ie53;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        {
                            for (;;) {
                                uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                                if (o64) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 196, 44);
                                uint64_t t65 = r63;
                                uint8_t t66 = (t61 > t65);
                                if (!t66) goto lb60;
                                uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                uint64_t r68; bool o69 = __builtin_sub_overflow(t67, ((uint64_t)1ULL), &r68);
                                if (o69) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 196, 92);
                                uint64_t t70 = r68;
                                uint64_t t71 = (uintptr_t)t70;
                                uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srclt70oog8j9io, 196, 81);
                                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t71 * 16));
                                uint8_t t74 = gm_f_is_blank_or_comment_lubpopi(t73);
                                if (!t74) goto lb60;
                                {
                                    {
                                        void * t78 = (void *)((uint8_t *)&F.l6);
                                        gm_th2lqbo77pvsc t79;
                                        gm_f_pop_vr08sii((void *)&t79, t78);
                                    }
                                }
lc60:;
                            }
lb60:;
                        }
                        {
                            uint64_t t80 = gm_l8;
                            uint64_t t81 = gm_l9;
                            uint64_t r82; bool o83 = __builtin_add_overflow(t80, t81, &r82);
                            if (o83) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 197, 21);
                            uint64_t t84 = r82;
                            gm_l8 = t84;
                        }
                        {
                            goto lc19;
                        }
                        goto ix53;
ie53:;
ix53:;
                    }
                    {
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t86 = gm_l9;
                            gm_l11 = t86;
                        }
                        {
                            for (;;) {
                                uint64_t t88 = gm_l12;
                                uint64_t t89 = gm_l11;
                                uint8_t t90 = (t88 < t89);
                                if (!t90) goto lb87;
                                {
                                    uint64_t t93 = gm_l12;
                                    uint64_t t94 = t93;
                                    uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&t94));
                                    gm_l13 = t97;
                                    {
                                        void * t99 = (void *)((uint8_t *)&F.l6);
                                        uint64_t t100 = gm_l8;
                                        uint64_t t101 = gm_l13;
                                        uint64_t r102; bool o103 = __builtin_add_overflow(t100, t101, &r102);
                                        if (o103) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 200, 55);
                                        uint64_t t104 = r102;
                                        uint64_t t105 = (uintptr_t)t104;
                                        uint64_t t106 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                        if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srclt70oog8j9io, 200, 43);
                                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t105 * 16));
                                        gm_tftb1odc739t5 t108 = gm_f_push_3ef746b(t99, t107);
                                        gm_tftb1odc739t5 t109 = t108;
                                        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srclt70oog8j9io, 200, 33);
                                        gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                                    }
                                    goto me95;
mn96:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 200, 17);
me95:;
                                }
lc87:;
                                {
                                    uint64_t t112 = gm_l12;
                                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                                    uint64_t t115 = r113;
                                    gm_l12 = t115;
                                }
                            }
lb87:;
                        }
                    }
                    {
                        uint64_t t116 = gm_l8;
                        uint64_t t117 = gm_l9;
                        uint64_t r118; bool o119 = __builtin_add_overflow(t116, t117, &r118);
                        if (o119) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 201, 17);
                        uint64_t t120 = r118;
                        gm_l8 = t120;
                    }
                    {
                        goto lc19;
                    }
                    goto ix25;
ie25:;
ix25:;
                }
                {
                    void * t121 = (void *)((uint8_t *)&F.l6);
                    uint64_t t122 = gm_l8;
                    uint64_t t123 = (uintptr_t)t122;
                    uint64_t t124 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t123 >= (uintptr_t)(t124)) gm_trap_index(t123, t124, gm_srclt70oog8j9io, 204, 23);
                    gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t123 * 16));
                    gm_tftb1odc739t5 t126 = gm_f_push_3ef746b(t121, t125);
                    gm_tftb1odc739t5 t127 = t126;
                    if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srclt70oog8j9io, 204, 13);
                    gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
                }
                {
                    uint64_t t129 = gm_l8;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    if (o131) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 205, 13);
                    uint64_t t132 = r130;
                    gm_l8 = t132;
                }
lc19:;
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
        }
        {
            uint8_t t134 = gm_l7;
            if (!t134) goto ie133;
            {
                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t137 = t136;
                gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                gm_b16_8 t139 = gm_f_join_without_ej3mkbe((void *)NULL, cx, NULL, t138, ((uint64_t)0ULL), ((uint64_t)0ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3ujhu6qph7de t140;
                GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                ((uint8_t *)&t140)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t140 + 8), t139);
                gm_t3ujhu6qph7de t141 = t140;
                uint8_t t142 = ((((uint8_t *)&t141))[0] != 0);
                F.ret = t140;
                goto gm_fret;
            }
            goto ix133;
ie133:;
ix133:;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_b16_8 t143 = gm_l1;
        gm_tnpifv20irnph t144;
        GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t143);
        gm_tabb710d9mq2e t145;
        gm_f_format_7plp021((void *)&t145, (void *)NULL, cx, NULL, t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t146 = t145;
        if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_srclt70oog8j9io, 210, 15);
        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
        gm_l14 = t147;
    }
    {
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t150 = t149;
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        gm_b16_8 t152 = gm_l14;
        gm_b16_8 t153 = t152;
        gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
        gm_tc70a9dbbmgkg t155;
        gm_f_table_range_09bngqh((void *)&t155, (void *)NULL, cx, NULL, t151, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tc70a9dbbmgkg t156 = t155;
        if (!((((uint8_t *)&t156))[16] != 0)) goto ie148;
        gm_t2lqu8jsj511d t157 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t156));
        gm_l15 = t157;
        {
            uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
            gm_l16 = t159;
        }
        {
            for (;;) {
                uint64_t t161 = gm_l16;
                uint8_t t162 = (t161 > ((uint64_t)0ULL));
                if (!t162) goto lb160;
                uint64_t t163 = gm_l16;
                uint64_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint64_t)1ULL), &r164);
                if (o165) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 213, 61);
                uint64_t t166 = r164;
                uint64_t t167 = (uintptr_t)t166;
                uint64_t t168 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                if (t167 >= (uintptr_t)(t168)) gm_trap_index(t167, t168, gm_srclt70oog8j9io, 213, 49);
                gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t167 * 16));
                uint8_t t170 = gm_f_is_blank_or_comment_lubpopi(t169);
                if (!t170) goto lb160;
                {
                    uint64_t t173 = gm_l16;
                    uint64_t r174; bool o175 = __builtin_sub_overflow(t173, ((uint64_t)1ULL), &r174);
                    if (o175) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 213, 75);
                    uint64_t t176 = r174;
                    gm_l16 = t176;
                }
lc160:;
            }
lb160:;
        }
        {
            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t178 = t177;
            gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
            uint64_t t180 = gm_l16;
            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
            gm_b16_8 t182 = gm_f_join_without_ej3mkbe((void *)NULL, cx, NULL, t179, t180, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3ujhu6qph7de t183;
            GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
            ((uint8_t *)&t183)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t183 + 8), t182);
            gm_t3ujhu6qph7de t184 = t183;
            uint8_t t185 = ((((uint8_t *)&t184))[0] != 0);
            F.ret = t183;
            goto gm_fret;
        }
        goto ix148;
ie148:;
ix148:;
    }
    {
        gm_b16_8 t186 = gm_l4;
        gm_b16_8 t187 = t186;
        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
        gm_t3ujhu6qph7de t189;
        GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
        ((uint8_t *)&t189)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t189 + 8), t188);
        gm_t3ujhu6qph7de t190 = t189;
        uint8_t t191 = ((((uint8_t *)&t190))[0] != 0);
        F.ret = t189;
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
inline __attribute__((aligned(4))) void gm_f_split_lines_jecth9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_lines_jecth9m F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 359u);
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
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 1));
                    gm_tf5srh4mp4b3i t15;
                    GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
                    gm_tf5srh4mp4b3i t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l5 = t19;
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t16) + 8));
                    gm_l6 = t20;
                    {
                        uint8_t t23 = gm_l6;
                        uint8_t t24 = ((t23 == ((uint8_t)10ULL)));
                        if (!t24) goto ie22;
                        {
                            void * t26 = (void *)((uint8_t *)&F.l1);
                            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t29 = gm_l2;
                            uint64_t t30 = (uintptr_t)t29;
                            uint64_t t31 = gm_l5;
                            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                            if (o33) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 26, 36);
                            uint64_t t34 = r32;
                            uint64_t t35 = (uintptr_t)t34;
                            if (t30 > t35 || t35 > (uintptr_t)t27) gm_trap_slice(t30, t35, t27, gm_srclt70oog8j9io, 26, 24);
                            gm_b16_8 t36;
                            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t28 + t30 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t30));
                            gm_tftb1odc739t5 t37 = gm_f_push_3ef746b(t26, t36);
                            gm_tftb1odc739t5 t38 = t37;
                            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srclt70oog8j9io, 26, 13);
                            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        }
                        {
                            uint64_t t40 = gm_l5;
                            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 27, 21);
                            uint64_t t43 = r41;
                            gm_l2 = t43;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 24, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t45 = gm_l4;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l4 = t48;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t50 = gm_l2;
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t52 = (t50 < t51);
        if (!t52) goto ie49;
        {
            void * t54 = (void *)((uint8_t *)&F.l1);
            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t56 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t57 = gm_l2;
            uint64_t t58 = (uintptr_t)t57;
            if (t58 > (uintptr_t)t55 || (uintptr_t)t55 > (uintptr_t)t55) gm_trap_slice(t58, (uintptr_t)t55, t55, gm_srclt70oog8j9io, 30, 38);
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t56 + t58 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)((uintptr_t)t55 - t58));
            gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t54, t59);
            gm_tftb1odc739t5 t61 = t60;
            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srclt70oog8j9io, 30, 27);
            gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
        }
        goto ix49;
ie49:;
ix49:;
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
__attribute__((aligned(4))) void gm_f_table_range_09bngqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_table_range_09bngqh F;
    gm_b16_8 gm_l0; /* lines */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* l */
    gm_b16_8 gm_l6; /* h */
    uint64_t gm_l7; /* end */
    GM_STACK_CHECK_SHALLOW(&F, 591u);
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
                    gm_tf1bjg81u0glv t10;
                    uint64_t t11 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t10 + 8, t15);
                    gm_tf1bjg81u0glv t16 = t10;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l4 = t19;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
                    gm_l5 = t20;
                    {
                        gm_b16_8 t23 = gm_l5;
                        gm_th2lqbo77pvsc t24;
                        gm_f_header_of_2u6atoc((void *)&t24, (void *)NULL, cx, NULL, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_th2lqbo77pvsc t25 = t24;
                        if (!((((uint8_t *)&t25))[16] != 0)) goto ie22;
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                        gm_l6 = t26;
                        {
                            gm_b16_8 t29 = gm_l6;
                            gm_b16_8 t30 = gm_l1;
                            gm_b16_8 t31 = t29;
                            gm_b16_8 t32 = t30;
                            uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
                            if (!t33) goto ie28;
                            {
                                uint64_t t35 = gm_l4;
                                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                                if (o37) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 106, 24);
                                uint64_t t38 = r36;
                                gm_l7 = t38;
                            }
                            {
                                for (;;) {
                                    uint64_t t40 = gm_l7;
                                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                    uint8_t t42 = (t40 < t41);
                                    if (!t42) goto lb39;
                                    uint64_t t43 = gm_l7;
                                    uint64_t t44 = (uintptr_t)t43;
                                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                    if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srclt70oog8j9io, 107, 57);
                                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t44 * 16));
                                    uint8_t t47 = gm_f_is_header_uakq83c((void *)NULL, cx, NULL, t46);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                    uint8_t t48 = (uint8_t)!t47;
                                    if (!t48) goto lb39;
                                    {
                                        uint64_t t51 = gm_l7;
                                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                                        if (o53) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 107, 71);
                                        uint64_t t54 = r52;
                                        gm_l7 = t54;
                                    }
lc39:;
                                }
lb39:;
                            }
                            {
                                uint64_t t55 = gm_l4;
                                uint64_t t56 = gm_l7;
                                gm_t2lqu8jsj511d t57;
                                GM_ST(uint64_t, ((uint8_t *)&t57 + 0), t55);
                                GM_ST(uint64_t, ((uint8_t *)&t57 + 8), t56);
                                gm_tc70a9dbbmgkg t58;
                                GM_ST(uint8_t, (uint8_t *)&t58 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t58 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t58 + 20, 0);
                                GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t58), t57);
                                ((uint8_t *)&t58)[16] = 1;
                                uint8_t t59 = 0;
                                F.ret = t58;
                                goto gm_fret;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 103, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t61 = gm_l3;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l3 = t64;
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
        gm_tc70a9dbbmgkg t65;
        memset(&t65, 0, sizeof t65);
        uint8_t t66 = 0;
        F.ret = t65;
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
