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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; } gm_t2abnvcnt53ot;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t f9; uint8_t f10; uint8_t f11; uint8_t f12; uint8_t f13; uint8_t f14; uint8_t f15; uint8_t f16; uint8_t f17; } gm_ta72bg8o6meed;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t2abnvcnt53ot) == 4 && _Alignof(gm_t2abnvcnt53ot) == 1 && offsetof(gm_t2abnvcnt53ot, f0) == 0 && offsetof(gm_t2abnvcnt53ot, f1) == 1 && offsetof(gm_t2abnvcnt53ot, f2) == 2 && offsetof(gm_t2abnvcnt53ot, f3) == 3, "layout of `(u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t pe[3]; } gm_t88jm1dgsmkjs;
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta72bg8o6meed) == 18 && _Alignof(gm_ta72bg8o6meed) == 1 && offsetof(gm_ta72bg8o6meed, f0) == 0 && offsetof(gm_ta72bg8o6meed, f1) == 1 && offsetof(gm_ta72bg8o6meed, f2) == 2 && offsetof(gm_ta72bg8o6meed, f3) == 3 && offsetof(gm_ta72bg8o6meed, f4) == 4 && offsetof(gm_ta72bg8o6meed, f5) == 5 && offsetof(gm_ta72bg8o6meed, f6) == 6 && offsetof(gm_ta72bg8o6meed, f7) == 7 && offsetof(gm_ta72bg8o6meed, f8) == 8 && offsetof(gm_ta72bg8o6meed, f9) == 9 && offsetof(gm_ta72bg8o6meed, f10) == 10 && offsetof(gm_ta72bg8o6meed, f11) == 11 && offsetof(gm_ta72bg8o6meed, f12) == 12 && offsetof(gm_ta72bg8o6meed, f13) == 13 && offsetof(gm_ta72bg8o6meed, f14) == 14 && offsetof(gm_ta72bg8o6meed, f15) == 15 && offsetof(gm_ta72bg8o6meed, f16) == 16 && offsetof(gm_ta72bg8o6meed, f17) == 17, "layout of `(u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t88jm1dgsmkjs) == 40 && _Alignof(gm_t88jm1dgsmkjs) == 8 && offsetof(gm_t88jm1dgsmkjs, f0) == 0 && offsetof(gm_t88jm1dgsmkjs, f1) == 8 && offsetof(gm_t88jm1dgsmkjs, f2) == 16 && offsetof(gm_t88jm1dgsmkjs, f3) == 32 && offsetof(gm_t88jm1dgsmkjs, f4) == 33 && offsetof(gm_t88jm1dgsmkjs, f5) == 34 && offsetof(gm_t88jm1dgsmkjs, f6) == 35 && offsetof(gm_t88jm1dgsmkjs, f7) == 36, "layout of `Info`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t6j6smj7qoctu v1; gm_b16_8 v2; } f1; } gm_tj63kbqqj2092;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; gm_tc70a9dbbmgkg f3; uint8_t f4; uint8_t pe[7]; } gm_tsnno3vacsa4v;
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_tj63kbqqj2092) == 40 && _Alignof(gm_tj63kbqqj2092) == 8 && offsetof(gm_tj63kbqqj2092, f0) == 0 && offsetof(gm_tj63kbqqj2092, f1) == 8, "layout of `Item`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tsnno3vacsa4v) == 48 && _Alignof(gm_tsnno3vacsa4v) == 8 && offsetof(gm_tsnno3vacsa4v, f0) == 0 && offsetof(gm_tsnno3vacsa4v, f1) == 8 && offsetof(gm_tsnno3vacsa4v, f2) == 9 && offsetof(gm_tsnno3vacsa4v, f3) == 16 && offsetof(gm_tsnno3vacsa4v, f4) == 40, "layout of `Frame`");
typedef struct { gm_tsnno3vacsa4v f0; uint8_t f1; uint8_t pe[7]; } gm_ttlblounidicj;
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_ttlblounidicj) == 56 && _Alignof(gm_ttlblounidicj) == 8 && offsetof(gm_ttlblounidicj, f0) == 0 && offsetof(gm_ttlblounidicj, f1) == 48, "layout of `?Frame`");
struct gm_fr_aligned_with_s024agj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_column_r91o38o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_continues_line_2cliit4 {
    gm_frame h;
    gm_t88jm1dgsmkjs l0; /* info */
    uint8_t ret;
};
struct gm_fr_continues_with_name_ejtvq2k {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* info */
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ojmeji2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_get_t1u93iu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_ss6fp5u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_punct_char_3bc3hom {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_word_kb7cdmu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keeps_padding_m0v49h1 {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* prev */
    gm_t88jm1dgsmkjs l2; /* cur */
    uint8_t ret;
};
struct gm_fr_layout_ufvu3cq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* infos */
    gm_trqfvmjtt4sij l5; /* items */
    gm_t88jm1dgsmkjs l10; /* info */
    gm_t4c7ipjj9s3fp l11; /* merges */
    gm_trqfvmjtt4sij l12; /* out */
    gm_trqfvmjtt4sij l16; /* stack */
    gm_tj63kbqqj2092 l26; /* item */
    gm_ttlblounidicj l34; /* f */
    gm_tsnno3vacsa4v l36; /* fr */
    gm_tsnno3vacsa4v l44; /* f */
    gm_tsnno3vacsa4v l49; /* f */
    gm_t88jm1dgsmkjs l56; /* info */
    gm_t88jm1dgsmkjs l60; /* pinfo */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lexes_apart_9scqa7n {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* scratch */
    gm_trqfvmjtt4sij l3; /* joined */
    gm_t2792tsg5eg72 l4; /* diags */
    gm_tr8dvlu9ik4gj l5; /* lexed */
    gm_tm4pc0emhrv1g l11; /* t */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_list_f0gjjbh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g3vmkan_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_merges_with_tail_ijmh3nm {
    gm_frame h;
    gm_tjt4ka19str13 l4; /* scratch */
    gm_trqfvmjtt4sij l5; /* joined */
    gm_t2792tsg5eg72 l6; /* diags */
    gm_tr8dvlu9ik4gj l7; /* lexed */
    gm_tm4pc0emhrv1g l10; /* t */
    uint8_t ret;
    gm_b0_1 w1;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_next_tok_qap88op {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_out_column_hr4hgng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_f3uajlg_3 {
    gm_frame h;
    gm_tsnno3vacsa4v l2; /* x */
    gm_ttlblounidicj ret;
};
struct gm_fr_push_1e500nc_4 {
    gm_frame h;
    gm_tj63kbqqj2092 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8h1r7s6_3 {
    gm_frame h;
    gm_tsnno3vacsa4v l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r51vrc8_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_relative_column_ctuu0pd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_scan_gap_tkq0f9l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_non_white_o0cd8c4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_would_merge_44g1m96 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_aligned_with_s024agj(gm_b16_8 a0, uint64_t a1, uint8_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_all_white_1b9moim(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_char_offset_kr8000h(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint64_t gm_f_column_r91o38o(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4_c(gm_t88jm1dgsmkjs a0);
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k(gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_deinit_26uqnl1(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_aii0n0l(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_ectdopm_2(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_p1kmdo2(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojmeji2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_h7591bf(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_t1u93iu(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_ss6fp5u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint64_t gm_f_indent_of_i911gpd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_char_3bc3hom(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_white_3njipsh(uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_word_kb7cdmu(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1(gm_b16_8 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1, gm_t88jm1dgsmkjs a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_last_line_hk6q5l4(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_layout_ufvu3cq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_lexes_apart_9scqa7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_line_end_jm825k5(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_line_start_pl10jt5(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_merges_with_tail_ijmh3nm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_tok_qap88op(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_only_spaces_bvta5sq(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_out_column_hr4hgng(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_3(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_spaces_732bua4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_fmu1emq(void * a0, const void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_replace_lf6hcmn(void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_gap_tkq0f9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_d3d35bq(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_would_merge_44g1m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
extern uint8_t gm_c3dq62rrli9[16] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_sccjki3qgi8m7[15];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srceoep1gquqdsq[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrchqfd7obmqe[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_column_r91o38o(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_column_r91o38o F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* pos */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    void * t2 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t4 = gm_l1;
    uint64_t t5 = (uintptr_t)t4;
    if (t3 > t5 || t5 > (uintptr_t)t1) gm_trap_slice(t3, t5, t1, gm_srcrchqfd7obmqe, 71, 80);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t2 + t3 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(t5 - t3));
    gm_b16_8 t7 = gm_f_last_line_hk6q5l4(t6);
    uint64_t t8 = gm_f_char_count_76unqru(t7);
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojmeji2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ojmeji2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_3 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_t1u93iu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_t1u93iu F;
    void * gm_l0; /* m */
    gm_t6j6smj7qoctu gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_h7591bf(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 48) + 40));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_ss6fp5u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ss6fp5u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_ojmeji2();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4_4();
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
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p5lo8ed_3();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) uint64_t gm_f_out_column_hr4hgng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_out_column_hr4hgng F;
    void * gm_l0; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_b16_8 t4 = gm_f_last_line_hk6q5l4(t3);
    uint64_t t5 = gm_f_char_count_76unqru(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
            gm_ttlblounidicj t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_tsnno3vacsa4v t17 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
        F.l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttlblounidicj *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttlblounidicj *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttlblounidicj *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_ttlblounidicj *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_ttlblounidicj *)gm_out) + 0, t29, 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_4 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_lpi80k4_4();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_o86m6d7_4(t10, ((uint64_t)1ULL), t12);
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
        gm_tj63kbqqj2092 t21 = F.l1;
        GM_ST(gm_tj63kbqqj2092, ((uint8_t *)t20 + t18 * 40), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8h1r7s6_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
            gm_b16_8 t11 = gm_f_empty_slice_p5lo8ed_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_r51vrc8_3(t10, ((uint64_t)1ULL), t12);
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
        gm_tsnno3vacsa4v t21 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)a1));
        GM_ST(gm_tsnno3vacsa4v, ((uint8_t *)t20 + t18 * 48), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_4 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r51vrc8_3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_relative_column_ctuu0pd F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_f_char_count_76unqru(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l0;
        uint64_t t5 = gm_f_indent_of_i911gpd(t4);
        gm_l2 = t5;
    }
    {
        uint64_t t6;
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l2;
        uint8_t t10 = (t8 > t9);
        if (!t10) goto ie7;
        uint64_t t12 = gm_l1;
        uint64_t t13 = gm_l2;
        uint64_t r14; bool o15 = __builtin_sub_overflow(t12, t13, &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 450, 23);
        uint64_t t16 = r14;
        uint64_t t17 = t16;
        t6 = t17;
        goto ix7;
ie7:;
        uint64_t t19 = ((uint64_t)0ULL);
        t6 = t19;
ix7:;
        uint8_t t20 = 0;
        F.ret = t6;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_non_white_o0cd8c4 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_tb31k5l794t7p t8 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t7, ((uint64_t)0ULL)));
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t8) + 0));
        uint8_t t10 = gm_f_is_white_3njipsh(t9);
        uint8_t t11 = (uint8_t)!t10;
        uint8_t t12 = 0;
        F.ret = t11;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_aligned_with_s024agj(gm_b16_8 a0, uint64_t a1, uint8_t a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_with_s024agj F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* col */
    uint8_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* text */
    uint64_t gm_l4; /* cut */
    gm_b16_8 gm_l5; /* rest */
    gm_b16_8 gm_l6; /* before */
    uint8_t gm_l7; /* spaced */
    GM_STACK_CHECK_SHALLOW(&F, 361u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_b16_8 t4 = gm_l0;
        uint64_t t5 = gm_f_indent_of_i911gpd(t4);
        uint64_t r6; bool o7 = __builtin_add_overflow(t3, t5, &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 198, 37);
        uint64_t t8 = r6;
        uint64_t t9 = gm_f_char_offset_kr8000h(t2, t8);
        gm_l4 = t9;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t12 = gm_l4;
        uint64_t t13 = (uintptr_t)t12;
        if (t13 > (uintptr_t)t10 || (uintptr_t)t10 > (uintptr_t)t10) gm_trap_slice(t13, (uintptr_t)t10, t10, gm_srceoep1gquqdsq, 199, 13);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t11 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)t10 - t13));
        gm_l5 = t14;
    }
    {
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t16 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = gm_l4;
        uint64_t t19 = (uintptr_t)t18;
        if (t17 > t19 || t19 > (uintptr_t)t15) gm_trap_slice(t17, t19, t15, gm_srceoep1gquqdsq, 200, 15);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t16 + t17 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19 - t17));
        gm_l6 = t20;
    }
    {
        uint8_t t21 = 0;
        gm_b16_8 t22 = gm_l6;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        uint8_t t24 = gm_f_ends_with_biriu5l(t22, t23);
        if (t24) {
            gm_b16_8 t25 = gm_l6;
            uint8_t t26 = gm_f_all_white_1b9moim(t25);
            uint8_t t27 = (uint8_t)!t26;
            t21 = t27;
        } else t21 = 0;
        gm_l7 = t21;
    }
    {
        uint8_t t29 = gm_l2;
        if (!t29) goto ie28;
        {
            uint8_t t31 = 0;
            uint8_t t32 = gm_l7;
            if (t32) {
                gm_b16_8 t33 = gm_l5;
                gm_b16_8 t34 = gm_l3;
                uint8_t t35 = gm_f_starts_with_6vvt01e(t33, t34);
                t31 = t35;
            } else t31 = 0;
            uint8_t t36 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint8_t t37 = 0;
        uint8_t t38 = gm_l7;
        if (t38) {
            gm_b16_8 t39 = gm_l5;
            uint8_t t40 = gm_f_starts_with_non_white_o0cd8c4(t39);
            t37 = t40;
        } else t37 = 0;
        uint8_t t41 = 0;
        F.ret = t37;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_continues_line_2cliit4 F;
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = 0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 34));
        if (!t4) {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 35));
            t3 = t5;
        } else t3 = 1;
        if (!t3) goto ie2;
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 16));
        gm_l1 = t8;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sgpdmvc9s0d2p, 2) == 0)) goto pa12;
        goto po11;
pa12:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_svbhcvlahd4n2, 2) == 0)) goto pa13;
        goto po11;
pa13:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s920dpvvv6cej, 3) == 0)) goto pa14;
        goto po11;
pa14:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_spi47lg4tr80j, 2) == 0)) goto pa15;
        goto po11;
pa15:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s9es2tos1bhfp, 4) == 0)) goto pa16;
        goto po11;
pa16:;
        goto mn10;
po11:;
        uint8_t t17 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me9;
mn10:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sc8o4oaph9gbr, 1) == 0)) goto pa20;
        goto po19;
