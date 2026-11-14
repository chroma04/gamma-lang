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
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; } gm_tb1msgo1fu1n3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t4kmic8080rrj;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1msgo1fu1n3) == 5 && _Alignof(gm_tb1msgo1fu1n3) == 1 && offsetof(gm_tb1msgo1fu1n3, f0) == 0 && offsetof(gm_tb1msgo1fu1n3, f1) == 1 && offsetof(gm_tb1msgo1fu1n3, f2) == 2 && offsetof(gm_tb1msgo1fu1n3, f3) == 3 && offsetof(gm_tb1msgo1fu1n3, f4) == 4, "layout of `(u8, u8, u8, u8, u8)`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; gm_tqs5p4n2tq19v f2; gm_tqs5p4n2tq19v f3; gm_tqs5p4n2tq19v f4; } gm_tcdtdmenq84v9;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_trqfvmjtt4sij v1; } f1; } gm_t21m1eu5ta01n;
_Static_assert(sizeof(gm_t4kmic8080rrj) == 40 && _Alignof(gm_t4kmic8080rrj) == 8 && offsetof(gm_t4kmic8080rrj, f0) == 0 && offsetof(gm_t4kmic8080rrj, f1) == 16 && offsetof(gm_t4kmic8080rrj, f2) == 24 && offsetof(gm_t4kmic8080rrj, f3) == 32, "layout of `Reader`");
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tcdtdmenq84v9) == 10 && _Alignof(gm_tcdtdmenq84v9) == 1 && offsetof(gm_tcdtdmenq84v9, f0) == 0 && offsetof(gm_tcdtdmenq84v9, f1) == 2 && offsetof(gm_tcdtdmenq84v9, f2) == 4 && offsetof(gm_tcdtdmenq84v9, f3) == 6 && offsetof(gm_tcdtdmenq84v9, f4) == 8, "layout of `(?u8, ?u8, ?u8, ?u8, ?u8)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; } f1; } gm_tr1k5nl69lhjo;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_b16_8 v1; } f1; } gm_tu615l4qlr8b2;
_Static_assert(sizeof(gm_t21m1eu5ta01n) == 48 && _Alignof(gm_t21m1eu5ta01n) == 8 && offsetof(gm_t21m1eu5ta01n, f0) == 0 && offsetof(gm_t21m1eu5ta01n, f1) == 8, "layout of `TomlError!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; gm_tktr4rmbindgv v1; } f1; } gm_tefknldmnh2si;
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr1k5nl69lhjo) == 40 && _Alignof(gm_tr1k5nl69lhjo) == 8 && offsetof(gm_tr1k5nl69lhjo, f0) == 0 && offsetof(gm_tr1k5nl69lhjo, f1) == 8, "layout of `TomlError!()`");
typedef struct { gm_b16_8 f0; gm_tktr4rmbindgv f1; uint64_t f2; } gm_trc3jdv2g5qre;
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tu615l4qlr8b2) == 40 && _Alignof(gm_tu615l4qlr8b2) == 8 && offsetof(gm_tu615l4qlr8b2, f0) == 0 && offsetof(gm_tu615l4qlr8b2, f1) == 8, "layout of `TomlError!string`");
_Static_assert(sizeof(gm_tefknldmnh2si) == 56 && _Alignof(gm_tefknldmnh2si) == 8 && offsetof(gm_tefknldmnh2si, f0) == 0 && offsetof(gm_tefknldmnh2si, f1) == 8, "layout of `TomlError!Value`");
_Static_assert(sizeof(gm_trc3jdv2g5qre) == 72 && _Alignof(gm_trc3jdv2g5qre) == 8 && offsetof(gm_trc3jdv2g5qre, f0) == 0 && offsetof(gm_trc3jdv2g5qre, f1) == 16 && offsetof(gm_trc3jdv2g5qre, f2) == 64, "layout of `Entry`");
struct gm_fr_array_canpt3e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tefknldmnh2si ret;
};
struct gm_fr_as_str_982o16v {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_at_line_6uoncqb {
    gm_frame h;
    gm_tob1snuii5bme ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mja1o69 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_of_line_056kej4 {
    gm_frame h;
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_error_96ci5lu {
    gm_frame h;
    gm_tob1snuii5bme ret;
};
struct gm_fr_get_9rme0gb {
    gm_frame h;
    void * ret;
};
struct gm_fr_hex_escape_eoc96dk {
    gm_frame h;
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_inline_table_cvvpgel {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* path */
    gm_tktr4rmbindgv l4; /* v */
    gm_tefknldmnh2si ret;
};
struct gm_fr_insert_path_cvuqfkc {
    gm_frame h;
    gm_tktr4rmbindgv l2; /* v */
    gm_tr1k5nl69lhjo ret;
};
struct gm_fr_key_5clrcin {
    gm_frame h;
    gm_tu615l4qlr8b2 ret;
};
struct gm_fr_key_path_7spb050 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* path */
    gm_t21m1eu5ta01n ret;
};
struct gm_fr_list_0202sdh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vvcbrjf {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_table_hh6cki7 {
    gm_frame h;
    void * ret;
};
struct gm_fr_number_4l95pjb {
    gm_frame h;
    gm_tefknldmnh2si ret;
};
struct gm_fr_parse_string_80niubc {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_tu615l4qlr8b2 ret;
};
struct gm_fr_parse_toml_gjtmf79 {
    gm_frame h;
    gm_t4kmic8080rrj l1; /* r */
    gm_trqfvmjtt4sij l6; /* path */
    gm_tktr4rmbindgv l7; /* v */
    gm_trssmtuv8jr0v ret;
};
struct gm_fr_peek_bkdvl65 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_push_0pgacsj_2 {
    gm_frame h;
    gm_tktr4rmbindgv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_2pp1ri6 {
    gm_frame h;
    gm_trc3jdv2g5qre l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2o1229b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ddqa9t6_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_skip_space_and_newlines_qagb1bk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_skip_space_voq272o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_table_header_j60mb6n {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* path */
    gm_trqfvmjtt4sij l13; /* items */
    gm_trqfvmjtt4sij l15; /* grown */
    gm_trqfvmjtt4sij l17; /* items */
    gm_trqfvmjtt4sij l20; /* items */
    gm_trssmtuv8jr0v ret;
};
struct gm_fr_value_oq952o2 {
    gm_frame h;
    gm_tefknldmnh2si ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_array_canpt3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_982o16v_c(gm_tktr4rmbindgv a0);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g_c(gm_tktr4rmbindgv a0);
inline __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_at_line_6uoncqb_c(gm_tob1snuii5bme a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mja1o69(void);
__attribute__((aligned(4))) void gm_f_end_of_line_056kej4(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tr1k5nl69lhjo gm_f_end_of_line_056kej4_c(void * a0);
__attribute__((aligned(4))) void gm_f_error_96ci5lu(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_error_96ci5lu_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_0pvgsic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u10ruvh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hex_escape_eoc96dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_inline_table_cvvpgel(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_insert_path_cvuqfkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_key_5clrcin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_key_path_7spb050(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vvcbrjf(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_9ani70g(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_number_4l95pjb(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tefknldmnh2si gm_f_number_4l95pjb_c(void * a0);
__attribute__((aligned(4))) void gm_f_parse_string_80niubc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2pp1ri6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2o1229b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_skip_space_and_newlines_qagb1bk(void * a0);
__attribute__((aligned(4))) void gm_f_skip_space_voq272o(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_table_header_j60mb6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_value_oq952o2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_cal7njs7es4[16] __attribute__((aligned(8)));
extern uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
extern uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
extern const uint8_t gm_s2a4d8jj74v6o[49];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s70iivik802q4[26];
extern const uint8_t gm_s8lbhmq10rt7i[42];
extern const uint8_t gm_s8rm42n03sfss[4];
extern const uint8_t gm_scvuqmspcpb1m[6];
extern const uint8_t gm_sdq87al6fhs9i[35];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_shlilv0hq3bqu[33];
extern const uint8_t gm_sithisfj5qgfv[67];
extern const uint8_t gm_sj294dvmirbmr[47];
extern const uint8_t gm_skiffh4urhl7e[14];
extern const uint8_t gm_sl2lo3hki3t4d[36];
extern const uint8_t gm_sne7l93pr2tmb[34];
extern const uint8_t gm_sngfc91643s6a[55];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_soakgb2h3g40i[4];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_spl83f693c752[32];
extern const uint8_t gm_sq5stv2m4p0h1[37];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srjddrdnspo96[29];
extern const uint8_t gm_srsr8fvlin2g6[65];
extern const uint8_t gm_std877jr2ed05[29];
extern const uint8_t gm_suonmnnb7u0t1[55];
extern const uint8_t gm_sv5o4caljvbmj[28];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_982o16v F;
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 50, 51);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_line_6uoncqb F;
    gm_tob1snuii5bme gm_l0; /* e */
    uint64_t gm_l1; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 0, t1);
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 8, t2);
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a0) + 24));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mja1o69(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mja1o69 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi_2();
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
inline __attribute__((aligned(4))) void gm_f_list_vvcbrjf(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vvcbrjf F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_mja1o69();
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
inline __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_table_hh6cki7 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tqetrgo0q24b5 t1;
    memset((uint8_t *)&t1 + 41, 0, 7);
    gm_f_list_vvcbrjf((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t1 + 0)), (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&t1 + 40, ((uint8_t)0));
    gm_tq7s98ihonkju t2 = gm_f_new_9ani70g((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kt2nhjs8rv5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_src3nt8pf0qk5gn, 39, 44);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_bkdvl65 F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
    uint64_t t7 = (uintptr_t)t6;
    gm_tqs5p4n2tq19v t8;
    if (t7 < t4) {
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)t3 + t7 * 1));
    gm_tqs5p4n2tq19v t10;
    GM_ST(uint8_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[1] = 1;
        t8 = t10;
    } else {
    gm_tqs5p4n2tq19v t11;
    memset(&t11, 0, sizeof t11);
        t8 = t11;
    }
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_dheabhi_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ddqa9t6_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tktr4rmbindgv t21 = F.l1;
        GM_ST(gm_tktr4rmbindgv, ((uint8_t *)t20 + t18 * 48), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2pp1ri6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2pp1ri6 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_mja1o69();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2o1229b(t10, ((uint64_t)1ULL), t12);
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
        gm_trc3jdv2g5qre t21 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)a1));
        GM_ST(gm_trc3jdv2g5qre, ((uint8_t *)t20 + t18 * 72), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 72)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2o1229b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2o1229b F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)72ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_2 F;
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
__attribute__((aligned(4))) void gm_f_array_canpt3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_array_canpt3e F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* first_line */
    uint64_t gm_l3; /* before */
    GM_STACK_CHECK(&F, 862u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 473, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 16), t9);
    }
    {
        gm_f_list_0202sdh_2((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                void * t13 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t13);
            }
            {
                void * t15 = gm_l0;
                gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)93ULL));
                ((uint8_t *)&t17)[1] = 1;
                uint8_t t18 = (gm_eqb44cb3cf1n(((uint8_t *)&t16), ((uint8_t *)&t17)));
                if (!t18) goto ie14;
                {
                    goto lb10;
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                void * t21 = gm_l0;
                gm_tqs5p4n2tq19v t22 = gm_f_peek_bkdvl65(t21);
                uint8_t t23;
                if (((((uint8_t *)&t22))[1] != 0)) goto mn25;
                t23 = ((uint8_t)1);
                goto me24;
mn25:;
                t23 = ((uint8_t)0);
                goto me24;
mn26:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 478, 12);
me24:;
                if (!t23) goto ie20;
                {
                    void * t28 = gm_l0;
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_srjddrdnspo96));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(28));
                    gm_tob1snuii5bme t30;
                    gm_f_error_96ci5lu((void *)&t30, t28, t29);
                    uint64_t t31 = gm_l1;
                    gm_tob1snuii5bme t32;
                    gm_f_at_line_6uoncqb((void *)&t32, &t30, t31);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t32);
                    goto gm_fdone;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                void * t34 = gm_l0;
                gm_tqs5p4n2tq19v t35 = gm_f_peek_bkdvl65(t34);
                gm_tqs5p4n2tq19v t36;
                GM_ST(uint8_t, ((uint8_t *)&t36), ((uint8_t)44ULL));
                ((uint8_t *)&t36)[1] = 1;
                uint8_t t37 = (gm_eqb44cb3cf1n(((uint8_t *)&t35), ((uint8_t *)&t36)));
                if (!t37) goto ie33;
                {
                    void * t39 = gm_l0;
                    gm_b16_8 t40;
                    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sq5stv2m4p0h1));
                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(36));
                    gm_tob1snuii5bme t41;
                    gm_f_error_96ci5lu((void *)&t41, t39, t40);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t41);
                    goto gm_fdone;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                void * t42 = (void *)((uint8_t *)&F.l2);
                void * t43 = gm_l0;
                gm_tefknldmnh2si t44;
                gm_f_value_oq952o2((void *)&t44, (void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (((((uint8_t *)&t44))[0] != 0)) {
                    gm_tefknldmnh2si t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t45 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t45 + 48, 0);
                    ((uint8_t *)&t45)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t45 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t44) + 8)));
                    F.ret = t45;
                    goto gm_fret;
                }
                gm_tftb1odc739t5 t46 = gm_f_push_0pgacsj_2(t42, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&t44) + 8)));
                if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_src3nt8pf0qk5gn, 480, 9);
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
            }
            {
                void * t48 = gm_l0;
                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 16));
                gm_l3 = t49;
            }
            {
                void * t50 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t50);
            }
            {
                void * t52 = gm_l0;
                gm_tqs5p4n2tq19v t53 = gm_f_peek_bkdvl65(t52);
                gm_tqs5p4n2tq19v t54;
                GM_ST(uint8_t, ((uint8_t *)&t54), ((uint8_t)44ULL));
                ((uint8_t *)&t54)[1] = 1;
                uint8_t t55 = (gm_eqb44cb3cf1n(((uint8_t *)&t53), ((uint8_t *)&t54)));
                if (!t55) goto ie51;
                {
                    void * t57 = gm_l0;
                    void * t58 = gm_l0;
                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 16));
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    if (o61) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 484, 13);
                    uint64_t t62 = r60;
                    GM_ST(uint64_t, (((uint8_t *)t57) + 16), t62);
                }
                goto ix51;
ie51:;
                void * t64 = gm_l0;
                gm_tqs5p4n2tq19v t65 = gm_f_peek_bkdvl65(t64);
                gm_tqs5p4n2tq19v t66;
                GM_ST(uint8_t, ((uint8_t *)&t66), ((uint8_t)93ULL));
                ((uint8_t *)&t66)[1] = 1;
                uint8_t t67 = (!gm_eqb44cb3cf1n(((uint8_t *)&t65), ((uint8_t *)&t66)));
                if (!t67) goto ie63;
                {
                    void * t70 = gm_l0;
                    gm_tqs5p4n2tq19v t71 = gm_f_peek_bkdvl65(t70);
                    uint8_t t72;
                    if (((((uint8_t *)&t71))[1] != 0)) goto mn74;
                    t72 = ((uint8_t)1);
                    goto me73;
mn74:;
                    t72 = ((uint8_t)0);
                    goto me73;
mn75:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 486, 16);
me73:;
                    if (!t72) goto ie69;
                    {
                        void * t77 = gm_l0;
                        gm_b16_8 t78;
                        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_srjddrdnspo96));
                        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(28));
                        gm_tob1snuii5bme t79;
                        gm_f_error_96ci5lu((void *)&t79, t77, t78);
                        uint64_t t80 = gm_l1;
                        gm_tob1snuii5bme t81;
                        gm_f_at_line_6uoncqb((void *)&t81, &t79, t80);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t81);
                        goto gm_fdone;
                    }
                    goto ix69;
ie69:;
ix69:;
                }
                {
                    void * t83 = gm_l0;
                    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)t83) + 32));
                    if (!t84) goto ie82;
                    void * t85 = gm_l0;
                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 16));
                    uint64_t t87 = gm_l3;
                    uint8_t t88 = (t86 > t87);
                    if (!t88) goto ie82;
                    {
                        void * t90 = gm_l0;
                        gm_b16_8 t91 = GM_LD(gm_b16_8, gm_cal7njs7es4);
                        gm_tob1snuii5bme t92;
                        gm_f_error_96ci5lu((void *)&t92, t90, t91);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t92);
                        goto gm_fdone;
                    }
                    goto ix82;
ie82:;
ix82:;
                }
                {
                    void * t93 = gm_l0;
                    gm_b16_8 t94;
                    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_srjddrdnspo96));
                    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(28));
                    gm_tob1snuii5bme t95;
                    gm_f_error_96ci5lu((void *)&t95, t93, t94);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t95);
                    goto gm_fdone;
                }
                goto ix63;
ie63:;
ix63:;
ix51:;
            }
lc10:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
        }
        goto gm_lad;