pa20:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_spod3tmkh25up, 1) == 0)) goto pa21;
        goto po19;
pa21:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sfqk3rro4g7t0, 1) == 0)) goto pa22;
        goto po19;
pa22:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sf9a2kg3eo1gs, 1) == 0)) goto pa23;
        goto po19;
pa23:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_srs1vrn2uaams, 1) == 0)) goto pa24;
        goto po19;
pa24:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sfi2ha42r1nr2, 1) == 0)) goto pa25;
        goto po19;
pa25:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_seccpktgmrevt, 1) == 0)) goto pa26;
        goto po19;
pa26:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_soqt3jnmovq5l, 3) == 0)) goto pa27;
        goto po19;
pa27:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sjfg2291kjugh, 3) == 0)) goto pa28;
        goto po19;
pa28:;
        goto mn18;
po19:;
        uint8_t t29 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me9;
mn18:;
        goto me9;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 156, 5);
me9:;
    }
    {
        uint8_t t32 = 0;
        uint8_t t33 = 0;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sccjki3qgi8m7));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(14));
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srceoep1gquqdsq, 161, 39);
        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t35 * 1));
        gm_tf5srh4mp4b3i t38 = gm_f_index_of_hdeg351(t34, t37);
        uint8_t t39;
        if (((((uint8_t *)&t38))[8] != 0)) goto mn41;
        t39 = ((uint8_t)0);
        goto me40;
mn41:;
        t39 = ((uint8_t)1);
        goto me40;
mn42:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 161, 12);
me40:;
        if (!t39) {
            uint8_t t43 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s1c4sfff8ac39, 6) == 0));
            t33 = t43;
        } else t33 = 1;
        if (!t33) {
            uint8_t t44 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sidtn67sdk4e0, 6) == 0));
            t32 = t44;
        } else t32 = 1;
        uint8_t t45 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4_c(gm_t88jm1dgsmkjs a0) { return gm_f_continues_line_2cliit4(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k(gm_b16_8 a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_continues_with_name_ejtvq2k F;
    gm_b16_8 gm_l0; /* src */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = (!((GM_LD(uintptr_t, (((uint8_t *)a1) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)a1) + 16)), gm_sl3q52ep6aa53, 1) == 0));
        if (!t3) goto ie2;
        {
            uint8_t t5 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t6 = gm_l0;
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t6)));
        uint64_t t8 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t10 = (uintptr_t)t9;
        gm_tqs5p4n2tq19v t11;
        if (t10 < t8) {
        uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)t7 + t10 * 1));
        gm_tqs5p4n2tq19v t13;
        GM_ST(uint8_t, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[1] = 1;
            t11 = t13;
        } else {
        gm_tqs5p4n2tq19v t14;
        memset(&t14, 0, sizeof t14);
            t11 = t14;
        }
        uint8_t t15;
        if (((((uint8_t *)&t11))[1] != 0)) {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
            t15 = t16;
        } else {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t15;
    }
    {
        uint8_t t18 = 0;
        uint8_t t19 = 0;
        uint8_t t20 = 0;
        {
            uint8_t t22 = gm_l2;
            gm_l3 = t22;
        }
        uint8_t t23 = 0;
        uint8_t t24 = gm_l3;
        uint8_t t25 = (((uint8_t)97ULL) <= t24);
        if (t25) {
            uint8_t t26 = gm_l3;
            uint8_t t27 = (t26 <= ((uint8_t)122ULL));
            t23 = t27;
        } else t23 = 0;
        uint8_t t28 = t23;
        if (!t28) {
            {
                uint8_t t30 = gm_l2;
                gm_l4 = t30;
            }
            uint8_t t31 = 0;
            uint8_t t32 = gm_l4;
            uint8_t t33 = (((uint8_t)65ULL) <= t32);
            if (t33) {
                uint8_t t34 = gm_l4;
                uint8_t t35 = (t34 <= ((uint8_t)90ULL));
                t31 = t35;
            } else t31 = 0;
            uint8_t t36 = t31;
            t20 = t36;
        } else t20 = 1;
        if (!t20) {
            uint8_t t37 = gm_l2;
            uint8_t t38 = ((t37 == ((uint8_t)95ULL)));
            t19 = t38;
        } else t19 = 1;
        if (t19) {
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t42 = (uintptr_t)t41;
            if (t42 > (uintptr_t)t39 || (uintptr_t)t39 > (uintptr_t)t39) gm_trap_slice(t42, (uintptr_t)t39, t39, gm_srceoep1gquqdsq, 149, 85);
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t40 + t42 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)((uintptr_t)t39 - t42));
            uint8_t t44 = gm_f_line_binds_jm0pnul(t43);
            uint8_t t45 = (uint8_t)!t44;
            t18 = t45;
        } else t18 = 0;
        uint8_t t46 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1) { return gm_f_continues_with_name_ejtvq2k(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_char_3bc3hom(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_char_3bc3hom F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_ta72bg8o6meed t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 0, ((uint8_t)45ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, ((uint8_t)43ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 2, ((uint8_t)42ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 3, ((uint8_t)47ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 4, ((uint8_t)37ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 5, ((uint8_t)60ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 6, ((uint8_t)62ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 7, ((uint8_t)61ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 8, ((uint8_t)33ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 9, ((uint8_t)38ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 10, ((uint8_t)124ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 11, ((uint8_t)94ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 12, ((uint8_t)126ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 13, ((uint8_t)46ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 14, ((uint8_t)58ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 15, ((uint8_t)63ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 16, ((uint8_t)64ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 17, ((uint8_t)35ULL));
    uint8_t t3 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 0, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 1, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 2, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 3, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 4, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 5, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 6, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 7, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 8, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 9, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 10, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 11, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 12, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 13, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 14, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 15, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 16, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 17, ((uint8_t *)&t1))) t3 = 1;
    uint8_t t4 = (uint8_t)(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_kb7cdmu(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_word_kb7cdmu F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    {
        uint8_t t5 = gm_l0;
        gm_l1 = t5;
    }
    uint8_t t6 = 0;
    uint8_t t7 = gm_l1;
    uint8_t t8 = (((uint8_t)97ULL) <= t7);
    if (t8) {
        uint8_t t9 = gm_l1;
        uint8_t t10 = (t9 <= ((uint8_t)122ULL));
        t6 = t10;
    } else t6 = 0;
    uint8_t t11 = t6;
    if (!t11) {
        {
            uint8_t t13 = gm_l0;
            gm_l2 = t13;
        }
        uint8_t t14 = 0;
        uint8_t t15 = gm_l2;
        uint8_t t16 = (((uint8_t)65ULL) <= t15);
        if (t16) {
            uint8_t t17 = gm_l2;
            uint8_t t18 = (t17 <= ((uint8_t)90ULL));
            t14 = t18;
        } else t14 = 0;
        uint8_t t19 = t14;
        t3 = t19;
    } else t3 = 1;
    if (!t3) {
        {
            uint8_t t21 = gm_l0;
            gm_l3 = t21;
        }
        uint8_t t22 = 0;
        uint8_t t23 = gm_l3;
        uint8_t t24 = (((uint8_t)48ULL) <= t23);
        if (t24) {
            uint8_t t25 = gm_l3;
            uint8_t t26 = (t25 <= ((uint8_t)57ULL));
            t22 = t26;
        } else t22 = 0;
        uint8_t t27 = t22;
        t2 = t27;
    } else t2 = 1;
    if (!t2) {
        uint8_t t28 = gm_l0;
        gm_t2abnvcnt53ot t29;
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)95ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 1), ((uint8_t)34ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 2), ((uint8_t)39ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 3), ((uint8_t)96ULL));
        uint8_t t30 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t29) + 0, ((uint8_t *)&t28))) t30 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t29) + 1, ((uint8_t *)&t28))) t30 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t29) + 2, ((uint8_t *)&t28))) t30 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t29) + 3, ((uint8_t *)&t28))) t30 = 1;
        uint8_t t31 = (uint8_t)(t30);
        t1 = t31;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1(gm_b16_8 a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keeps_padding_m0v49h1 F;
    gm_b16_8 gm_l0; /* src */
    uint8_t gm_l3; /* op */
    uint64_t gm_l4; /* start */
    uint64_t gm_l5; /* end */
    gm_th2lqbo77pvsc gm_l6; /* above */
    gm_th2lqbo77pvsc gm_l7; /* below */
    gm_b16_8 gm_l8; /* rest */
    gm_b16_8 gm_l9; /* own */
    uint64_t gm_l10; /* own_indent */
    uint64_t gm_l11; /* here */
    uint64_t gm_l12; /* col */
    gm_b16_8 gm_l13; /* line */
    gm_b16_8 gm_l14; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 923u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
        gm_t6j6smj7qoctu t3;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_ss67f0gmfen1m));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sm8obe455p2d9));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 16, t5);
        uint8_t t6 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t3) + 0, ((uint8_t *)&t2))) t6 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t3) + 16, ((uint8_t *)&t2))) t6 = 1;
        uint8_t t7 = (uint8_t)(t6);
        gm_l3 = t7;
    }
    {
        uint8_t t9 = gm_l3;
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie8;
        uint8_t t11 = (!((GM_LD(uintptr_t, (((uint8_t *)a1) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)a1) + 16)), gm_s39mljh669qgl, 1) == 0));
        if (!t11) goto ie8;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_b16_8 t14 = gm_l0;
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)a2) + 0));
        uint64_t t16 = gm_f_line_start_pl10jt5(t14, t15);
        gm_l4 = t16;
    }
    {
        gm_b16_8 t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)a2) + 0));
        uint64_t t19 = gm_f_line_end_jm825k5(t17, t18);
        gm_l5 = t19;
    }
    {
        gm_th2lqbo77pvsc t20;
        memset(&t20, 0, sizeof t20);
        gm_l6 = t20;
    }
    {
        uint64_t t22 = gm_l4;
        uint8_t t23 = (t22 > ((uint64_t)0ULL));
        if (!t23) goto ie21;
        {
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t26 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = gm_l4;
            uint64_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint64_t)1ULL), &r29);
            if (o30) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 176, 45);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            if (t27 > t32 || t32 > (uintptr_t)t25) gm_trap_slice(t27, t32, t25, gm_srceoep1gquqdsq, 176, 38);
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t26 + t27 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t27));
            gm_b16_8 t34 = gm_f_last_line_hk6q5l4(t33);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 0, t34);
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_th2lqbo77pvsc t35;
        memset(&t35, 0, sizeof t35);
        gm_l7 = t35;
    }
    {
        uint64_t t37 = gm_l5;
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t39 = (t37 < t38);
        if (!t39) goto ie36;
        {
            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t43 = gm_l5;
            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
            if (o45) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 179, 21);
            uint64_t t46 = r44;
            uint64_t t47 = (uintptr_t)t46;
            if (t47 > (uintptr_t)t41 || (uintptr_t)t41 > (uintptr_t)t41) gm_trap_slice(t47, (uintptr_t)t41, t41, gm_srceoep1gquqdsq, 179, 17);
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t42 + t47 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)((uintptr_t)t41 - t47));
            gm_l8 = t48;
        }
        {
            gm_b16_8 t49 = gm_l8;
            uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&t49) + 8);
            void * t51 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t49)));
            uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t53 = gm_l8;
            uint64_t t54 = gm_f_line_end_jm825k5(t53, ((uint64_t)0ULL));
            uint64_t t55 = (uintptr_t)t54;
            if (t52 > t55 || t55 > (uintptr_t)t50) gm_trap_slice(t52, t55, t50, gm_srceoep1gquqdsq, 180, 17);
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t51 + t52 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t52));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 0, t56);
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t58 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t59 = gm_l4;
        uint64_t t60 = (uintptr_t)t59;
        uint64_t t61 = gm_l5;
        uint64_t t62 = (uintptr_t)t61;
        if (t60 > t62 || t62 > (uintptr_t)t57) gm_trap_slice(t60, t62, t57, gm_srceoep1gquqdsq, 182, 12);
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t58 + t60 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62 - t60));
        gm_l9 = t63;
    }
    {
        gm_b16_8 t64 = gm_l9;
        uint64_t t65 = gm_f_indent_of_i911gpd(t64);
        gm_l10 = t65;
    }
    {
        gm_b16_8 t66 = gm_l0;
        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)a2) + 0));
        uint64_t t68 = gm_f_column_r91o38o(t66, t67);
        gm_l11 = t68;
    }
    {
        uint64_t t69;
        uint64_t t71 = gm_l11;
        uint64_t t72 = gm_l10;
        uint8_t t73 = (t71 > t72);
        if (!t73) goto ie70;
        uint64_t t75 = gm_l11;
        uint64_t t76 = gm_l10;
        uint64_t r77; bool o78 = __builtin_sub_overflow(t75, t76, &r77);
        if (o78) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 185, 35);
        uint64_t t79 = r77;
        uint64_t t80 = t79;
        t69 = t80;
        goto ix70;
ie70:;
        uint64_t t82 = ((uint64_t)0ULL);
        t69 = t82;
ix70:;
        gm_l12 = t69;
    }
    {
        if (!((((uint8_t *)&gm_l6))[16] != 0)) goto ie83;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l13 = t84;
        {
            gm_b16_8 t87 = gm_l13;
            uint64_t t88 = gm_l12;
            uint8_t t89 = gm_l3;
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            uint8_t t91 = gm_f_aligned_with_s024agj(t87, t88, t89, t90);
            if (!t91) goto ie86;
            {
                uint8_t t93 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix86;
ie86:;
ix86:;
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        if (!((((uint8_t *)&gm_l7))[16] != 0)) goto ie94;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l14 = t95;
        {
            gm_b16_8 t98 = gm_l14;
            uint64_t t99 = gm_l12;
            uint8_t t100 = gm_l3;
            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            uint8_t t102 = gm_f_aligned_with_s024agj(t98, t99, t100, t101);
            if (!t102) goto ie97;
            {
                uint8_t t104 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        goto ix94;
ie94:;
ix94:;
    }
    {
        uint8_t t105 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1, gm_t88jm1dgsmkjs a2) { return gm_f_keeps_padding_m0v49h1(a0, &a1, &a2); }
__attribute__((aligned(4))) void gm_f_layout_ufvu3cq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_layout_ufvu3cq F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint8_t gm_l3; /* bom */
    uint64_t gm_l4; /* base */
    uint64_t gm_l6; /* prev_end */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    uint8_t gm_l13; /* line_open */
    uint64_t gm_l14; /* nl */
    gm_t8c5h7u09n10a gm_l15; /* prev */
    uint64_t gm_l17; /* cur_base */
    uint64_t gm_l18; /* min_len */
    uint8_t gm_l19; /* cont_next */
    uint8_t gm_l20; /* after_lbrace */
    gm_tf5srh4mp4b3i gm_l21; /* line_head */
    uint8_t gm_l22; /* after_chain */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* idx */
    uint8_t gm_l27; /* close_tok */
    uint8_t gm_l28; /* close_brace */
    uint64_t gm_l29; /* k */
    uint8_t gm_l30; /* trailing */
    uint8_t gm_l31; /* first */
    gm_tf5srh4mp4b3i gm_l32; /* head */
    uint64_t gm_l33; /* indent */
    uint64_t gm_l35; /* v */
    uint8_t gm_l37; /* plain_ctx */
    uint8_t gm_l38; /* starts_cont */
    uint8_t gm_l39; /* is_else */
    uint64_t gm_l40; /* h */
    uint8_t gm_l41; /* chain_else */
    uint8_t gm_l42; /* continues */
    gm_tf5srh4mp4b3i gm_l43; /* aligned */
    gm_t2lqu8jsj511d gm_l45; /* v */
    uint64_t gm_l46; /* h */
    uint64_t gm_l47; /* level */
    uint64_t gm_l48; /* col */
    uint8_t gm_l50; /* lbrace */
    gm_t6j6smj7qoctu gm_l51; /* l */
    gm_b16_8 gm_l52; /* text */
    uint8_t gm_l53; /* prev_ta */
    uint64_t gm_l54; /* p */
    uint64_t gm_l55; /* k */
    uint64_t gm_l57; /* pad */
    uint8_t gm_l58; /* space */
    uint64_t gm_l59; /* p */
    uint8_t gm_l61; /* tight */
    gm_b16_8 gm_l62; /* gap */
    uint64_t gm_l63; /* min_gap */
    uint64_t gm_l64; /* want */
    uint64_t gm_l65; /* have */
    uint8_t gm_l66; /* glued */
    uint64_t gm_l67; /* p */
    void * gm_l68; /* f */
    uint8_t gm_l69; /* brace */
    uint8_t gm_l70; /* chain */
    uint64_t gm_l71; /* h */
    GM_STACK_CHECK(&F, 3804u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_classify_e96658j((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_srh29phg8b30r));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        gm_l3 = t7;
    }
    {
        uint64_t t8;
        uint8_t t10 = gm_l3;
        if (!t10) goto ie9;
        uint64_t t12 = ((uint64_t)3ULL);
        t8 = t12;
        goto ix9;
ie9:;
        uint64_t t14 = ((uint64_t)0ULL);
        t8 = t14;
ix9:;
        gm_l4 = t8;
    }
    {
        gm_f_list_f0gjjbh_4((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        uint64_t t16 = gm_l4;
        gm_l6 = t16;
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
                        uint64_t t30 = gm_l9;
                        uint64_t t31 = (uintptr_t)t30;
                        uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                        if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srceoep1gquqdsq, 247, 17);
                        gm_t88jm1dgsmkjs t33 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t31 * 40));
                        F.l10 = t33;
                    }
                    {
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t35 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t36 = gm_l6;
                        uint64_t t37 = (uintptr_t)t36;
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 0));
                        uint64_t t39 = (uintptr_t)t38;
                        if (t37 > t39 || t39 > (uintptr_t)t34) gm_trap_slice(t37, t39, t34, gm_srceoep1gquqdsq, 248, 18);
                        gm_b16_8 t40;
                        GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t35 + t37 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t37));
                        void * t41 = (void *)((uint8_t *)&F.l5);
                        gm_b0_1 t42 = gm_f_scan_gap_tkq0f9l((void *)NULL, cx, NULL, t40, t41);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                    }
                    {
                        void * t43 = (void *)((uint8_t *)&F.l5);
                        uint64_t t44 = gm_l9;
                        gm_tj63kbqqj2092 t45;
                        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t45 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t45 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t45 + 8), t44);
                        gm_tftb1odc739t5 t46 = gm_f_push_1e500nc_4(t43, &t45);
                        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srceoep1gquqdsq, 249, 9);
                        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                    }
                    {
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                        gm_l6 = t48;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 246, 5);
me26:;
                }
lc19:;
                {
                    uint64_t t50 = gm_l8;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l8 = t53;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_ld1_2;
lb19:;
        }
    }
    {
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t56 = gm_l6;
        uint64_t t57 = (uintptr_t)t56;
        if (t57 > (uintptr_t)t54 || (uintptr_t)t54 > (uintptr_t)t54) gm_trap_slice(t57, (uintptr_t)t54, t54, gm_srceoep1gquqdsq, 252, 14);
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t55 + t57 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)((uintptr_t)t54 - t57));
        void * t59 = (void *)((uint8_t *)&F.l5);
        gm_b0_1 t60 = gm_f_scan_gap_tkq0f9l((void *)NULL, cx, NULL, t58, t59);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        gm_f_hash_map_ss6fp5u((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_3;
        }
    }
    {
        uint8_t t63 = gm_l3;
        if (!t63) goto ie62;
        {
            void * t65 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_srh29phg8b30r));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t67 = gm_f_extend_0cs85gs(t65, t66);
            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srceoep1gquqdsq, 257, 14);
            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        gm_l13 = ((uint8_t)0);
    }
    {
        gm_l14 = ((uint64_t)0ULL);
    }
    {
        gm_t8c5h7u09n10a t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
        gm_l15 = t69;
    }
    {
        gm_f_list_g3vmkan_3((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_3;
        }
    }
    {
    }
    {
        gm_l17 = ((uint64_t)0ULL);
    }
    {
        gm_l18 = ((uint64_t)0ULL);
    }
    {
        gm_l19 = ((uint8_t)0);
    }
    {
        gm_l20 = ((uint8_t)0);
    }
    {
        gm_tf5srh4mp4b3i t71;
        memset(&t71, 0, sizeof t71);
        gm_l21 = t71;
    }
    {
        gm_l22 = ((uint8_t)0);
    }
    {
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            gm_l23 = t73;
        }
        {
            for (;;) {
                uint64_t t75 = gm_l24;
                uint64_t t76 = gm_l23;
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    uint64_t t80 = gm_l24;
                    uint64_t t83 = GM_LD(uint64_t, ((uint8_t *)&t80));
                    gm_l25 = t83;
                    {
                        uint64_t t85 = gm_l25;
                        uint64_t t86 = (uintptr_t)t85;
                        uint64_t t87 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                        if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srceoep1gquqdsq, 278, 17);
                        gm_tj63kbqqj2092 t88 = GM_LD(gm_tj63kbqqj2092, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t86 * 40));
                        F.l26 = t88;
                    }
                    {
                        uint8_t t90 = ((GM_LD(uint8_t, ((uint8_t *)&F.l26) + 0) == ((uint8_t)2ULL)));
                        if (!t90) goto ie89;
                        {
                            uint64_t t92 = gm_l14;
                            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                            if (o94) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 280, 13);
                            uint64_t t95 = r93;
                            gm_l14 = t95;
                        }
                        {
                            goto lc74;
                        }
                        goto ix89;
ie89:;
ix89:;
                    }
                    {
                        gm_l27 = ((uint8_t)0);
                    }
                    {
                        gm_l28 = ((uint8_t)0);
                    }
                    {
                        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        if (t97 != ((uint8_t)0ULL)) goto ie96;
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                        gm_l29 = t98;
                        {
                            uint64_t t100 = gm_l29;
                            uint64_t t101 = (uintptr_t)t100;
                            uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srceoep1gquqdsq, 286, 25);
                            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t101 * 40) + 35));
                            gm_l27 = t103;
                        }
                        {
                            uint8_t t104 = 0;
                            uint8_t t105 = gm_l27;
                            if (t105) {
                                uint64_t t106 = gm_l29;
                                uint64_t t107 = (uintptr_t)t106;
                                uint64_t t108 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_srceoep1gquqdsq, 287, 41);
                                uint8_t t109 = (((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t107 * 40) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t107 * 40) + 16)), gm_s2cuflhqkkuaq, 1) == 0));
                                t104 = t109;
                            } else t104 = 0;
                            gm_l28 = t104;
                        }
                        goto ix96;
ie96:;
ix96:;
                    }
                    {
                        uint8_t t110 = 0;
                        uint8_t t111 = 0;
                        uint8_t t112 = gm_l13;
                        if (t112) {
                            uint64_t t113 = gm_l14;
                            uint8_t t114 = ((t113 == ((uint64_t)0ULL)));
                            t111 = t114;
                        } else t111 = 0;
                        if (t111) {
                            uint8_t t115;
                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                            if (t118 != ((uint8_t)3ULL)) goto mn117;
                            t115 = ((uint8_t)1);
                            goto me116;
mn117:;
                            t115 = ((uint8_t)0);
                            goto me116;
mn119:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 289, 47);
me116:;
                            t110 = t115;
                        } else t110 = 0;
                        gm_l30 = t110;
                    }
                    {
                        uint64_t t121 = gm_l14;
                        uint8_t t122 = (t121 > ((uint64_t)0ULL));
                        if (!t122) goto ie120;
                        uint8_t t123 = gm_l13;
                        if (!t123) goto ie120;
                        {
                            void * t125 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t126 = gm_f_push_t83rg2m(t125, ((uint8_t)10ULL));
                            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srceoep1gquqdsq, 291, 13);
                            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                        }
                        {
                            uint64_t t129 = gm_l14;
                            uint8_t t130 = (t129 >= ((uint64_t)2ULL));
                            if (!t130) goto ie128;
                            uint8_t t131 = gm_l20;
                            uint8_t t132 = (uint8_t)!t131;
                            if (!t132) goto ie128;
                            uint8_t t133 = gm_l28;
                            uint8_t t134 = (uint8_t)!t133;
                            if (!t134) goto ie128;
                            {
                                void * t136 = (void *)((uint8_t *)&F.l12);
                                gm_tftb1odc739t5 t137 = gm_f_push_t83rg2m(t136, ((uint8_t)10ULL));
                                if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srceoep1gquqdsq, 292, 67);
                                gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
                            }
                            goto ix128;
ie128:;
ix128:;
                        }
                        {
                            gm_l13 = ((uint8_t)0);
                        }
                        goto ix120;
ie120:;
ix120:;
                    }
                    {
                        gm_l14 = ((uint64_t)0ULL);
                    }
                    {
                        uint8_t t139 = gm_l13;
                        uint8_t t140 = (uint8_t)!t139;
                        gm_l31 = t140;
                    }
                    {
                        uint8_t t142 = gm_l31;
                        if (!t142) goto ie141;
                        {
                            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                            uint64_t t146 = gm_l25;
                            gm_tf5srh4mp4b3i t147 = gm_f_next_tok_qap88op(t145, t146);
                            gm_l32 = t147;
                        }
                        {
                            gm_tf5srh4mp4b3i t148 = gm_l32;
                            gm_l21 = t148;
                        }
                        {
                            gm_l33 = ((uint64_t)0ULL);
                        }
                        {
                            uint8_t t150 = gm_l27;
                            if (!t150) goto ie149;
                            {
                                void * t152 = (void *)((uint8_t *)&F.l16);
                                gm_f_pop_f3uajlg_3((void *)&F.l34, t152);
                            }
                            {
                                gm_l22 = ((uint8_t)0);
                            }
                            {
                                gm_l35 = ((uint64_t)0ULL);
                            }
                            {
                                if (!((((uint8_t *)&F.l34))[48] != 0)) goto ie153;
                                gm_tsnno3vacsa4v t154 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)&F.l34));
                                F.l36 = t154;
                                {
                                    uint8_t t156 = 0;
                                    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&F.l36) + 40));
                                    if (t157) {
                                        uint8_t t158 = gm_l28;
                                        t156 = t158;
                                    } else t156 = 0;
                                    gm_l22 = t156;
                                }
                                {
                                    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 0));
                                    gm_l35 = t159;
                                }
                                goto ix153;