lb10:;
    }
    {
        void * t96 = gm_l0;
        void * t97 = gm_l0;
        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 16));
        uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
        if (o100) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 491, 5);
        uint64_t t101 = r99;
        GM_ST(uint64_t, (((uint8_t *)t96) + 16), t101);
    }
    {
        void * t102 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t102) + 32), ((uint8_t)0));
    }
    {
        gm_trqfvmjtt4sij t103 = F.l2;
        gm_tktr4rmbindgv t104;
        GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)3ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t104 + 8), t103);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t104);
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
extern __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_982o16v_c(gm_tktr4rmbindgv a0) { gm_th2lqbo77pvsc gm_r; gm_f_as_str_982o16v(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g_c(gm_tktr4rmbindgv a0) { return gm_f_as_table_4f7n31g(&a0); }
extern __attribute__((aligned(4))) void gm_f_at_line_6uoncqb(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_at_line_6uoncqb_c(gm_tob1snuii5bme a0, uint64_t a1) { gm_tob1snuii5bme gm_r; gm_f_at_line_6uoncqb(&gm_r, &a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_end_of_line_056kej4(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_end_of_line_056kej4 F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_f_skip_space_voq272o(t4);
    }
    {
        void * t6 = gm_l0;
        gm_tqs5p4n2tq19v t7 = gm_f_peek_bkdvl65(t6);
        gm_tqs5p4n2tq19v t8;
        GM_ST(uint8_t, ((uint8_t *)&t8), ((uint8_t)35ULL));
        ((uint8_t *)&t8)[1] = 1;
        uint8_t t9 = (gm_eqb44cb3cf1n(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie5;
        {
            for (;;) {
                void * t12 = gm_l0;
                gm_tqs5p4n2tq19v t13 = gm_f_peek_bkdvl65(t12);
                uint8_t t14;
                if (((((uint8_t *)&t13))[1] != 0)) goto mn16;
                t14 = ((uint8_t)0);
                goto me15;
mn16:;
                t14 = ((uint8_t)1);
                goto me15;
mn17:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 135, 15);
me15:;
                if (!t14) goto lb11;
                void * t18 = gm_l0;
                gm_tqs5p4n2tq19v t19 = gm_f_peek_bkdvl65(t18);
                gm_tqs5p4n2tq19v t20;
                GM_ST(uint8_t, ((uint8_t *)&t20), ((uint8_t)10ULL));
                ((uint8_t *)&t20)[1] = 1;
                uint8_t t21 = (!gm_eqb44cb3cf1n(((uint8_t *)&t19), ((uint8_t *)&t20)));
                if (!t21) goto lb11;
                {
                    void * t24 = gm_l0;
                    void * t25 = gm_l0;
                    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 16));
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    if (o28) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 135, 63);
                    uint64_t t29 = r27;
                    GM_ST(uint64_t, (((uint8_t *)t24) + 16), t29);
                }
lc11:;
            }
lb11:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t31 = gm_l0;
        gm_tqs5p4n2tq19v t32 = gm_f_peek_bkdvl65(t31);
        gm_tqs5p4n2tq19v t33;
        GM_ST(uint8_t, ((uint8_t *)&t33), ((uint8_t)13ULL));
        ((uint8_t *)&t33)[1] = 1;
        uint8_t t34 = (gm_eqb44cb3cf1n(((uint8_t *)&t32), ((uint8_t *)&t33)));
        if (!t34) goto ie30;
        {
            void * t36 = gm_l0;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 16));
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 137, 27);
            uint64_t t41 = r39;
            GM_ST(uint64_t, (((uint8_t *)t36) + 16), t41);
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t43 = gm_l0;
        gm_tqs5p4n2tq19v t44 = gm_f_peek_bkdvl65(t43);
        uint8_t t45;
        if (((((uint8_t *)&t44))[1] != 0)) goto mn47;
        t45 = ((uint8_t)1);
        goto me46;
mn47:;
        t45 = ((uint8_t)0);
        goto me46;
mn48:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 138, 8);
me46:;
        if (!t45) goto ie42;
        {
            gm_b0_1 t50;
            memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t50);
            goto gm_fdone;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t52 = gm_l0;
        gm_tqs5p4n2tq19v t53 = gm_f_peek_bkdvl65(t52);
        gm_tqs5p4n2tq19v t54;
        GM_ST(uint8_t, ((uint8_t *)&t54), ((uint8_t)10ULL));
        ((uint8_t *)&t54)[1] = 1;
        uint8_t t55 = (!gm_eqb44cb3cf1n(((uint8_t *)&t53), ((uint8_t *)&t54)));
        if (!t55) goto ie51;
        {
            void * t58 = gm_l0;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t58) + 32));
            if (!t59) goto ie57;
            void * t60 = gm_l0;
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 16));
            uint64_t t62 = gm_l1;
            uint8_t t63 = (t61 > t62);
            if (!t63) goto ie57;
            {
                void * t65 = gm_l0;
                gm_b16_8 t66 = GM_LD(gm_b16_8, gm_cal7njs7es4);
                gm_tob1snuii5bme t67;
                gm_f_error_96ci5lu((void *)&t67, t65, t66);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t67);
                goto gm_fdone;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t68 = gm_l0;
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sj294dvmirbmr));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(46));
            gm_tob1snuii5bme t70;
            gm_f_error_96ci5lu((void *)&t70, t68, t69);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t70);
            goto gm_fdone;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_b0_1 t71;
        memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t71);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tr1k5nl69lhjo gm_f_end_of_line_056kej4_c(void * a0) { gm_tr1k5nl69lhjo gm_r; gm_f_end_of_line_056kej4(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_error_96ci5lu(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_96ci5lu F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* message */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        gm_l3 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l3;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 99, 19);
                uint64_t t13 = r11;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src3nt8pf0qk5gn, 99, 12);
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 1));
                uint8_t t18 = ((t17 == ((uint8_t)10ULL)));
                if (!t18) goto ie9;
                {
                    uint64_t t20 = gm_l3;
                    gm_l2 = t20;
                }
                {
                    goto lb4;
                }
                goto ix9;
ie9:;
ix9:;
            }
            {
                uint64_t t21 = gm_l3;
                uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 103, 9);
                uint64_t t24 = r22;
                gm_l3 = t24;
            }
lc4:;
        }
lb4:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 24));
        gm_b16_8 t27 = gm_l1;
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
        uint64_t t30 = gm_l2;
        uint64_t r31; bool o32 = __builtin_sub_overflow(t29, t30, &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 105, 59);
        uint64_t t33 = r31;
        uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 105, 59);
        uint64_t t36 = r34;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 0, t26);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 8, t27);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tob1snuii5bme *)gm_out) + 24, t36);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tob1snuii5bme gm_f_error_96ci5lu_c(void * a0, gm_b16_8 a1) { gm_tob1snuii5bme gm_r; gm_f_error_96ci5lu(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_9rme0gb F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
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
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 0) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_src3nt8pf0qk5gn, 44, 12);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 0) + 0)) + t18 * 72) + 0));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie16;
                        {
                            uint64_t t25 = gm_l4;
                            uint64_t t26 = (uintptr_t)t25;
                            void * t27 = gm_l0;
                            uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 0) + 0) + 8);
                            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_src3nt8pf0qk5gn, 44, 52);
                            void * t29 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 0) + 0)) + t26 * 72);
                            uint8_t t30 = 0;
                            F.ret = t29;
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 43, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t36 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_hex_escape_eoc96dk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2) {
    struct gm_fr_hex_escape_eoc96dk F;
    void * gm_l0; /* r */
    void * gm_l1; /* out */
    uint64_t gm_l2; /* digits */
    uint32_t gm_l3; /* code */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* d */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* v */
    GM_STACK_CHECK(&F, 666u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint32_t)0ULL);
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = gm_l4;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    {
                        void * t14 = gm_l0;
                        gm_tqs5p4n2tq19v t15 = gm_f_peek_bkdvl65(t14);
                        uint8_t t16;
                        if (((((uint8_t *)&t15))[1] != 0)) {
                            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
                            t16 = t17;
                        } else {
                            void * t18 = gm_l0;
                            gm_b16_8 t19;
                            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_srsr8fvlin2g6));
                            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(64));
                            gm_tob1snuii5bme t20;
                            gm_f_error_96ci5lu((void *)&t20, t18, t19);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t20);
                            goto gm_fdone;
                        }
                        gm_l6 = t16;
                    }
                    {
                        uint32_t t21;
                        {
                            uint8_t t24 = gm_l6;
                            gm_l7 = t24;
                        }
                        uint8_t t25 = 0;
                        uint8_t t26 = gm_l7;
                        uint8_t t27 = (((uint8_t)48ULL) <= t26);
                        if (t27) {
                            uint8_t t28 = gm_l7;
                            uint8_t t29 = (t28 <= ((uint8_t)57ULL));
                            t25 = t29;
                        } else t25 = 0;
                        uint8_t t30 = t25;
                        if (!t30) goto ie22;
                        uint8_t t32 = gm_l6;
                        uint8_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint8_t)48ULL), &r33);
                        if (o34) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 437, 17);
                        uint8_t t35 = r33;
                        uint32_t t36 = (uint32_t)t35;
                        uint32_t t37 = t36;
                        t21 = t37;
                        goto ix22;
ie22:;
                        uint32_t t38;
                        {
                            uint8_t t41 = gm_l6;
                            gm_l8 = t41;
                        }
                        uint8_t t42 = 0;
                        uint8_t t43 = gm_l8;
                        uint8_t t44 = (((uint8_t)97ULL) <= t43);
                        if (t44) {
                            uint8_t t45 = gm_l8;
                            uint8_t t46 = (t45 <= ((uint8_t)102ULL));
                            t42 = t46;
                        } else t42 = 0;
                        uint8_t t47 = t42;
                        if (!t47) goto ie39;
                        uint8_t t49 = gm_l6;
                        uint8_t r50; bool o51 = __builtin_sub_overflow(t49, ((uint8_t)97ULL), &r50);
                        if (o51) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 439, 17);
                        uint8_t t52 = r50;
                        uint32_t t53 = (uint32_t)t52;
                        uint32_t r54; bool o55 = __builtin_add_overflow(t53, ((uint32_t)10ULL), &r54);
                        if (o55) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 439, 13);
                        uint32_t t56 = r54;
                        uint32_t t57 = t56;
                        t38 = t57;
                        goto ix39;
ie39:;
                        uint32_t t58;
                        {
                            uint8_t t61 = gm_l6;
                            gm_l9 = t61;
                        }
                        uint8_t t62 = 0;
                        uint8_t t63 = gm_l9;
                        uint8_t t64 = (((uint8_t)65ULL) <= t63);
                        if (t64) {
                            uint8_t t65 = gm_l9;
                            uint8_t t66 = (t65 <= ((uint8_t)70ULL));
                            t62 = t66;
                        } else t62 = 0;
                        uint8_t t67 = t62;
                        if (!t67) goto ie59;
                        uint8_t t69 = gm_l6;
                        uint8_t r70; bool o71 = __builtin_sub_overflow(t69, ((uint8_t)65ULL), &r70);
                        if (o71) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 441, 17);
                        uint8_t t72 = r70;
                        uint32_t t73 = (uint32_t)t72;
                        uint32_t r74; bool o75 = __builtin_add_overflow(t73, ((uint32_t)10ULL), &r74);
                        if (o75) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 441, 13);
                        uint32_t t76 = r74;
                        uint32_t t77 = t76;
                        t58 = t77;
                        goto ix59;
ie59:;
                        void * t79 = gm_l0;
                        gm_b16_8 t80;
                        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_srsr8fvlin2g6));
                        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(64));
                        gm_tob1snuii5bme t81;
                        gm_f_error_96ci5lu((void *)&t81, t79, t80);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t81);
                        goto gm_fdone;
                        t58 = ((uint32_t)0);
ix59:;
                        t38 = t58;
ix39:;
                        t21 = t38;
ix22:;
                        gm_l10 = t21;
                    }
                    {
                        uint32_t t82 = gm_l3;
                        uint32_t r83; bool o84 = __builtin_mul_overflow(t82, ((uint32_t)16ULL), &r83);
                        if (o84) gm_trap("integer overflow in `*`", gm_src3nt8pf0qk5gn, 445, 16);
                        uint32_t t85 = r83;
                        uint32_t t86 = gm_l10;
                        uint32_t r87; bool o88 = __builtin_add_overflow(t85, t86, &r87);
                        if (o88) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 445, 16);
                        uint32_t t89 = r87;
                        gm_l3 = t89;
                    }
                    {
                        void * t90 = gm_l0;
                        void * t91 = gm_l0;
                        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)t91) + 16));
                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                        if (o94) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 446, 9);
                        uint64_t t95 = r93;
                        GM_ST(uint64_t, (((uint8_t *)t90) + 16), t95);
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 434, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t97 = gm_l5;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                    uint64_t t100 = r98;
                    gm_l5 = t100;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t102 = 0;
        uint32_t t103 = gm_l3;
        uint8_t t104 = (t103 > ((uint32_t)1114111ULL));
        if (!t104) {
            uint8_t t105 = 0;
            uint32_t t106 = gm_l3;
            uint8_t t107 = (t106 >= ((uint32_t)55296ULL));
            if (t107) {
                uint32_t t108 = gm_l3;
                uint8_t t109 = (t108 < ((uint32_t)57344ULL));
                t105 = t109;
            } else t105 = 0;
            t102 = t105;
        } else t102 = 1;
        if (!t102) goto ie101;
        {
            void * t111 = gm_l0;
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sngfc91643s6a));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(54));
            gm_tob1snuii5bme t113;
            gm_f_error_96ci5lu((void *)&t113, t111, t112);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t113);
            goto gm_fdone;
        }
        goto ix101;
ie101:;
ix101:;
    }
    {
        uint32_t t115 = gm_l3;
        uint8_t t116 = (t115 < ((uint32_t)128ULL));
        if (!t116) goto ie114;
        {
            void * t118 = gm_l1;
            uint32_t t119 = gm_l3;
            uint8_t t120 = (uint8_t)t119;
            gm_tftb1odc739t5 t121 = gm_f_push_t83rg2m(t118, t120);
            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_src3nt8pf0qk5gn, 450, 9);
            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        }
        goto ix114;
ie114:;
        uint32_t t124 = gm_l3;
        uint8_t t125 = (t124 < ((uint32_t)2048ULL));
        if (!t125) goto ie123;
        {
            void * t127 = gm_l1;
            uint32_t t128 = gm_l3;
            unsigned __int128 n129 = (unsigned __int128)((uint64_t)6ULL);
            if (n129 >= 32) gm_trap_shift((uint64_t)n129, "u32", gm_src3nt8pf0qk5gn, 452, 27);
            uint32_t t130 = (uint32_t)(t128 >> (int)n129);
            uint32_t t131 = (uint32_t)(((uint32_t)192ULL) | t130);
            uint8_t t132 = (uint8_t)t131;
            gm_tftb1odc739t5 t133 = gm_f_push_t83rg2m(t127, t132);
            if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_src3nt8pf0qk5gn, 452, 9);
            gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
        }
        {
            void * t135 = gm_l1;
            uint32_t t136 = gm_l3;
            uint32_t t137 = (uint32_t)(t136 & ((uint32_t)63ULL));
            uint32_t t138 = (uint32_t)(((uint32_t)128ULL) | t137);
            uint8_t t139 = (uint8_t)t138;
            gm_tftb1odc739t5 t140 = gm_f_push_t83rg2m(t135, t139);
            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_src3nt8pf0qk5gn, 453, 9);
            gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
        }
        goto ix123;
ie123:;
        uint32_t t143 = gm_l3;
        uint8_t t144 = (t143 < ((uint32_t)65536ULL));
        if (!t144) goto ie142;
        {
            void * t146 = gm_l1;
            uint32_t t147 = gm_l3;
            unsigned __int128 n148 = (unsigned __int128)((uint64_t)12ULL);
            if (n148 >= 32) gm_trap_shift((uint64_t)n148, "u32", gm_src3nt8pf0qk5gn, 455, 27);
            uint32_t t149 = (uint32_t)(t147 >> (int)n148);
            uint32_t t150 = (uint32_t)(((uint32_t)224ULL) | t149);
            uint8_t t151 = (uint8_t)t150;
            gm_tftb1odc739t5 t152 = gm_f_push_t83rg2m(t146, t151);
            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_src3nt8pf0qk5gn, 455, 9);
            gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
        }
        {
            void * t154 = gm_l1;
            uint32_t t155 = gm_l3;
            unsigned __int128 n156 = (unsigned __int128)((uint64_t)6ULL);
            if (n156 >= 32) gm_trap_shift((uint64_t)n156, "u32", gm_src3nt8pf0qk5gn, 456, 28);
            uint32_t t157 = (uint32_t)(t155 >> (int)n156);
            uint32_t t158 = (uint32_t)(t157 & ((uint32_t)63ULL));
            uint32_t t159 = (uint32_t)(((uint32_t)128ULL) | t158);
            uint8_t t160 = (uint8_t)t159;
            gm_tftb1odc739t5 t161 = gm_f_push_t83rg2m(t154, t160);
            if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_src3nt8pf0qk5gn, 456, 9);
            gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
        }
        {
            void * t163 = gm_l1;
            uint32_t t164 = gm_l3;
            uint32_t t165 = (uint32_t)(t164 & ((uint32_t)63ULL));
            uint32_t t166 = (uint32_t)(((uint32_t)128ULL) | t165);
            uint8_t t167 = (uint8_t)t166;
            gm_tftb1odc739t5 t168 = gm_f_push_t83rg2m(t163, t167);
            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_src3nt8pf0qk5gn, 457, 9);
            gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
        }
        goto ix142;
ie142:;
        {
            void * t171 = gm_l1;
            uint32_t t172 = gm_l3;
            unsigned __int128 n173 = (unsigned __int128)((uint64_t)18ULL);
            if (n173 >= 32) gm_trap_shift((uint64_t)n173, "u32", gm_src3nt8pf0qk5gn, 459, 27);
            uint32_t t174 = (uint32_t)(t172 >> (int)n173);
            uint32_t t175 = (uint32_t)(((uint32_t)240ULL) | t174);
            uint8_t t176 = (uint8_t)t175;
            gm_tftb1odc739t5 t177 = gm_f_push_t83rg2m(t171, t176);
            if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 1, gm_src3nt8pf0qk5gn, 459, 9);
            gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 1));
        }
        {
            void * t179 = gm_l1;
            uint32_t t180 = gm_l3;
            unsigned __int128 n181 = (unsigned __int128)((uint64_t)12ULL);
            if (n181 >= 32) gm_trap_shift((uint64_t)n181, "u32", gm_src3nt8pf0qk5gn, 460, 28);
            uint32_t t182 = (uint32_t)(t180 >> (int)n181);
            uint32_t t183 = (uint32_t)(t182 & ((uint32_t)63ULL));
            uint32_t t184 = (uint32_t)(((uint32_t)128ULL) | t183);
            uint8_t t185 = (uint8_t)t184;
            gm_tftb1odc739t5 t186 = gm_f_push_t83rg2m(t179, t185);
            if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_src3nt8pf0qk5gn, 460, 9);
            gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
        }
        {
            void * t188 = gm_l1;
            uint32_t t189 = gm_l3;
            unsigned __int128 n190 = (unsigned __int128)((uint64_t)6ULL);
            if (n190 >= 32) gm_trap_shift((uint64_t)n190, "u32", gm_src3nt8pf0qk5gn, 461, 28);
            uint32_t t191 = (uint32_t)(t189 >> (int)n190);
            uint32_t t192 = (uint32_t)(t191 & ((uint32_t)63ULL));
            uint32_t t193 = (uint32_t)(((uint32_t)128ULL) | t192);
            uint8_t t194 = (uint8_t)t193;
            gm_tftb1odc739t5 t195 = gm_f_push_t83rg2m(t188, t194);
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_src3nt8pf0qk5gn, 461, 9);
            gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
        }
        {
            void * t197 = gm_l1;
            uint32_t t198 = gm_l3;
            uint32_t t199 = (uint32_t)(t198 & ((uint32_t)63ULL));
            uint32_t t200 = (uint32_t)(((uint32_t)128ULL) | t199);
            uint8_t t201 = (uint8_t)t200;
            gm_tftb1odc739t5 t202 = gm_f_push_t83rg2m(t197, t201);
            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 1, gm_src3nt8pf0qk5gn, 462, 9);
            gm_b0_1 t203 = GM_LD(gm_b0_1, (((uint8_t *)&t202) + 1));
        }