ie153:;
ix153:;
                            }
                            {
                                uint64_t t160 = gm_l35;
                                gm_l17 = t160;
                            }
                            {
                                uint64_t t161 = gm_l35;
                                uint64_t r162; bool o163 = __builtin_mul_overflow(t161, ((uint64_t)4ULL), &r162);
                                if (o163) gm_trap("integer overflow in `*`", gm_srceoep1gquqdsq, 310, 26);
                                uint64_t t164 = r162;
                                gm_l33 = t164;
                            }
                            goto ix149;
ie149:;
                            {
                                uint64_t t166;
                                uint64_t t168 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                uint8_t t169 = (t168 > ((uint64_t)0ULL));
                                if (!t169) goto ie167;
                                uint64_t t171 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                uint64_t r172; bool o173 = __builtin_sub_overflow(t171, ((uint64_t)1ULL), &r172);
                                if (o173) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 312, 65);
                                uint64_t t174 = r172;
                                uint64_t t175 = (uintptr_t)t174;
                                uint64_t t176 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srceoep1gquqdsq, 312, 53);
                                uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t175 * 48) + 0));
                                uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
                                if (o179) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 312, 53);
                                uint64_t t180 = r178;
                                uint64_t t181 = t180;
                                t166 = t181;
                                goto ix167;
ie167:;
                                uint64_t t183 = ((uint64_t)0ULL);
                                t166 = t183;
ix167:;
                                gm_l17 = t166;
                            }
                            {
                                uint8_t t184 = 0;
                                uint64_t t185 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                uint8_t t186 = ((t185 == ((uint64_t)0ULL)));
                                if (!t186) {
                                    uint64_t t187 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    uint64_t r188; bool o189 = __builtin_sub_overflow(t187, ((uint64_t)1ULL), &r188);
                                    if (o189) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 313, 66);
                                    uint64_t t190 = r188;
                                    uint64_t t191 = (uintptr_t)t190;
                                    uint64_t t192 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    if (t191 >= (uintptr_t)(t192)) gm_trap_index(t191, t192, gm_srceoep1gquqdsq, 313, 54);
                                    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t191 * 48) + 8));
                                    t184 = t193;
                                } else t184 = 1;
                                gm_l37 = t184;
                            }
                            {
                                gm_l38 = ((uint8_t)0);
                            }
                            {
                                gm_l39 = ((uint8_t)0);
                            }
                            {
                                if (!((((uint8_t *)&gm_l32))[8] != 0)) goto ie194;
                                uint64_t t195 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                                gm_l40 = t195;
                                {
                                    uint8_t t197 = 0;
                                    uint64_t t198 = gm_l40;
                                    uint64_t t199 = (uintptr_t)t198;
                                    uint64_t t200 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_srceoep1gquqdsq, 317, 35);
                                    gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t199 * 40) + 16));
                                    gm_tfeab279c16rr t202;
                                    gm_b16_8 t203;
                                    GM_ST(void *, (uint8_t *)&t203, (void *)(gm_s920dpvvv6cej));
                                    GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(3));
                                    GM_ST(gm_b16_8, (uint8_t *)&t202 + 0, t203);
                                    gm_b16_8 t204;
                                    GM_ST(void *, (uint8_t *)&t204, (void *)(gm_spi47lg4tr80j));
                                    GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(2));
                                    GM_ST(gm_b16_8, (uint8_t *)&t202 + 16, t204);
                                    gm_b16_8 t205;
                                    GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s9es2tos1bhfp));
                                    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(4));
                                    GM_ST(gm_b16_8, (uint8_t *)&t202 + 32, t205);
                                    uint8_t t206 = 0;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t202) + 0, ((uint8_t *)&t201))) t206 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t202) + 16, ((uint8_t *)&t201))) t206 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t202) + 32, ((uint8_t *)&t201))) t206 = 1;
                                    uint8_t t207 = (uint8_t)(t206);
                                    if (!t207) {
                                        gm_b16_8 t208 = gm_l0;
                                        uint64_t t209 = gm_l40;
                                        uint64_t t210 = (uintptr_t)t209;
                                        uint64_t t211 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t210 >= (uintptr_t)(t211)) gm_trap_index(t210, t211, gm_srceoep1gquqdsq, 317, 108);
                                        uint8_t t212 = gm_f_continues_with_name_ejtvq2k(t208, &(*(gm_t88jm1dgsmkjs *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t210 * 40)));
                                        t197 = t212;
                                    } else t197 = 1;
                                    gm_l38 = t197;
                                }
                                {
                                    uint64_t t213 = gm_l40;
                                    uint64_t t214 = (uintptr_t)t213;
                                    uint64_t t215 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t214 >= (uintptr_t)(t215)) gm_trap_index(t214, t215, gm_srceoep1gquqdsq, 318, 31);
                                    uint8_t t216 = (((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t214 * 40) + 16) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t214 * 40) + 16)), gm_s9es2tos1bhfp, 4) == 0));
                                    gm_l39 = t216;
                                }
                                goto ix194;
ie194:;
ix194:;
                            }
                            {
                                uint8_t t217 = 0;
                                uint8_t t218 = 0;
                                uint8_t t219 = gm_l22;
                                if (t219) {
                                    uint8_t t220;
                                    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                    if (t223 != ((uint8_t)0ULL)) goto mn222;
                                    t220 = ((uint8_t)1);
                                    goto me221;
mn222:;
                                    t220 = ((uint8_t)0);
                                    goto me221;
mn224:;
                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 320, 47);
me221:;
                                    t218 = t220;
                                } else t218 = 0;
                                if (t218) {
                                    uint8_t t225 = gm_l39;
                                    t217 = t225;
                                } else t217 = 0;
                                gm_l41 = t217;
                            }
                            {
                                uint8_t t226 = 0;
                                uint8_t t227 = 0;
                                uint8_t t228 = 0;
                                uint8_t t229 = gm_l37;
                                if (t229) {
                                    uint8_t t230 = gm_l41;
                                    uint8_t t231 = (uint8_t)!t230;
                                    t228 = t231;
                                } else t228 = 0;
                                if (t228) {
                                    uint8_t t232 = gm_l20;
                                    uint8_t t233 = (uint8_t)!t232;
                                    t227 = t233;
                                } else t227 = 0;
                                if (t227) {
                                    uint8_t t234 = 0;
                                    uint8_t t235 = gm_l19;
                                    if (!t235) {
                                        uint8_t t236 = gm_l38;
                                        t234 = t236;
                                    } else t234 = 1;
                                    t226 = t234;
                                } else t226 = 0;
                                gm_l42 = t226;
                            }
                            {
                                gm_tf5srh4mp4b3i t237;
                                memset(&t237, 0, sizeof t237);
                                gm_l43 = t237;
                            }
                            {
                                uint64_t t239 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                uint8_t t240 = (t239 > ((uint64_t)0ULL));
                                if (!t240) goto ie238;
                                {
                                    uint64_t t242 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    uint64_t r243; bool o244 = __builtin_sub_overflow(t242, ((uint64_t)1ULL), &r243);
                                    if (o244) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 326, 38);
                                    uint64_t t245 = r243;
                                    uint64_t t246 = (uintptr_t)t245;
                                    uint64_t t247 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    if (t246 >= (uintptr_t)(t247)) gm_trap_index(t246, t247, gm_srceoep1gquqdsq, 326, 26);
                                    gm_tsnno3vacsa4v t248 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t246 * 48));
                                    F.l44 = t248;
                                }
                                {
                                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&F.l44) + 9));
                                    if (!t250) goto ie249;
                                    {
                                        if (!(((((uint8_t *)&F.l44) + 16))[16] != 0)) goto ie252;
                                        gm_t2lqu8jsj511d t253 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&F.l44) + 16));
                                        gm_l45 = t253;
                                        {
                                            if (!((((uint8_t *)&gm_l32))[8] != 0)) goto ie255;
                                            uint64_t t256 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                                            gm_l46 = t256;
                                            {
                                                gm_b16_8 t259 = gm_l0;
                                                uint64_t t260 = gm_l46;
                                                uint64_t t261 = (uintptr_t)t260;
                                                uint64_t t262 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                                if (t261 >= (uintptr_t)(t262)) gm_trap_index(t261, t262, gm_srceoep1gquqdsq, 330, 48);
                                                uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t261 * 40) + 0));
                                                uint64_t t264 = gm_f_column_r91o38o(t259, t263);
                                                uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 0));
                                                uint8_t t266 = ((t264 == t265));
                                                if (!t266) goto ie258;
                                                {
                                                    uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                    gm_tf5srh4mp4b3i t269;
                                                    GM_ST(uint8_t, (uint8_t *)&t269 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t269 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t269 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t269), t268);
                                                    ((uint8_t *)&t269)[8] = 1;
                                                    gm_l43 = t269;
                                                }
                                                goto ix258;
ie258:;
ix258:;
                                            }
                                            goto ix255;
ie255:;
ix255:;
                                        }
                                        goto ix252;
ie252:;
ix252:;
                                    }
                                    goto ix249;
ie249:;
ix249:;
                                }
                                goto ix238;
ie238:;
ix238:;
                            }
                            {
                                uint64_t t270 = gm_l17;
                                gm_l47 = t270;
                            }
                            {
                                uint8_t t272 = gm_l42;
                                if (!t272) goto ie271;
                                {
                                    uint64_t t274 = gm_l47;
                                    uint64_t r275; bool o276 = __builtin_add_overflow(t274, ((uint64_t)1ULL), &r275);
                                    if (o276) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 336, 32);
                                    uint64_t t277 = r275;
                                    gm_l47 = t277;
                                }
                                goto ix271;
ie271:;
ix271:;
                            }
                            {
                                uint64_t t278;
                                if (!((((uint8_t *)&gm_l43))[8] != 0)) goto ie279;
                                uint64_t t280 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                                gm_l48 = t280;
                                uint64_t t282 = gm_l48;
                                uint64_t t283 = t282;
                                t278 = t283;
                                goto ix279;
ie279:;
                                uint64_t t285 = gm_l47;
                                uint64_t r286; bool o287 = __builtin_mul_overflow(t285, ((uint64_t)4ULL), &r286);
                                if (o287) gm_trap("integer overflow in `*`", gm_srceoep1gquqdsq, 337, 59);
                                uint64_t t288 = r286;
                                uint64_t t289 = t288;
                                t278 = t289;
ix279:;
                                gm_l33 = t278;
                            }
ix149:;
                        }
                        {
                            uint64_t t290 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                            gm_l18 = t290;
                        }
                        {
                            void * t291 = (void *)((uint8_t *)&F.l12);
                            uint64_t t292 = gm_l33;
                            gm_b0_1 t293 = gm_f_push_spaces_732bua4((void *)NULL, cx, NULL, t291, t292);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                        }
                        {
                            gm_l13 = ((uint8_t)1);
                        }
                        {
                            gm_t8c5h7u09n10a t294;
                            GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t294 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)0ULL));
                            gm_l15 = t294;
                        }
                        goto ix141;
ie141:;
                        uint8_t t296 = gm_l27;
                        if (!t296) goto ie295;
                        {
                            void * t299 = (void *)((uint8_t *)&F.l16);
                            gm_ttlblounidicj t300;
                            gm_f_pop_f3uajlg_3((void *)&t300, t299);
                            if (!((((uint8_t *)&t300))[48] != 0)) goto ie298;
                            F.l49 = (*(gm_tsnno3vacsa4v *)(((uint8_t *)&t300) + 0));
                            {
                                uint8_t t302 = 0;
                                uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&F.l49) + 40));
                                if (t303) {
                                    uint8_t t304 = gm_l28;
                                    t302 = t304;
                                } else t302 = 0;
                                gm_l22 = t302;
                            }
                            {
                                uint64_t t306 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                uint64_t t307 = gm_l18;
                                uint8_t t308 = (t306 < t307);
                                if (!t308) goto ie305;
                                {
                                    uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&F.l49) + 0));
                                    gm_l17 = t310;
                                }
                                {
                                    uint64_t t311 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    gm_l18 = t311;
                                }
                                goto ix305;
ie305:;
ix305:;
                            }
                            goto ix298;
ie298:;
ix298:;
                        }
                        goto ix295;
ie295:;
ix295:;
ix141:;
                    }
                    {
                        gm_l50 = ((uint8_t)0);
                    }
                    {
                        uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        switch (t319) { case ((uint8_t)0ULL): goto ma317; case ((uint8_t)1ULL): goto ma314; case ((uint8_t)2ULL): goto ma313; case ((uint8_t)3ULL): goto ma315; case ((uint8_t)4ULL): goto ma316; default: goto ma318; }
ma313:;
                        uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        if (t321 != ((uint8_t)2ULL)) goto mn320;
                        goto me312;
mn320:;
ma314:;
                        uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        if (t324 != ((uint8_t)1ULL)) goto mn323;
                        {
                            void * t326 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t327 = gm_f_push_t83rg2m(t326, ((uint8_t)59ULL));
                            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 1, gm_srceoep1gquqdsq, 356, 17);
                            gm_b0_1 t328 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
                        }
                        {
                            gm_t8c5h7u09n10a t329;
                            GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t329 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)2ULL));
                            gm_l15 = t329;
                        }
                        {
                            gm_l19 = ((uint8_t)0);
                        }
                        goto me312;