ix142:;
ix123:;
ix114:;
    }
    {
        gm_b0_1 t204;
        memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t204);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_inline_table_cvvpgel(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_inline_table_cvvpgel F;
    void * gm_l0; /* r */
    void * gm_l1; /* t */
    uint64_t gm_l2; /* line */
    GM_STACK_CHECK(&F, 954u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 498, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 16), t7);
    }
    {
        void * t8 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t8;
    }
    {
        void * t9 = gm_l1;
        GM_ST(uint8_t, (((uint8_t *)t9) + 40), ((uint8_t)1));
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                void * t13 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t13);
            }
            {
                void * t15 = gm_l0;
                gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)125ULL));
                ((uint8_t *)&t17)[1] = 1;
                uint8_t t18 = (gm_eqb44cb3cf1n(((uint8_t *)&t16), ((uint8_t *)&t17)));
                if (!t18) goto ie14;
                {
                    goto lb10;
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                void * t21 = gm_l0;
                gm_tqs5p4n2tq19v t22 = gm_f_peek_bkdvl65(t21);
                uint8_t t23;
                if (((((uint8_t *)&t22))[1] != 0)) goto mn25;
                t23 = ((uint8_t)1);
                goto me24;
mn25:;
                t23 = ((uint8_t)0);
                goto me24;
mn26:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 504, 12);
me24:;
                if (!t23) goto ie20;
                {
                    void * t28 = gm_l0;
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sl2lo3hki3t4d));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(35));
                    gm_tob1snuii5bme t30;
                    gm_f_error_96ci5lu((void *)&t30, t28, t29);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t30);
                    goto gm_fdone;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                void * t31 = gm_l0;
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                gm_l2 = t32;
            }
            {
                void * t33 = gm_l0;
                gm_t21m1eu5ta01n t34;
                gm_f_key_path_7spb050((void *)&t34, (void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (((((uint8_t *)&t34))[0] != 0)) {
                    gm_tefknldmnh2si t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t35 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t35 + 48, 0);
                    ((uint8_t *)&t35)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t35 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t34) + 8)));
                    F.ret = t35;
                    goto gm_fret;
                }
                F.l3 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t34) + 8));
            }
            {
                void * t36 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t36);
            }
            {
                void * t38 = gm_l0;
                gm_tqs5p4n2tq19v t39 = gm_f_peek_bkdvl65(t38);
                gm_tqs5p4n2tq19v t40;
                GM_ST(uint8_t, ((uint8_t *)&t40), ((uint8_t)61ULL));
                ((uint8_t *)&t40)[1] = 1;
                uint8_t t41 = (!gm_eqb44cb3cf1n(((uint8_t *)&t39), ((uint8_t *)&t40)));
                if (!t41) goto ie37;
                {
                    void * t43 = gm_l0;
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_spl83f693c752));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(31));
                    gm_tob1snuii5bme t45;
                    gm_f_error_96ci5lu((void *)&t45, t43, t44);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t45);
                    goto gm_fdone;
                }
                goto ix37;
ie37:;
ix37:;
            }
            {
                void * t46 = gm_l0;
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)t47) + 16));
                uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                if (o50) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 509, 9);
                uint64_t t51 = r49;
                GM_ST(uint64_t, (((uint8_t *)t46) + 16), t51);
            }
            {
                void * t52 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t52);
            }
            {
                void * t53 = gm_l0;
                gm_tefknldmnh2si t54;
                gm_f_value_oq952o2((void *)&t54, (void *)NULL, cx, NULL, t53);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (((((uint8_t *)&t54))[0] != 0)) {
                    gm_tefknldmnh2si t55;
                    GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t55 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t55 + 48, 0);
                    ((uint8_t *)&t55)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t55 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t54) + 8)));
                    F.ret = t55;
                    goto gm_fret;
                }
                F.l4 = (*(gm_tktr4rmbindgv *)(((uint8_t *)&t54) + 8));
            }
            {
                void * t56 = gm_l1;
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                uint64_t t59 = gm_l2;
                gm_tr1k5nl69lhjo t60;
                gm_f_insert_path_cvuqfkc((void *)&t60, (void *)NULL, cx, NULL, t56, t58, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l4)), t59);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (((((uint8_t *)&t60))[0] != 0)) {
                    gm_tefknldmnh2si t61;
                    GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t61 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t61 + 48, 0);
                    ((uint8_t *)&t61)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t61 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t60) + 8)));
                    F.ret = t61;
                    goto gm_fret;
                }
                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 8));
            }
            {
                void * t63 = gm_l0;
                gm_f_skip_space_and_newlines_qagb1bk(t63);
            }
            {
                void * t65 = gm_l0;
                gm_tqs5p4n2tq19v t66 = gm_f_peek_bkdvl65(t65);
                gm_tqs5p4n2tq19v t67;
                GM_ST(uint8_t, ((uint8_t *)&t67), ((uint8_t)125ULL));
                ((uint8_t *)&t67)[1] = 1;
                uint8_t t68 = (gm_eqb44cb3cf1n(((uint8_t *)&t66), ((uint8_t *)&t67)));
                if (!t68) goto ie64;
                {
                    goto lb10;
                }
                goto ix64;
ie64:;
ix64:;
            }
            {
                void * t71 = gm_l0;
                gm_tqs5p4n2tq19v t72 = gm_f_peek_bkdvl65(t71);
                uint8_t t73;
                if (((((uint8_t *)&t72))[1] != 0)) goto mn75;
                t73 = ((uint8_t)1);
                goto me74;
mn75:;
                t73 = ((uint8_t)0);
                goto me74;
mn76:;
                gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 515, 12);
me74:;
                if (!t73) goto ie70;
                {
                    void * t78 = gm_l0;
                    gm_b16_8 t79;
                    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sl2lo3hki3t4d));
                    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(35));
                    gm_tob1snuii5bme t80;
                    gm_f_error_96ci5lu((void *)&t80, t78, t79);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t80);
                    goto gm_fdone;
                }
                goto ix70;
ie70:;
ix70:;
            }
            {
                void * t82 = gm_l0;
                gm_tqs5p4n2tq19v t83 = gm_f_peek_bkdvl65(t82);
                gm_tqs5p4n2tq19v t84;
                GM_ST(uint8_t, ((uint8_t *)&t84), ((uint8_t)44ULL));
                ((uint8_t *)&t84)[1] = 1;
                uint8_t t85 = (!gm_eqb44cb3cf1n(((uint8_t *)&t83), ((uint8_t *)&t84)));
                if (!t85) goto ie81;
                {
                    void * t87 = gm_l0;
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_suonmnnb7u0t1));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(54));
                    gm_tob1snuii5bme t89;
                    gm_f_error_96ci5lu((void *)&t89, t87, t88);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t89);
                    goto gm_fdone;
                }
                goto ix81;
ie81:;
ix81:;
            }
            {
                void * t90 = gm_l0;
                void * t91 = gm_l0;
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)t91) + 16));
                uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                if (o94) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 517, 9);
                uint64_t t95 = r93;
                GM_ST(uint64_t, (((uint8_t *)t90) + 16), t95);
            }
lc10:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
        }
        goto gm_lad;
lb10:;
    }
    {
        void * t96 = gm_l0;
        void * t97 = gm_l0;
        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 16));
        uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
        if (o100) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 519, 5);
        uint64_t t101 = r99;
        GM_ST(uint64_t, (((uint8_t *)t96) + 16), t101);
    }
    {
        void * t102 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t102) + 32), ((uint8_t)0));
    }
    {
        void * t103 = gm_l1;
        gm_tktr4rmbindgv t104;
        memset(&t104, 0, sizeof t104);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t104 + 8), t103);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t104);
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
__attribute__((aligned(4))) void gm_f_insert_path_cvuqfkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, uint64_t a3) {
    struct gm_fr_insert_path_cvuqfkc F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* path */
    uint64_t gm_l3; /* line */
    void * gm_l4; /* target */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* name */
    void * gm_l8; /* e */
    void * gm_l9; /* sub */
    void * gm_l10; /* fresh */
    gm_b16_8 gm_l11; /* name */
    GM_STACK_CHECK(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_l4 = t2;
    }
    {
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 234, 25);
            uint64_t t10 = r8;
            uint64_t t11 = (uintptr_t)t10;
            if (t6 > t11 || t11 > (uintptr_t)t4) gm_trap_slice(t6, t11, t4, gm_src3nt8pf0qk5gn, 234, 17);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t5 + t6 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(t11 - t6));
            gm_l5 = t12;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l6;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l6;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t20 * 16));
                    gm_l7 = t24;
                    {
                        void * t27 = gm_l4;
                        gm_b16_8 t28 = gm_l7;
                        void * t29 = gm_f_get_9rme0gb(t27, t28);
                        if (!(GM_LD(void *, ((uint8_t *)&t29)) != 0)) goto ie26;
                        void * t30 = GM_LD(void *, ((uint8_t *)&t29));
                        gm_l8 = t30;
                        {
                            void * t32 = gm_l8;
                            void * t33 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t32) + 16)));
                            void * t34;
                            if ((GM_LD(void *, ((uint8_t *)&t33)) != 0)) {
                                void * t35 = GM_LD(void *, (((uint8_t *)&t33) + 0));
                                t34 = t35;
                            } else {
                                gm_tob1snuii5bme t36;
                                uint64_t t37 = gm_l3;
                                GM_ST(uint64_t, (uint8_t *)&t36 + 0, t37);
                                gm_b16_8 t38;
                                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_skiffh4urhl7e));
                                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(13));
                                GM_ST(gm_b16_8, (uint8_t *)&t36 + 8, t38);
                                GM_ST(uint64_t, (uint8_t *)&t36 + 24, ((uint64_t)1ULL));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
                                GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t36);
                                goto gm_fdone;
                            }
                            gm_l9 = t34;
                        }
                        {
                            void * t39 = gm_l9;
                            gm_l4 = t39;
                        }
                        goto ix26;
ie26:;
                        {
                            void * t41 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_l10 = t41;
                        }
                        {
                            void * t42 = gm_l4;
                            void * t43 = (void *)(((uint8_t *)t42) + 0);
                            gm_trc3jdv2g5qre t44;
                            gm_b16_8 t45 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
                            void * t46 = gm_l10;
                            gm_tktr4rmbindgv t47;
                            memset(&t47, 0, sizeof t47);
                            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)4ULL));
                            GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
                            GM_ST(gm_tktr4rmbindgv, (uint8_t *)&t44 + 16, t47);
                            uint64_t t48 = gm_l3;
                            GM_ST(uint64_t, (uint8_t *)&t44 + 64, t48);
                            gm_tftb1odc739t5 t49 = gm_f_push_2pp1ri6(t43, &t44);
                            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src3nt8pf0qk5gn, 240, 13);
                            gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                        }
                        {
                            void * t51 = gm_l10;
                            gm_l4 = t51;
                        }
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 234, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t53 = gm_l6;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l6 = t56;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r58; bool o59 = __builtin_sub_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 244, 18);
        uint64_t t60 = r58;
        uint64_t t61 = (uintptr_t)t60;
        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t61 >= (uintptr_t)(t62)) gm_trap_index(t61, t62, gm_src3nt8pf0qk5gn, 244, 13);
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t61 * 16));
        gm_l11 = t63;
    }
    {
        void * t65 = gm_l4;
        gm_b16_8 t66 = gm_l11;
        void * t67 = gm_f_get_9rme0gb(t65, t66);
        uint8_t t68;
        if ((GM_LD(void *, ((uint8_t *)&t67)) != 0)) goto mn70;
        t68 = ((uint8_t)0);
        goto me69;
mn70:;
        t68 = ((uint8_t)1);
        goto me69;
mn71:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 245, 8);
me69:;
        if (!t68) goto ie64;
        {
            gm_tob1snuii5bme t73;
            uint64_t t74 = gm_l3;
            GM_ST(uint64_t, (uint8_t *)&t73 + 0, t74);
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_skiffh4urhl7e));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(13));
            GM_ST(gm_b16_8, (uint8_t *)&t73 + 8, t75);
            GM_ST(uint64_t, (uint8_t *)&t73 + 24, ((uint64_t)1ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t73);
            goto gm_fdone;
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        void * t76 = gm_l4;
        void * t77 = (void *)(((uint8_t *)t76) + 0);
        gm_trc3jdv2g5qre t78;
        gm_b16_8 t79 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t79);
        memcpy((uint8_t *)&t78 + 16, ((uint8_t *)a2), 48);
        uint64_t t80 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t78 + 64, t80);
        gm_tftb1odc739t5 t81 = gm_f_push_2pp1ri6(t77, &t78);
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_src3nt8pf0qk5gn, 246, 5);
        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
    }
    {
        gm_b0_1 t83;
        memset(&(*(gm_tr1k5nl69lhjo *)gm_out), 0, sizeof (*(gm_tr1k5nl69lhjo *)gm_out));
        ((uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tr1k5nl69lhjo *)gm_out) + 8, t83);
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
__attribute__((aligned(4))) void gm_f_key_5clrcin(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_key_5clrcin F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    gm_tob1snuii5bme gm_l2; /* e */
    uint64_t gm_l3; /* start */
    uint8_t gm_l4; /* k */
    uint8_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 512u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tqs5p4n2tq19v t3 = gm_f_peek_bkdvl65(t2);
        uint8_t t4;
        if (((((uint8_t *)&t3))[1] != 0)) {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            void * t6 = gm_l0;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_spl83f693c752));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(31));
            gm_tob1snuii5bme t8;
            gm_f_error_96ci5lu((void *)&t8, t6, t7);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        gm_l1 = t4;
    }
    {
        uint8_t t10 = gm_l1;
        gm_tqs5p4n2tq19v t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)34ULL));
        GM_ST(uint8_t, ((uint8_t *)&t11 + 1), ((uint8_t)39ULL));
        uint8_t t12 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t11) + 0, ((uint8_t *)&t10))) t12 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t11) + 1, ((uint8_t *)&t10))) t12 = 1;
        uint8_t t13 = (uint8_t)(t12);
        if (!t13) goto ie9;
        {
            void * t15 = gm_l0;
            gm_tu615l4qlr8b2 t16;
            gm_f_parse_string_80niubc((void *)&t16, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t17;
            if (!((((uint8_t *)&t16))[0] != 0)) {
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
                t17 = t18;
            } else {
                gm_l2 = (*(gm_tob1snuii5bme *)(((uint8_t *)&t16) + 8));
                gm_tob1snuii5bme t20;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
                GM_ST(uint64_t, (uint8_t *)&t20 + 0, t21);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_spl83f693c752));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(31));
                GM_ST(gm_b16_8, (uint8_t *)&t20 + 8, t22);
                GM_ST(uint64_t, (uint8_t *)&t20 + 24, ((uint64_t)1ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t20);
                goto gm_fdone;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t17);
            goto gm_fdone;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 16));
        gm_l3 = t24;
    }
    {
        for (;;) {
            void * t26 = gm_l0;
            gm_tqs5p4n2tq19v t27 = gm_f_peek_bkdvl65(t26);
            if (!((((uint8_t *)&t27))[1] != 0)) goto lb25;
            uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&t27));
            gm_l4 = t28;
            {
                uint8_t t32 = 0;
                uint8_t t33 = 0;
                uint8_t t34 = 0;
                {
                    uint8_t t36 = gm_l4;
                    gm_l5 = t36;
                }
                uint8_t t37 = 0;
                uint8_t t38 = gm_l5;
                uint8_t t39 = (((uint8_t)97ULL) <= t38);
                if (t39) {
                    uint8_t t40 = gm_l5;
                    uint8_t t41 = (t40 <= ((uint8_t)122ULL));
                    t37 = t41;
                } else t37 = 0;
                uint8_t t42 = t37;
                if (!t42) {
                    {
                        uint8_t t44 = gm_l4;
                        gm_l6 = t44;
                    }
                    uint8_t t45 = 0;
                    uint8_t t46 = gm_l6;
                    uint8_t t47 = (((uint8_t)65ULL) <= t46);
                    if (t47) {
                        uint8_t t48 = gm_l6;
                        uint8_t t49 = (t48 <= ((uint8_t)90ULL));
                        t45 = t49;
                    } else t45 = 0;
                    uint8_t t50 = t45;
                    t34 = t50;
                } else t34 = 1;
                if (!t34) {
                    {
                        uint8_t t52 = gm_l4;
                        gm_l7 = t52;
                    }
                    uint8_t t53 = 0;
                    uint8_t t54 = gm_l7;
                    uint8_t t55 = (((uint8_t)48ULL) <= t54);
                    if (t55) {
                        uint8_t t56 = gm_l7;
                        uint8_t t57 = (t56 <= ((uint8_t)57ULL));
                        t53 = t57;
                    } else t53 = 0;
                    uint8_t t58 = t53;
                    t33 = t58;
                } else t33 = 1;
                if (!t33) {
                    uint8_t t59 = gm_l4;
                    gm_tqs5p4n2tq19v t60;
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)95ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 1), ((uint8_t)45ULL));
                    uint8_t t61 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t60) + 0, ((uint8_t *)&t59))) t61 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t60) + 1, ((uint8_t *)&t59))) t61 = 1;
                    uint8_t t62 = (uint8_t)(t61);
                    t32 = t62;
                } else t32 = 1;
                uint8_t t63 = (uint8_t)!t32;
                if (!t63) goto ie31;
                {
                    goto lb25;
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                void * t65 = gm_l0;
                void * t66 = gm_l0;
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t66) + 16));
                uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                if (o69) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 225, 9);
                uint64_t t70 = r68;
                GM_ST(uint64_t, (((uint8_t *)t65) + 16), t70);
            }
lc25:;
        }
lb25:;
    }
    {
        void * t72 = gm_l0;
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 16));
        uint64_t t74 = gm_l3;
        uint8_t t75 = ((t73 == t74));
        if (!t75) goto ie71;
        {
            void * t77 = gm_l0;
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sithisfj5qgfv));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(66));
            gm_tob1snuii5bme t79;
            gm_f_error_96ci5lu((void *)&t79, t77, t78);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t79);
            goto gm_fdone;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        void * t80 = gm_l0;
        uint64_t t81 = GM_LD(uintptr_t, (((uint8_t *)t80) + 0) + 8);
        void * t82 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t80) + 0)));
        uint64_t t83 = gm_l3;
        uint64_t t84 = (uintptr_t)t83;
        void * t85 = gm_l0;
        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 16));
        uint64_t t87 = (uintptr_t)t86;
        if (t84 > t87 || t87 > (uintptr_t)t81) gm_trap_slice(t84, t87, t81, gm_src3nt8pf0qk5gn, 228, 12);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t82 + t84 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87 - t84));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t88);
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
inline __attribute__((aligned(4))) void gm_f_key_path_7spb050(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_key_path_7spb050 F;
    void * gm_l0; /* r */
    GM_STACK_CHECK(&F, 199u);
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
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = gm_l0;
                gm_f_skip_space_voq272o(t5);
            }
            {
                void * t6 = (void *)((uint8_t *)&F.l1);
                void * t7 = gm_l0;
                gm_tu615l4qlr8b2 t8;
                gm_f_key_5clrcin((void *)&t8, (void *)NULL, cx, NULL, t7);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                if (((((uint8_t *)&t8))[0] != 0)) {
                    gm_t21m1eu5ta01n t9;
                    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
                    ((uint8_t *)&t9)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t9 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t8) + 8)));
                    F.ret = t9;
                    goto gm_fret;
                }
                gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
                gm_tftb1odc739t5 t11 = gm_f_push_3ef746b(t6, t10);
                if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src3nt8pf0qk5gn, 209, 9);
                gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
            }
            {
                void * t13 = gm_l0;
                gm_f_skip_space_voq272o(t13);
            }
            {
                void * t15 = gm_l0;
                gm_tqs5p4n2tq19v t16 = gm_f_peek_bkdvl65(t15);
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17), ((uint8_t)46ULL));
                ((uint8_t *)&t17)[1] = 1;
                uint8_t t18 = (!gm_eqb44cb3cf1n(((uint8_t *)&t16), ((uint8_t *)&t17)));
                if (!t18) goto ie14;
                {
                    goto lb2;
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                void * t20 = gm_l0;
                void * t21 = gm_l0;
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 16));
                uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                if (o24) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 212, 9);
                uint64_t t25 = r23;
                GM_ST(uint64_t, (((uint8_t *)t20) + 16), t25);
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        const uint8_t *t26 = ((uint8_t *)&F.l1);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_t21m1eu5ta01n *)gm_out) + 8, t26, 40);
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
extern __attribute__((aligned(4))) void * gm_f_new_table_hh6cki7(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_number_4l95pjb(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_number_4l95pjb F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* negative */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    int64_t gm_l5; /* radix */
    uint8_t gm_l6; /* n */
    int64_t gm_l7; /* n */
    int32_t gm_l8; /* digits */
    uint8_t gm_l9; /* last_underscore */
    uint64_t gm_l10; /* first */
    uint8_t gm_l11; /* d */
    uint8_t gm_l12; /* <tmp> */
    uint8_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* <tmp> */
    int64_t gm_l15; /* v */
    gm_t913bb735pvt5 gm_l16; /* <tmp> */
    int64_t gm_l17; /* <tmp> */
    int64_t gm_l18; /* <tmp> */
    uint8_t gm_l19; /* k */
    uint8_t gm_l20; /* <tmp> */
    uint8_t gm_l21; /* <tmp> */
    uint8_t gm_l22; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 1936u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t4) + 32), ((uint8_t)1));
    }
    {
        void * t5 = gm_l0;
        gm_tqs5p4n2tq19v t6 = gm_f_peek_bkdvl65(t5);
        gm_tqs5p4n2tq19v t7;
        GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)45ULL));
        ((uint8_t *)&t7)[1] = 1;
        uint8_t t8 = (gm_eqb44cb3cf1n(((uint8_t *)&t6), ((uint8_t *)&t7)));
        gm_l2 = t8;
    }
    {
        void * t10 = gm_l0;
        gm_tqs5p4n2tq19v t11 = gm_f_peek_bkdvl65(t10);
        gm_tqs5p4n2tq19v t12;
        GM_ST(uint8_t, ((uint8_t *)&t12), ((uint8_t)43ULL));
        ((uint8_t *)&t12)[1] = 1;
        gm_tqs5p4n2tq19v t13;
        GM_ST(uint8_t, ((uint8_t *)&t13), ((uint8_t)45ULL));
        ((uint8_t *)&t13)[1] = 1;
        gm_t5r6k0fdcfsb3 t14;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t14 + 0), t12);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t14 + 2), t13);
        uint8_t t15 = 0;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t14) + 0, ((uint8_t *)&t11))) t15 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t14) + 2, ((uint8_t *)&t11))) t15 = 1;
        uint8_t t16 = (uint8_t)(t15);
        if (!t16) goto ie9;
        {
            void * t18 = gm_l0;
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 16));
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            if (o22) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 289, 33);
            uint64_t t23 = r21;
            GM_ST(uint64_t, (((uint8_t *)t18) + 16), t23);
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t25 = 0;
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
        void * t28 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t26) + 0)));
        void * t29 = gm_l0;
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 16));
        uint64_t t31 = (uintptr_t)t30;
        if (t31 > (uintptr_t)t27 || (uintptr_t)t27 > (uintptr_t)t27) gm_trap_slice(t31, (uintptr_t)t27, t27, gm_src3nt8pf0qk5gn, 290, 8);
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t28 + t31 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)((uintptr_t)t27 - t31));
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sns75reqq32d2));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(3));
        uint8_t t34 = gm_f_starts_with_6vvt01e(t32, t33);
        if (!t34) {
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
            void * t37 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)));
            void * t38 = gm_l0;
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
            uint64_t t40 = (uintptr_t)t39;
            if (t40 > (uintptr_t)t36 || (uintptr_t)t36 > (uintptr_t)t36) gm_trap_slice(t40, (uintptr_t)t36, t36, gm_src3nt8pf0qk5gn, 290, 46);
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t40 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)t36 - t40));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sfa86erpod86s));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(3));
            uint8_t t43 = gm_f_starts_with_6vvt01e(t41, t42);
            t25 = t43;
        } else t25 = 1;
        if (!t25) goto ie24;
        {
            void * t45 = gm_l0;
            void * t46 = gm_l0;
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 16));
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)3ULL), &r48);
            if (o49) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 291, 9);
            uint64_t t50 = r48;
            GM_ST(uint64_t, (((uint8_t *)t45) + 16), t50);
        }
        {
            void * t51 = gm_l0;
            uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)t51) + 0) + 8);
            void * t53 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t51) + 0)));
            uint64_t t54 = gm_l1;
            uint64_t t55 = (uintptr_t)t54;
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
            uint64_t t58 = (uintptr_t)t57;
            if (t55 > t58 || t58 > (uintptr_t)t52) gm_trap_slice(t55, t58, t52, gm_src3nt8pf0qk5gn, 292, 28);
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t53 + t55 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58 - t55));
            gm_tktr4rmbindgv t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t60 + 8), t59);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
            GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t60);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t61 = gm_l0;
        gm_tqs5p4n2tq19v t62 = gm_f_peek_bkdvl65(t61);
        uint8_t t63;
        if (((((uint8_t *)&t62))[1] != 0)) {
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
            t63 = t64;
        } else {
            void * t65 = gm_l0;
            gm_b16_8 t66 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t67;
            gm_f_error_96ci5lu((void *)&t67, t65, t66);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t67);
            goto gm_fdone;
        }
        gm_l3 = t63;
    }
    {
        {
            uint8_t t70 = gm_l3;
            gm_l4 = t70;
        }
        uint8_t t71 = 0;
        uint8_t t72 = gm_l4;
        uint8_t t73 = (((uint8_t)48ULL) <= t72);
        if (t73) {
            uint8_t t74 = gm_l4;
            uint8_t t75 = (t74 <= ((uint8_t)57ULL));
            t71 = t75;
        } else t71 = 0;
        uint8_t t76 = (uint8_t)!t71;
        uint8_t t77 = t76;
        if (!t77) goto ie68;
        {
            void * t79 = gm_l0;
            gm_b16_8 t80 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t81;
            gm_f_error_96ci5lu((void *)&t81, t79, t80);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t81);
            goto gm_fdone;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        gm_l5 = ((int64_t)10LL);
    }
    {
        uint8_t t83 = gm_l3;
        uint8_t t84 = ((t83 == ((uint8_t)48ULL)));
        if (!t84) goto ie82;
        {
            void * t87 = gm_l0;
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)t87) + 0));
            void * t89 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t88)));
            uint64_t t90 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t88) + 8);
            void * t91 = gm_l0;
            uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)t91) + 16));
            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
            if (o94) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 298, 28);
            uint64_t t95 = r93;
            uint64_t t96 = (uintptr_t)t95;
            gm_tqs5p4n2tq19v t97;
            if (t96 < t90) {
            uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)t89 + t96 * 1));
            gm_tqs5p4n2tq19v t99;
            GM_ST(uint8_t, ((uint8_t *)&t99), t98);
            ((uint8_t *)&t99)[1] = 1;
                t97 = t99;
            } else {
            gm_tqs5p4n2tq19v t100;
            memset(&t100, 0, sizeof t100);
                t97 = t100;
            }
            if (!((((uint8_t *)&t97))[1] != 0)) goto ie86;
            uint8_t t101 = GM_LD(uint8_t, ((uint8_t *)&t97));
            gm_l6 = t101;
            {
                uint8_t t104 = gm_l6;
                uint8_t t105 = ((t104 == ((uint8_t)120ULL)));
                if (!t105) goto ie103;
                {
                    gm_l5 = ((int64_t)16LL);
                }
                goto ix103;
ie103:;
                uint8_t t108 = gm_l6;
                uint8_t t109 = ((t108 == ((uint8_t)111ULL)));
                if (!t109) goto ie107;
                {
                    gm_l5 = ((int64_t)8LL);
                }
                goto ix107;
ie107:;
                uint8_t t112 = gm_l6;
                uint8_t t113 = ((t112 == ((uint8_t)98ULL)));
                if (!t113) goto ie111;
                {
                    gm_l5 = ((int64_t)2LL);
                }
                goto ix111;
ie111:;
ix111:;
ix107:;
ix103:;
            }
            goto ix86;
ie86:;
ix86:;
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        int64_t t116 = gm_l5;
        uint8_t t117 = (!(t116 == ((int64_t)10LL)));
        if (!t117) goto ie115;
        {
            void * t119 = gm_l0;
            void * t120 = gm_l0;
            uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)t120) + 16));
            uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)2ULL), &r122);
            if (o123) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 302, 26);
            uint64_t t124 = r122;
            GM_ST(uint64_t, (((uint8_t *)t119) + 16), t124);
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        gm_l7 = ((int64_t)0LL);
    }
    {
        gm_l8 = ((int32_t)0LL);
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        void * t125 = gm_l0;
        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)t125) + 16));
        gm_l10 = t126;
    }
    {
        for (;;) {
            void * t128 = gm_l0;
            gm_tqs5p4n2tq19v t129 = gm_f_peek_bkdvl65(t128);
            if (!((((uint8_t *)&t129))[1] != 0)) goto lb127;
            uint8_t t130 = GM_LD(uint8_t, ((uint8_t *)&t129));
            gm_l11 = t130;
            {
                uint8_t t134 = gm_l11;
                uint8_t t135 = ((t134 == ((uint8_t)95ULL)));
                if (!t135) goto ie133;
                {
                    uint8_t t138 = 0;
                    int32_t t139 = gm_l8;
                    uint8_t t140 = ((t139 == ((int32_t)0LL)));
                    if (!t140) {
                        uint8_t t141 = gm_l9;
                        t138 = t141;
                    } else t138 = 1;
                    if (!t138) goto ie137;
                    {
                        void * t143 = gm_l0;
                        gm_b16_8 t144;
                        GM_ST(void *, (uint8_t *)&t144, (void *)(gm_s2a4d8jj74v6o));
                        GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(48));
                        gm_tob1snuii5bme t145;
                        gm_f_error_96ci5lu((void *)&t145, t143, t144);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t145);
                        goto gm_fdone;
                    }
                    goto ix137;
ie137:;
ix137:;
                }
                {
                    gm_l9 = ((uint8_t)1);
                }
                {
                    void * t146 = gm_l0;
                    void * t147 = gm_l0;
                    uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)t147) + 16));
                    uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                    if (o150) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 311, 13);
                    uint64_t t151 = r149;
                    GM_ST(uint64_t, (((uint8_t *)t146) + 16), t151);
                }
                {
                    goto lc127;
                }
                goto ix133;
ie133:;
ix133:;
            }
            {
                int64_t t152;
                {
                    uint8_t t155 = gm_l11;
                    gm_l12 = t155;
                }
                uint8_t t156 = 0;
                uint8_t t157 = gm_l12;
                uint8_t t158 = (((uint8_t)48ULL) <= t157);
                if (t158) {
                    uint8_t t159 = gm_l12;
                    uint8_t t160 = (t159 <= ((uint8_t)57ULL));
                    t156 = t160;
                } else t156 = 0;
                uint8_t t161 = t156;
                if (!t161) goto ie153;
                uint8_t t163 = gm_l11;
                uint8_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint8_t)48ULL), &r164);
                if (o165) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 315, 17);
                uint8_t t166 = r164;
                int64_t t167 = (int64_t)t166;
                int64_t t168 = t167;
                t152 = t168;
                goto ix153;
ie153:;
                int64_t t169;
                int64_t t171 = gm_l5;
                uint8_t t172 = ((t171 == ((int64_t)16LL)));
                if (!t172) goto ie170;
                {
                    uint8_t t174 = gm_l11;
                    gm_l13 = t174;
                }
                uint8_t t175 = 0;
                uint8_t t176 = gm_l13;
                uint8_t t177 = (((uint8_t)97ULL) <= t176);
                if (t177) {
                    uint8_t t178 = gm_l13;
                    uint8_t t179 = (t178 <= ((uint8_t)102ULL));
                    t175 = t179;
                } else t175 = 0;
                uint8_t t180 = t175;
                if (!t180) goto ie170;
                uint8_t t182 = gm_l11;
                uint8_t r183; bool o184 = __builtin_sub_overflow(t182, ((uint8_t)97ULL), &r183);
                if (o184) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 317, 17);
                uint8_t t185 = r183;
                int64_t t186 = (int64_t)t185;
                int64_t r187; bool o188 = __builtin_add_overflow(t186, ((int64_t)10LL), &r187);
                if (o188) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 317, 13);
                int64_t t189 = r187;
                int64_t t190 = t189;
                t169 = t190;
                goto ix170;
ie170:;
                int64_t t191;
                int64_t t193 = gm_l5;
                uint8_t t194 = ((t193 == ((int64_t)16LL)));
                if (!t194) goto ie192;
                {
                    uint8_t t196 = gm_l11;
                    gm_l14 = t196;
                }
                uint8_t t197 = 0;
                uint8_t t198 = gm_l14;
                uint8_t t199 = (((uint8_t)65ULL) <= t198);
                if (t199) {
                    uint8_t t200 = gm_l14;
                    uint8_t t201 = (t200 <= ((uint8_t)70ULL));
                    t197 = t201;
                } else t197 = 0;
                uint8_t t202 = t197;
                if (!t202) goto ie192;
                uint8_t t204 = gm_l11;
                uint8_t r205; bool o206 = __builtin_sub_overflow(t204, ((uint8_t)65ULL), &r205);
                if (o206) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 319, 17);
                uint8_t t207 = r205;
                int64_t t208 = (int64_t)t207;
                int64_t r209; bool o210 = __builtin_add_overflow(t208, ((int64_t)10LL), &r209);
                if (o210) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 319, 13);
                int64_t t211 = r209;
                int64_t t212 = t211;
                t191 = t212;
                goto ix192;
ie192:;
                int64_t t214 = ((int64_t)99LL);
                t191 = t214;
ix192:;
                t169 = t191;
ix170:;
                t152 = t169;
ix153:;
                gm_l15 = t152;
            }
            {
                int64_t t216 = gm_l15;
                int64_t t217 = gm_l5;
                uint8_t t218 = (t216 >= t217);
                if (!t218) goto ie215;
                {
                    goto lb127;
                }
                goto ix215;
ie215:;
ix215:;
            }
            {
                int32_t t221 = gm_l8;
                uint8_t t222 = ((t221 == ((int32_t)1LL)));
                if (!t222) goto ie220;
                uint64_t t223 = gm_l10;
                uint64_t t224 = (uintptr_t)t223;
                void * t225 = gm_l0;
                uint64_t t226 = GM_LD(uintptr_t, (((uint8_t *)t225) + 0) + 8);
                if (t224 >= (uintptr_t)(t226)) gm_trap_index(t224, t226, gm_src3nt8pf0qk5gn, 324, 28);
                uint8_t t227 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t225) + 0)) + t224 * 1));
                uint8_t t228 = ((t227 == ((uint8_t)48ULL)));
                if (!t228) goto ie220;
                int64_t t229 = gm_l5;
                uint8_t t230 = ((t229 == ((int64_t)10LL)));
                if (!t230) goto ie220;
                {
                    void * t232 = gm_l0;
                    gm_b16_8 t233;
                    GM_ST(void *, (uint8_t *)&t233, (void *)(gm_s8lbhmq10rt7i));
                    GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(41));
                    gm_tob1snuii5bme t234;
                    gm_f_error_96ci5lu((void *)&t234, t232, t233);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t234);
                    goto gm_fdone;
                }
                goto ix220;