mn323:;
ma315:;
                        uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        if (t331 != ((uint8_t)3ULL)) goto mn330;
                        gm_t6j6smj7qoctu t332 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&F.l26) + 8));
                        gm_l51 = t332;
                        {
                            uint8_t t335 = gm_l31;
                            uint8_t t336 = (uint8_t)!t335;
                            if (!t336) goto ie334;
                            {
                                uint64_t t339 = GM_LD(uintptr_t, (((uint8_t *)&gm_l51) + 16) + 8);
                                uint8_t t340 = (t339 > ((uint64_t)0ULL));
                                if (!t340) goto ie338;
                                gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 16));
                                uint8_t t342 = gm_f_only_spaces_bvta5sq(t341);
                                if (!t342) goto ie338;
                                {
                                    void * t344 = (void *)((uint8_t *)&F.l12);
                                    gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 16));
                                    gm_tftb1odc739t5 t346 = gm_f_extend_0cs85gs(t344, t345);
                                    if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 1, gm_srceoep1gquqdsq, 363, 25);
                                    gm_b0_1 t347 = GM_LD(gm_b0_1, (((uint8_t *)&t346) + 1));
                                }
                                goto ix338;
ie338:;
                                {
                                    void * t349 = (void *)((uint8_t *)&F.l12);
                                    gm_tftb1odc739t5 t350 = gm_f_push_t83rg2m(t349, ((uint8_t)32ULL));
                                    if (((((uint8_t *)&t350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t350) + 1, gm_srceoep1gquqdsq, 365, 25);
                                    gm_b0_1 t351 = GM_LD(gm_b0_1, (((uint8_t *)&t350) + 1));
                                }
ix338:;
                            }
                            goto ix334;
ie334:;
ix334:;
                        }
                        {
                            void * t352 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 0));
                            gm_tftb1odc739t5 t354 = gm_f_extend_0cs85gs(t352, t353);
                            if (((((uint8_t *)&t354))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t354) + 1, gm_srceoep1gquqdsq, 368, 17);
                            gm_b0_1 t355 = GM_LD(gm_b0_1, (((uint8_t *)&t354) + 1));
                        }
                        {
                            gm_t8c5h7u09n10a t356;
                            GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t356 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t356 + 0), ((uint8_t)2ULL));
                            gm_l15 = t356;
                        }
                        goto me312;
mn330:;
ma316:;
                        uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        if (t358 != ((uint8_t)4ULL)) goto mn357;
                        gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 8));
                        gm_l52 = t359;
                        {
                            gm_l53 = ((uint8_t)0);
                        }
                        {
                            uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            if (t362 != ((uint8_t)1ULL)) goto ie361;
                            uint64_t t363 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            gm_l54 = t363;
                            {
                                uint64_t t365 = gm_l54;
                                uint64_t t366 = (uintptr_t)t365;
                                uint64_t t367 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                if (t366 >= (uintptr_t)(t367)) gm_trap_index(t366, t367, gm_srceoep1gquqdsq, 373, 48);
                                uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t366 * 40) + 33));
                                gm_l53 = t368;
                            }
                            goto ix361;
ie361:;
ix361:;
                        }
                        {
                            uint8_t t370 = gm_l31;
                            uint8_t t371 = (uint8_t)!t370;
                            if (!t371) goto ie369;
                            uint8_t t372 = gm_l53;
                            uint8_t t373 = (uint8_t)!t372;
                            if (!t373) goto ie369;
                            {
                                void * t375 = (void *)((uint8_t *)&F.l12);
                                gm_tftb1odc739t5 t376 = gm_f_push_t83rg2m(t375, ((uint8_t)32ULL));
                                if (((((uint8_t *)&t376))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t376) + 1, gm_srceoep1gquqdsq, 374, 48);
                                gm_b0_1 t377 = GM_LD(gm_b0_1, (((uint8_t *)&t376) + 1));
                            }
                            goto ix369;
ie369:;
ix369:;
                        }
                        {
                            void * t378 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t379 = gm_l52;
                            gm_b16_8 t380;
                            GM_ST(void *, (uint8_t *)&t380, (void *)(gm_skmt2kt2p7v0u));
                            GM_ST(uintptr_t, (uint8_t *)&t380 + 8, (uintptr_t)(2));
                            gm_b16_8 t381;
                            GM_ST(void *, (uint8_t *)&t381, (void *)(gm_so514oj5nbhcu));
                            GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(1));
                            gm_tftb1odc739t5 t382 = gm_f_replace_lf6hcmn(t378, t379, t380, t381);
                            if (((((uint8_t *)&t382))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t382) + 1, gm_srceoep1gquqdsq, 376, 17);
                            gm_b0_1 t383 = GM_LD(gm_b0_1, (((uint8_t *)&t382) + 1));
                        }
                        {
                            gm_t8c5h7u09n10a t384;
                            GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t384 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)2ULL));
                            gm_l15 = t384;
                        }
                        goto me312;
mn357:;
ma317:;
                        uint8_t t386 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                        if (t386 != ((uint8_t)0ULL)) goto mn385;
                        uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                        gm_l55 = t387;
                        {
                            uint64_t t389 = gm_l55;
                            uint64_t t390 = (uintptr_t)t389;
                            uint64_t t391 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            if (t390 >= (uintptr_t)(t391)) gm_trap_index(t390, t391, gm_srceoep1gquqdsq, 380, 25);
                            gm_t88jm1dgsmkjs t392 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t390 * 40));
                            F.l56 = t392;
                        }
                        {
                            gm_l57 = ((uint64_t)1ULL);
                        }
                        {
                            gm_l58 = ((uint8_t)0);
                        }
                        {
                            uint8_t t398 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            switch (t398) { case ((uint8_t)0ULL): goto ma394; case ((uint8_t)1ULL): goto ma395; case ((uint8_t)2ULL): goto ma396; default: goto ma397; }
ma394:;
                            uint8_t t400 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            if (t400 != ((uint8_t)0ULL)) goto mn399;
                            goto me393;
mn399:;
ma395:;
                            uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            if (t403 != ((uint8_t)1ULL)) goto mn402;
                            uint64_t t404 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            gm_l59 = t404;
                            {
                                uint64_t t406 = gm_l59;
                                uint64_t t407 = (uintptr_t)t406;
                                uint64_t t408 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                if (t407 >= (uintptr_t)(t408)) gm_trap_index(t407, t408, gm_srceoep1gquqdsq, 386, 34);
                                gm_t88jm1dgsmkjs t409 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t407 * 40));
                                F.l60 = t409;
                            }
                            {
                                uint8_t t410 = 0;
                                uint8_t t411 = GM_LD(uint8_t, (((uint8_t *)&F.l60) + 33));
                                if (!t411) {
                                    uint8_t t412 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 32));
                                    t410 = t412;
                                } else t410 = 1;
                                gm_l61 = t410;
                            }
                            {
                                uint64_t t413 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t414 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t415 = GM_LD(uint64_t, (((uint8_t *)&F.l60) + 8));
                                uint64_t t416 = (uintptr_t)t415;
                                uint64_t t417 = GM_LD(uint64_t, (((uint8_t *)&F.l56) + 0));
                                uint64_t t418 = (uintptr_t)t417;
                                if (t416 > t418 || t418 > (uintptr_t)t413) gm_trap_slice(t416, t418, t413, gm_srceoep1gquqdsq, 388, 32);
                                gm_b16_8 t419;
                                GM_ST(void *, (uint8_t *)&t419, (void *)(((uint8_t *)t414 + t416 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t419 + 8, (uintptr_t)(t418 - t416));
                                gm_l62 = t419;
                            }
                            {
                                uint64_t t420;
                                gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                gm_t6j6smj7qoctu t423;
                                gm_b16_8 t424;
                                GM_ST(void *, (uint8_t *)&t424, (void *)(gm_ss67f0gmfen1m));
                                GM_ST(uintptr_t, (uint8_t *)&t424 + 8, (uintptr_t)(2));
                                GM_ST(gm_b16_8, (uint8_t *)&t423 + 0, t424);
                                gm_b16_8 t425;
                                GM_ST(void *, (uint8_t *)&t425, (void *)(gm_sm8obe455p2d9));
                                GM_ST(uintptr_t, (uint8_t *)&t425 + 8, (uintptr_t)(2));
                                GM_ST(gm_b16_8, (uint8_t *)&t423 + 16, t425);
                                uint8_t t426 = 0;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t423) + 0, ((uint8_t *)&t422))) t426 = 1;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t423) + 16, ((uint8_t *)&t422))) t426 = 1;
                                uint8_t t427 = (uint8_t)(t426);
                                if (!t427) goto ie421;
                                uint64_t t429 = ((uint64_t)1ULL);
                                t420 = t429;
                                goto ix421;
ie421:;
                                uint64_t t431 = ((uint64_t)2ULL);
                                t420 = t431;
ix421:;
                                gm_l63 = t420;
                            }
                            {
                                uint8_t t433 = gm_l61;
                                uint8_t t434 = (uint8_t)!t433;
                                if (!t434) goto ie432;
                                uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                                uint64_t t436 = gm_l63;
                                uint8_t t437 = (t435 >= t436);
                                if (!t437) goto ie432;
                                gm_b16_8 t438 = gm_l62;
                                uint8_t t439 = gm_f_only_spaces_bvta5sq(t438);
                                if (!t439) goto ie432;
                                gm_b16_8 t440 = gm_l0;
                                uint8_t t441 = gm_f_keeps_padding_m0v49h1(t440, &(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l60)), &(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l56)));
                                if (!t441) goto ie432;
                                {
                                    gm_b16_8 t443 = gm_l0;
                                    uint64_t t444 = GM_LD(uintptr_t, ((uint8_t *)&t443) + 8);
                                    void * t445 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t443)));
                                    gm_b16_8 t446 = gm_l0;
                                    uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&F.l56) + 0));
                                    uint64_t t448 = gm_f_line_start_pl10jt5(t446, t447);
                                    uint64_t t449 = (uintptr_t)t448;
                                    uint64_t t450 = GM_LD(uint64_t, (((uint8_t *)&F.l56) + 0));
                                    uint64_t t451 = (uintptr_t)t450;
                                    if (t449 > t451 || t451 > (uintptr_t)t444) gm_trap_slice(t449, t451, t444, gm_srceoep1gquqdsq, 396, 53);
                                    gm_b16_8 t452;
                                    GM_ST(void *, (uint8_t *)&t452, (void *)(((uint8_t *)t445 + t449 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t452 + 8, (uintptr_t)(t451 - t449));
                                    uint64_t t453 = gm_f_relative_column_ctuu0pd(t452);
                                    gm_l64 = t453;
                                }
                                {
                                    gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                    gm_b16_8 t455 = GM_LD(gm_b16_8, ((uint8_t *)&t454));
                                    gm_b16_8 t456 = gm_f_last_line_hk6q5l4(t455);
                                    uint64_t t457 = gm_f_relative_column_ctuu0pd(t456);
                                    gm_l65 = t457;
                                }
                                {
                                    uint64_t t458;
                                    uint64_t t460 = gm_l64;
                                    uint64_t t461 = gm_l65;
                                    uint8_t t462 = (t460 > t461);
                                    if (!t462) goto ie459;
                                    uint64_t t464 = gm_l64;
                                    uint64_t t465 = gm_l65;
                                    uint64_t r466; bool o467 = __builtin_sub_overflow(t464, t465, &r466);
                                    if (o467) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 398, 52);
                                    uint64_t t468 = r466;
                                    uint64_t t469 = t468;
                                    t458 = t469;
                                    goto ix459;
ie459:;
                                    uint64_t t471 = ((uint64_t)1ULL);
                                    t458 = t471;
ix459:;
                                    gm_l57 = t458;
                                }
                                goto ix432;