ie220:;
ix220:;
            }
            {
                {
                    int64_t t236 = gm_l7;
                    int64_t t237 = gm_l5;
                    int64_t r238; bool o239 = __builtin_mul_overflow(t236, t237, &r238);
                    gm_t913bb735pvt5 t240;
                    if (o239) {
                    gm_t913bb735pvt5 t241;
                    memset(&t241, 0, sizeof t241);
                        t240 = t241;
                    } else {
                    int64_t t242 = r238;
                    gm_t913bb735pvt5 t243;
                    GM_ST(uint8_t, (uint8_t *)&t243 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t243 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t243 + 12, 0);
                    GM_ST(int64_t, ((uint8_t *)&t243), t242);
                    ((uint8_t *)&t243)[8] = 1;
                        t240 = t243;
                    }
                    gm_l16 = t240;
                }
                {
                    int64_t t244 = gm_l15;
                    gm_l18 = t244;
                }
                gm_t913bb735pvt5 t245;
                if (!((((uint8_t *)&gm_l16))[8] != 0)) goto ie246;
                int64_t t247 = GM_LD(int64_t, ((uint8_t *)&gm_l16));
                gm_l17 = t247;
                int64_t t248 = gm_l17;
                int64_t t249 = gm_l18;
                int64_t r250; bool o251 = __builtin_add_overflow(t248, t249, &r250);
                gm_t913bb735pvt5 t252;
                if (o251) {
                gm_t913bb735pvt5 t253;
                memset(&t253, 0, sizeof t253);
                    t252 = t253;
                } else {
                int64_t t254 = r250;
                gm_t913bb735pvt5 t255;
                GM_ST(uint8_t, (uint8_t *)&t255 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t255 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t255 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t255), t254);
                ((uint8_t *)&t255)[8] = 1;
                    t252 = t255;
                }
                t245 = t252;
                goto ix246;
ie246:;
                gm_t913bb735pvt5 t256;
                memset(&t256, 0, sizeof t256);
                t245 = t256;
ix246:;
                gm_t913bb735pvt5 t257 = t245;
                int64_t t258;
                if (((((uint8_t *)&t257))[8] != 0)) {
                    int64_t t259 = GM_LD(int64_t, (((uint8_t *)&t257) + 0));
                    t258 = t259;
                } else {
                    void * t260 = gm_l0;
                    gm_b16_8 t261;
                    GM_ST(void *, (uint8_t *)&t261, (void *)(gm_s70iivik802q4));
                    GM_ST(uintptr_t, (uint8_t *)&t261 + 8, (uintptr_t)(25));
                    gm_tob1snuii5bme t262;
                    gm_f_error_96ci5lu((void *)&t262, t260, t261);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                    ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t262);
                    goto gm_fdone;
                }
                gm_l7 = t258;
            }
            {
                int32_t t263 = gm_l8;
                int32_t r264; bool o265 = __builtin_add_overflow(t263, ((int32_t)1LL), &r264);
                if (o265) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 326, 9);
                int32_t t266 = r264;
                gm_l8 = t266;
            }
            {
                gm_l9 = ((uint8_t)0);
            }
            {
                void * t267 = gm_l0;
                void * t268 = gm_l0;
                uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)t268) + 16));
                uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                if (o271) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 328, 9);
                uint64_t t272 = r270;
                GM_ST(uint64_t, (((uint8_t *)t267) + 16), t272);
            }
lc127:;
        }
lb127:;
    }
    {
        uint8_t t274 = gm_l9;
        if (!t274) goto ie273;
        {
            void * t276 = gm_l0;
            gm_b16_8 t277;
            GM_ST(void *, (uint8_t *)&t277, (void *)(gm_s2a4d8jj74v6o));
            GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(48));
            gm_tob1snuii5bme t278;
            gm_f_error_96ci5lu((void *)&t278, t276, t277);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t278);
            goto gm_fdone;
        }
        goto ix273;
ie273:;
ix273:;
    }
    {
        int32_t t280 = gm_l8;
        uint8_t t281 = ((t280 == ((int32_t)0LL)));
        if (!t281) goto ie279;
        {
            void * t283 = gm_l0;
            gm_b16_8 t284;
            GM_ST(void *, (uint8_t *)&t284, (void *)(gm_s70iivik802q4));
            GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(25));
            gm_tob1snuii5bme t285;
            gm_f_error_96ci5lu((void *)&t285, t283, t284);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t285);
            goto gm_fdone;
        }
        goto ix279;
ie279:;
ix279:;
    }
    {
        int64_t t287 = gm_l5;
        uint8_t t288 = ((t287 == ((int64_t)10LL)));
        if (!t288) goto ie286;
        void * t289 = gm_l0;
        gm_tqs5p4n2tq19v t290 = gm_f_peek_bkdvl65(t289);
        gm_tqs5p4n2tq19v t291;
        GM_ST(uint8_t, ((uint8_t *)&t291), ((uint8_t)46ULL));
        ((uint8_t *)&t291)[1] = 1;
        gm_tqs5p4n2tq19v t292;
        GM_ST(uint8_t, ((uint8_t *)&t292), ((uint8_t)101ULL));
        ((uint8_t *)&t292)[1] = 1;
        gm_tqs5p4n2tq19v t293;
        GM_ST(uint8_t, ((uint8_t *)&t293), ((uint8_t)69ULL));
        ((uint8_t *)&t293)[1] = 1;
        gm_tqs5p4n2tq19v t294;
        GM_ST(uint8_t, ((uint8_t *)&t294), ((uint8_t)45ULL));
        ((uint8_t *)&t294)[1] = 1;
        gm_tqs5p4n2tq19v t295;
        GM_ST(uint8_t, ((uint8_t *)&t295), ((uint8_t)58ULL));
        ((uint8_t *)&t295)[1] = 1;
        gm_tcdtdmenq84v9 t296;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t296 + 0), t291);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t296 + 2), t292);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t296 + 4), t293);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t296 + 6), t294);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t296 + 8), t295);
        uint8_t t297 = 0;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t296) + 0, ((uint8_t *)&t290))) t297 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t296) + 2, ((uint8_t *)&t290))) t297 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t296) + 4, ((uint8_t *)&t290))) t297 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t296) + 6, ((uint8_t *)&t290))) t297 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t296) + 8, ((uint8_t *)&t290))) t297 = 1;
        uint8_t t298 = (uint8_t)(t297);
        if (!t298) goto ie286;
        {
            for (;;) {
                void * t301 = gm_l0;
                gm_tqs5p4n2tq19v t302 = gm_f_peek_bkdvl65(t301);
                if (!((((uint8_t *)&t302))[1] != 0)) goto lb300;
                uint8_t t303 = GM_LD(uint8_t, ((uint8_t *)&t302));
                gm_l19 = t303;
                {
                    uint8_t t307 = 0;
                    uint8_t t308 = 0;
                    uint8_t t309 = 0;
                    {
                        uint8_t t311 = gm_l19;
                        gm_l20 = t311;
                    }
                    uint8_t t312 = 0;
                    uint8_t t313 = gm_l20;
                    uint8_t t314 = (((uint8_t)97ULL) <= t313);
                    if (t314) {
                        uint8_t t315 = gm_l20;
                        uint8_t t316 = (t315 <= ((uint8_t)122ULL));
                        t312 = t316;
                    } else t312 = 0;
                    uint8_t t317 = t312;
                    if (!t317) {
                        {
                            uint8_t t319 = gm_l19;
                            gm_l21 = t319;
                        }
                        uint8_t t320 = 0;
                        uint8_t t321 = gm_l21;
                        uint8_t t322 = (((uint8_t)65ULL) <= t321);
                        if (t322) {
                            uint8_t t323 = gm_l21;
                            uint8_t t324 = (t323 <= ((uint8_t)90ULL));
                            t320 = t324;
                        } else t320 = 0;
                        uint8_t t325 = t320;
                        t309 = t325;
                    } else t309 = 1;
                    if (!t309) {
                        {
                            uint8_t t327 = gm_l19;
                            gm_l22 = t327;
                        }
                        uint8_t t328 = 0;
                        uint8_t t329 = gm_l22;
                        uint8_t t330 = (((uint8_t)48ULL) <= t329);
                        if (t330) {
                            uint8_t t331 = gm_l22;
                            uint8_t t332 = (t331 <= ((uint8_t)57ULL));
                            t328 = t332;
                        } else t328 = 0;
                        uint8_t t333 = t328;
                        t308 = t333;
                    } else t308 = 1;
                    if (!t308) {
                        uint8_t t334 = gm_l19;
                        gm_tb1msgo1fu1n3 t335;
                        GM_ST(uint8_t, ((uint8_t *)&t335 + 0), ((uint8_t)46ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t335 + 1), ((uint8_t)58ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t335 + 2), ((uint8_t)45ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t335 + 3), ((uint8_t)43ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t335 + 4), ((uint8_t)95ULL));
                        uint8_t t336 = 0;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t335) + 0, ((uint8_t *)&t334))) t336 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t335) + 1, ((uint8_t *)&t334))) t336 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t335) + 2, ((uint8_t *)&t334))) t336 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t335) + 3, ((uint8_t *)&t334))) t336 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t335) + 4, ((uint8_t *)&t334))) t336 = 1;
                        uint8_t t337 = (uint8_t)(t336);
                        t307 = t337;
                    } else t307 = 1;
                    uint8_t t338 = (uint8_t)!t307;
                    if (!t338) goto ie306;
                    {
                        goto lb300;
                    }
                    goto ix306;
ie306:;
ix306:;
                }
                {
                    void * t340 = gm_l0;
                    void * t341 = gm_l0;
                    uint64_t t342 = GM_LD(uint64_t, (((uint8_t *)t341) + 16));
                    uint64_t r343; bool o344 = __builtin_add_overflow(t342, ((uint64_t)1ULL), &r343);
                    if (o344) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 336, 13);
                    uint64_t t345 = r343;
                    GM_ST(uint64_t, (((uint8_t *)t340) + 16), t345);
                }
lc300:;
            }
lb300:;
        }
        {
            void * t346 = gm_l0;
            uint64_t t347 = GM_LD(uintptr_t, (((uint8_t *)t346) + 0) + 8);
            void * t348 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t346) + 0)));
            uint64_t t349 = gm_l1;
            uint64_t t350 = (uintptr_t)t349;
            void * t351 = gm_l0;
            uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)t351) + 16));
            uint64_t t353 = (uintptr_t)t352;
            if (t350 > t353 || t353 > (uintptr_t)t347) gm_trap_slice(t350, t353, t347, gm_src3nt8pf0qk5gn, 338, 28);
            gm_b16_8 t354;
            GM_ST(void *, (uint8_t *)&t354, (void *)(((uint8_t *)t348 + t350 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(t353 - t350));
            gm_tktr4rmbindgv t355;
            GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t355 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t355 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t355 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t355 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t355 + 8), t354);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
            GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t355);
            goto gm_fdone;
        }
        goto ix286;
ie286:;
ix286:;
    }
    {
        int64_t t356;
        uint8_t t358 = gm_l2;
        if (!t358) goto ie357;
        int64_t t360 = gm_l7;
        if (t360 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 340, 36);
        int64_t t361 = (int64_t)((uint64_t)0 - (uint64_t)t360);
        int64_t t362 = t361;
        t356 = t362;
        goto ix357;
ie357:;
        int64_t t364 = gm_l7;
        int64_t t365 = t364;
        t356 = t365;
ix357:;
        gm_tktr4rmbindgv t366;
        memset(&t366, 0, sizeof t366);
        GM_ST(uint8_t, ((uint8_t *)&t366 + 0), ((uint8_t)1ULL));
        GM_ST(int64_t, ((uint8_t *)&t366 + 8), t356);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t366);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tefknldmnh2si gm_f_number_4l95pjb_c(void * a0) { gm_tefknldmnh2si gm_r; gm_f_number_4l95pjb(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_string_80niubc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_string_80niubc F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* quote */
    uint8_t gm_l2; /* triple */
    gm_b16_8 gm_l3; /* kind */
    gm_b16_8 gm_l4; /* unclosed */
    uint8_t gm_l6; /* c */
    int32_t gm_l7; /* extra */
    uint8_t gm_l8; /* e */
    uint8_t gm_l9; /* broke */
    uint8_t gm_l10; /* k */
    GM_STACK_CHECK(&F, 2385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src3nt8pf0qk5gn, 345, 14);
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t5) + 0)) + t4 * 1));
        gm_l1 = t7;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 0) + 8);
        void * t10 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 0)));
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 16));
        uint64_t t13 = (uintptr_t)t12;
        if (t13 > (uintptr_t)t9 || (uintptr_t)t9 > (uintptr_t)t9) gm_trap_slice(t13, (uintptr_t)t9, t9, gm_src3nt8pf0qk5gn, 346, 15);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t10 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)t9 - t13));
        gm_b16_8 t15;
        uint8_t t17 = gm_l1;
        uint8_t t18 = ((t17 == ((uint8_t)34ULL)));
        if (!t18) goto ie16;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s8rm42n03sfss));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
        gm_b16_8 t21 = t20;
        t15 = t21;
        goto ix16;
ie16:;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_soakgb2h3g40i));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
        gm_b16_8 t24 = t23;
        t15 = t24;
ix16:;
        uint8_t t25 = gm_f_starts_with_6vvt01e(t14, t15);
        gm_l2 = t25;
    }
    {
        void * t26 = gm_l0;
        uint8_t *t27 = (((uint8_t *)t26) + 16);
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 16));
        uint64_t t30;
        uint8_t t32 = gm_l2;
        if (!t32) goto ie31;
        uint64_t t34 = ((uint64_t)3ULL);
        t30 = t34;
        goto ix31;
ie31:;
        uint64_t t36 = ((uint64_t)1ULL);
        t30 = t36;
ix31:;
        uint64_t r37; bool o38 = __builtin_add_overflow(t29, t30, &r37);
        if (o38) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 347, 5);
        uint64_t t39 = r37;
        GM_ST(uint64_t, t27, t39);
    }
    {
        gm_b16_8 t40;
        uint8_t t42 = gm_l1;
        uint8_t t43 = ((t42 == ((uint8_t)34ULL)));
        if (!t43) goto ie41;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_scvuqmspcpb1m));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(5));
        gm_b16_8 t46 = t45;
        t40 = t46;
        goto ix41;
ie41:;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s4lo69slu4igt));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(7));
        gm_b16_8 t49 = t48;
        t40 = t49;
ix41:;
        gm_l3 = t40;
    }
    {
        gm_b16_8 t50;
        uint8_t t52 = gm_l2;
        if (!t52) goto ie51;
        gm_t6j6smj7qoctu t54;
        gm_b16_8 t55 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)t56) + 0) + 8);
        void * t58 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t56) + 0)));
        void * t59 = gm_l0;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 16));
        uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
        if (o62) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 350, 80);
        uint64_t t63 = r61;
        uint64_t t64 = (uintptr_t)t63;
        void * t65 = gm_l0;
        uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 16));
        uint64_t t67 = (uintptr_t)t66;
        if (t64 > t67 || t67 > (uintptr_t)t57) gm_trap_slice(t64, t67, t57, gm_src3nt8pf0qk5gn, 350, 73);
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t58 + t64 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t64));
        GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t68);
        gm_tabb710d9mq2e t69;
        gm_f_format_u10ruvh((void *)&t69, (void *)NULL, cx, NULL, &t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_src3nt8pf0qk5gn, 350, 9);
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
        gm_b16_8 t71 = t70;
        t50 = t71;
        goto ix51;
ie51:;
        gm_t6j6smj7qoctu t73;
        gm_b16_8 t74 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t73 + 0, t74);
        void * t75 = gm_l0;
        uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)t75) + 0) + 8);
        void * t77 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t75) + 0)));
        void * t78 = gm_l0;
        uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 16));
        uint64_t r80; bool o81 = __builtin_sub_overflow(t79, ((uint64_t)1ULL), &r80);
        if (o81) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 352, 69);
        uint64_t t82 = r80;
        uint64_t t83 = (uintptr_t)t82;
        void * t84 = gm_l0;
        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 16));
        uint64_t t86 = (uintptr_t)t85;
        if (t83 > t86 || t86 > (uintptr_t)t76) gm_trap_slice(t83, t86, t76, gm_src3nt8pf0qk5gn, 352, 62);
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t77 + t83 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(t86 - t83));
        GM_ST(gm_b16_8, (uint8_t *)&t73 + 16, t87);
        gm_tabb710d9mq2e t88;
        gm_f_format_0pvgsic((void *)&t88, (void *)NULL, cx, NULL, &t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_src3nt8pf0qk5gn, 352, 9);
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
        gm_b16_8 t90 = t89;
        t50 = t90;
ix51:;
        gm_l4 = t50;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t92 = gm_l2;
        if (!t92) goto ie91;
        {
            void * t95 = gm_l0;
            gm_tqs5p4n2tq19v t96 = gm_f_peek_bkdvl65(t95);
            gm_tqs5p4n2tq19v t97;
            GM_ST(uint8_t, ((uint8_t *)&t97), ((uint8_t)13ULL));
            ((uint8_t *)&t97)[1] = 1;
            uint8_t t98 = (gm_eqb44cb3cf1n(((uint8_t *)&t96), ((uint8_t *)&t97)));
            if (!t98) goto ie94;
            void * t99 = gm_l0;
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)t99) + 0));
            void * t101 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t100)));
            uint64_t t102 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t100) + 8);
            void * t103 = gm_l0;
            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)t103) + 16));
            uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
            if (o106) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 357, 44);
            uint64_t t107 = r105;
            uint64_t t108 = (uintptr_t)t107;
            gm_tqs5p4n2tq19v t109;
            if (t108 < t102) {
            uint8_t t110 = GM_LD(uint8_t, ((uint8_t *)t101 + t108 * 1));
            gm_tqs5p4n2tq19v t111;
            GM_ST(uint8_t, ((uint8_t *)&t111), t110);
            ((uint8_t *)&t111)[1] = 1;
                t109 = t111;
            } else {
            gm_tqs5p4n2tq19v t112;
            memset(&t112, 0, sizeof t112);
                t109 = t112;
            }
            gm_tqs5p4n2tq19v t113;
            GM_ST(uint8_t, ((uint8_t *)&t113), ((uint8_t)10ULL));
            ((uint8_t *)&t113)[1] = 1;
            uint8_t t114 = (gm_eqb44cb3cf1n(((uint8_t *)&t109), ((uint8_t *)&t113)));
            if (!t114) goto ie94;
            {
                void * t116 = gm_l0;
                void * t117 = gm_l0;
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)t117) + 16));
                uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                if (o120) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 357, 65);
                uint64_t t121 = r119;
                GM_ST(uint64_t, (((uint8_t *)t116) + 16), t121);
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            void * t123 = gm_l0;
            gm_tqs5p4n2tq19v t124 = gm_f_peek_bkdvl65(t123);
            gm_tqs5p4n2tq19v t125;
            GM_ST(uint8_t, ((uint8_t *)&t125), ((uint8_t)10ULL));
            ((uint8_t *)&t125)[1] = 1;
            uint8_t t126 = (gm_eqb44cb3cf1n(((uint8_t *)&t124), ((uint8_t *)&t125)));
            if (!t126) goto ie122;
            {
                void * t128 = gm_l0;
                void * t129 = gm_l0;
                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)t129) + 16));
                uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                if (o132) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 359, 13);
                uint64_t t133 = r131;
                GM_ST(uint64_t, (((uint8_t *)t128) + 16), t133);
            }
            {
                void * t134 = gm_l0;
                void * t135 = gm_l0;
                uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)t135) + 24));
                uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                if (o138) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 360, 13);
                uint64_t t139 = r137;
                GM_ST(uint64_t, (((uint8_t *)t134) + 24), t139);
            }
            goto ix122;
ie122:;
ix122:;
        }
        goto ix91;
ie91:;
ix91:;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb140;
            {
                void * t143 = gm_l0;
                gm_tqs5p4n2tq19v t144 = gm_f_peek_bkdvl65(t143);
                uint8_t t145;
                if (((((uint8_t *)&t144))[1] != 0)) {
                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t144) + 0));
                    t145 = t146;
                } else {
                    void * t147 = gm_l0;
                    gm_b16_8 t148 = gm_l4;
                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                    gm_tob1snuii5bme t150;
                    gm_f_error_96ci5lu((void *)&t150, t147, t149);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t150);
                    goto gm_fdone;
                }
                gm_l6 = t145;
            }
            {
                uint8_t t152 = gm_l6;
                uint8_t t153 = gm_l1;
                uint8_t t154 = ((t152 == t153));
                if (!t154) goto ie151;
                {
                    uint8_t t157 = gm_l2;
                    uint8_t t158 = (uint8_t)!t157;
                    if (!t158) goto ie156;
                    {
                        void * t160 = gm_l0;
                        void * t161 = gm_l0;
                        uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)t161) + 16));
                        uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                        if (o164) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 367, 17);
                        uint64_t t165 = r163;
                        GM_ST(uint64_t, (((uint8_t *)t160) + 16), t165);
                    }
                    {
                        goto lb140;
                    }
                    goto ix156;
ie156:;
ix156:;
                }
                {
                    void * t167 = gm_l0;
                    uint64_t t168 = GM_LD(uintptr_t, (((uint8_t *)t167) + 0) + 8);
                    void * t169 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t167) + 0)));
                    void * t170 = gm_l0;
                    uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)t170) + 16));
                    uint64_t t172 = (uintptr_t)t171;
                    if (t172 > (uintptr_t)t168 || (uintptr_t)t168 > (uintptr_t)t168) gm_trap_slice(t172, (uintptr_t)t168, t168, gm_src3nt8pf0qk5gn, 370, 16);
                    gm_b16_8 t173;
                    GM_ST(void *, (uint8_t *)&t173, (void *)(((uint8_t *)t169 + t172 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)((uintptr_t)t168 - t172));
                    gm_b16_8 t174;
                    uint8_t t176 = gm_l1;
                    uint8_t t177 = ((t176 == ((uint8_t)34ULL)));
                    if (!t177) goto ie175;
                    gm_b16_8 t179;
                    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_s8rm42n03sfss));
                    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(3));
                    gm_b16_8 t180 = t179;
                    t174 = t180;
                    goto ix175;
ie175:;
                    gm_b16_8 t182;
                    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_soakgb2h3g40i));
                    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(3));
                    gm_b16_8 t183 = t182;
                    t174 = t183;
ix175:;
                    uint8_t t184 = gm_f_starts_with_6vvt01e(t173, t174);
                    if (!t184) goto ie166;
                    {
                        void * t186 = gm_l0;
                        void * t187 = gm_l0;
                        uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)t187) + 16));
                        uint64_t r189; bool o190 = __builtin_add_overflow(t188, ((uint64_t)3ULL), &r189);
                        if (o190) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 371, 17);
                        uint64_t t191 = r189;
                        GM_ST(uint64_t, (((uint8_t *)t186) + 16), t191);
                    }
                    {
                        gm_l7 = ((int32_t)0LL);
                    }
                    {
                        for (;;) {
                            int32_t t193 = gm_l7;
                            uint8_t t194 = (t193 < ((int32_t)2LL));
                            if (!t194) goto lb192;
                            void * t195 = gm_l0;
                            gm_tqs5p4n2tq19v t196 = gm_f_peek_bkdvl65(t195);
                            uint8_t t197 = gm_l1;
                            gm_tqs5p4n2tq19v t198;
                            GM_ST(uint8_t, ((uint8_t *)&t198), t197);
                            ((uint8_t *)&t198)[1] = 1;
                            uint8_t t199 = (gm_eqb44cb3cf1n(((uint8_t *)&t196), ((uint8_t *)&t198)));
                            if (!t199) goto lb192;
                            {
                                void * t202 = (void *)((uint8_t *)&F.l5);
                                uint8_t t203 = gm_l1;
                                gm_tftb1odc739t5 t204 = gm_f_push_t83rg2m(t202, t203);
                                if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_src3nt8pf0qk5gn, 375, 21);
                                gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
                            }
                            {
                                void * t206 = gm_l0;
                                void * t207 = gm_l0;
                                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)t207) + 16));
                                uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                                if (o210) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 376, 21);
                                uint64_t t211 = r209;
                                GM_ST(uint64_t, (((uint8_t *)t206) + 16), t211);
                            }
                            {
                                int32_t t212 = gm_l7;
                                int32_t r213; bool o214 = __builtin_add_overflow(t212, ((int32_t)1LL), &r213);
                                if (o214) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 377, 21);
                                int32_t t215 = r213;
                                gm_l7 = t215;
                            }
lc192:;
                        }
lb192:;
                    }
                    {
                        goto lb140;
                    }
                    goto ix166;
ie166:;
ix166:;
                }
                {
                    void * t216 = (void *)((uint8_t *)&F.l5);
                    uint8_t t217 = gm_l6;
                    gm_tftb1odc739t5 t218 = gm_f_push_t83rg2m(t216, t217);
                    if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_src3nt8pf0qk5gn, 381, 13);
                    gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
                }
                {
                    void * t220 = gm_l0;
                    void * t221 = gm_l0;
                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)t221) + 16));
                    uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                    if (o224) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 382, 13);
                    uint64_t t225 = r223;
                    GM_ST(uint64_t, (((uint8_t *)t220) + 16), t225);
                }
                {
                    goto lc140;
                }
                goto ix151;
ie151:;
ix151:;
            }
            {
                uint8_t t227 = gm_l6;
                uint8_t t228 = ((t227 == ((uint8_t)10ULL)));
                if (!t228) goto ie226;
                {
                    uint8_t t231 = gm_l2;
                    uint8_t t232 = (uint8_t)!t231;
                    if (!t232) goto ie230;
                    {
                        void * t234 = gm_l0;
                        gm_b16_8 t235 = gm_l4;
                        gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                        gm_tob1snuii5bme t237;
                        gm_f_error_96ci5lu((void *)&t237, t234, t236);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t237);
                        goto gm_fdone;
                    }
                    goto ix230;
ie230:;
ix230:;
                }
                {
                    void * t238 = gm_l0;
                    void * t239 = gm_l0;
                    uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)t239) + 24));
                    uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                    if (o242) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 387, 13);
                    uint64_t t243 = r241;
                    GM_ST(uint64_t, (((uint8_t *)t238) + 24), t243);
                }
                goto ix226;
ie226:;
ix226:;
            }
            {
                void * t244 = gm_l0;
                void * t245 = gm_l0;
                uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)t245) + 16));
                uint64_t r247; bool o248 = __builtin_add_overflow(t246, ((uint64_t)1ULL), &r247);
                if (o248) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 389, 9);
                uint64_t t249 = r247;
                GM_ST(uint64_t, (((uint8_t *)t244) + 16), t249);
            }
            {
                uint8_t t251 = gm_l6;
                uint8_t t252 = ((t251 == ((uint8_t)92ULL)));
                if (!t252) goto ie250;
                uint8_t t253 = gm_l1;
                uint8_t t254 = ((t253 == ((uint8_t)34ULL)));
                if (!t254) goto ie250;
                {
                    void * t256 = gm_l0;
                    gm_tqs5p4n2tq19v t257 = gm_f_peek_bkdvl65(t256);
                    uint8_t t258;
                    if (((((uint8_t *)&t257))[1] != 0)) {
                        uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&t257) + 0));
                        t258 = t259;
                    } else {
                        void * t260 = gm_l0;
                        gm_b16_8 t261 = gm_l4;
                        gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&t261));
                        gm_tob1snuii5bme t263;
                        gm_f_error_96ci5lu((void *)&t263, t260, t262);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t263);
                        goto gm_fdone;
                    }
                    gm_l8 = t258;
                }
                {
                    void * t264 = gm_l0;
                    void * t265 = gm_l0;
                    uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)t265) + 16));
                    uint64_t r267; bool o268 = __builtin_add_overflow(t266, ((uint64_t)1ULL), &r267);
                    if (o268) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 392, 13);
                    uint64_t t269 = r267;
                    GM_ST(uint64_t, (((uint8_t *)t264) + 16), t269);
                }
                {
                    uint8_t t270 = gm_l8;
                    uint8_t t273 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t273 == ((uint8_t)98ULL))) goto mn272;
                    void * t274 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t275 = gm_f_push_t83rg2m(t274, ((uint8_t)8ULL));
                    if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 1, gm_src3nt8pf0qk5gn, 394, 24);
                    gm_b0_1 t276 = GM_LD(gm_b0_1, (((uint8_t *)&t275) + 1));
                    goto me271;
mn272:;
                    uint8_t t278 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t278 == ((uint8_t)101ULL))) goto mn277;
                    void * t279 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t280 = gm_f_push_t83rg2m(t279, ((uint8_t)27ULL));
                    if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_src3nt8pf0qk5gn, 395, 24);
                    gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
                    goto me271;
mn277:;
                    uint8_t t283 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t283 == ((uint8_t)102ULL))) goto mn282;
                    void * t284 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t285 = gm_f_push_t83rg2m(t284, ((uint8_t)12ULL));
                    if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 1, gm_src3nt8pf0qk5gn, 396, 24);
                    gm_b0_1 t286 = GM_LD(gm_b0_1, (((uint8_t *)&t285) + 1));
                    goto me271;
mn282:;
                    uint8_t t288 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t288 == ((uint8_t)110ULL))) goto mn287;
                    void * t289 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t290 = gm_f_push_t83rg2m(t289, ((uint8_t)10ULL));
                    if (((((uint8_t *)&t290))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t290) + 1, gm_src3nt8pf0qk5gn, 397, 24);
                    gm_b0_1 t291 = GM_LD(gm_b0_1, (((uint8_t *)&t290) + 1));
                    goto me271;
mn287:;
                    uint8_t t293 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t293 == ((uint8_t)114ULL))) goto mn292;
                    void * t294 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t295 = gm_f_push_t83rg2m(t294, ((uint8_t)13ULL));
                    if (((((uint8_t *)&t295))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t295) + 1, gm_src3nt8pf0qk5gn, 398, 24);
                    gm_b0_1 t296 = GM_LD(gm_b0_1, (((uint8_t *)&t295) + 1));
                    goto me271;
mn292:;
                    uint8_t t298 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t298 == ((uint8_t)116ULL))) goto mn297;
                    void * t299 = (void *)((uint8_t *)&F.l5);
                    gm_tftb1odc739t5 t300 = gm_f_push_t83rg2m(t299, ((uint8_t)9ULL));
                    if (((((uint8_t *)&t300))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t300) + 1, gm_src3nt8pf0qk5gn, 399, 24);
                    gm_b0_1 t301 = GM_LD(gm_b0_1, (((uint8_t *)&t300) + 1));
                    goto me271;
mn297:;
                    uint8_t t305 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t305 == ((uint8_t)34ULL))) goto pa304;
                    goto po303;
pa304:;
                    uint8_t t307 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t307 == ((uint8_t)92ULL))) goto pa306;
                    goto po303;
pa306:;
                    goto mn302;
po303:;
                    void * t308 = (void *)((uint8_t *)&F.l5);
                    uint8_t t309 = gm_l8;
                    gm_tftb1odc739t5 t310 = gm_f_push_t83rg2m(t308, t309);
                    if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_src3nt8pf0qk5gn, 400, 30);
                    gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
                    goto me271;
mn302:;
                    uint8_t t313 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t313 == ((uint8_t)120ULL))) goto mn312;
                    void * t314 = gm_l0;
                    void * t315 = (void *)((uint8_t *)&F.l5);
                    gm_tr1k5nl69lhjo t316;
                    gm_f_hex_escape_eoc96dk((void *)&t316, (void *)NULL, cx, NULL, t314, t315, ((uint64_t)2ULL));
                    if (gm_unw.kind) {
                        goto gm_ll141;
                    }
                    if (((((uint8_t *)&t316))[0] != 0)) {
                        gm_tu615l4qlr8b2 t317;
                        GM_ST(uint8_t, (uint8_t *)&t317 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t317 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t317 + 4, 0);
                        ((uint8_t *)&t317)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t317 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t316) + 8)));
                        F.ret = t317;
                        goto gm_fret;
                    }
                    gm_b0_1 t318 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 8));
                    goto me271;
mn312:;
                    uint8_t t320 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t320 == ((uint8_t)117ULL))) goto mn319;
                    void * t321 = gm_l0;
                    void * t322 = (void *)((uint8_t *)&F.l5);
                    gm_tr1k5nl69lhjo t323;
                    gm_f_hex_escape_eoc96dk((void *)&t323, (void *)NULL, cx, NULL, t321, t322, ((uint64_t)4ULL));
                    if (gm_unw.kind) {
                        goto gm_ll141;
                    }
                    if (((((uint8_t *)&t323))[0] != 0)) {
                        gm_tu615l4qlr8b2 t324;
                        GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
                        ((uint8_t *)&t324)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t324 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t323) + 8)));
                        F.ret = t324;
                        goto gm_fret;
                    }
                    gm_b0_1 t325 = GM_LD(gm_b0_1, (((uint8_t *)&t323) + 8));
                    goto me271;
mn319:;
                    uint8_t t327 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t327 == ((uint8_t)85ULL))) goto mn326;
                    void * t328 = gm_l0;
                    void * t329 = (void *)((uint8_t *)&F.l5);
                    gm_tr1k5nl69lhjo t330;
                    gm_f_hex_escape_eoc96dk((void *)&t330, (void *)NULL, cx, NULL, t328, t329, ((uint64_t)8ULL));
                    if (gm_unw.kind) {
                        goto gm_ll141;
                    }
                    if (((((uint8_t *)&t330))[0] != 0)) {
                        gm_tu615l4qlr8b2 t331;
                        GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                        ((uint8_t *)&t331)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t331 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t330) + 8)));
                        F.ret = t331;
                        goto gm_fret;
                    }
                    gm_b0_1 t332 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 8));
                    goto me271;
mn326:;
                    uint8_t t336 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t336 == ((uint8_t)32ULL))) goto pa335;
                    goto po334;
pa335:;
                    uint8_t t338 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t338 == ((uint8_t)9ULL))) goto pa337;
                    goto po334;
pa337:;
                    uint8_t t340 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t340 == ((uint8_t)13ULL))) goto pa339;
                    goto po334;
pa339:;
                    uint8_t t342 = GM_LD(uint8_t, ((uint8_t *)&t270));
                    if (!(t342 == ((uint8_t)10ULL))) goto pa341;
                    goto po334;
pa341:;
                    goto mn333;
po334:;
                    {
                        uint8_t t345 = gm_l2;
                        uint8_t t346 = (uint8_t)!t345;
                        if (!t346) goto ie344;
                        {
                            void * t348 = gm_l0;
                            gm_b16_8 t349 = GM_LD(gm_b16_8, gm_cn9bf8d1ktm);
                            gm_tob1snuii5bme t350;
                            gm_f_error_96ci5lu((void *)&t350, t348, t349);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t350);
                            goto gm_fdone;
                        }
                        goto ix344;
ie344:;
ix344:;
                    }
                    {
                        void * t351 = gm_l0;
                        void * t352 = gm_l0;
                        uint64_t t353 = GM_LD(uint64_t, (((uint8_t *)t352) + 16));
                        uint64_t r354; bool o355 = __builtin_sub_overflow(t353, ((uint64_t)1ULL), &r354);
                        if (o355) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 407, 21);
                        uint64_t t356 = r354;
                        GM_ST(uint64_t, (((uint8_t *)t351) + 16), t356);
                    }
                    {
                        gm_l9 = ((uint8_t)0);
                    }
                    {
                        for (;;) {
                            void * t358 = gm_l0;
                            gm_tqs5p4n2tq19v t359 = gm_f_peek_bkdvl65(t358);
                            if (!((((uint8_t *)&t359))[1] != 0)) goto lb357;
                            uint8_t t360 = GM_LD(uint8_t, ((uint8_t *)&t359));
                            gm_l10 = t360;
                            {
                                uint8_t t364 = gm_l10;
                                uint8_t t365 = ((t364 == ((uint8_t)10ULL)));
                                if (!t365) goto ie363;
                                {
                                    void * t367 = gm_l0;
                                    void * t368 = gm_l0;
                                    uint64_t t369 = GM_LD(uint64_t, (((uint8_t *)t368) + 24));
                                    uint64_t r370; bool o371 = __builtin_add_overflow(t369, ((uint64_t)1ULL), &r370);
                                    if (o371) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 411, 29);
                                    uint64_t t372 = r370;
                                    GM_ST(uint64_t, (((uint8_t *)t367) + 24), t372);
                                }
                                {
                                    gm_l9 = ((uint8_t)1);
                                }
                                goto ix363;
ie363:;
                                uint8_t t374 = gm_l10;
                                gm_toh0ootodhjue t375;
                                GM_ST(uint8_t, ((uint8_t *)&t375 + 0), ((uint8_t)32ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t375 + 1), ((uint8_t)9ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t375 + 2), ((uint8_t)13ULL));
                                uint8_t t376 = 0;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t375) + 0, ((uint8_t *)&t374))) t376 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t375) + 1, ((uint8_t *)&t374))) t376 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t375) + 2, ((uint8_t *)&t374))) t376 = 1;
                                uint8_t t377 = (uint8_t)(!t376);
                                if (!t377) goto ie373;
                                {
                                    goto lb357;
                                }
                                goto ix373;
ie373:;
ix373:;
ix363:;
                            }
                            {
                                void * t379 = gm_l0;
                                void * t380 = gm_l0;
                                uint64_t t381 = GM_LD(uint64_t, (((uint8_t *)t380) + 16));
                                uint64_t r382; bool o383 = __builtin_add_overflow(t381, ((uint64_t)1ULL), &r382);
                                if (o383) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 416, 25);
                                uint64_t t384 = r382;
                                GM_ST(uint64_t, (((uint8_t *)t379) + 16), t384);
                            }