ie432:;
ix432:;
                            }
                            {
                                uint8_t t472 = 0;
                                uint8_t t473 = 0;
                                uint64_t t474 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                                uint8_t t475 = ((t474 == ((uint64_t)0ULL)));
                                if (t475) {
                                    uint64_t t476 = GM_LD(uintptr_t, (((uint8_t *)&F.l60) + 16) + 8);
                                    uint64_t r477; bool o478 = __builtin_sub_overflow(t476, ((uint64_t)1ULL), &r477);
                                    if (o478) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 401, 70);
                                    uint64_t t479 = r477;
                                    uint64_t t480 = (uintptr_t)t479;
                                    uint64_t t481 = GM_LD(uintptr_t, (((uint8_t *)&F.l60) + 16) + 8);
                                    if (t480 >= (uintptr_t)(t481)) gm_trap_index(t480, t481, gm_srceoep1gquqdsq, 401, 59);
                                    uint8_t t482 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l60) + 16)) + t480 * 1));
                                    uint8_t t483 = gm_f_is_word_kb7cdmu(t482);
                                    t473 = t483;
                                } else t473 = 0;
                                if (t473) {
                                    uint64_t t484 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t485 = GM_LD(uintptr_t, (((uint8_t *)&F.l56) + 16) + 8);
                                    if (t484 >= (uintptr_t)(t485)) gm_trap_index(t484, t485, gm_srceoep1gquqdsq, 401, 103);
                                    uint8_t t486 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l56) + 16)) + t484 * 1));
                                    uint8_t t487 = gm_f_is_word_kb7cdmu(t486);
                                    t472 = t487;
                                } else t472 = 0;
                                gm_l66 = t472;
                            }
                            {
                                uint8_t t488 = 0;
                                uint8_t t489 = gm_l66;
                                uint8_t t490 = (uint8_t)!t489;
                                if (t490) {
                                    uint8_t t491 = 0;
                                    uint8_t t492 = 0;
                                    uint8_t t493 = gm_l61;
                                    uint8_t t494 = (uint8_t)!t493;
                                    if (!t494) {
                                        gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&F.l60) + 16));
                                        gm_b16_8 t496 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                        void * t497 = (void *)((uint8_t *)&F.l11);
                                        uint8_t t498 = gm_f_would_merge_44g1m96((void *)NULL, cx, NULL, t495, t496, t497);
                                        if (gm_unw.kind) {
                                            goto gm_ll78;
                                        }
                                        t492 = t498;
                                    } else t492 = 1;
                                    if (!t492) {
                                        gm_b16_8 t499 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                        gm_b16_8 t500 = GM_LD(gm_b16_8, ((uint8_t *)&t499));
                                        gm_b16_8 t501 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                                        uint8_t t502 = gm_f_merges_with_tail_ijmh3nm((void *)NULL, cx, NULL, t500, t501);
                                        if (gm_unw.kind) {
                                            goto gm_ll78;
                                        }
                                        t491 = t502;
                                    } else t491 = 1;
                                    t488 = t491;
                                } else t488 = 0;
                                gm_l58 = t488;
                            }
                            goto me393;
mn402:;
ma396:;
                            uint8_t t504 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            if (t504 != ((uint8_t)2ULL)) goto mn503;
                            {
                                uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 32));
                                uint8_t t507 = (uint8_t)!t506;
                                gm_l58 = t507;
                            }
                            goto me393;
mn503:;
ma397:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 383, 17);
me393:;
                        }
                        {
                            uint8_t t509 = gm_l58;
                            if (!t509) goto ie508;
                            {
                                void * t511 = (void *)((uint8_t *)&F.l12);
                                uint64_t t512 = gm_l57;
                                gm_b0_1 t513 = gm_f_push_spaces_732bua4((void *)NULL, cx, NULL, t511, t512);
                                if (gm_unw.kind) {
                                    goto gm_ll78;
                                }
                            }
                            goto ix508;
ie508:;
ix508:;
                        }
                        {
                            uint8_t t515 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                            if (t515 != ((uint8_t)1ULL)) goto ie514;
                            uint64_t t516 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            gm_l67 = t516;
                            {
                                uint64_t t519 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                uint8_t t520 = (t519 > ((uint64_t)0ULL));
                                if (!t520) goto ie518;
                                {
                                    uint64_t t522 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    uint64_t r523; bool o524 = __builtin_sub_overflow(t522, ((uint64_t)1ULL), &r523);
                                    if (o524) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 409, 43);
                                    uint64_t t525 = r523;
                                    uint64_t t526 = (uintptr_t)t525;
                                    uint64_t t527 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                                    if (t526 >= (uintptr_t)(t527)) gm_trap_index(t526, t527, gm_srceoep1gquqdsq, 409, 31);
                                    void * t528 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t526 * 48);
                                    gm_l68 = t528;
                                }
                                {
                                    void * t530 = gm_l68;
                                    uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)t530) + 9));
                                    if (!t531) goto ie529;
                                    void * t532 = gm_l68;
                                    uint8_t t533;
                                    if ((((((uint8_t *)t532) + 16))[16] != 0)) goto mn535;
                                    t533 = ((uint8_t)1);
                                    goto me534;
mn535:;
                                    t533 = ((uint8_t)0);
                                    goto me534;
mn536:;
                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 410, 40);
me534:;
                                    if (!t533) goto ie529;
                                    uint64_t t537 = gm_l67;
                                    uint64_t t538 = (uintptr_t)t537;
                                    uint64_t t539 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t538 >= (uintptr_t)(t539)) gm_trap_index(t538, t539, gm_srceoep1gquqdsq, 410, 61);
                                    uint8_t t540 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t538 * 40) + 34));
                                    if (!t540) goto ie529;
                                    uint8_t t541 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 35));
                                    uint8_t t542 = (uint8_t)!t541;
                                    if (!t542) goto ie529;
                                    {
                                        void * t544 = gm_l68;
                                        uint8_t *t545 = (((uint8_t *)t544) + 16);
                                        gm_b16_8 t546 = gm_l0;
                                        uint64_t t547 = GM_LD(uint64_t, (((uint8_t *)&F.l56) + 0));
                                        uint64_t t548 = gm_f_column_r91o38o(t546, t547);
                                        void * t549 = (void *)((uint8_t *)&F.l12);
                                        uint64_t t550 = gm_f_out_column_hr4hgng(t549);
                                        gm_t2lqu8jsj511d t551;
                                        GM_ST(uint64_t, ((uint8_t *)&t551 + 0), t548);
                                        GM_ST(uint64_t, ((uint8_t *)&t551 + 8), t550);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)t545) + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)t545) + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)t545) + 20, 0);
                                        ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)t545))[16] = 1;
                                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)t545) + 0, t551);
                                    }
                                    goto ix529;
ie529:;
ix529:;
                                }
                                goto ix518;
ie518:;
ix518:;
                            }
                            goto ix514;
ie514:;
ix514:;
                        }
                        {
                            void * t552 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t553 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 16));
                            gm_tftb1odc739t5 t554 = gm_f_extend_0cs85gs(t552, t553);
                            if (((((uint8_t *)&t554))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t554) + 1, gm_srceoep1gquqdsq, 415, 17);
                            gm_b0_1 t555 = GM_LD(gm_b0_1, (((uint8_t *)&t554) + 1));
                        }
                        {
                            uint8_t t557 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 34));
                            if (!t557) goto ie556;
                            {
                                uint8_t t559 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l56) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l56) + 16)), gm_sh40na9qbga3l, 1) == 0));
                                gm_l69 = t559;
                            }
                            {
                                gm_l70 = ((uint8_t)0);
                            }
                            {
                                uint8_t t561 = gm_l69;
                                if (!t561) goto ie560;
                                {
                                    if (!((((uint8_t *)&gm_l21))[8] != 0)) goto ie563;
                                    uint64_t t564 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                                    gm_l71 = t564;
                                    {
                                        uint64_t t566 = gm_l71;
                                        uint64_t t567 = (uintptr_t)t566;
                                        uint64_t t568 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t567 >= (uintptr_t)(t568)) gm_trap_index(t567, t568, gm_srceoep1gquqdsq, 420, 53);
                                        gm_b16_8 t569 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t567 * 40) + 16));
                                        gm_tfeab279c16rr t570;
                                        gm_b16_8 t571;
                                        GM_ST(void *, (uint8_t *)&t571, (void *)(gm_sl9nvct67or45));
                                        GM_ST(uintptr_t, (uint8_t *)&t571 + 8, (uintptr_t)(2));
                                        GM_ST(gm_b16_8, (uint8_t *)&t570 + 0, t571);
                                        gm_b16_8 t572;
                                        GM_ST(void *, (uint8_t *)&t572, (void *)(gm_slpo22sjs83b1));
                                        GM_ST(uintptr_t, (uint8_t *)&t572 + 8, (uintptr_t)(4));
                                        GM_ST(gm_b16_8, (uint8_t *)&t570 + 16, t572);
                                        gm_b16_8 t573;
                                        GM_ST(void *, (uint8_t *)&t573, (void *)(gm_s9es2tos1bhfp));
                                        GM_ST(uintptr_t, (uint8_t *)&t573 + 8, (uintptr_t)(4));
                                        GM_ST(gm_b16_8, (uint8_t *)&t570 + 32, t573);
                                        uint8_t t574 = 0;
                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t570) + 0, ((uint8_t *)&t569))) t574 = 1;
                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t570) + 16, ((uint8_t *)&t569))) t574 = 1;
                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t570) + 32, ((uint8_t *)&t569))) t574 = 1;
                                        uint8_t t575 = (uint8_t)(t574);
                                        gm_l70 = t575;
                                    }
                                    goto ix563;
ie563:;
ix563:;
                                }
                                goto ix560;
ie560:;
ix560:;
                            }
                            {
                                void * t576 = (void *)((uint8_t *)&F.l16);
                                gm_tsnno3vacsa4v t577;
                                memset((uint8_t *)&t577 + 10, 0, 6);
                                memset((uint8_t *)&t577 + 41, 0, 7);
                                uint64_t t578 = gm_l17;
                                GM_ST(uint64_t, (uint8_t *)&t577 + 0, t578);
                                uint8_t t579 = 0;
                                uint8_t t580 = gm_l69;
                                if (t580) {
                                    uint8_t t581 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 36));
                                    uint8_t t582 = (uint8_t)!t581;
                                    t579 = t582;
                                } else t579 = 0;
                                GM_ST(uint8_t, (uint8_t *)&t577 + 8, t579);
                                uint8_t t583 = gm_l69;
                                uint8_t t584 = (uint8_t)!t583;
                                GM_ST(uint8_t, (uint8_t *)&t577 + 9, t584);
                                gm_tc70a9dbbmgkg t585;
                                memset(&t585, 0, sizeof t585);
                                GM_ST(gm_tc70a9dbbmgkg, (uint8_t *)&t577 + 16, t585);
                                uint8_t t586 = gm_l70;
                                GM_ST(uint8_t, (uint8_t *)&t577 + 40, t586);
                                gm_tftb1odc739t5 t587 = gm_f_push_8h1r7s6_3(t576, &t577);
                                if (((((uint8_t *)&t587))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t587) + 1, gm_srceoep1gquqdsq, 422, 21);
                                gm_b0_1 t588 = GM_LD(gm_b0_1, (((uint8_t *)&t587) + 1));
                            }
                            goto ix556;
ie556:;
ix556:;
                        }
                        {
                            uint8_t t589 = gm_f_continues_line_2cliit4(&(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l56)));
                            gm_l19 = t589;
                        }
                        {
                            uint8_t t591 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 35));
                            uint8_t t592 = (uint8_t)!t591;
                            if (!t592) goto ie590;
                            {
                                gm_l22 = ((uint8_t)0);
                            }
                            goto ix590;
ie590:;
ix590:;
                        }
                        {
                            uint8_t t594 = 0;
                            uint8_t t595 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l56) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l56) + 16)), gm_sh40na9qbga3l, 1) == 0));
                            if (t595) {
                                uint8_t t596 = GM_LD(uint8_t, (((uint8_t *)&F.l56) + 33));
                                uint8_t t597 = (uint8_t)!t596;
                                t594 = t597;
                            } else t594 = 0;
                            gm_l50 = t594;
                        }
                        {
                            uint64_t t598 = gm_l55;
                            gm_t8c5h7u09n10a t599;
                            GM_ST(uint8_t, (uint8_t *)&t599 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t599 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t599 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t599 + 0), ((uint8_t)1ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t599 + 8), t598);
                            gm_l15 = t599;
                        }
                        goto me312;
mn385:;
ma318:;
                        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 353, 9);
me312:;
                    }
                    {
                        uint8_t t600 = 0;
                        uint8_t t601 = gm_l50;
                        if (!t601) {
                            uint8_t t602 = 0;
                            uint8_t t603 = gm_l30;
                            if (t603) {
                                uint8_t t604 = gm_l20;
                                t602 = t604;
                            } else t602 = 0;
                            t600 = t602;
                        } else t600 = 1;
                        gm_l20 = t600;
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 277, 5);
me81:;
                }
lc74:;
                {
                    uint64_t t606 = gm_l24;
                    uint64_t r607; bool o608 = __builtin_add_overflow(t606, ((uint64_t)1ULL), &r607);
                    uint64_t t609 = r607;
                    gm_l24 = t609;
                }
            }
gm_ll78:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
            }
            goto gm_ld1_4;
lb74:;
        }
    }
    {
        uint8_t t611 = gm_l13;
        if (!t611) goto ie610;
        {
            void * t613 = (void *)((uint8_t *)&F.l12);
            gm_tftb1odc739t5 t614 = gm_f_push_t83rg2m(t613, ((uint8_t)10ULL));
            if (((((uint8_t *)&t614))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t614) + 1, gm_srceoep1gquqdsq, 438, 20);
            gm_b0_1 t615 = GM_LD(gm_b0_1, (((uint8_t *)&t614) + 1));
        }
        goto ix610;
ie610:;
ix610:;
    }
    {
        uint8_t t616 = 0;
        F.ret = (*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l12));
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t616;
        goto gm_ld1_4;
    }
    {
        void * t617 = (void *)((uint8_t *)&F.l16);
        gm_f_deinit_ectdopm_2(t617);
    }
    {
        void * t618 = (void *)((uint8_t *)&F.l11);
        gm_f_deinit_p1kmdo2(t618);
    }
    {
        void * t619 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_aii0n0l(t619);
    }
    {
        void * t620 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_26uqnl1(t620);
    }
    goto gm_ls621;
gm_ld1_4:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t622 = (void *)((uint8_t *)&F.l16);
        gm_f_deinit_ectdopm_2(t622);
        gm_unw = gm_saved;
    }
gm_ld1_3:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t623 = (void *)((uint8_t *)&F.l11);
        gm_f_deinit_p1kmdo2(t623);
        gm_unw = gm_saved;
    }
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t624 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_aii0n0l(t624);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t625 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_26uqnl1(t625);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls621:;
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
__attribute__((aligned(4))) uint8_t gm_f_lexes_apart_9scqa7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_lexes_apart_9scqa7n F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* second */
    uint64_t gm_l8; /* count */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l12; /* piece */
    GM_STACK_CHECK(&F, 843u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_new_jr5ejs0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, (&wn6), NULL);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            void * t10 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t11 = gm_l0;
            gm_tftb1odc739t5 t12 = gm_f_extend_0cs85gs(t10, t11);
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srceoep1gquqdsq, 99, 9);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        {
            void * t14 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t15 = gm_l1;
            gm_tftb1odc739t5 t16 = gm_f_extend_0cs85gs(t14, t15);
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srceoep1gquqdsq, 100, 9);
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        }
        {
            gm_f_diagnostics_n3kkhf4((void *)&F.l4, (void *)NULL, (&wn6), NULL);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            void * t20 = (void *)((uint8_t *)&F.l4);
            gm_f_lex_dc8mbmf((void *)&F.l5, (void *)NULL, (&wn6), NULL, ((uint32_t)0ULL), t19, t20);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(0));
            gm_l6 = t21;
        }
        {
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(0));
            gm_l7 = t22;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 0) + 0));
                gm_l9 = t24;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l10;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l10;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tm4pc0emhrv1g t36 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t32 * 48));
                        F.l11 = t36;
                        {
                            gm_t3fcbuehh3bs2 t39 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)&F.l11) + 0));
                            gm_t23tohc57ebhi t40;
                            gm_t3fcbuehh3bs2 t41;
                            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t41 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t41 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)9ULL));
                            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t40 + 0, t41);
                            gm_t3fcbuehh3bs2 t42;
                            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t42 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t42 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)10ULL));
                            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t40 + 32, t42);
                            uint8_t t43 = 0;
                            if (gm_eqt7ftn1841u(((uint8_t *)&t40) + 0, ((uint8_t *)&t39))) t43 = 1;
                            if (gm_eqt7ftn1841u(((uint8_t *)&t40) + 32, ((uint8_t *)&t39))) t43 = 1;
                            uint8_t t44 = (uint8_t)(t43);
                            if (!t44) goto ie38;
                            {
                                goto lc25;
                            }
                            goto ix38;
ie38:;
ix38:;
                        }
                        {
                            uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            void * t47 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)));
                            uint32_t t48 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 4));
                            uint64_t t49 = (uint64_t)t48;
                            uint64_t t50 = (uintptr_t)t49;
                            uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 8));
                            uint64_t t52 = (uint64_t)t51;
                            uint64_t t53 = (uintptr_t)t52;
                            if (t50 > t53 || t53 > (uintptr_t)t46) gm_trap_slice(t50, t53, t46, gm_srceoep1gquqdsq, 108, 22);
                            gm_b16_8 t54;
                            GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t47 + t50 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53 - t50));
                            gm_l12 = t54;
                        }
                        {
                            uint64_t t56 = gm_l8;
                            uint8_t t57 = ((t56 == ((uint64_t)0ULL)));
                            if (!t57) goto ie55;
                            {
                                gm_b16_8 t59 = gm_l12;
                                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                                gm_l6 = t60;
                            }
                            goto ix55;
ie55:;
ix55:;
                        }
                        {
                            uint64_t t62 = gm_l8;
                            uint8_t t63 = ((t62 == ((uint64_t)1ULL)));
                            if (!t63) goto ie61;
                            {
                                gm_b16_8 t65 = gm_l12;
                                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                                gm_l7 = t66;
                            }
                            goto ix61;
ie61:;
ix61:;
                        }
                        {
                            uint64_t t67 = gm_l8;
                            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                            if (o69) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 111, 13);
                            uint64_t t70 = r68;
                            gm_l8 = t70;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 106, 9);
me34:;
                    }
lc25:;
                    {
                        uint64_t t72 = gm_l10;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l10 = t75;
                    }
                }
lb25:;
            }
        }
        {
            uint8_t t76 = 0;
            uint8_t t77 = 0;
            uint8_t t78 = 0;
            uint64_t t79 = gm_l8;
            uint8_t t80 = (!(t79 == ((uint64_t)2ULL)));
            if (!t80) {
                gm_b16_8 t81 = gm_l6;
                gm_b16_8 t82 = gm_l0;
                uint8_t t83 = (!gm_eq0iqiubfm7e(((uint8_t *)&t81), ((uint8_t *)&t82)));
                t78 = t83;
            } else t78 = 1;
            if (!t78) {
                gm_b16_8 t84 = gm_l7;
                gm_b16_8 t85 = gm_l1;
                uint8_t t86 = (!gm_eq0iqiubfm7e(((uint8_t *)&t84), ((uint8_t *)&t85)));
                t77 = t86;
            } else t77 = 1;
            if (!t77) {
                void * t87 = (void *)((uint8_t *)&F.l4);
                uint8_t t88 = gm_f_has_errors_if7ai0m(t87);
                t76 = t88;
            } else t76 = 1;
            uint8_t t89 = 0;
            F.ret = t76;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t89;
            goto gm_ll8;
        }
        goto gm_ls90;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls90:;
we7:;
    }
    {
        void * t91 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t91);
    }
    goto gm_ls92;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t93 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t93);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls92:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_merges_with_tail_ijmh3nm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_merges_with_tail_ijmh3nm F;
    gm_b16_8 gm_l0; /* out */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* tail */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 621u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srceoep1gquqdsq, 120, 40);
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t6 * 1));
            uint8_t t9 = gm_f_is_punct_char_3bc3hom(t8);
            uint8_t t10 = (uint8_t)!t9;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l2;
            uint8_t t16 = (t15 > ((uint64_t)0ULL));
            if (!t16) goto lb14;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t18 = gm_l2;
            uint64_t r19; bool o20 = __builtin_sub_overflow(t17, t18, &r19);
            if (o20) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 122, 25);
            uint64_t t21 = r19;
            uint8_t t22 = (t21 < ((uint64_t)3ULL));
            if (!t22) goto lb14;
            uint64_t t23 = gm_l2;
            uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 122, 67);
            uint64_t t26 = r24;
            uint64_t t27 = (uintptr_t)t26;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srceoep1gquqdsq, 122, 63);
            uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t27 * 1));
            uint8_t t30 = gm_f_is_punct_char_3bc3hom(t29);
            if (!t30) goto lb14;
            {
                uint64_t t33 = gm_l2;
                uint64_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint64_t)1ULL), &r34);
                if (o35) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 122, 81);
                uint64_t t36 = r34;
                gm_l2 = t36;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t39 = gm_l2;
        uint64_t t40 = (uintptr_t)t39;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t42 = (uintptr_t)t41;
        if (t40 > t42 || t42 > (uintptr_t)t37) gm_trap_slice(t40, t42, t37, gm_srceoep1gquqdsq, 123, 13);
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t38 + t40 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t40));
        gm_l3 = t43;
    }
    {
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t46 = (t45 < ((uint64_t)2ULL));
        if (!t46) goto ie44;
        {
            uint8_t t48 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_f_new_jr5ejs0((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l4);
        gm_t3mf58gv9kf3n t51 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t50));
        gm_t3mf58gv9kf3n t52 = t51;
        gm_node wn53 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t52, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        {
            gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, (&wn53), NULL);
            if (gm_unw.kind) {
                goto gm_ll55;
            }
        }
        {
            void * t57 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t58 = gm_l3;
            gm_tftb1odc739t5 t59 = gm_f_extend_0cs85gs(t57, t58);
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srceoep1gquqdsq, 129, 9);
            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
        }
        {
            void * t61 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t62 = gm_l1;
            gm_tftb1odc739t5 t63 = gm_f_extend_0cs85gs(t61, t62);
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srceoep1gquqdsq, 130, 9);
            gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
        }
        {
            gm_f_diagnostics_n3kkhf4((void *)&F.l6, (void *)NULL, (&wn53), NULL);
            if (gm_unw.kind) {
                goto gm_ll55;
            }
        }
        {
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            void * t67 = (void *)((uint8_t *)&F.l6);
            gm_f_lex_dc8mbmf((void *)&F.l7, (void *)NULL, (&wn53), NULL, ((uint32_t)0ULL), t66, t67);
            if (gm_unw.kind) {
                goto gm_ll55;
            }
        }
        {
            {
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                gm_l8 = t69;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t71 = gm_l9;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t73 = (t71 < t72);
                    if (!t73) goto lb70;
                    {
                        uint64_t t76 = gm_l9;
                        uint64_t t77 = (uintptr_t)t76;
                        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tm4pc0emhrv1g t81 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t77 * 48));
                        F.l10 = t81;
                        {
                            uint32_t t84 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 4));
                            uint64_t t85 = (uint64_t)t84;
                            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t87 = ((t85 == t86));
                            if (!t87) goto ie83;
                            {
                                uint8_t t89 = 0;
                                F.ret = ((uint8_t)0);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t89;
                                goto gm_ll74;
                            }
                            goto ix83;
ie83:;
ix83:;
                        }
                        {
                            uint32_t t91 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 4));
                            uint64_t t92 = (uint64_t)t91;
                            uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t94 = (t92 < t93);
                            if (!t94) goto ie90;
                            uint32_t t95 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 8));
                            uint64_t t96 = (uint64_t)t95;
                            uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t98 = (t96 > t97);
                            if (!t98) goto ie90;
                            {
                                uint8_t t100 = 0;
                                F.ret = ((uint8_t)1);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t100;
                                goto gm_ll74;
                            }
                            goto ix90;
ie90:;
ix90:;
                        }
                        goto me79;
mn80:;
                        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 133, 9);
me79:;
                    }
lc70:;
                    {
                        uint64_t t102 = gm_l9;
                        uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                        uint64_t t105 = r103;
                        gm_l9 = t105;
                    }
                }
gm_ll74:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                }
                goto gm_ll55;
lb70:;
            }
        }
        {
            uint8_t t106 = 0;
            F.ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t106;
            goto gm_ll55;
        }
        goto gm_ls107;
gm_ll55:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we54; }
        goto gm_ld1_1;
gm_ls107:;
we54:;
    }
    {
        void * t108 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t108);
    }
    goto gm_ls109;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t110 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t110);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls109:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_tok_qap88op(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_tok_qap88op F;
    gm_b16_8 gm_l0; /* items */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* k */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            uint64_t t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        uint64_t t17 = gm_l4;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srceoep1gquqdsq, 231, 23);
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t18 * 40) + 0));
                        if (t20 != ((uint8_t)0ULL)) goto ie16;
                        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t18 * 40) + 8));
                        gm_l5 = t21;
                        {
                            uint64_t t23 = gm_l5;
                            gm_tf5srh4mp4b3i t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t24), t23);
                            ((uint8_t *)&t24)[8] = 1;
                            uint8_t t25 = 0;
                            F.ret = t24;
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 230, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_gap_tkq0f9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_scan_gap_tkq0f9l F;
    gm_b16_8 gm_l0; /* gap */
    void * gm_l1; /* items */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* ws_start */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* end */
    int32_t gm_l6; /* depth */
    uint64_t gm_l7; /* j */
    GM_STACK_CHECK(&F, 1097u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srceoep1gquqdsq, 32, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l4 = t11;
            }
            {
                uint8_t t13 = gm_l4;
                uint8_t t14 = ((t13 == ((uint8_t)10ULL)));
                if (!t14) goto ie12;
                {
                    void * t16 = gm_l1;
                    gm_tj63kbqqj2092 t17;
                    memset(&t17, 0, sizeof t17);
                    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)2ULL));
                    gm_tftb1odc739t5 t18 = gm_f_push_1e500nc_4(t16, &t17);
                    if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srceoep1gquqdsq, 34, 13);
                    gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
                }
                {
                    uint64_t t20 = gm_l2;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    if (o22) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 35, 13);
                    uint64_t t23 = r21;
                    gm_l2 = t23;
                }
                {
                    uint64_t t24 = gm_l2;
                    gm_l3 = t24;
                }
                goto ix12;
ie12:;
                uint8_t t26 = gm_l4;
                uint8_t t27 = ((t26 == ((uint8_t)59ULL)));
                if (!t27) goto ie25;
                {
                    void * t29 = gm_l1;
                    gm_tj63kbqqj2092 t30;
                    memset(&t30, 0, sizeof t30);
                    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
                    gm_tftb1odc739t5 t31 = gm_f_push_1e500nc_4(t29, &t30);
                    if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srceoep1gquqdsq, 38, 13);
                    gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                }
                {
                    uint64_t t33 = gm_l2;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    if (o35) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 39, 13);
                    uint64_t t36 = r34;
                    gm_l2 = t36;
                }
                {
                    uint64_t t37 = gm_l2;
                    gm_l3 = t37;
                }
                goto ix25;