lc357:;
                        }
lb357:;
                    }
                    {
                        uint8_t t386 = gm_l9;
                        uint8_t t387 = (uint8_t)!t386;
                        if (!t387) goto ie385;
                        {
                            void * t389 = gm_l0;
                            gm_b16_8 t390 = GM_LD(gm_b16_8, gm_cn9bf8d1ktm);
                            gm_tob1snuii5bme t391;
                            gm_f_error_96ci5lu((void *)&t391, t389, t390);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t391);
                            goto gm_fdone;
                        }
                        goto ix385;
ie385:;
ix385:;
                    }
                    goto me271;
mn333:;
                    void * t393 = gm_l0;
                    gm_b16_8 t394 = GM_LD(gm_b16_8, gm_cn9bf8d1ktm);
                    gm_tob1snuii5bme t395;
                    gm_f_error_96ci5lu((void *)&t395, t393, t394);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 1;
                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t395);
                    goto gm_fdone;
                    goto me271;
mn392:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 393, 13);
me271:;
                }
                goto ix250;
ie250:;
                {
                    void * t397 = (void *)((uint8_t *)&F.l5);
                    uint8_t t398 = gm_l6;
                    gm_tftb1odc739t5 t399 = gm_f_push_t83rg2m(t397, t398);
                    if (((((uint8_t *)&t399))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t399) + 1, gm_src3nt8pf0qk5gn, 423, 13);
                    gm_b0_1 t400 = GM_LD(gm_b0_1, (((uint8_t *)&t399) + 1));
                }
ix250:;
            }
lc140:;
        }
gm_ll141:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb140; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc140; }
        }
        goto gm_lad;
lb140:;
    }
    {
        gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tu615l4qlr8b2 *)gm_out) + 8, t402);
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
inline __attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_toml_gjtmf79 F;
    gm_b16_8 gm_l0; /* text */
    void * gm_l2; /* root */
    void * gm_l3; /* current */
    uint8_t gm_l4; /* c */
    uint64_t gm_l5; /* line */
    GM_STACK_CHECK(&F, 732u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&F.l1 + 33, 0, 7);
        gm_b16_8 t2 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&F.l1 + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&F.l1 + 24, ((uint64_t)1ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 32, ((uint8_t)0));
    }
    {
        void * t3 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        gm_l3 = t4;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb5;
            {
                void * t8 = (void *)((uint8_t *)&F.l1);
                gm_f_skip_space_and_newlines_qagb1bk(t8);
            }
            {
                void * t9 = (void *)((uint8_t *)&F.l1);
                gm_tqs5p4n2tq19v t10 = gm_f_peek_bkdvl65(t9);
                uint8_t t11;
                if (((((uint8_t *)&t10))[1] != 0)) {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                    t11 = t12;
                } else {
                    goto lb5;
                }
                gm_l4 = t11;
            }
            {
                uint8_t t14 = gm_l4;
                uint8_t t15 = ((t14 == ((uint8_t)91ULL)));
                if (!t15) goto ie13;
                {
                    void * t17 = (void *)((uint8_t *)&F.l1);
                    void * t18 = gm_l2;
                    gm_trssmtuv8jr0v t19;
                    gm_f_table_header_j60mb6n((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    if (((((uint8_t *)&t19))[0] != 0)) {
                        gm_trssmtuv8jr0v t20;
                        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                        ((uint8_t *)&t20)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t20 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t19) + 8)));
                        F.ret = t20;
                        goto gm_fret;
                    }
                    void * t21 = GM_LD(void *, (((uint8_t *)&t19) + 8));
                    gm_l3 = t21;
                }
                goto ix13;
ie13:;
                {
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 24));
                    gm_l5 = t23;
                }
                {
                    void * t24 = (void *)((uint8_t *)&F.l1);
                    gm_t21m1eu5ta01n t25;
                    gm_f_key_path_7spb050((void *)&t25, (void *)NULL, cx, NULL, t24);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    if (((((uint8_t *)&t25))[0] != 0)) {
                        gm_trssmtuv8jr0v t26;
                        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                        ((uint8_t *)&t26)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t26 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t25) + 8)));
                        F.ret = t26;
                        goto gm_fret;
                    }
                    F.l6 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t25) + 8));
                }
                {
                    void * t27 = (void *)((uint8_t *)&F.l1);
                    gm_f_skip_space_voq272o(t27);
                }
                {
                    void * t29 = (void *)((uint8_t *)&F.l1);
                    gm_tqs5p4n2tq19v t30 = gm_f_peek_bkdvl65(t29);
                    gm_tqs5p4n2tq19v t31;
                    GM_ST(uint8_t, ((uint8_t *)&t31), ((uint8_t)61ULL));
                    ((uint8_t *)&t31)[1] = 1;
                    uint8_t t32 = (!gm_eqb44cb3cf1n(((uint8_t *)&t30), ((uint8_t *)&t31)));
                    if (!t32) goto ie28;
                    {
                        void * t34 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t35;
                        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_spl83f693c752));
                        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(31));
                        gm_tob1snuii5bme t36;
                        gm_f_error_96ci5lu((void *)&t36, t34, t35);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t36);
                        goto gm_fdone;
                    }
                    goto ix28;
ie28:;
ix28:;
                }
                {
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 16));
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    if (o39) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 85, 13);
                    uint64_t t40 = r38;
                    GM_ST(uint64_t, (((uint8_t *)&F.l1) + 16), t40);
                }
                {
                    void * t41 = (void *)((uint8_t *)&F.l1);
                    gm_f_skip_space_voq272o(t41);
                }
                {
                    void * t42 = (void *)((uint8_t *)&F.l1);
                    gm_tefknldmnh2si t43;
                    gm_f_value_oq952o2((void *)&t43, (void *)NULL, cx, NULL, t42);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    if (((((uint8_t *)&t43))[0] != 0)) {
                        gm_trssmtuv8jr0v t44;
                        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                        ((uint8_t *)&t44)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t44 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t43) + 8)));
                        F.ret = t44;
                        goto gm_fret;
                    }
                    F.l7 = (*(gm_tktr4rmbindgv *)(((uint8_t *)&t43) + 8));
                }
                {
                    void * t45 = gm_l3;
                    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                    uint64_t t48 = gm_l5;
                    gm_tr1k5nl69lhjo t49;
                    gm_f_insert_path_cvuqfkc((void *)&t49, (void *)NULL, cx, NULL, t45, t47, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l7)), t48);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    if (((((uint8_t *)&t49))[0] != 0)) {
                        gm_trssmtuv8jr0v t50;
                        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                        ((uint8_t *)&t50)[0] = 1;
                        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t50 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t49) + 8)));
                        F.ret = t50;
                        goto gm_fret;
                    }
                    gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 8));
                }
ix13:;
            }
            {
                void * t52 = (void *)((uint8_t *)&F.l1);
                gm_tr1k5nl69lhjo t53;
                gm_f_end_of_line_056kej4((void *)&t53, t52);
                if (((((uint8_t *)&t53))[0] != 0)) {
                    gm_trssmtuv8jr0v t54;
                    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                    ((uint8_t *)&t54)[0] = 1;
                    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t54 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t53) + 8)));
                    F.ret = t54;
                    goto gm_fret;
                }
                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 8));
            }
lc5:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
        }
        goto gm_lad;
lb5:;
    }
    {
        void * t56 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 0;
        GM_ST(void *, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t56);
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_bkdvl65(void * a0);
inline __attribute__((aligned(4))) void gm_f_skip_space_and_newlines_qagb1bk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_space_and_newlines_qagb1bk F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_tqs5p4n2tq19v t4 = gm_f_peek_bkdvl65(t3);
            if (!((((uint8_t *)&t4))[1] != 0)) goto lb2;
            uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&t4));
            gm_l1 = t5;
            {
                uint8_t t9 = gm_l1;
                gm_toh0ootodhjue t10;
                GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t10 + 1), ((uint8_t)9ULL));
                GM_ST(uint8_t, ((uint8_t *)&t10 + 2), ((uint8_t)13ULL));
                uint8_t t11 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t10) + 0, ((uint8_t *)&t9))) t11 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t10) + 1, ((uint8_t *)&t9))) t11 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t10) + 2, ((uint8_t *)&t9))) t11 = 1;
                uint8_t t12 = (uint8_t)(t11);
                if (!t12) goto ie8;
                {
                    void * t14 = gm_l0;
                    void * t15 = gm_l0;
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
                    uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                    if (o18) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 118, 13);
                    uint64_t t19 = r17;
                    GM_ST(uint64_t, (((uint8_t *)t14) + 16), t19);
                }
                goto ix8;
ie8:;
                uint8_t t21 = gm_l1;
                uint8_t t22 = ((t21 == ((uint8_t)10ULL)));
                if (!t22) goto ie20;
                {
                    void * t24 = gm_l0;
                    void * t25 = gm_l0;
                    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 16));
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    if (o28) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 120, 13);
                    uint64_t t29 = r27;
                    GM_ST(uint64_t, (((uint8_t *)t24) + 16), t29);
                }
                {
                    void * t30 = gm_l0;
                    void * t31 = gm_l0;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 121, 13);
                    uint64_t t35 = r33;
                    GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
                }
                goto ix20;
ie20:;
                uint8_t t37 = gm_l1;
                uint8_t t38 = ((t37 == ((uint8_t)35ULL)));
                if (!t38) goto ie36;
                {
                    for (;;) {
                        void * t41 = gm_l0;
                        gm_tqs5p4n2tq19v t42 = gm_f_peek_bkdvl65(t41);
                        uint8_t t43;
                        if (((((uint8_t *)&t42))[1] != 0)) goto mn45;
                        t43 = ((uint8_t)0);
                        goto me44;
mn45:;
                        t43 = ((uint8_t)1);
                        goto me44;
mn46:;
                        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 123, 19);
me44:;
                        if (!t43) goto lb40;
                        void * t47 = gm_l0;
                        gm_tqs5p4n2tq19v t48 = gm_f_peek_bkdvl65(t47);
                        gm_tqs5p4n2tq19v t49;
                        GM_ST(uint8_t, ((uint8_t *)&t49), ((uint8_t)10ULL));
                        ((uint8_t *)&t49)[1] = 1;
                        uint8_t t50 = (!gm_eqb44cb3cf1n(((uint8_t *)&t48), ((uint8_t *)&t49)));
                        if (!t50) goto lb40;
                        {
                            void * t53 = gm_l0;
                            void * t54 = gm_l0;
                            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 16));
                            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                            if (o57) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 123, 67);
                            uint64_t t58 = r56;
                            GM_ST(uint64_t, (((uint8_t *)t53) + 16), t58);
                        }
lc40:;
                    }
lb40:;
                }
                goto ix36;
ie36:;
                {
                    gm_b0_1 t60;
                    uint8_t t61 = 0;
                    F.ret = t60;
                    goto gm_fret;
                }
ix36:;
ix20:;
ix8:;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_skip_space_voq272o(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_space_voq272o F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_tqs5p4n2tq19v t4 = gm_f_peek_bkdvl65(t3);
            gm_tqs5p4n2tq19v t5;
            GM_ST(uint8_t, ((uint8_t *)&t5), ((uint8_t)32ULL));
            ((uint8_t *)&t5)[1] = 1;
            gm_tqs5p4n2tq19v t6;
            GM_ST(uint8_t, ((uint8_t *)&t6), ((uint8_t)9ULL));
            ((uint8_t *)&t6)[1] = 1;
            gm_t5r6k0fdcfsb3 t7;
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t7 + 0), t5);
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t7 + 2), t6);
            uint8_t t8 = 0;
            if (gm_eqb44cb3cf1n(((uint8_t *)&t7) + 0, ((uint8_t *)&t4))) t8 = 1;
            if (gm_eqb44cb3cf1n(((uint8_t *)&t7) + 2, ((uint8_t *)&t4))) t8 = 1;
            uint8_t t9 = (uint8_t)(t8);
            if (!t9) goto lb2;
            {
                void * t12 = gm_l0;
                void * t13 = gm_l0;
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 111, 37);
                uint64_t t17 = r15;
                GM_ST(uint64_t, (((uint8_t *)t12) + 16), t17);
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_table_header_j60mb6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_table_header_j60mb6n F;
    void * gm_l0; /* r */
    void * gm_l1; /* root */
    uint8_t gm_l2; /* array */
    uint64_t gm_l3; /* line */
    gm_b16_8 gm_l5; /* names */
    void * gm_l6; /* t */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* name */
    uint8_t gm_l11; /* last */
    void * gm_l12; /* e */
    void * gm_l14; /* fresh */
    void * gm_l16; /* sub */
    void * gm_l18; /* sub */
    void * gm_l19; /* fresh */
    GM_STACK_CHECK(&F, 1951u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 150, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 16), t7);
    }
    {
        void * t8 = gm_l0;
        gm_tqs5p4n2tq19v t9 = gm_f_peek_bkdvl65(t8);
        gm_tqs5p4n2tq19v t10;
        GM_ST(uint8_t, ((uint8_t *)&t10), ((uint8_t)91ULL));
        ((uint8_t *)&t10)[1] = 1;
        uint8_t t11 = (gm_eqb44cb3cf1n(((uint8_t *)&t9), ((uint8_t *)&t10)));
        gm_l2 = t11;
    }
    {
        uint8_t t13 = gm_l2;
        if (!t13) goto ie12;
        {
            void * t15 = gm_l0;
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 16));
            uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 152, 16);
            uint64_t t20 = r18;
            GM_ST(uint64_t, (((uint8_t *)t15) + 16), t20);
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t21 = gm_l0;
        gm_f_skip_space_voq272o(t21);
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 24));
        gm_l3 = t23;
    }
    {
        void * t24 = gm_l0;
        gm_t21m1eu5ta01n t25;
        gm_f_key_path_7spb050((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) {
            gm_trssmtuv8jr0v t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            ((uint8_t *)&t26)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t26 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t25) + 8)));
            F.ret = t26;
            goto gm_fret;
        }
        F.l4 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t25) + 8));
    }
    {
        void * t27 = gm_l0;
        gm_f_skip_space_voq272o(t27);
    }
    {
        void * t29 = gm_l0;
        gm_tqs5p4n2tq19v t30 = gm_f_peek_bkdvl65(t29);
        gm_tqs5p4n2tq19v t31;
        GM_ST(uint8_t, ((uint8_t *)&t31), ((uint8_t)93ULL));
        ((uint8_t *)&t31)[1] = 1;
        uint8_t t32 = (!gm_eqb44cb3cf1n(((uint8_t *)&t30), ((uint8_t *)&t31)));
        if (!t32) goto ie28;
        {
            void * t34 = gm_l0;
            gm_b16_8 t35;
            uint8_t t37 = gm_l2;
            if (!t37) goto ie36;
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sdq87al6fhs9i));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(34));
            gm_b16_8 t40 = t39;
            t35 = t40;
            goto ix36;
ie36:;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_std877jr2ed05));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(28));
            gm_b16_8 t43 = t42;
            t35 = t43;
ix36:;
            gm_tob1snuii5bme t44;
            gm_f_error_96ci5lu((void *)&t44, t34, t35);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t44);
            goto gm_fdone;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t45 = gm_l0;
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 16));
        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
        if (o49) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 158, 5);
        uint64_t t50 = r48;
        GM_ST(uint64_t, (((uint8_t *)t45) + 16), t50);
    }
    {
        uint8_t t52 = gm_l2;
        if (!t52) goto ie51;
        {
            void * t55 = gm_l0;
            gm_tqs5p4n2tq19v t56 = gm_f_peek_bkdvl65(t55);
            gm_tqs5p4n2tq19v t57;
            GM_ST(uint8_t, ((uint8_t *)&t57), ((uint8_t)93ULL));
            ((uint8_t *)&t57)[1] = 1;
            uint8_t t58 = (!gm_eqb44cb3cf1n(((uint8_t *)&t56), ((uint8_t *)&t57)));
            if (!t58) goto ie54;
            {
                void * t60 = gm_l0;
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sdq87al6fhs9i));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(34));
                gm_tob1snuii5bme t62;
                gm_f_error_96ci5lu((void *)&t62, t60, t61);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t62);
                goto gm_fdone;
            }
            goto ix54;
ie54:;
ix54:;
        }
        {
            void * t63 = gm_l0;
            void * t64 = gm_l0;
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 16));
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            if (o67) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 161, 9);
            uint64_t t68 = r66;
            GM_ST(uint64_t, (((uint8_t *)t63) + 16), t68);
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_l5 = t69;
    }
    {
        void * t70 = gm_l1;
        gm_l6 = t70;
    }
    {
        {
            gm_b16_8 t72 = gm_l5;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_l7 = t73;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t75 = gm_l8;
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    gm_tf1bjg81u0glv t80;
                    uint64_t t81 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t80 + 0, t81);
                    uint64_t t82 = gm_l8;
                    uint64_t t83 = (uintptr_t)t82;
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t83 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t80 + 8, t85);
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&t80) + 0));
                    gm_l9 = t88;
                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                    gm_l10 = t89;
                    {
                        uint64_t t91 = gm_l9;
                        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint64_t r93; bool o94 = __builtin_sub_overflow(t92, ((uint64_t)1ULL), &r93);
                        if (o94) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 166, 22);
                        uint64_t t95 = r93;
                        uint8_t t96 = ((t91 == t95));
                        gm_l11 = t96;
                    }
                    {
                        void * t98 = gm_l6;
                        gm_b16_8 t99 = gm_l10;
                        void * t100 = gm_f_get_9rme0gb(t98, t99);
                        if (!(GM_LD(void *, ((uint8_t *)&t100)) != 0)) goto ie97;
                        void * t101 = GM_LD(void *, ((uint8_t *)&t100));
                        gm_l12 = t101;
                        {
                            uint8_t t104 = gm_l11;
                            if (!t104) goto ie103;
                            uint8_t t105 = gm_l2;
                            if (!t105) goto ie103;
                            {
                                void * t108 = gm_l12;
                                uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t108) + 16) + 0));
                                if (t109 != ((uint8_t)3ULL)) goto ie107;
                                gm_trqfvmjtt4sij t110 = GM_LD(gm_trqfvmjtt4sij, ((((uint8_t *)t108) + 16) + 8));
                                F.l13 = t110;
                                {
                                    void * t112 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_ll78;
                                    }
                                    gm_l14 = t112;
                                }
                                {
                                    gm_trqfvmjtt4sij t113 = F.l13;
                                    F.l15 = t113;
                                }
                                {
                                    void * t114 = (void *)((uint8_t *)&F.l15);
                                    void * t115 = gm_l14;
                                    gm_tktr4rmbindgv t116;
                                    memset(&t116, 0, sizeof t116);
                                    GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)4ULL));
                                    GM_ST(void *, ((uint8_t *)&t116 + 8), t115);
                                    gm_tftb1odc739t5 t117 = gm_f_push_0pgacsj_2(t114, &t116);
                                    if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_src3nt8pf0qk5gn, 172, 17);
                                    gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
                                }
                                {
                                    void * t119 = gm_l12;
                                    const uint8_t *t120 = ((uint8_t *)&F.l15);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tktr4rmbindgv *)(((uint8_t *)t119) + 16)) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tktr4rmbindgv *)(((uint8_t *)t119) + 16)) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tktr4rmbindgv *)(((uint8_t *)t119) + 16)) + 4, 0);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tktr4rmbindgv *)(((uint8_t *)t119) + 16)) + 0, ((uint8_t)3ULL));
                                    memmove((uint8_t *)&(*(gm_tktr4rmbindgv *)(((uint8_t *)t119) + 16)) + 8, t120, 40);
                                }
                                {
                                    void * t121 = gm_l14;
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 0;
                                    GM_ST(void *, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t121);
                                    goto gm_fdone;
                                }
                                goto ix107;