ie25:;
                uint8_t t39 = gm_l4;
                uint8_t t40 = ((t39 == ((uint8_t)47ULL)));
                if (!t40) goto ie38;
                gm_b16_8 t41 = gm_l0;
                void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t41)));
                uint64_t t43 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t41) + 8);
                uint64_t t44 = gm_l2;
                uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                if (o46) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 41, 40);
                uint64_t t47 = r45;
                uint64_t t48 = (uintptr_t)t47;
                gm_tqs5p4n2tq19v t49;
                if (t48 < t43) {
                uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)t42 + t48 * 1));
                gm_tqs5p4n2tq19v t51;
                GM_ST(uint8_t, ((uint8_t *)&t51), t50);
                ((uint8_t *)&t51)[1] = 1;
                    t49 = t51;
                } else {
                gm_tqs5p4n2tq19v t52;
                memset(&t52, 0, sizeof t52);
                    t49 = t52;
                }
                gm_tqs5p4n2tq19v t53;
                GM_ST(uint8_t, ((uint8_t *)&t53), ((uint8_t)47ULL));
                ((uint8_t *)&t53)[1] = 1;
                uint8_t t54 = (gm_eqb44cb3cf1n(((uint8_t *)&t49), ((uint8_t *)&t53)));
                if (!t54) goto ie38;
                {
                    gm_b16_8 t56 = gm_l0;
                    uint64_t t57 = gm_l2;
                    uint64_t t58 = gm_f_line_end_jm825k5(t56, t57);
                    gm_l5 = t58;
                }
                {
                    void * t59 = gm_l1;
                    gm_t6j6smj7qoctu t60;
                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t62 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t63 = gm_l2;
                    uint64_t t64 = (uintptr_t)t63;
                    uint64_t t65 = gm_l5;
                    uint64_t t66 = (uintptr_t)t65;
                    if (t64 > t66 || t66 > (uintptr_t)t61) gm_trap_slice(t64, t66, t61, gm_srceoep1gquqdsq, 43, 54);
                    gm_b16_8 t67;
                    GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t62 + t64 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66 - t64));
                    gm_b16_8 t68 = gm_f_trim_end_d3d35bq(t67);
                    GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t68);
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t71 = gm_l3;
                    uint64_t t72 = (uintptr_t)t71;
                    uint64_t t73 = gm_l2;
                    uint64_t t74 = (uintptr_t)t73;
                    if (t72 > t74 || t74 > (uintptr_t)t69) gm_trap_slice(t72, t74, t69, gm_srceoep1gquqdsq, 43, 72);
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(((uint8_t *)t70 + t72 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(t74 - t72));
                    GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t75);
                    gm_tj63kbqqj2092 t76;
                    GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t76 + 8), t60);
                    gm_tftb1odc739t5 t77 = gm_f_push_1e500nc_4(t59, &t76);
                    if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srceoep1gquqdsq, 43, 13);
                    gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                }
                {
                    uint64_t t79 = gm_l5;
                    gm_l2 = t79;
                }
                {
                    uint64_t t80 = gm_l2;
                    gm_l3 = t80;
                }
                goto ix38;
ie38:;
                uint8_t t82 = gm_l4;
                uint8_t t83 = ((t82 == ((uint8_t)47ULL)));
                if (!t83) goto ie81;
                gm_b16_8 t84 = gm_l0;
                void * t85 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t84)));
                uint64_t t86 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t84) + 8);
                uint64_t t87 = gm_l2;
                uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                if (o89) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 46, 40);
                uint64_t t90 = r88;
                uint64_t t91 = (uintptr_t)t90;
                gm_tqs5p4n2tq19v t92;
                if (t91 < t86) {
                uint8_t t93 = GM_LD(uint8_t, ((uint8_t *)t85 + t91 * 1));
                gm_tqs5p4n2tq19v t94;
                GM_ST(uint8_t, ((uint8_t *)&t94), t93);
                ((uint8_t *)&t94)[1] = 1;
                    t92 = t94;
                } else {
                gm_tqs5p4n2tq19v t95;
                memset(&t95, 0, sizeof t95);
                    t92 = t95;
                }
                gm_tqs5p4n2tq19v t96;
                GM_ST(uint8_t, ((uint8_t *)&t96), ((uint8_t)42ULL));
                ((uint8_t *)&t96)[1] = 1;
                uint8_t t97 = (gm_eqb44cb3cf1n(((uint8_t *)&t92), ((uint8_t *)&t96)));
                if (!t97) goto ie81;
                {
                    gm_l6 = ((int32_t)1LL);
                }
                {
                    uint64_t t99 = gm_l2;
                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)2ULL), &r100);
                    if (o101) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 48, 18);
                    uint64_t t102 = r100;
                    gm_l7 = t102;
                }
                {
                    for (;;) {
                        int32_t t104 = gm_l6;
                        uint8_t t105 = (t104 > ((int32_t)0LL));
                        if (!t105) goto lb103;
                        uint64_t t106 = gm_l7;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t108 = (t106 < t107);
                        if (!t108) goto lb103;
                        {
                            uint64_t t112 = gm_l7;
                            uint64_t t113 = (uintptr_t)t112;
                            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srceoep1gquqdsq, 50, 20);
                            uint8_t t115 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t113 * 1));
                            uint8_t t116 = ((t115 == ((uint8_t)47ULL)));
                            if (!t116) goto ie111;
                            gm_b16_8 t117 = gm_l0;
                            void * t118 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t117)));
                            uint64_t t119 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t117) + 8);
                            uint64_t t120 = gm_l7;
                            uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                            if (o122) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 50, 46);
                            uint64_t t123 = r121;
                            uint64_t t124 = (uintptr_t)t123;
                            gm_tqs5p4n2tq19v t125;
                            if (t124 < t119) {
                            uint8_t t126 = GM_LD(uint8_t, ((uint8_t *)t118 + t124 * 1));
                            gm_tqs5p4n2tq19v t127;
                            GM_ST(uint8_t, ((uint8_t *)&t127), t126);
                            ((uint8_t *)&t127)[1] = 1;
                                t125 = t127;
                            } else {
                            gm_tqs5p4n2tq19v t128;
                            memset(&t128, 0, sizeof t128);
                                t125 = t128;
                            }
                            gm_tqs5p4n2tq19v t129;
                            GM_ST(uint8_t, ((uint8_t *)&t129), ((uint8_t)42ULL));
                            ((uint8_t *)&t129)[1] = 1;
                            uint8_t t130 = (gm_eqb44cb3cf1n(((uint8_t *)&t125), ((uint8_t *)&t129)));
                            if (!t130) goto ie111;
                            {
                                int32_t t132 = gm_l6;
                                int32_t r133; bool o134 = __builtin_add_overflow(t132, ((int32_t)1LL), &r133);
                                if (o134) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 51, 21);
                                int32_t t135 = r133;
                                gm_l6 = t135;
                            }
                            {
                                uint64_t t136 = gm_l7;
                                uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)2ULL), &r137);
                                if (o138) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 52, 21);
                                uint64_t t139 = r137;
                                gm_l7 = t139;
                            }
                            goto ix111;
ie111:;
                            uint64_t t141 = gm_l7;
                            uint64_t t142 = (uintptr_t)t141;
                            uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t142 >= (uintptr_t)(t143)) gm_trap_index(t142, t143, gm_srceoep1gquqdsq, 53, 27);
                            uint8_t t144 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t142 * 1));
                            uint8_t t145 = ((t144 == ((uint8_t)42ULL)));
                            if (!t145) goto ie140;
                            gm_b16_8 t146 = gm_l0;
                            void * t147 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t146)));
                            uint64_t t148 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t146) + 8);
                            uint64_t t149 = gm_l7;
                            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                            if (o151) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 53, 53);
                            uint64_t t152 = r150;
                            uint64_t t153 = (uintptr_t)t152;
                            gm_tqs5p4n2tq19v t154;
                            if (t153 < t148) {
                            uint8_t t155 = GM_LD(uint8_t, ((uint8_t *)t147 + t153 * 1));
                            gm_tqs5p4n2tq19v t156;
                            GM_ST(uint8_t, ((uint8_t *)&t156), t155);
                            ((uint8_t *)&t156)[1] = 1;
                                t154 = t156;
                            } else {
                            gm_tqs5p4n2tq19v t157;
                            memset(&t157, 0, sizeof t157);
                                t154 = t157;
                            }
                            gm_tqs5p4n2tq19v t158;
                            GM_ST(uint8_t, ((uint8_t *)&t158), ((uint8_t)47ULL));
                            ((uint8_t *)&t158)[1] = 1;
                            uint8_t t159 = (gm_eqb44cb3cf1n(((uint8_t *)&t154), ((uint8_t *)&t158)));
                            if (!t159) goto ie140;
                            {
                                int32_t t161 = gm_l6;
                                int32_t r162; bool o163 = __builtin_sub_overflow(t161, ((int32_t)1LL), &r162);
                                if (o163) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 54, 21);
                                int32_t t164 = r162;
                                gm_l6 = t164;
                            }
                            {
                                uint64_t t165 = gm_l7;
                                uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)2ULL), &r166);
                                if (o167) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 55, 21);
                                uint64_t t168 = r166;
                                gm_l7 = t168;
                            }
                            goto ix140;
ie140:;
                            {
                                uint64_t t170 = gm_l7;
                                uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                                if (o172) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 57, 21);
                                uint64_t t173 = r171;
                                gm_l7 = t173;
                            }
ix140:;
ix111:;
                        }
lc103:;
                    }
lb103:;
                }
                {
                    void * t174 = gm_l1;
                    uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t176 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t177 = gm_l2;
                    uint64_t t178 = (uintptr_t)t177;
                    uint64_t t179 = gm_l7;
                    uint64_t t180 = (uintptr_t)t179;
                    if (t178 > t180 || t180 > (uintptr_t)t175) gm_trap_slice(t178, t180, t175, gm_srceoep1gquqdsq, 60, 31);
                    gm_b16_8 t181;
                    GM_ST(void *, (uint8_t *)&t181, (void *)(((uint8_t *)t176 + t178 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(t180 - t178));
                    gm_tj63kbqqj2092 t182;
                    GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t182 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t182 + 32, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t182 + 8), t181);
                    gm_tftb1odc739t5 t183 = gm_f_push_1e500nc_4(t174, &t182);
                    if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 1, gm_srceoep1gquqdsq, 60, 13);
                    gm_b0_1 t184 = GM_LD(gm_b0_1, (((uint8_t *)&t183) + 1));
                }
                {
                    uint64_t t185 = gm_l7;
                    gm_l2 = t185;
                }
                {
                    uint64_t t186 = gm_l2;
                    gm_l3 = t186;
                }
                goto ix81;
ie81:;
                {
                    uint64_t t188 = gm_l2;
                    uint64_t r189; bool o190 = __builtin_add_overflow(t188, ((uint64_t)1ULL), &r189);
                    if (o190) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 64, 13);
                    uint64_t t191 = r189;
                    gm_l2 = t191;
                }
ix81:;
ix38:;
ix25:;
ix12:;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_would_merge_44g1m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_would_merge_44g1m96 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    void * gm_l2; /* cache */
    uint8_t gm_l3; /* x */
    uint8_t gm_l4; /* y */
    uint8_t gm_l5; /* known */
    uint8_t gm_l6; /* merges */
    GM_STACK_CHECK(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
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
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 83, 12);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srceoep1gquqdsq, 83, 10);
        uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
        gm_l3 = t16;
    }
    {
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srceoep1gquqdsq, 84, 10);
        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 1));
        gm_l4 = t19;
    }
    {
        gm_b0_1 t20;
    }
    {
        uint8_t t22 = 0;
        gm_b16_8 t23 = GM_LD(gm_b16_8, gm_c3dq62rrli9);
        uint8_t t24 = gm_l3;
        gm_tf5srh4mp4b3i t25 = gm_f_index_of_hdeg351(t23, t24);
        uint8_t t26;
        if (((((uint8_t *)&t25))[8] != 0)) goto mn28;
        t26 = ((uint8_t)0);
        goto me27;
mn28:;
        t26 = ((uint8_t)1);
        goto me27;
mn29:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 86, 8);
me27:;
        if (!t26) {
            gm_b16_8 t30 = GM_LD(gm_b16_8, gm_c3dq62rrli9);
            uint8_t t31 = gm_l4;
            gm_tf5srh4mp4b3i t32 = gm_f_index_of_hdeg351(t30, t31);
            uint8_t t33;
            if (((((uint8_t *)&t32))[8] != 0)) goto mn35;
            t33 = ((uint8_t)0);
            goto me34;
mn35:;
            t33 = ((uint8_t)1);
            goto me34;
mn36:;
            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 86, 40);
me34:;
            t22 = t33;
        } else t22 = 1;
        if (!t22) goto ie21;
        {
            uint8_t t38 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t40 = gm_l2;
        gm_t6j6smj7qoctu t41;
        gm_b16_8 t42 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t42);
        gm_b16_8 t43 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t43);
        gm_tqs5p4n2tq19v t44 = gm_f_get_t1u93iu(t40, &t41);
        if (!((((uint8_t *)&t44))[1] != 0)) goto ie39;
        uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&t44));
        gm_l5 = t45;
        {
            uint8_t t47 = gm_l5;
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        gm_b16_8 t49 = gm_l0;
        gm_b16_8 t50 = gm_l1;
        uint8_t t51 = gm_f_lexes_apart_9scqa7n((void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t51;
    }
    {
        void * t52 = gm_l2;
        gm_t6j6smj7qoctu t53;
        gm_b16_8 t54 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t53 + 0, t54);
        gm_b16_8 t55 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t53 + 16, t55);
        uint8_t t56 = gm_l6;
        gm_tftb1odc739t5 t57 = gm_f_put_fmu1emq(t52, &t53, t56);
        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srceoep1gquqdsq, 89, 5);
        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
    }
    {
        uint8_t t59 = gm_l6;
        uint8_t t60 = 0;
        F.ret = t59;
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