ie107:;
                                {
                                    void * t123 = gm_l0;
                                    gm_b16_8 t124;
                                    GM_ST(void *, (uint8_t *)&t124, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t125;
                                    gm_f_error_96ci5lu((void *)&t125, t123, t124);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t125);
                                    goto gm_fdone;
                                }
ix107:;
                            }
                            goto ix103;
ie103:;
ix103:;
                        }
                        {
                            void * t126 = gm_l12;
                            gm_tktr4rmbindgv t127 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)t126) + 16));
                            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t127) + 0));
                            switch (t133) { case ((uint8_t)0ULL): goto ma131; case ((uint8_t)1ULL): goto ma131; case ((uint8_t)2ULL): goto ma131; case ((uint8_t)3ULL): goto ma130; case ((uint8_t)4ULL): goto ma129; case ((uint8_t)5ULL): goto ma131; default: goto ma131; }
ma129:;
                            uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&t127) + 0));
                            if (t135 != ((uint8_t)4ULL)) goto mn134;
                            void * t136 = GM_LD(void *, (((uint8_t *)&t127) + 8));
                            gm_l16 = t136;
                            {
                                uint8_t t139 = gm_l11;
                                if (!t139) goto ie138;
                                uint8_t t140 = gm_l2;
                                uint8_t t141 = (uint8_t)!t140;
                                if (!t141) goto ie138;
                                void * t142 = gm_l16;
                                uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)t142) + 40));
                                if (!t143) goto ie138;
                                {
                                    void * t145 = gm_l0;
                                    gm_b16_8 t146;
                                    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t147;
                                    gm_f_error_96ci5lu((void *)&t147, t145, t146);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t147);
                                    goto gm_fdone;
                                }
                                goto ix138;
ie138:;
ix138:;
                            }
                            {
                                void * t148 = gm_l16;
                                gm_l6 = t148;
                            }
                            goto me128;
mn134:;
ma130:;
                            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&t127) + 0));
                            if (t150 != ((uint8_t)3ULL)) goto mn149;
                            F.l17 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t127) + 8));
                            uint64_t t151 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                            uint8_t t152 = (t151 > ((uint64_t)0ULL));
                            if (!t152) goto mn149;
                            {
                                uint8_t t155 = gm_l11;
                                if (!t155) goto ie154;
                                uint8_t t156 = gm_l2;
                                uint8_t t157 = (uint8_t)!t156;
                                if (!t157) goto ie154;
                                {
                                    void * t159 = gm_l0;
                                    gm_b16_8 t160;
                                    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t161;
                                    gm_f_error_96ci5lu((void *)&t161, t159, t160);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t161);
                                    goto gm_fdone;
                                }
                                goto ix154;
ie154:;
ix154:;
                            }
                            {
                                uint64_t t163 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                                uint64_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint64_t)1ULL), &r164);
                                if (o165) gm_trap("integer overflow in `-`", gm_src3nt8pf0qk5gn, 183, 48);
                                uint64_t t166 = r164;
                                uint64_t t167 = (uintptr_t)t166;
                                uint64_t t168 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                                if (t167 >= (uintptr_t)(t168)) gm_trap_index(t167, t168, gm_src3nt8pf0qk5gn, 183, 36);
                                uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 0)) + t167 * 48) + 0));
                                if (t169 != ((uint8_t)4ULL)) goto ie162;
                                void * t170 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 0)) + t167 * 48) + 8));
                                gm_l18 = t170;
                                {
                                    void * t172 = gm_l18;
                                    gm_l6 = t172;
                                }
                                goto ix162;
ie162:;
                                {
                                    void * t174 = gm_l0;
                                    gm_b16_8 t175;
                                    GM_ST(void *, (uint8_t *)&t175, (void *)(gm_skiffh4urhl7e));
                                    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(13));
                                    gm_tob1snuii5bme t176;
                                    gm_f_error_96ci5lu((void *)&t176, t174, t175);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                                    GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t176);
                                    goto gm_fdone;
                                }
ix162:;
                            }
                            goto me128;
mn149:;
ma131:;
                            void * t178 = gm_l0;
                            gm_b16_8 t179;
                            GM_ST(void *, (uint8_t *)&t179, (void *)(gm_skiffh4urhl7e));
                            GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(13));
                            gm_tob1snuii5bme t180;
                            gm_f_error_96ci5lu((void *)&t180, t178, t179);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 1;
                            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t180);
                            goto gm_fdone;
                            goto me128;
mn177:;
ma132:;
                            gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 176, 13);
me128:;
                        }
                        goto ix97;
ie97:;
                        {
                            void * t182 = gm_f_new_table_hh6cki7((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            gm_l19 = t182;
                        }
                        {
                            uint8_t t184 = gm_l11;
                            if (!t184) goto ie183;
                            uint8_t t185 = gm_l2;
                            if (!t185) goto ie183;
                            {
                                gm_f_list_0202sdh_2((void *)&F.l20, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll78;
                                }
                            }
                            {
                                void * t187 = (void *)((uint8_t *)&F.l20);
                                void * t188 = gm_l19;
                                gm_tktr4rmbindgv t189;
                                memset(&t189, 0, sizeof t189);
                                GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t189 + 8), t188);
                                gm_tftb1odc739t5 t190 = gm_f_push_0pgacsj_2(t187, &t189);
                                if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_src3nt8pf0qk5gn, 192, 17);
                                gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
                            }
                            {
                                void * t192 = gm_l6;
                                void * t193 = (void *)(((uint8_t *)t192) + 0);
                                gm_trc3jdv2g5qre t194;
                                gm_b16_8 t195 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t194 + 0, t195);
                                gm_trqfvmjtt4sij t196 = F.l20;
                                gm_tktr4rmbindgv t197;
                                GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)3ULL));
                                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t197 + 8), t196);
                                GM_ST(gm_tktr4rmbindgv, (uint8_t *)&t194 + 16, t197);
                                uint64_t t198 = gm_l3;
                                GM_ST(uint64_t, (uint8_t *)&t194 + 64, t198);
                                gm_tftb1odc739t5 t199 = gm_f_push_2pp1ri6(t193, &t194);
                                if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_src3nt8pf0qk5gn, 193, 17);
                                gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
                            }
                            goto ix183;
ie183:;
                            {
                                void * t202 = gm_l6;
                                void * t203 = (void *)(((uint8_t *)t202) + 0);
                                gm_trc3jdv2g5qre t204;
                                gm_b16_8 t205 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t204 + 0, t205);
                                void * t206 = gm_l19;
                                gm_tktr4rmbindgv t207;
                                memset(&t207, 0, sizeof t207);
                                GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t207 + 8), t206);
                                GM_ST(gm_tktr4rmbindgv, (uint8_t *)&t204 + 16, t207);
                                uint64_t t208 = gm_l3;
                                GM_ST(uint64_t, (uint8_t *)&t204 + 64, t208);
                                gm_tftb1odc739t5 t209 = gm_f_push_2pp1ri6(t203, &t204);
                                if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 1, gm_src3nt8pf0qk5gn, 195, 17);
                                gm_b0_1 t210 = GM_LD(gm_b0_1, (((uint8_t *)&t209) + 1));
                            }
ix183:;
                        }
                        {
                            void * t211 = gm_l19;
                            gm_l6 = t211;
                        }
ix97:;
                    }
                    goto me86;
mn87:;
                    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 165, 5);
me86:;
                }
lc74:;
                {
                    uint64_t t213 = gm_l8;
                    uint64_t r214; bool o215 = __builtin_add_overflow(t213, ((uint64_t)1ULL), &r214);
                    uint64_t t216 = r214;
                    gm_l8 = t216;
                }
            }
gm_ll78:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
            }
            goto gm_lad;
lb74:;
        }
    }
    {
        void * t217 = gm_l6;
        GM_ST(uint8_t, (((uint8_t *)t217) + 40), ((uint8_t)1));
    }
    {
        void * t218 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out))[0] = 0;
        GM_ST(void *, (uint8_t *)&(*(gm_trssmtuv8jr0v *)gm_out) + 8, t218);
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
__attribute__((aligned(4))) void gm_f_value_oq952o2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_value_oq952o2 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* c */
    gm_b16_8 gm_l2; /* rest */
    GM_STACK_CHECK(&F, 1193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t2) + 32), ((uint8_t)0));
    }
    {
        void * t3 = gm_l0;
        gm_tqs5p4n2tq19v t4 = gm_f_peek_bkdvl65(t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[1] != 0)) {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t9;
            gm_f_error_96ci5lu((void *)&t9, t7, t8);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t10 = gm_l1;
        uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t15 == ((uint8_t)34ULL))) goto pa14;
        goto po13;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t17 == ((uint8_t)39ULL))) goto pa16;
        goto po13;
pa16:;
        goto mn12;
po13:;
        void * t18 = gm_l0;
        gm_tu615l4qlr8b2 t19;
        gm_f_parse_string_80niubc((void *)&t19, (void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_tefknldmnh2si t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 48, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t20 + 8), (*(gm_tob1snuii5bme *)(((uint8_t *)&t19) + 8)));
            F.ret = t20;
            goto gm_fret;
        }
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_tktr4rmbindgv t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
        GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t22);
        goto gm_fdone;
        goto me11;
mn12:;
        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t24 == ((uint8_t)91ULL))) goto mn23;
        void * t25 = gm_l0;
        gm_f_array_canpt3e((void *)&(*(gm_tefknldmnh2si *)gm_out), (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me11;
mn23:;
        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t27 == ((uint8_t)123ULL))) goto mn26;
        void * t28 = gm_l0;
        gm_f_inline_table_cvvpgel((void *)&(*(gm_tefknldmnh2si *)gm_out), (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me11;
mn26:;
        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t30 == ((uint8_t)116ULL))) goto mn29;
        {
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 0) + 8);
            void * t35 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 0)));
            void * t36 = gm_l0;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 16));
            uint64_t t38 = (uintptr_t)t37;
            if (t38 > (uintptr_t)t34 || (uintptr_t)t34 > (uintptr_t)t34) gm_trap_slice(t38, (uintptr_t)t34, t34, gm_src3nt8pf0qk5gn, 258, 20);
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t35 + t38 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)((uintptr_t)t34 - t38));
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(4));
            uint8_t t41 = gm_f_starts_with_6vvt01e(t39, t40);
            uint8_t t42 = (uint8_t)!t41;
            if (!t42) goto ie32;
            {
                void * t44 = gm_l0;
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_shlilv0hq3bqu));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(32));
                gm_tob1snuii5bme t46;
                gm_f_error_96ci5lu((void *)&t46, t44, t45);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t46);
                goto gm_fdone;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            void * t47 = gm_l0;
            void * t48 = gm_l0;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 16));
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)4ULL), &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 259, 13);
            uint64_t t52 = r50;
            GM_ST(uint64_t, (((uint8_t *)t47) + 16), t52);
        }
        {
            void * t53 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t53) + 32), ((uint8_t)1));
        }
        {
            gm_tktr4rmbindgv t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)2ULL));
            GM_ST(uint8_t, ((uint8_t *)&t54 + 8), ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
            GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t54);
            goto gm_fdone;
        }
        goto me11;
mn29:;
        uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t56 == ((uint8_t)102ULL))) goto mn55;
        {
            void * t59 = gm_l0;
            uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 0) + 8);
            void * t61 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t59) + 0)));
            void * t62 = gm_l0;
            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 16));
            uint64_t t64 = (uintptr_t)t63;
            if (t64 > (uintptr_t)t60 || (uintptr_t)t60 > (uintptr_t)t60) gm_trap_slice(t64, (uintptr_t)t60, t60, gm_src3nt8pf0qk5gn, 264, 20);
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t61 + t64 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)((uintptr_t)t60 - t64));
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(5));
            uint8_t t67 = gm_f_starts_with_6vvt01e(t65, t66);
            uint8_t t68 = (uint8_t)!t67;
            if (!t68) goto ie58;
            {
                void * t70 = gm_l0;
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sne7l93pr2tmb));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(33));
                gm_tob1snuii5bme t72;
                gm_f_error_96ci5lu((void *)&t72, t70, t71);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
                GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t72);
                goto gm_fdone;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            void * t73 = gm_l0;
            void * t74 = gm_l0;
            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 16));
            uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)5ULL), &r76);
            if (o77) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 265, 13);
            uint64_t t78 = r76;
            GM_ST(uint64_t, (((uint8_t *)t73) + 16), t78);
        }
        {
            void * t79 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t79) + 32), ((uint8_t)1));
        }
        {
            gm_tktr4rmbindgv t80;
            memset(&t80, 0, sizeof t80);
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)2ULL));
            GM_ST(uint8_t, ((uint8_t *)&t80 + 8), ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
            GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t80);
            goto gm_fdone;
        }
        goto me11;
mn55:;
        uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t84 == ((uint8_t)43ULL))) goto pa83;
        goto po82;
pa83:;
        uint8_t t86 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t86 == ((uint8_t)45ULL))) goto pa85;
        goto po82;
pa85:;
        uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if ((t88 < ((uint8_t)48ULL)) || (t88 > ((uint8_t)57ULL))) goto pa87;
        goto po82;
pa87:;
        goto mn81;
po82:;
        void * t89 = gm_l0;
        gm_f_number_4l95pjb((void *)&(*(gm_tefknldmnh2si *)gm_out), t89);
        goto gm_fdone;
        goto me11;
mn81:;
        uint8_t t93 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t93 == ((uint8_t)105ULL))) goto pa92;
        goto po91;
pa92:;
        uint8_t t95 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t95 == ((uint8_t)110ULL))) goto pa94;
        goto po91;
pa94:;
        goto mn90;
po91:;
        {
            void * t97 = gm_l0;
            uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)t97) + 0) + 8);
            void * t99 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t97) + 0)));
            void * t100 = gm_l0;
            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t100) + 16));
            uint64_t t102 = (uintptr_t)t101;
            if (t102 > (uintptr_t)t98 || (uintptr_t)t98 > (uintptr_t)t98) gm_trap_slice(t102, (uintptr_t)t98, t98, gm_src3nt8pf0qk5gn, 271, 21);
            gm_b16_8 t103;
            GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t99 + t102 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)((uintptr_t)t98 - t102));
            gm_l2 = t103;
        }
        {
            uint8_t t105 = 0;
            gm_b16_8 t106 = gm_l2;
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(3));
            uint8_t t108 = gm_f_starts_with_6vvt01e(t106, t107);
            if (!t108) {
                gm_b16_8 t109 = gm_l2;
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sfa86erpod86s));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(3));
                uint8_t t111 = gm_f_starts_with_6vvt01e(t109, t110);
                t105 = t111;
            } else t105 = 1;
            if (!t105) goto ie104;
            {
                void * t113 = gm_l0;
                void * t114 = gm_l0;
                uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)t114) + 16));
                uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)3ULL), &r116);
                if (o117) gm_trap("integer overflow in `+`", gm_src3nt8pf0qk5gn, 273, 17);
                uint64_t t118 = r116;
                GM_ST(uint64_t, (((uint8_t *)t113) + 16), t118);
            }
            {
                void * t119 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t119) + 32), ((uint8_t)1));
            }
            {
                uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t121 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t122 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t123 = (uintptr_t)((uint64_t)3ULL);
                if (t122 > t123 || t123 > (uintptr_t)t120) gm_trap_slice(t122, t123, t120, gm_src3nt8pf0qk5gn, 275, 36);
                gm_b16_8 t124;
                GM_ST(void *, (uint8_t *)&t124, (void *)(((uint8_t *)t121 + t122 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(t123 - t122));
                gm_tktr4rmbindgv t125;
                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t125 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t125 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t125 + 8), t124);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 0;
                GM_ST(gm_tktr4rmbindgv, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t125);
                goto gm_fdone;
            }
            goto ix104;
ie104:;
ix104:;
        }
        {
            void * t126 = gm_l0;
            gm_b16_8 t127 = GM_LD(gm_b16_8, gm_cal7njs7es4);
            gm_tob1snuii5bme t128;
            gm_f_error_96ci5lu((void *)&t128, t126, t127);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
            ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
            GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t128);
            goto gm_fdone;
        }
        goto me11;
mn90:;
        uint8_t t130 = GM_LD(uint8_t, ((uint8_t *)&t10));
        if (!(t130 == ((uint8_t)61ULL))) goto mn129;
        void * t131 = gm_l0;
        gm_b16_8 t132;
        GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sv5o4caljvbmj));
        GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(27));
        gm_tob1snuii5bme t133;
        gm_f_error_96ci5lu((void *)&t133, t131, t132);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t133);
        goto gm_fdone;
        goto me11;
mn129:;
        void * t135 = gm_l0;
        gm_b16_8 t136 = GM_LD(gm_b16_8, gm_cal7njs7es4);
        gm_tob1snuii5bme t137;
        gm_f_error_96ci5lu((void *)&t137, t135, t136);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 48, 0);
        ((uint8_t *)&(*(gm_tefknldmnh2si *)gm_out))[0] = 1;
        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tefknldmnh2si *)gm_out) + 8, t137);
        goto gm_fdone;
        goto me11;
mn134:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 253, 5);
me11:;
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
