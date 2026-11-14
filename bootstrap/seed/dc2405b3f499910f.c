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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 a[5]; } gm_tjgkb0mi112kg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tfeab279c16rr v1; } f1; } gm_tjdv8ltmrgtv0;
_Static_assert(sizeof(gm_tjgkb0mi112kg) == 80 && _Alignof(gm_tjgkb0mi112kg) == 8 && offsetof(gm_tjgkb0mi112kg, a) == 0, "layout of `[5]string`");
typedef struct { gm_tsoiri44d0i6q f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tjm4rm50v5mle;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t0rfe1ou14h21 v1; } f1; } gm_tod6pa33o8u9u;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_tjm4rm50v5mle v1; } f1; } gm_tat31u3lvqjk4;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tjdv8ltmrgtv0) == 56 && _Alignof(gm_tjdv8ltmrgtv0) == 8 && offsetof(gm_tjdv8ltmrgtv0, f0) == 0 && offsetof(gm_tjdv8ltmrgtv0, f1) == 8, "layout of `string!Toolchain`");
_Static_assert(sizeof(gm_tjm4rm50v5mle) == 96 && _Alignof(gm_tjm4rm50v5mle) == 8 && offsetof(gm_tjm4rm50v5mle, f0) == 0 && offsetof(gm_tjm4rm50v5mle, f1) == 8 && offsetof(gm_tjm4rm50v5mle, f2) == 48 && offsetof(gm_tjm4rm50v5mle, f3) == 88, "layout of `Output`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
_Static_assert(sizeof(gm_tod6pa33o8u9u) == 72 && _Alignof(gm_tod6pa33o8u9u) == 8 && offsetof(gm_tod6pa33o8u9u, f0) == 0 && offsetof(gm_tod6pa33o8u9u, f1) == 8, "layout of `string!Target`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
_Static_assert(sizeof(gm_tat31u3lvqjk4) == 104 && _Alignof(gm_tat31u3lvqjk4) == 8 && offsetof(gm_tat31u3lvqjk4, f0) == 0 && offsetof(gm_tat31u3lvqjk4, f1) == 8, "layout of `RunError!Output`");
struct gm_fr_any_is_2366lre {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_any_starts_dv2l7a5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_triple_uqaropr {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_t0rfe1ou14h21 l9; /* tg */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_default_cc_qpj8rre {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_defined_kppjhtk {
    gm_frame h;
    gm_b16_8 l1; /* name */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_display_30kc1i9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_env_var_mbpep74 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_for_body_3400t3p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_g9kn84c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_i2koblr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tdd1e60 {
    gm_frame h;
    gm_ttu09lij36f8c l3; /* first */
    gm_b0_1 ret;
};
struct gm_fr_for_safe_build_9nqaffp {
    gm_frame h;
    gm_tfeab279c16rr l0; /* t */
    gm_th2lqbo77pvsc l1; /* found */
    gm_tjdv8ltmrgtv0 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_target_439caa2 {
    gm_frame h;
    gm_tjdv8ltmrgtv0 ret;
};
struct gm_fr_for_triple_ok3tm31 {
    gm_frame h;
    gm_tfeab279c16rr l7; /* t */
    gm_trqfvmjtt4sij l13; /* words */
    gm_tfeab279c16rr l14; /* t */
    gm_tjdv8ltmrgtv0 ret;
};
struct gm_fr_from_env_rukl5lk {
    gm_frame h;
    gm_tjdv8ltmrgtv0 ret;
};
struct gm_fr_from_vars_r2mjdrk {
    gm_frame h;
    gm_tfeab279c16rr l3; /* t */
    gm_trqfvmjtt4sij l6; /* words */
    gm_trqfvmjtt4sij l9; /* words */
    gm_trqfvmjtt4sij l12; /* words */
    gm_tjdv8ltmrgtv0 ret;
};
struct gm_fr_has_c7mpl87 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_white_space_569v0vo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_default_24c3cui {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_macros_hrin9id {
    gm_frame h;
    gm_tjgkb0mi112kg l1; /* tail */
    gm_trqfvmjtt4sij l2; /* argv */
    gm_tjm4rm50v5mle l4; /* out */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_num_1gcf1u8 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_quote_word_3e35l5m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_run_error_gs1dinp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_runner_for_79o4h4m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* runner */
    gm_trqfvmjtt4sij l2; /* words */
    gm_t470aqkia5212 ret;
};
struct gm_fr_runner_from_env_bnildq8 {
    gm_frame h;
    gm_t470aqkia5212 ret;
};
struct gm_fr_split_words_jk1lams {
    gm_frame h;
    gm_t470aqkia5212 ret;
};
struct gm_fr_strip_wasm_trap_report_h977b8r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_success_q3lb6vt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_target_72cra6q {
    gm_frame h;
    gm_tod6pa33o8u9u ret;
};
struct gm_fr_target_for_omoprob {
    gm_frame h;
    gm_t0rfe1ou14h21 l4; /* tg */
    gm_tod6pa33o8u9u ret;
};
struct gm_fr_target_from_macros_hkgk1pc {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* defs */
    gm_tod6pa33o8u9u ret;
    gm_b0_1 w0;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_value_3oqtvqn {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_with_manifest_flags_objbdaj {
    gm_frame h;
    gm_tfeab279c16rr l0; /* t */
    gm_tfeab279c16rr l3; /* r */
    gm_trqfvmjtt4sij l4; /* words */
    gm_trqfvmjtt4sij l5; /* words */
    gm_tfeab279c16rr ret;
};
struct gm_fr_words_of_f1gud4i {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* l */
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqmpqhhfmb90(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_any_is_2366lre(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_any_starts_dv2l7a5(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_check_triple_uqaropr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_command_b2u33fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_default_cc_qpj8rre(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_defined_kppjhtk(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_env_var_mbpep74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3400t3p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g9kn84c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_safe_build_9nqaffp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_for_target_439caa2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_triple_ok3tm31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2, const void * a3, uint8_t a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_format_0acc90p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_13mdm96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2g98t2r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5htdr0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7dijd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8pjgcns(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_blcbfcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dl7scf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g2mro4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gshr6sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mcmtfgd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n1doesn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o4fb8d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oaqgi6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q3io8lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ra2b58j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_seuq78o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_svnhg71(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_env_rukl5lk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_from_vars_r2mjdrk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_c7mpl87(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_has_white_space_569v0vo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_host_target_vsu78om(void *gm_out);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_24c3cui(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s30klli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sssrp43(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_macros_hrin9id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_num_1gcf1u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_output_mg6eie5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_int_8aueeu2(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_program_on_path_fm43kjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_quote_posix_56modn2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_quote_word_3e35l5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_runner_from_env_bnildq8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq2k3vc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svmmoet(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_posix_e978ams(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_split_words_jk1lams(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_strip_wasm_trap_report_h977b8r(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0);
__attribute__((aligned(4))) void gm_f_target_72cra6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_target_for_omoprob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_target_from_macros_hkgk1pc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_value_3oqtvqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_with_manifest_flags_objbdaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_words_of_f1gud4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
extern uint8_t gm_c695aum3fk1[16] __attribute__((aligned(8)));
extern uint8_t gm_cc7b06pnl1q[1] __attribute__((aligned(1)));
extern uint8_t gm_cl03rhutns3[112] __attribute__((aligned(8)));
extern uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
extern uint8_t gm_cojk58d4vkf[16] __attribute__((aligned(8)));
extern void *const gm_hl0tthikq3q4[];
extern void *const gm_hl5iavnkj4ht[];
extern void *const gm_hlib8i5oaffv[];
extern void *const gm_hloaeaafij99[];
extern const uint8_t gm_s048hmhpmbi7c[8];
extern const uint8_t gm_s0thiorolmqbb[14];
extern const uint8_t gm_s1d20j48bjjln[12];
extern const uint8_t gm_s24frr2u6q1at[9];
extern const uint8_t gm_s26219ruuua5s[19];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s2ct6k0fls3pb[12];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s33a8rv0vs9rh[3];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3pufl09hvbbf[6];
extern const uint8_t gm_s4109lou8dsdj[12];
extern const uint8_t gm_s5ejk09ngvect[3];
extern const uint8_t gm_s5gha6q48spt5[6];
extern const uint8_t gm_s5lr3gdic3o7e[15];
extern const uint8_t gm_s6juej7e0fhbb[8];
extern const uint8_t gm_s8tkvhpqkq5en[8];
extern const uint8_t gm_s9aa9jihjgf22[9];
extern const uint8_t gm_sa9o54tgja188[12];
extern const uint8_t gm_saalafjdd3tnp[9];
extern const uint8_t gm_sar7ovt8nasdo[8];
extern const uint8_t gm_sb4hpcnufos1f[12];
extern const uint8_t gm_sb8829ojmkbv6[6];
extern const uint8_t gm_sbc0kjtg5aiab[5];
extern const uint8_t gm_sbdp6ovojv1tg[10];
extern const uint8_t gm_sbjdqrlfpuoqj[4];
extern const uint8_t gm_sbk48vc8h32c1[9];
extern const uint8_t gm_sblsu018nl4o3[3];
extern const uint8_t gm_sbrifoc7ea21m[7];
extern const uint8_t gm_sc5p6b8d5c87a[13];
extern const uint8_t gm_sc6celu0e642t[6];
extern const uint8_t gm_sc829ikr3c206[10];
extern const uint8_t gm_scgvr0isof71e[9];
extern const uint8_t gm_scq91tgm9lph4[6];
extern const uint8_t gm_sdad15q200tmk[4];
extern const uint8_t gm_se006k0nbfdeg[8];
extern const uint8_t gm_se4kub9a3omhd[5];
extern const uint8_t gm_se6fhe7nkildm[18];
extern const uint8_t gm_sennl2fjkcbdb[4];
extern const uint8_t gm_sf6pbim706kre[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfrku7jk647q0[83];
extern const uint8_t gm_sg4edpe0eroav[8];
extern const uint8_t gm_sg90vqvamkjbo[11];
extern const uint8_t gm_sgfrcimthamdn[10];
extern const uint8_t gm_sgs7af5k634cn[11];
extern const uint8_t gm_sh5f7b7njpj3l[6];
extern const uint8_t gm_shjpff93a0f25[7];
extern const uint8_t gm_shki2pqrgmpd1[19];
extern const uint8_t gm_si56o26p8hqsl[8];
extern const uint8_t gm_sibidb2jab0ss[14];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sj7cc74b29dv3[14];
extern const uint8_t gm_sjdv8icqpfhc5[6];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sk50arqpf74k6[6];
extern const uint8_t gm_sk754qkda90si[6];
extern const uint8_t gm_skbsro97j503c[7];
extern const uint8_t gm_skkog7g7r3edb[8];
extern const uint8_t gm_skn7gps778eo2[3];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl5rf5cfkcmdk[7];
extern const uint8_t gm_sm3fgo8acha61[13];
extern const uint8_t gm_smiidqh71uo72[10];
extern const uint8_t gm_smnkiec8dekcp[8];
extern const uint8_t gm_smuveuet82l27[11];
extern const uint8_t gm_sn2e6t65fstoj[19];
extern const uint8_t gm_sn3ltks10rrv7[8];
extern const uint8_t gm_snl0g0eaf7ift[8];
extern const uint8_t gm_snnagva49avpd[8];
extern const uint8_t gm_snqe7tsf40ume[138];
extern const uint8_t gm_snrk30ug2qmu2[8];
extern const uint8_t gm_sntolo76g17vv[7];
extern const uint8_t gm_so5q4maooc958[35];
extern const uint8_t gm_so9c50giqp3gn[6];
extern const uint8_t gm_sonht3t87vai7[9];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_spervk999ikim[8];
extern const uint8_t gm_sq8qkmb2jr755[7];
extern const uint8_t gm_sqg9do4ma3giu[6];
extern const uint8_t gm_sr389r8t7d0mf[11];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcmm27gepldtcm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4i4cmesm57g[];
extern const uint8_t gm_ssevq7ii4bscf[10];
extern const uint8_t gm_sss60if2anvc8[5];
extern const uint8_t gm_st5oflrv0tmc6[21];
extern const uint8_t gm_stfp83525k3nr[7];
extern const uint8_t gm_su4retifs0sre[9];
extern const uint8_t gm_sup67cuqst8bs[8];
extern const uint8_t gm_suu5u3potp0p3[7];
extern const uint8_t gm_suvioac7tr94t[10];
extern const uint8_t gm_svgn73l5s66lk[7];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const uint8_t gm_svln5ui59rjil[6];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqmpqhhfmb90(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eqb2nno7n67r(a, b);
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    gm_tf5srh4mp4b3i t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 82, 50);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_default_cc_qpj8rre(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_default_cc_qpj8rre F;
    GM_STACK_CHECK_SHALLOW(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b0_1 t2;
    }
    {
        gm_th2lqbo77pvsc t3;
        memset(&t3, 0, sizeof t3);
        uint8_t t4 = 0;
        F.ret = t3;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_defined_kppjhtk(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_defined_kppjhtk F;
    gm_b16_8 gm_l0; /* macros */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlib8i5oaffv, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_sssrp43((void *)NULL, (&wn5), NULL, t8);
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
        uint8_t t13 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_display_30kc1i9 F;
    void * gm_l0; /* tc */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    gm_tabb710d9mq2e t4;
    gm_f_join_sfknljb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src7u0d314efpdm, 175, 66);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_name_em7v8lv F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 288u);
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
    gm_tl7hpf5nlilqq t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&t3) + 16) + 0));
    gm_b16_8 t5 = t4;
    void * t6 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)));
    uint64_t t7 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
    uint64_t t8 = (t7 - 1);
    gm_th2lqbo77pvsc t9;
    if (t8 < t7) {
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t6 + t8 * 16));
    gm_th2lqbo77pvsc t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[16] = 1;
        t9 = t11;
    } else {
    gm_th2lqbo77pvsc t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_i2koblr F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_triple_uqaropr *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcmm27gepldtcm, 257, 39);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 257, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_c7mpl87(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_has_c7mpl87 F;
    gm_b16_8 gm_l0; /* m */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
    gm_b16_8 t2 = gm_l0;
    gm_th2lqbo77pvsc t3;
    gm_f_get_e3joqcc((void *)&t3, t1, t2);
    gm_th2lqbo77pvsc t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 388, 37);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_24c3cui(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_24c3cui F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
    uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
    if (t6) {
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srcmm27gepldtcm, 159, 68);
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 0)) + t7 * 16));
        gm_b16_8 t11 = GM_LD(gm_b16_8, gm_c695aum3fk1);
        gm_b16_8 t12 = t10;
        gm_b16_8 t13 = t11;
        uint8_t t14 = (gm_eq0iqiubfm7e(((uint8_t *)&t12), ((uint8_t *)&t13)));
        t3 = t14;
    } else t3 = 0;
    if (t3) {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 16) + 8);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        t2 = t17;
    } else t2 = 0;
    if (t2) {
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 32) + 8);
        uint8_t t20 = ((t19 == ((uint64_t)0ULL)));
        t1 = t20;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 213u);
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
        gm_tih6h4avv5nrb t5 = t4;
        gm_t5j14m8ru132p t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_t5j14m8ru132p t7 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_quote_word_3e35l5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_quote_word_3e35l5m F;
    gm_b16_8 gm_l0; /* w */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_b16_8 t4 = gm_f_quote_posix_56modn2((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t5 = 0;
            F.ret = t4;
            goto gm_fret;
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_run_error_gs1dinp F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 211, 67);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_split_words_jk1lams(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_words_jk1lams F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_f_split_posix_e978ams((void *)&(*(gm_t470aqkia5212 *)gm_out), (void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
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
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_success_q3lb6vt F;
    gm_tsoiri44d0i6q gm_l0; /* s */
    uint32_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        gm_l1 = t6;
        uint32_t t7 = gm_l1;
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        t2 = t8;
        goto me3;
mn4:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        t2 = ((uint8_t)0);
        goto me3;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srct4i4cmesm57g, 65, 12);
me3:;
        uint8_t t11 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_is_2366lre(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_is_2366lre F;
    gm_b16_8 gm_l0; /* words */
    gm_b16_8 gm_l1; /* word */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 230u);
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
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 16));
                    gm_l4 = t15;
                    {
                        gm_b16_8 t18 = gm_l4;
                        gm_b16_8 t19 = gm_l1;
                        gm_b16_8 t20 = t18;
                        gm_b16_8 t21 = t19;
                        uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (!t22) goto ie17;
                        {
                            uint8_t t24 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 336, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_starts_dv2l7a5(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_starts_dv2l7a5 F;
    gm_b16_8 gm_l0; /* words */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 198u);
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
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 16));
                    gm_l4 = t15;
                    {
                        gm_b16_8 t18 = gm_l4;
                        gm_b16_8 t19 = gm_l1;
                        uint8_t t20 = gm_f_starts_with_6vvt01e(t18, t19);
                        if (!t20) goto ie17;
                        {
                            uint8_t t22 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 329, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t24 = gm_l3;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l3 = t27;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t28 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_check_triple_uqaropr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_check_triple_uqaropr F;
    gm_b16_8 gm_l0; /* triple */
    gm_b16_8 gm_l1; /* macros */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* arch */
    gm_b16_8 gm_l5; /* rest */
    gm_b16_8 gm_l6; /* m1 */
    gm_b16_8 gm_l7; /* m2 */
    uint8_t gm_l8; /* big */
    gm_b16_8 gm_l10; /* os_name */
    gm_b16_8 gm_l11; /* os_macro */
    GM_STACK_CHECK(&F, 2973u);
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
            gm_b16_8 t3 = gm_l0;
            gm_l3 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hloaeaafij99, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l3;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        gm_b0_1 t10 = gm_f_split_obtinjv_svmmoet((void *)NULL, (&wn5), NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls11:;
we6:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcmm27gepldtcm, 258, 13);
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t14 * 16));
        gm_l4 = t16;
    }
    {
        uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)));
        uint64_t t19 = (uintptr_t)((uint64_t)1ULL);
        if (t19 > (uintptr_t)t17 || (uintptr_t)t17 > (uintptr_t)t17) gm_trap_slice(t19, (uintptr_t)t17, t17, gm_srcmm27gepldtcm, 259, 13);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t18 + t19 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)((uintptr_t)t17 - t19));
        gm_l5 = t20;
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
        gm_l8 = ((uint8_t)0);
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_skbsro97j503c, 6) == 0)) goto pa26;
        goto po25;
pa26:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sk50arqpf74k6, 5) == 0)) goto pa27;
        goto po25;
pa27:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_s8tkvhpqkq5en, 7) == 0)) goto pa28;
        goto po25;
pa28:;
        goto mn24;
po25:;
        {
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_smuveuet82l27));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(10));
            gm_l6 = t30;
        }
        goto me23;
mn24:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sg4edpe0eroav, 7) == 0)) goto pa33;
        goto po32;
pa33:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_s3pufl09hvbbf, 5) == 0)) goto pa34;
        goto po32;
pa34:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_suu5u3potp0p3, 6) == 0)) goto pa35;
        goto po32;
pa35:;
        goto mn31;
po32:;
        {
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s2ct6k0fls3pb));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(11));
            gm_l6 = t37;
        }
        goto me23;
mn31:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sg90vqvamkjbo, 10) == 0)) goto pa40;
        goto po39;
pa40:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_s9aa9jihjgf22, 8) == 0)) goto pa41;
        goto po39;
pa41:;
        goto mn38;
po39:;
        {
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s2ct6k0fls3pb));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(11));
            gm_l6 = t43;
        }
        {
            gm_l8 = ((uint8_t)1);
        }
        goto me23;
mn38:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_se006k0nbfdeg, 7) == 0)) goto mn44;
        {
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sup67cuqst8bs));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(7));
            gm_l6 = t46;
        }
        goto me23;
mn44:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sb8829ojmkbv6, 5) == 0)) goto mn47;
        {
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sgfrcimthamdn));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(9));
            gm_l6 = t49;
        }
        {
            gm_l8 = ((uint8_t)1);
        }
        goto me23;
mn47:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sc829ikr3c206, 9) == 0)) goto pa52;
        goto po51;
pa52:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_so9c50giqp3gn, 5) == 0)) goto pa53;
        goto po51;
pa53:;
        goto mn50;
po51:;
        {
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s0thiorolmqbb));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(13));
            gm_l6 = t55;
        }
        {
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_suvioac7tr94t));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(9));
            gm_l7 = t56;
        }
        {
            gm_l8 = ((uint8_t)1);
        }
        goto me23;
mn50:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 11) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sa9o54tgja188, 11) == 0)) goto pa59;
        goto po58;
pa59:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_si56o26p8hqsl, 7) == 0)) goto pa60;
        goto po58;
pa60:;
        goto mn57;
po58:;
        {
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s0thiorolmqbb));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(13));
            gm_l6 = t62;
        }
        {
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_suvioac7tr94t));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(9));
            gm_l7 = t63;
        }
        goto me23;
mn57:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sq8qkmb2jr755, 6) == 0)) goto mn64;
        {
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sonht3t87vai7));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(8));
            gm_l6 = t66;
        }
        {
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sgs7af5k634cn));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(10));
            gm_l7 = t67;
        }
        {
            gm_l8 = ((uint8_t)1);
        }
        goto me23;
mn64:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_s24frr2u6q1at, 8) == 0)) goto mn68;
        {
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sonht3t87vai7));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(8));
            gm_l6 = t70;
        }
        {
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sgs7af5k634cn));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(10));
            gm_l7 = t71;
        }
        goto me23;
mn68:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_snl0g0eaf7ift, 7) == 0)) goto mn72;
        {
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sbdp6ovojv1tg));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(9));
            gm_l6 = t74;
        }
        {
            gm_l8 = ((uint8_t)1);
        }
        goto me23;
mn72:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_svgn73l5s66lk, 6) == 0)) goto mn75;
        {
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sr389r8t7d0mf));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(10));
            gm_l6 = t77;
        }
        goto me23;
mn75:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 11) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sb4hpcnufos1f, 11) == 0)) goto mn78;
        {
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sibidb2jab0ss));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(13));
            gm_l6 = t80;
        }
        goto me23;
mn78:;
        gm_t6j6smj7qoctu t82;
        gm_b16_8 t83 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t83);
        gm_b16_8 t84 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t84);
        gm_tabb710d9mq2e t85;
        gm_f_format_oaqgi6o((void *)&t85, (void *)NULL, cx, NULL, &t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t86 = t85;
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_srcmm27gepldtcm, 300, 22);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
        gm_b16_8 t88 = t87;
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        gm_tl572d7579hns t90;
        GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
        ((uint8_t *)&t90)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t90 + 8), t89);
        gm_tl572d7579hns t91 = t90;
        uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
        F.ret = t90;
        goto gm_fret;
        goto me23;
mn81:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 264, 5);
me23:;
    }
    {
        gm_b16_8 t94 = gm_l1;
        gm_b16_8 t95 = gm_l6;
        uint8_t t96 = gm_f_defined_kppjhtk(t94, t95);
        uint8_t t97 = (uint8_t)!t96;
        if (!t97) goto ie93;
        uint8_t t98 = 0;
        uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        uint8_t t100 = ((t99 == ((uint64_t)0ULL)));
        if (!t100) {
            gm_b16_8 t101 = gm_l1;
            gm_b16_8 t102 = gm_l7;
            uint8_t t103 = gm_f_defined_kppjhtk(t101, t102);
            uint8_t t104 = (uint8_t)!t103;
            t98 = t104;
        } else t98 = 1;
        if (!t98) goto ie93;
        {
            gm_t6j6smj7qoctu t106;
            gm_b16_8 t107 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
            gm_b16_8 t108 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t108);
            gm_tabb710d9mq2e t109;
            gm_f_format_5htdr0o((void *)&t109, (void *)NULL, cx, NULL, &t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t110 = t109;
            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_srcmm27gepldtcm, 303, 17);
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
            gm_b16_8 t112 = t111;
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
            gm_tl572d7579hns t114;
            GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
            ((uint8_t *)&t114)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
            gm_tl572d7579hns t115 = t114;
            uint8_t t116 = ((((uint8_t *)&t115))[0] != 0);
            F.ret = t114;
            goto gm_fret;
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        gm_b16_8 t117 = gm_l1;
        gm_tod6pa33o8u9u t118;
        gm_f_target_from_macros_hkgk1pc((void *)&t118, (void *)NULL, cx, NULL, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tod6pa33o8u9u t119 = t118;
        if (((((uint8_t *)&t119))[0] != 0)) {
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
            gm_tl572d7579hns t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
            ((uint8_t *)&t121)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 8), t120);
            F.ret = t121;
            goto gm_fret;
        }
        gm_t0rfe1ou14h21 t122 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)&t119) + 8));
        F.l9 = t122;
    }
    {
        uint8_t t124 = gm_l8;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 8));
        uint8_t t126 = ((t124 == t125));
        if (!t126) goto ie123;
        {
            gm_tfeab279c16rr t128;
            gm_b16_8 t129;
            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 8));
            if (!t131) goto ie130;
            gm_b16_8 t133;
            GM_ST(void *, (uint8_t *)&t133, (void *)(gm_stfp83525k3nr));
            GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(6));
            gm_b16_8 t134 = t133;
            t129 = t134;
            goto ix130;
ie130:;
            gm_b16_8 t136;
            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sennl2fjkcbdb));
            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(3));
            gm_b16_8 t137 = t136;
            t129 = t137;
ix130:;
            GM_ST(gm_b16_8, (uint8_t *)&t128 + 0, t129);
            gm_b16_8 t138 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t128 + 16, t138);
            gm_b16_8 t139;
            uint8_t t141 = gm_l8;
            if (!t141) goto ie140;
            gm_b16_8 t143;
            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sennl2fjkcbdb));
            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(3));
            gm_b16_8 t144 = t143;
            t139 = t144;
            goto ix140;
ie140:;
            gm_b16_8 t146;
            GM_ST(void *, (uint8_t *)&t146, (void *)(gm_stfp83525k3nr));
            GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(6));
            gm_b16_8 t147 = t146;
            t139 = t147;
ix140:;
            GM_ST(gm_b16_8, (uint8_t *)&t128 + 32, t139);
            gm_tabb710d9mq2e t148;
            gm_f_format_0acc90p((void *)&t148, (void *)NULL, cx, NULL, &t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t149 = t148;
            if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 8, gm_srcmm27gepldtcm, 307, 17);
            gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&t149) + 8));
            gm_b16_8 t151 = t150;
            gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
            gm_tl572d7579hns t153;
            GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
            ((uint8_t *)&t153)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t153 + 8), t152);
            gm_tl572d7579hns t154 = t153;
            uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
            F.ret = t153;
            goto gm_fret;
        }
        goto ix123;
ie123:;
ix123:;
    }
    {
        gm_b16_8 t156;
        GM_ST(void *, (uint8_t *)&t156, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(0));
        gm_l10 = t156;
    }
    {
        gm_b16_8 t157;
        GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(0));
        gm_l11 = t157;
    }
    {
        gm_b16_8 t159 = gm_l5;
        gm_b16_8 t160 = t159;
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sk754qkda90si));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(5));
        uint8_t t163 = gm_f_any_starts_dv2l7a5(t161, t162);
        if (!t163) goto ie158;
        {
            gm_t6j6smj7qoctu t165;
            gm_b16_8 t166;
            GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sk754qkda90si));
            GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t166);
            gm_b16_8 t167;
            GM_ST(void *, (uint8_t *)&t167, (void *)(gm_ssevq7ii4bscf));
            GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&t165 + 16, t167);
            gm_t6j6smj7qoctu t168 = t165;
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l10), GM_LD(gm_b16_8, (uint8_t *)&t168 + 0));
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), GM_LD(gm_b16_8, (uint8_t *)&t168 + 16));
        }
        goto ix158;
ie158:;
        uint8_t t170 = 0;
        gm_b16_8 t171 = gm_l5;
        gm_b16_8 t172 = t171;
        gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
        gm_b16_8 t174;
        GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sntolo76g17vv));
        GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(6));
        uint8_t t175 = gm_f_any_starts_dv2l7a5(t173, t174);
        if (!t175) {
            gm_b16_8 t176 = gm_l5;
            gm_b16_8 t177 = t176;
            gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
            gm_b16_8 t179;
            GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sh5f7b7njpj3l));
            GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(5));
            uint8_t t180 = gm_f_any_is_2366lre(t178, t179);
            t170 = t180;
        } else t170 = 1;
        if (!t170) goto ie169;
        {
            gm_t6j6smj7qoctu t182;
            gm_b16_8 t183;
            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sntolo76g17vv));
            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t182 + 0, t183);
            gm_b16_8 t184;
            GM_ST(void *, (uint8_t *)&t184, (void *)(gm_smiidqh71uo72));
            GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&t182 + 16, t184);
            gm_t6j6smj7qoctu t185 = t182;
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l10), GM_LD(gm_b16_8, (uint8_t *)&t185 + 0));
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), GM_LD(gm_b16_8, (uint8_t *)&t185 + 16));
        }
        goto ix169;
ie169:;
        gm_b16_8 t187 = gm_l5;
        gm_b16_8 t188 = t187;
        gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
        gm_b16_8 t190;
        GM_ST(void *, (uint8_t *)&t190, (void *)(gm_snrk30ug2qmu2));
        GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(7));
        uint8_t t191 = gm_f_any_starts_dv2l7a5(t189, t190);
        if (!t191) goto ie186;
        {
            gm_t6j6smj7qoctu t193;
            gm_b16_8 t194;
            GM_ST(void *, (uint8_t *)&t194, (void *)(gm_snrk30ug2qmu2));
            GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t193 + 0, t194);
            gm_b16_8 t195;
            GM_ST(void *, (uint8_t *)&t195, (void *)(gm_s1d20j48bjjln));
            GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(11));
            GM_ST(gm_b16_8, (uint8_t *)&t193 + 16, t195);
            gm_t6j6smj7qoctu t196 = t193;
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l10), GM_LD(gm_b16_8, (uint8_t *)&t196 + 0));
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), GM_LD(gm_b16_8, (uint8_t *)&t196 + 16));
        }
        goto ix186;
ie186:;
        uint8_t t198 = 0;
        gm_b16_8 t199 = gm_l5;
        gm_b16_8 t200 = t199;
        gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
        gm_b16_8 t202;
        GM_ST(void *, (uint8_t *)&t202, (void *)(gm_skkog7g7r3edb));
        GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(7));
        uint8_t t203 = gm_f_any_starts_dv2l7a5(t201, t202);
        if (!t203) {
            gm_b16_8 t204 = gm_l5;
            gm_b16_8 t205 = t204;
            gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
            gm_b16_8 t207;
            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_s6juej7e0fhbb));
            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(7));
            uint8_t t208 = gm_f_any_is_2366lre(t206, t207);
            t198 = t208;
        } else t198 = 1;
        if (!t198) goto ie197;
        {
            gm_t6j6smj7qoctu t210;
            gm_b16_8 t211;
            GM_ST(void *, (uint8_t *)&t211, (void *)(gm_skkog7g7r3edb));
            GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t210 + 0, t211);
            gm_b16_8 t212;
            GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sl5rf5cfkcmdk));
            GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t210 + 16, t212);
            gm_t6j6smj7qoctu t213 = t210;
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l10), GM_LD(gm_b16_8, (uint8_t *)&t213 + 0));
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), GM_LD(gm_b16_8, (uint8_t *)&t213 + 16));
        }
        goto ix197;
ie197:;
        gm_b16_8 t215 = gm_l5;
        gm_b16_8 t216 = t215;
        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
        gm_b16_8 t218;
        GM_ST(void *, (uint8_t *)&t218, (void *)(gm_se4kub9a3omhd));
        GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(4));
        uint8_t t219 = gm_f_any_starts_dv2l7a5(t217, t218);
        if (!t219) goto ie214;
        {
            gm_t6j6smj7qoctu t221;
            gm_b16_8 t222;
            GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sbc0kjtg5aiab));
            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t221 + 0, t222);
            gm_b16_8 t223;
            GM_ST(void *, (uint8_t *)&t223, (void *)(gm_scgvr0isof71e));
            GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&t221 + 16, t223);
            gm_t6j6smj7qoctu t224 = t221;
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l10), GM_LD(gm_b16_8, (uint8_t *)&t224 + 0));
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), GM_LD(gm_b16_8, (uint8_t *)&t224 + 16));
        }
        goto ix214;
ie214:;
ix214:;
ix197:;
ix186:;
ix169:;
ix158:;
    }
    {
        uint64_t t226 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
        uint8_t t227 = (t226 > ((uint64_t)0ULL));
        if (!t227) goto ie225;
        gm_b16_8 t228 = gm_l1;
        gm_b16_8 t229 = gm_l11;
        uint8_t t230 = gm_f_defined_kppjhtk(t228, t229);
        uint8_t t231 = (uint8_t)!t230;
        if (!t231) goto ie225;
        {
            gm_t6j6smj7qoctu t233;
            gm_b16_8 t234 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t233 + 0, t234);
            gm_b16_8 t235 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t233 + 16, t235);
            gm_tabb710d9mq2e t236;
            gm_f_format_dl7scf9((void *)&t236, (void *)NULL, cx, NULL, &t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t237 = t236;
            if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 8, gm_srcmm27gepldtcm, 323, 17);
            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
            gm_b16_8 t239 = t238;
            gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
            gm_tl572d7579hns t241;
            GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
            ((uint8_t *)&t241)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t241 + 8), t240);
            gm_tl572d7579hns t242 = t241;
            uint8_t t243 = ((((uint8_t *)&t242))[0] != 0);
            F.ret = t241;
            goto gm_fret;
        }
        goto ix225;
ie225:;
ix225:;
    }
    {
        gm_b0_1 t244;
        gm_tl572d7579hns t245;
        GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 16, 0);
        ((uint8_t *)&t245)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t245 + 8), t244);
        gm_tl572d7579hns t246 = t245;
        uint8_t t247 = ((((uint8_t *)&t246))[0] != 0);
        F.ret = t245;
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
extern __attribute__((aligned(4))) uint8_t gm_f_defined_kppjhtk(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_env_var_mbpep74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_env_var_mbpep74 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 621690230);
        if (!pn3) gm_trap("no handler installed for effect `Env`", gm_srcmm27gepldtcm, 30, 10);
        gm_th2lqbo77pvsc t4;
        if (pn3->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t2), GM_LD(uintptr_t, (uint8_t *)&t2 + 8), &ev, &evn);
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[16] = 1;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            t4 = evok ? t6 : t7;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[1])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8 = t4;
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l1 = t9;
    }
    {
        gm_th2lqbo77pvsc t13;
        gm_b16_8 t15 = gm_l1;
        gm_b16_8 t16 = gm_f_trim_fdrfe6i(t15);
        gm_b16_8 t17 = t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
        uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
        if (!t19) goto ie14;
        gm_th2lqbo77pvsc t21;
        memset(&t21, 0, sizeof t21);
        gm_th2lqbo77pvsc t22 = t21;
        t13 = t22;
        goto ix14;
ie14:;
        gm_b16_8 t24 = gm_l1;
        gm_b16_8 t25 = t24;
        gm_th2lqbo77pvsc t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[16] = 1;
        t13 = t26;
ix14:;
        uint8_t t27 = 0;
        F.ret = t13;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3400t3p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_3400t3p F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* value */
    gm_t6j6smj7qoctu gm_l5; /* kv */
    GM_STACK_CHECK(&F, 518u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_saalafjdd3tnp));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(8));
        gm_th2lqbo77pvsc t7;
        gm_f_strip_prefix_b4949s8((void *)&t7, t5, t6);
        gm_th2lqbo77pvsc t8 = t7;
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l2 = t9;
    }
    {
        gm_b16_8 t13 = gm_l2;
        gm_l3 = t13;
    }
    {
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
        gm_l4 = t14;
    }
    {
        gm_b16_8 t16 = gm_l2;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
        gm_ttu09lij36f8c t18;
        gm_f_split_once_mqcu1ce((void *)&t18, t16, t17);
        gm_ttu09lij36f8c t19 = t18;
        if (!((((uint8_t *)&t19))[32] != 0)) goto ie15;
        gm_t6j6smj7qoctu t20 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t19));
        gm_l5 = t20;
        {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_l3 = t22;
        }
        {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
            gm_b16_8 t24 = gm_f_trim_fdrfe6i(t23);
            gm_l4 = t24;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t27 = (t26 > ((uint64_t)0ULL));
        if (!t27) goto ie25;
        {
            void * t29 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
            gm_b16_8 t30 = gm_l3;
            gm_b16_8 t31 = gm_l4;
            gm_tftb1odc739t5 t32 = gm_f_put_7shfu9u(t29, t30, t31);
            gm_tftb1odc739t5 t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcmm27gepldtcm, 365, 27);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix25;
ie25:;
ix25:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 357, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g9kn84c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_g9kn84c F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* f */
    GM_STACK_CHECK(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s5gha6q48spt5));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
        gm_b16_8 t7 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        uint8_t t9;
        if (((((uint8_t *)&((struct gm_fr_for_safe_build_9nqaffp *)((&F.h)->up))->l1))[16] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 148, 16);
me10:;
        if (!t9) goto ie8;
        gm_b16_8 t13 = gm_l2;
        uint8_t t14 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t14) goto ie8;
        {
            gm_b16_8 t16 = gm_l2;
            gm_th2lqbo77pvsc t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&((struct gm_fr_for_safe_build_9nqaffp *)((&F.h)->up))->l1), t17);
        }
        goto ix8;
ie8:;
ix8:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 146, 9);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tdd1e60 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* rest */
    gm_t6j6smj7qoctu gm_l4; /* f */
    gm_b16_8 gm_l5; /* word */
    GM_STACK_CHECK_SHALLOW(&F, 435u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_saalafjdd3tnp));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(8));
        gm_th2lqbo77pvsc t7;
        gm_f_strip_prefix_b4949s8((void *)&t7, t5, t6);
        gm_th2lqbo77pvsc t8 = t7;
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l2 = t9;
    }
    {
        gm_b16_8 t13 = gm_l2;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
        gm_f_split_once_mqcu1ce((void *)&F.l3, t13, t14);
    }
    {
        gm_b16_8 t15;
        if (!((((uint8_t *)&F.l3))[32] != 0)) goto ie16;
        gm_t6j6smj7qoctu t17 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&F.l3));
        gm_l4 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
        gm_b16_8 t20 = t19;
        t15 = t20;
        goto ix16;
ie16:;
        gm_b16_8 t22 = gm_l2;
        gm_b16_8 t23 = t22;
        t15 = t23;
ix16:;
        gm_l5 = t15;
    }
    {
        gm_b16_8 t25 = gm_l5;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_defined_kppjhtk *)((&F.h)->up))->l1));
        gm_b16_8 t27 = t25;
        gm_b16_8 t28 = t26;
        uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
        if (!t29) goto ie24;
        {
            uint8_t t31 = 0;
            ((struct gm_fr_defined_kppjhtk *)((&F.h)->up))->ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t31;
            goto gm_lad;
        }
        goto ix24;
ie24:;
ix24:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 344, 5);
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
__attribute__((aligned(4))) void gm_f_for_safe_build_9nqaffp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_for_safe_build_9nqaffp F;
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* filcc */
    GM_STACK_CHECK(&F, 1222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tjdv8ltmrgtv0 t2;
        gm_f_from_env_rukl5lk((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjdv8ltmrgtv0 t3 = t2;
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
            gm_tjdv8ltmrgtv0 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        gm_tfeab279c16rr t6 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t3) + 8));
        F.l0 = t6;
    }
    {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(2));
        gm_th2lqbo77pvsc t9;
        gm_f_env_var_mbpep74((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t10 = t9;
        uint8_t t11;
        if (((((uint8_t *)&t10))[16] != 0)) goto mn13;
        t11 = ((uint8_t)0);
        goto me12;
mn13:;
        t11 = ((uint8_t)1);
        goto me12;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 143, 8);
me12:;
        if (!t11) goto ie7;
        {
            gm_tfeab279c16rr t16 = F.l0;
            gm_tjdv8ltmrgtv0 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 0;
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t17 + 8), t16);
            gm_tjdv8ltmrgtv0 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_th2lqbo77pvsc t20;
        memset(&t20, 0, sizeof t20);
        F.l1 = t20;
    }
    {
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sss60if2anvc8));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
        gm_node *pn23 = gm_find(cx, 621690230);
        if (!pn23) gm_trap("no handler installed for effect `Env`", gm_srcmm27gepldtcm, 145, 16);
        gm_th2lqbo77pvsc t24;
        if (pn23->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t22), GM_LD(uintptr_t, (uint8_t *)&t22 + 8), &ev, &evn);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[16] = 1;
            gm_th2lqbo77pvsc t27;
            memset(&t27, 0, sizeof t27);
            t24 = evok ? t26 : t27;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn23->clauses[1])((void *)&t24, pn23->env, pn23->hctx, pn23, t22);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t28 = t24;
        if (!((((uint8_t *)&t28))[16] != 0)) goto ie21;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_l2 = t29;
        {
            {
                gm_b16_8 t32 = gm_l2;
                gm_l3 = t32;
            }
            gm_hval t33 = ((gm_hval){(void *)&F.h, cx});
            gm_node wn34 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl0tthikq3q4, .env = t33.env, .hctx = t33.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t37 = gm_l3;
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(1));
            gm_b0_1 t39 = gm_f_split_obtinjv_sq2k3vc((void *)NULL, (&wn34), NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_ll36;
            }
            F.w0 = t39;
            goto gm_ls40;
gm_ll36:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we35; }
            goto gm_lad;
gm_ls40:;
we35:;
            gm_b0_1 t41 = F.w0;
            gm_b0_1 t42 = t41;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        uint8_t t44;
        if (((((uint8_t *)&F.l1))[16] != 0)) goto mn46;
        t44 = ((uint8_t)1);
        goto me45;
mn46:;
        t44 = ((uint8_t)0);
        goto me45;
mn47:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 151, 8);
me45:;
        if (!t44) goto ie43;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_shki2pqrgmpd1));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(18));
        uint8_t t49 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t49) goto ie43;
        {
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_shki2pqrgmpd1));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(18));
            gm_th2lqbo77pvsc t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[16] = 1;
            F.l1 = t52;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        gm_th2lqbo77pvsc t53 = F.l1;
        gm_th2lqbo77pvsc t54 = t53;
        gm_b16_8 t55;
        if (((((uint8_t *)&t54))[16] != 0)) {
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 0));
            t55 = t56;
        } else {
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_snqe7tsf40ume));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(137));
            gm_tjdv8ltmrgtv0 t58;
            memset(&t58, 0, sizeof t58);
            ((uint8_t *)&t58)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
            gm_tjdv8ltmrgtv0 t59 = t58;
            uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
            F.ret = t58;
            goto gm_fret;
        }
        gm_l4 = t55;
    }
    {
        gm_b16_8 t61 = gm_l4;
        gm_th2lqbo77pvsc t62;
        memset(&t62, 0, sizeof t62);
        gm_b16_8 t63 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t61, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (((uint8_t *)&F.l0) + 0), t63);
    }
    {
        gm_tfeab279c16rr t64 = F.l0;
        gm_tjdv8ltmrgtv0 t65;
        GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
        ((uint8_t *)&t65)[0] = 0;
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t65 + 8), t64);
        gm_tjdv8ltmrgtv0 t66 = t65;
        uint8_t t67 = ((((uint8_t *)&t66))[0] != 0);
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
__attribute__((aligned(4))) void gm_f_for_target_439caa2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_target_439caa2 F;
    gm_b16_8 gm_l0; /* triple */
    gm_th2lqbo77pvsc gm_l1; /* cc */
    gm_b16_8 gm_l2; /* sdk */
    uint64_t gm_l3; /* end */
    gm_b16_8 gm_l4; /* root */
    GM_STACK_CHECK(&F, 1074u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(2));
        gm_f_env_var_mbpep74((void *)&gm_l1, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4;
        if (((((uint8_t *)&gm_l1))[16] != 0)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 64, 8);
me5:;
        if (!t4) goto ie3;
        gm_b16_8 t8 = gm_l0;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s4109lou8dsdj));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(11));
        uint8_t t10 = gm_f_starts_with_6vvt01e(t8, t9);
        if (!t10) goto ie3;
        {
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sj7cc74b29dv3));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(13));
            gm_th2lqbo77pvsc t14;
            gm_f_env_var_mbpep74((void *)&t14, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t15 = t14;
            if (!((((uint8_t *)&t15))[16] != 0)) goto ie12;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_l2 = t16;
            {
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                gm_l3 = t18;
            }
            {
                for (;;) {
                    uint64_t t20 = gm_l3;
                    uint8_t t21 = (t20 > ((uint64_t)0ULL));
                    if (!t21) goto lb19;
                    uint8_t t22 = 0;
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
                    if (o25) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 67, 36);
                    uint64_t t26 = r24;
                    uint64_t t27 = (uintptr_t)t26;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcmm27gepldtcm, 67, 32);
                    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t27 * 1));
                    uint8_t t30 = ((t29 == ((uint8_t)47ULL)));
                    if (!t30) {
                        uint64_t t31 = gm_l3;
                        uint64_t r32; bool o33 = __builtin_sub_overflow(t31, ((uint64_t)1ULL), &r32);
                        if (o33) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 67, 59);
                        uint64_t t34 = r32;
                        uint64_t t35 = (uintptr_t)t34;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srcmm27gepldtcm, 67, 55);
                        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t35 * 1));
                        uint8_t t38 = ((t37 == ((uint8_t)92ULL)));
                        t22 = t38;
                    } else t22 = 1;
                    if (!t22) goto lb19;
                    {
                        uint64_t t41 = gm_l3;
                        uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                        if (o43) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 67, 79);
                        uint64_t t44 = r42;
                        gm_l3 = t44;
                    }
lc19:;
                }
lb19:;
            }
            {
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t46 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t47 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t48 = gm_l3;
                uint64_t t49 = (uintptr_t)t48;
                if (t47 > t49 || t49 > (uintptr_t)t45) gm_trap_slice(t47, t49, t45, gm_srcmm27gepldtcm, 68, 21);
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t46 + t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
                gm_l4 = t50;
            }
            {
                gm_t6j6smj7qoctu t51;
                gm_b16_8 t52 = gm_l4;
                gm_tnpifv20irnph t53;
                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                gm_tabb710d9mq2e t54;
                gm_f_format_svnhg71((void *)&t54, (void *)NULL, cx, NULL, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t55 = t54;
                if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srcmm27gepldtcm, 69, 53);
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                gm_b16_8 t57 = t56;
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                gm_b16_8 t59 = gm_f_quote_word_3e35l5m((void *)NULL, cx, NULL, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t59);
                gm_b16_8 t60 = gm_l4;
                gm_tnpifv20irnph t61;
                GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t60);
                gm_tabb710d9mq2e t62;
                gm_f_format_mcmtfgd((void *)&t62, (void *)NULL, cx, NULL, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcmm27gepldtcm, 69, 104);
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                gm_b16_8 t65 = t64;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                gm_b16_8 t67 = gm_f_quote_word_3e35l5m((void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t67);
                gm_tabb710d9mq2e t68;
                gm_f_format_2lgoflc((void *)&t68, (void *)NULL, cx, NULL, &t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t69 = t68;
                if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcmm27gepldtcm, 69, 18);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
                gm_b16_8 t71 = t70;
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                gm_th2lqbo77pvsc t73;
                GM_ST(uint8_t, (uint8_t *)&t73 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t73 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t73 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t73), t72);
                ((uint8_t *)&t73)[16] = 1;
                gm_l1 = t73;
            }
            goto ix12;
ie12:;
ix12:;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_b16_8 t74 = gm_l0;
        gm_th2lqbo77pvsc t75 = gm_l1;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_shjpff93a0f25));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(6));
        gm_th2lqbo77pvsc t77;
        gm_f_env_var_mbpep74((void *)&t77, (void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_snnagva49avpd));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t79;
        gm_f_env_var_mbpep74((void *)&t79, (void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t80 = gm_l0;
        gm_tnpifv20irnph t81;
        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
        gm_tabb710d9mq2e t82;
        gm_f_format_g2mro4q((void *)&t82, (void *)NULL, cx, NULL, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t83 = t82;
        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcmm27gepldtcm, 72, 95);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
        gm_b16_8 t85 = t84;
        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
        uint8_t t87 = gm_f_program_on_path_fm43kjf((void *)NULL, cx, NULL, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s2cgbvfh1b7rp));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(5));
        uint8_t t89 = gm_f_program_on_path_fm43kjf((void *)NULL, cx, NULL, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_for_triple_ok3tm31((void *)&(*(gm_tjdv8ltmrgtv0 *)gm_out), (void *)NULL, cx, NULL, t74, &t75, &t77, &t79, t87, t89);
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
__attribute__((aligned(4))) void gm_f_for_triple_ok3tm31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2, const void * a3, uint8_t a4, uint8_t a5) {
    struct gm_fr_for_triple_ok3tm31 F;
    gm_b16_8 gm_l0; /* triple */
    gm_th2lqbo77pvsc gm_l1; /* cc */
    gm_th2lqbo77pvsc gm_l2; /* cflags */
    gm_th2lqbo77pvsc gm_l3; /* ldflags */
    uint8_t gm_l4; /* have_gcc */
    uint8_t gm_l5; /* have_clang */
    gm_b16_8 gm_l6; /* c */
    gm_b16_8 gm_l8; /* prog */
    uint8_t gm_l9; /* names_target */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* w */
    gm_b16_8 gm_l15; /* gcc */
    GM_STACK_CHECK(&F, 2560u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l4 = a4;
    gm_l5 = a5;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            gm_b16_8 t6 = gm_l0;
            uint8_t t7 = gm_f_has_white_space_569v0vo(t6);
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b16_8 t9 = gm_l0;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_8pjgcns((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srcmm27gepldtcm, 88, 17);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_tjdv8ltmrgtv0 t16;
            memset(&t16, 0, sizeof t16);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
            gm_tjdv8ltmrgtv0 t17 = t16;
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie19;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l6 = t20;
        {
            gm_b16_8 t22 = gm_l6;
            gm_th2lqbo77pvsc t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[16] = 1;
            gm_tjdv8ltmrgtv0 t24;
            gm_f_from_vars_r2mjdrk((void *)&t24, (void *)NULL, cx, NULL, &t23, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), &(*(gm_th2lqbo77pvsc *)((uint8_t *)a3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tjdv8ltmrgtv0 t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) {
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
                gm_tjdv8ltmrgtv0 t27;
                memset(&t27, 0, sizeof t27);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t27 + 8), t26);
                F.ret = t27;
                goto gm_fret;
            }
            gm_tfeab279c16rr t28 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t25) + 8));
            F.l7 = t28;
        }
        {
            uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcmm27gepldtcm, 92, 32);
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t29 * 16));
            gm_th2lqbo77pvsc t32;
            gm_f_file_name_em7v8lv((void *)&t32, (void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t33 = t32;
            gm_b16_8 t34;
            if (((((uint8_t *)&t33))[16] != 0)) {
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 0));
                t34 = t35;
            } else {
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(0));
                t34 = t36;
            }
            gm_l8 = t34;
        }
        {
            gm_l9 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_l10 = t38;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t40 = gm_l11;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t42 = (t40 < t41);
                    if (!t42) goto lb39;
                    {
                        uint64_t t45 = gm_l11;
                        uint64_t t46 = (uintptr_t)t45;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t46 * 16));
                        gm_l12 = t50;
                        {
                            uint8_t t53 = 0;
                            uint8_t t54 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_smnkiec8dekcp, 7) == 0));
                            if (!t54) {
                                gm_b16_8 t55 = gm_l12;
                                gm_b16_8 t56;
                                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sbk48vc8h32c1));
                                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(8));
                                uint8_t t57 = gm_f_starts_with_6vvt01e(t55, t56);
                                t53 = t57;
                            } else t53 = 1;
                            if (!t53) goto ie52;
                            {
                                gm_l9 = ((uint8_t)1);
                            }
                            goto ix52;
ie52:;
ix52:;
                        }
                        goto me48;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 94, 9);
me48:;
                    }
lc39:;
                    {
                        uint64_t t60 = gm_l11;
                        uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                        uint64_t t63 = r61;
                        gm_l11 = t63;
                    }
                }
lb39:;
            }
        }
        {
            uint8_t t65 = gm_l9;
            uint8_t t66 = (uint8_t)!t65;
            if (!t66) goto ie64;
            {
                gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t68 = (void *)((uint8_t *)&F.l13);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_tftb1odc739t5 t70 = gm_f_extend_vogsd72(t68, t69);
                gm_tftb1odc739t5 t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcmm27gepldtcm, 99, 13);
                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
            }
            {
                gm_b16_8 t74 = gm_l8;
                gm_b16_8 t75;
                GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s2cgbvfh1b7rp));
                GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(5));
                uint8_t t76 = gm_f_contains_gkki4kq(t74, t75);
                if (!t76) goto ie73;
                {
                    void * t78 = (void *)((uint8_t *)&F.l13);
                    gm_b16_8 t79 = gm_l0;
                    gm_tnpifv20irnph t80;
                    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
                    gm_tabb710d9mq2e t81;
                    gm_f_format_13mdm96((void *)&t81, (void *)NULL, cx, NULL, t80);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t82 = t81;
                    if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srcmm27gepldtcm, 101, 28);
                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
                    gm_b16_8 t84 = t83;
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                    gm_tftb1odc739t5 t86 = gm_f_push_3ef746b(t78, t85);
                    gm_tftb1odc739t5 t87 = t86;
                    if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcmm27gepldtcm, 101, 17);
                    gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                }
                goto ix73;
ie73:;
                uint8_t t90 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sf6pbim706kre, 3) == 0));
                if (!t90) goto ie89;
                uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                uint8_t t92 = (t91 > ((uint64_t)1ULL));
                if (!t92) goto ie89;
                uint8_t t93 = 0;
                uint64_t t94 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t95 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_srcmm27gepldtcm, 102, 59);
                uint8_t t96 = (((GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t94 * 16) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t94 * 16)), gm_sblsu018nl4o3, 2) == 0));
                if (!t96) {
                    uint64_t t97 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                    if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcmm27gepldtcm, 102, 78);
                    uint8_t t99 = (((GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t97 * 16) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t97 * 16)), gm_sbjdqrlfpuoqj, 3) == 0));
                    t93 = t99;
                } else t93 = 1;
                if (!t93) goto ie89;
                {
                    void * t101 = (void *)((uint8_t *)&F.l13);
                    gm_b16_8 t102;
                    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_smnkiec8dekcp));
                    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(7));
                    gm_tftb1odc739t5 t103 = gm_f_push_3ef746b(t101, t102);
                    gm_tftb1odc739t5 t104 = t103;
                    if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcmm27gepldtcm, 103, 17);
                    gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                }
                {
                    void * t106 = (void *)((uint8_t *)&F.l13);
                    gm_b16_8 t107 = gm_l0;
                    gm_tftb1odc739t5 t108 = gm_f_push_3ef746b(t106, t107);
                    gm_tftb1odc739t5 t109 = t108;
                    if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcmm27gepldtcm, 104, 17);
                    gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                }
                goto ix89;
ie89:;
ix89:;
ix73:;
            }
            {
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t112 = t111;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                GM_ST(gm_b16_8, (((uint8_t *)&F.l7) + 0), t113);
            }
            goto ix64;
ie64:;
ix64:;
        }
        {
            gm_tfeab279c16rr t114 = F.l7;
            gm_tjdv8ltmrgtv0 t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            ((uint8_t *)&t115)[0] = 0;
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t115 + 8), t114);
            gm_tjdv8ltmrgtv0 t116 = t115;
            uint8_t t117 = ((((uint8_t *)&t116))[0] != 0);
            F.ret = t115;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_th2lqbo77pvsc t118;
        memset(&t118, 0, sizeof t118);
        gm_tjdv8ltmrgtv0 t119;
        gm_f_from_vars_r2mjdrk((void *)&t119, (void *)NULL, cx, NULL, &t118, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)), &(*(gm_th2lqbo77pvsc *)((uint8_t *)a3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjdv8ltmrgtv0 t120 = t119;
        if (((((uint8_t *)&t120))[0] != 0)) {
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
            gm_tjdv8ltmrgtv0 t122;
            memset(&t122, 0, sizeof t122);
            ((uint8_t *)&t122)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t122 + 8), t121);
            F.ret = t122;
            goto gm_fret;
        }
        gm_tfeab279c16rr t123 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t120) + 8));
        F.l14 = t123;
    }
    {
        gm_b16_8 t124 = gm_l0;
        gm_tnpifv20irnph t125;
        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
        gm_tabb710d9mq2e t126;
        gm_f_format_g2mro4q((void *)&t126, (void *)NULL, cx, NULL, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t127 = t126;
        if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 8, gm_srcmm27gepldtcm, 111, 12);
        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
        gm_l15 = t128;
    }
    {
        uint8_t t130 = gm_l4;
        if (!t130) goto ie129;
        {
            gm_b16_8 t132 = gm_l15;
            gm_b16_8 t133 = t132;
            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
            gm_th2lqbo77pvsc t135;
            memset(&t135, 0, sizeof t135);
            gm_b16_8 t136 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t134, &t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (((uint8_t *)&F.l14) + 0), t136);
        }
        goto ix129;
ie129:;
        uint8_t t138 = gm_l5;
        if (!t138) goto ie137;
        {
            gm_b16_8 t140;
            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_s2cgbvfh1b7rp));
            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(5));
            gm_b16_8 t141 = gm_l0;
            gm_tnpifv20irnph t142;
            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t141);
            gm_tabb710d9mq2e t143;
            gm_f_format_13mdm96((void *)&t143, (void *)NULL, cx, NULL, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t144 = t143;
            if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 8, gm_srcmm27gepldtcm, 115, 34);
            gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t144) + 8));
            gm_b16_8 t146 = t145;
            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
            gm_th2lqbo77pvsc t148;
            GM_ST(uint8_t, (uint8_t *)&t148 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t148 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t148 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t148), t147);
            ((uint8_t *)&t148)[16] = 1;
            gm_b16_8 t149 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t140, &t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (((uint8_t *)&F.l14) + 0), t149);
        }
        goto ix137;
ie137:;
        {
            gm_t6j6smj7qoctu t151;
            gm_b16_8 t152 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t151 + 0, t152);
            gm_b16_8 t153 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t151 + 16, t153);
            gm_tabb710d9mq2e t154;
            gm_f_format_blcbfcc((void *)&t154, (void *)NULL, cx, NULL, &t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t155 = t154;
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 8, gm_srcmm27gepldtcm, 117, 17);
            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&t155) + 8));
            gm_b16_8 t157 = t156;
            gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
            gm_tjdv8ltmrgtv0 t159;
            memset(&t159, 0, sizeof t159);
            ((uint8_t *)&t159)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t159 + 8), t158);
            gm_tjdv8ltmrgtv0 t160 = t159;
            uint8_t t161 = ((((uint8_t *)&t160))[0] != 0);
            F.ret = t159;
            goto gm_fret;
        }
ix137:;
ix129:;
    }
    {
        gm_tfeab279c16rr t162 = F.l14;
        gm_tjdv8ltmrgtv0 t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
        ((uint8_t *)&t163)[0] = 0;
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t163 + 8), t162);
        gm_tjdv8ltmrgtv0 t164 = t163;
        uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
        F.ret = t163;
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
inline __attribute__((aligned(4))) void gm_f_from_env_rukl5lk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_from_env_rukl5lk F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5ejk09ngvect));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(2));
        gm_th2lqbo77pvsc t3;
        gm_f_env_var_mbpep74((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_th2lqbo77pvsc t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            t5 = t3;
        } else {
            gm_th2lqbo77pvsc t6;
            gm_f_default_cc_qpj8rre((void *)&t6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t5 = t6;
        }
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_shjpff93a0f25));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(6));
        gm_th2lqbo77pvsc t8;
        gm_f_env_var_mbpep74((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_snnagva49avpd));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t10;
        gm_f_env_var_mbpep74((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_from_vars_r2mjdrk((void *)&(*(gm_tjdv8ltmrgtv0 *)gm_out), (void *)NULL, cx, NULL, &t5, &t8, &t10);
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
__attribute__((aligned(4))) void gm_f_from_vars_r2mjdrk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1, const void * a2) {
    struct gm_fr_from_vars_r2mjdrk F;
    gm_th2lqbo77pvsc gm_l0; /* cc */
    gm_th2lqbo77pvsc gm_l1; /* cflags */
    gm_th2lqbo77pvsc gm_l2; /* ldflags */
    gm_b16_8 gm_l4; /* c */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l7; /* f */
    gm_b16_8 gm_l8; /* e */
    gm_b16_8 gm_l10; /* f */
    gm_b16_8 gm_l11; /* e */
    GM_STACK_CHECK(&F, 2029u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, gm_c695aum3fk1);
        gm_th2lqbo77pvsc t3;
        memset(&t3, 0, sizeof t3);
        gm_b16_8 t4 = gm_f_words_of_f1gud4i((void *)NULL, cx, NULL, t2, &t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 0, t4);
        gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 16, t7);
        gm_b16_8 t8 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 32, t10);
    }
    {
        if (!((((uint8_t *)&gm_l0))[16] != 0)) goto ie11;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
        gm_l4 = t12;
        {
            gm_b16_8 t14 = gm_l4;
            gm_t470aqkia5212 t15;
            gm_f_split_words_jk1lams((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t470aqkia5212 t16 = t15;
            gm_trqfvmjtt4sij t17;
            if (!((((uint8_t *)&t16))[0] != 0)) {
                gm_trqfvmjtt4sij t18 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t16) + 8));
                t17 = t18;
            } else {
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
                gm_l5 = t19;
                gm_b16_8 t21 = gm_l5;
                gm_tnpifv20irnph t22;
                GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
                gm_tabb710d9mq2e t23;
                gm_f_format_o4fb8d3((void *)&t23, (void *)NULL, cx, NULL, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t24 = t23;
                if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcmm27gepldtcm, 44, 57);
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
                gm_b16_8 t26 = t25;
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                gm_tjdv8ltmrgtv0 t28;
                memset(&t28, 0, sizeof t28);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
                gm_tjdv8ltmrgtv0 t29 = t28;
                uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
                F.ret = t28;
                goto gm_fret;
            }
            F.l6 = t17;
        }
        {
            uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            uint8_t t33 = (t32 > ((uint64_t)0ULL));
            if (!t33) goto ie31;
            {
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t36 = t35;
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 0), t37);
            }
            goto ix31;
ie31:;
ix31:;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie38;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l7 = t39;
        {
            gm_b16_8 t41 = gm_l7;
            gm_t470aqkia5212 t42;
            gm_f_split_words_jk1lams((void *)&t42, (void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t470aqkia5212 t43 = t42;
            gm_trqfvmjtt4sij t44;
            if (!((((uint8_t *)&t43))[0] != 0)) {
                gm_trqfvmjtt4sij t45 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t43) + 8));
                t44 = t45;
            } else {
                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                gm_l8 = t46;
                gm_b16_8 t48 = gm_l8;
                gm_tnpifv20irnph t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                gm_tabb710d9mq2e t50;
                gm_f_format_n1doesn((void *)&t50, (void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t51 = t50;
                if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srcmm27gepldtcm, 48, 57);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                gm_b16_8 t53 = t52;
                gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                gm_tjdv8ltmrgtv0 t55;
                memset(&t55, 0, sizeof t55);
                ((uint8_t *)&t55)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 8), t54);
                gm_tjdv8ltmrgtv0 t56 = t55;
                uint8_t t57 = ((((uint8_t *)&t56))[0] != 0);
                F.ret = t55;
                goto gm_fret;
            }
            F.l9 = t44;
        }
        {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t59 = t58;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 16), t60);
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        if (!((((uint8_t *)a2))[16] != 0)) goto ie61;
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_l10 = t62;
        {
            gm_b16_8 t64 = gm_l10;
            gm_t470aqkia5212 t65;
            gm_f_split_words_jk1lams((void *)&t65, (void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t470aqkia5212 t66 = t65;
            gm_trqfvmjtt4sij t67;
            if (!((((uint8_t *)&t66))[0] != 0)) {
                gm_trqfvmjtt4sij t68 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t66) + 8));
                t67 = t68;
            } else {
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                gm_l11 = t69;
                gm_b16_8 t71 = gm_l11;
                gm_tnpifv20irnph t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
                gm_tabb710d9mq2e t73;
                gm_f_format_7dijd7n((void *)&t73, (void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t74 = t73;
                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcmm27gepldtcm, 52, 57);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
                gm_b16_8 t76 = t75;
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_tjdv8ltmrgtv0 t78;
                memset(&t78, 0, sizeof t78);
                ((uint8_t *)&t78)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t78 + 8), t77);
                gm_tjdv8ltmrgtv0 t79 = t78;
                uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
                F.ret = t78;
                goto gm_fret;
            }
            F.l12 = t67;
        }
        {
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t82 = t81;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 32), t83);
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        gm_tfeab279c16rr t84 = F.l3;
        gm_tjdv8ltmrgtv0 t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
        ((uint8_t *)&t85)[0] = 0;
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t85 + 8), t84);
        gm_tjdv8ltmrgtv0 t86 = t85;
        uint8_t t87 = ((((uint8_t *)&t86))[0] != 0);
        F.ret = t85;
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
extern __attribute__((aligned(4))) uint8_t gm_f_has_c7mpl87(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_white_space_569v0vo(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_white_space_569v0vo F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l1 = t3;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l3 = t17;
                    {
                        uint8_t t20 = gm_l3;
                        gm_t4q2b5ibsrdik t21;
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)32ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 1), ((uint8_t)9ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 2), ((uint8_t)10ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 3), ((uint8_t)13ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 4), ((uint8_t)11ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 5), ((uint8_t)12ULL));
                        gm_t4q2b5ibsrdik t22 = t21;
                        uint8_t t23 = 0;
                        uint8_t t24 = t20;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 0, ((uint8_t *)&t24))) t23 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 1, ((uint8_t *)&t24))) t23 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 2, ((uint8_t *)&t24))) t23 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 3, ((uint8_t *)&t24))) t23 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 4, ((uint8_t *)&t24))) t23 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 5, ((uint8_t *)&t24))) t23 = 1;
                        uint8_t t25 = (uint8_t)(t23);
                        if (!t25) goto ie19;
                        {
                            uint8_t t27 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 131, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t33 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_default_24c3cui(void * a0);
__attribute__((aligned(4))) void gm_f_macros_hrin9id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_macros_hrin9id F;
    void * gm_l0; /* t */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    GM_STACK_CHECK(&F, 1404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdad15q200tmk));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skn7gps778eo2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 16, t3);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s33a8rv0vs9rh));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s2gnpj7k4l22n));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 48, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 64, t6);
    }
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        void * t11 = ((uint8_t *)&F.l1);
        if ((uintptr_t)0 > (uintptr_t)5 || (uintptr_t)5 > (uintptr_t)5) gm_trap_slice((uintptr_t)0, (uintptr_t)5, 5, gm_srcmm27gepldtcm, 199, 49);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t11 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)5 - (uintptr_t)0));
        gm_b16_8 t13 = t12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_f_command_b2u33fl((void *)&F.l2, (void *)NULL, cx, NULL, t7, t10, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_t32dckm4jfarl t18;
        memset((uint8_t *)&t18 + 3, 0, 5);
        memset((uint8_t *)&t18 + 50, 0, 6);
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 0, t19);
        gm_t9qkh0rsooc8d t20 = GM_LD(gm_t9qkh0rsooc8d, gm_cc7b06pnl1q);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 1, t20);
        gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, gm_cc7b06pnl1q);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 2, t21);
        gm_th2lqbo77pvsc t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t18 + 8, t22);
        gm_b16_8 t23 = GM_LD(gm_b16_8, gm_clslitrb510);
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 32, t23);
        GM_ST(uint8_t, (uint8_t *)&t18 + 48, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t18 + 49, ((uint8_t)0));
        gm_th2lqbo77pvsc t24;
        memset(&t24, 0, sizeof t24);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t18 + 56, t24);
        gm_tat31u3lvqjk4 t25;
        gm_f_output_mg6eie5((void *)&t25, (void *)NULL, cx, NULL, t17, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tat31u3lvqjk4 t26 = t25;
        gm_tjm4rm50v5mle t27;
        if (!((((uint8_t *)&t26))[0] != 0)) {
            gm_tjm4rm50v5mle t28 = GM_LD(gm_tjm4rm50v5mle, (((uint8_t *)&t26) + 8));
            t27 = t28;
        } else {
            gm_tj5jn79kq9pnm t29 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t26) + 8));
            gm_l3 = t29;
            gm_t6j6smj7qoctu t31;
            void * t32 = gm_l0;
            gm_b16_8 t33 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t33);
            gm_tj5jn79kq9pnm t34 = gm_l3;
            gm_b16_8 t35 = gm_f_run_error_gs1dinp((void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t35);
            gm_tabb710d9mq2e t36;
            gm_f_format_ra2b58j((void *)&t36, (void *)NULL, cx, NULL, &t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcmm27gepldtcm, 201, 17);
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
            gm_b16_8 t39 = t38;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_t3ujhu6qph7de t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            ((uint8_t *)&t41)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 8), t40);
            gm_t3ujhu6qph7de t42 = t41;
            uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
            F.ret = t41;
            goto gm_fret;
        }
        F.l4 = t27;
    }
    {
        gm_tsoiri44d0i6q t45 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&F.l4) + 0));
        uint8_t t46 = gm_f_success_q3lb6vt(t45);
        uint8_t t47 = (uint8_t)!t46;
        if (!t47) goto ie44;
        {
            gm_t6j6smj7qoctu t49;
            void * t50 = gm_l0;
            gm_b16_8 t51 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t51);
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 48) + 0));
            gm_b16_8 t53 = t52;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t54);
            gm_tabb710d9mq2e t55;
            gm_f_format_q3io8lg((void *)&t55, (void *)NULL, cx, NULL, &t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t56 = t55;
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcmm27gepldtcm, 204, 17);
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
            gm_b16_8 t58 = t57;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            gm_t3ujhu6qph7de t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            ((uint8_t *)&t60)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t60 + 8), t59);
            gm_t3ujhu6qph7de t61 = t60;
            uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
            F.ret = t60;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_b16_8 t64 = t63;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_t3ujhu6qph7de t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        ((uint8_t *)&t66)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
        gm_t3ujhu6qph7de t67 = t66;
        uint8_t t68 = ((((uint8_t *)&t67))[0] != 0);
        F.ret = t66;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_num_1gcf1u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_num_1gcf1u8 F;
    gm_b16_8 gm_l0; /* m */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_get_e3joqcc((void *)&t4, t2, t3);
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tf5srh4mp4b3i t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        gm_b16_8 t11 = gm_l1;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sfdbfrv46lihd));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
        uint8_t t13 = gm_f_contains_gkki4kq(t11, t12);
        if (!t13) goto ie10;
        {
            gm_tf5srh4mp4b3i t15;
            memset(&t15, 0, sizeof t15);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_b16_8 t17 = gm_l1;
        gm_tutnubt6u4hoq t18;
        gm_f_parse_int_8aueeu2((void *)&t18, t17);
        gm_tutnubt6u4hoq t19 = t18;
        gm_tf5srh4mp4b3i t20;
        if (!((((uint8_t *)&t19))[0] != 0)) {
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t19) + 8));
            gm_tf5srh4mp4b3i t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[8] = 1;
            t20 = t22;
        } else {
            gm_tf5srh4mp4b3i t23;
            memset(&t23, 0, sizeof t23);
            t20 = t23;
        }
        uint8_t t24 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runner_for_79o4h4m F;
    gm_b16_8 gm_l0; /* exe */
    GM_STACK_CHECK(&F, 568u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t470aqkia5212 t2;
        gm_f_runner_from_env_bnildq8((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t470aqkia5212 t3 = t2;
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
            gm_t470aqkia5212 t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t5 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t5 + 40, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t6 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t3) + 8));
        F.l1 = t6;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
        uint8_t t9 = ((t8 == ((uint64_t)0ULL)));
        if (!t9) goto ie7;
        gm_b16_8 t10 = gm_l0;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sc6celu0e642t));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        uint8_t t12 = gm_f_ends_with_biriu5l(t10, t11);
        if (!t12) goto ie7;
        {
            gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t14 = (void *)((uint8_t *)&F.l2);
            void * t15 = ((uint8_t *)gm_cl03rhutns3);
            if ((uintptr_t)0 > (uintptr_t)7 || (uintptr_t)7 > (uintptr_t)7) gm_trap_slice((uintptr_t)0, (uintptr_t)7, 7, gm_srcmm27gepldtcm, 231, 22);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t15 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)((uintptr_t)7 - (uintptr_t)0));
            gm_tftb1odc739t5 t17 = gm_f_extend_vogsd72(t14, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcmm27gepldtcm, 231, 9);
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        {
            gm_trqfvmjtt4sij t20 = F.l2;
            gm_t470aqkia5212 t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            ((uint8_t *)&t21)[0] = 0;
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t21 + 8), t20);
            gm_t470aqkia5212 t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_trqfvmjtt4sij t24 = F.l1;
        gm_t470aqkia5212 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        ((uint8_t *)&t25)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t25 + 8), t24);
        gm_t470aqkia5212 t26 = t25;
        uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
        F.ret = t25;
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
inline __attribute__((aligned(4))) void gm_f_runner_from_env_bnildq8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_runner_from_env_bnildq8 F;
    gm_b16_8 gm_l0; /* v */
    gm_b16_8 gm_l1; /* e */
    GM_STACK_CHECK(&F, 859u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc5p6b8d5c87a));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        gm_node *pn3 = gm_find(cx, 621690230);
        if (!pn3) gm_trap("no handler installed for effect `Env`", gm_srcmm27gepldtcm, 220, 10);
        gm_th2lqbo77pvsc t4;
        if (pn3->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t2), GM_LD(uintptr_t, (uint8_t *)&t2 + 8), &ev, &evn);
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[16] = 1;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            t4 = evok ? t6 : t7;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[1])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8 = t4;
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_trqfvmjtt4sij t11;
            gm_f_list_epvducg((void *)&t11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t470aqkia5212 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 0;
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t12 + 8), t11);
            gm_t470aqkia5212 t13 = t12;
            uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
            F.ret = t12;
            goto gm_fret;
        }
        gm_l0 = t9;
    }
    {
        gm_b16_8 t15 = gm_l0;
        gm_t470aqkia5212 t16;
        gm_f_split_words_jk1lams((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t470aqkia5212 t17 = t16;
        gm_trqfvmjtt4sij t18;
        if (!((((uint8_t *)&t17))[0] != 0)) {
            gm_trqfvmjtt4sij t19 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t17) + 8));
            t18 = t19;
        } else {
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_l1 = t20;
            gm_b16_8 t22 = gm_l1;
            gm_tnpifv20irnph t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
            gm_tabb710d9mq2e t24;
            gm_f_format_2g98t2r((void *)&t24, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcmm27gepldtcm, 221, 51);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_t470aqkia5212 t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 40, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
            gm_t470aqkia5212 t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        gm_t470aqkia5212 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t32 + 8), t18);
        gm_t470aqkia5212 t33 = t32;
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t32;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_strip_wasm_trap_report_h977b8r(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_wasm_trap_report_h977b8r F;
    gm_b16_8 gm_l0; /* stderr */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 271u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b0_1 t2;
    }
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, gm_cojk58d4vkf);
        gm_tf5srh4mp4b3i t5 = gm_f_find_nab29co(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        uint64_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b16_8 t9 = gm_l0;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l1 = t7;
    }
    {
        uint8_t t12 = 0;
        uint64_t t13 = gm_l1;
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) {
            uint64_t t15 = gm_l1;
            uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
            if (o17) gm_trap("integer overflow in `-`", gm_srcmm27gepldtcm, 249, 25);
            uint64_t t18 = r16;
            uint64_t t19 = (uintptr_t)t18;
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcmm27gepldtcm, 249, 18);
            uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t19 * 1));
            uint8_t t22 = ((t21 == ((uint8_t)10ULL)));
            t12 = t22;
        } else t12 = 1;
        if (!t12) goto ie11;
        {
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t27 = gm_l1;
            uint64_t t28 = (uintptr_t)t27;
            if (t26 > t28 || t28 > (uintptr_t)t24) gm_trap_slice(t26, t28, t24, gm_srcmm27gepldtcm, 249, 49);
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t25 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t26));
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b16_8 t31 = gm_l0;
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_target_72cra6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_target_72cra6q F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* m */
    gm_b16_8 gm_l2; /* e */
    GM_STACK_CHECK(&F, 1180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_is_default_24c3cui(t3);
        if (!t4) goto ie2;
        {
            gm_t0rfe1ou14h21 t6;
            gm_f_host_target_vsu78om((void *)&t6);
            gm_tod6pa33o8u9u t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 0;
            GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&t7 + 8), t6);
            gm_tod6pa33o8u9u t8 = t7;
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t3ujhu6qph7de t11;
        gm_f_macros_hrin9id((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_tod6pa33o8u9u t14;
            memset(&t14, 0, sizeof t14);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
        gm_l1 = t15;
    }
    {
        gm_b16_8 t16 = gm_l1;
        gm_tod6pa33o8u9u t17;
        gm_f_target_from_macros_hkgk1pc((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tod6pa33o8u9u t18 = t17;
        gm_t0rfe1ou14h21 t19;
        if (!((((uint8_t *)&t18))[0] != 0)) {
            gm_t0rfe1ou14h21 t20 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)&t18) + 8));
            t19 = t20;
        } else {
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_l2 = t21;
            gm_t6j6smj7qoctu t23;
            void * t24 = gm_l0;
            gm_b16_8 t25 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t25);
            gm_b16_8 t26 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t26);
            gm_tabb710d9mq2e t27;
            gm_f_format_gshr6sq((void *)&t27, (void *)NULL, cx, NULL, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t28 = t27;
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcmm27gepldtcm, 184, 54);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_b16_8 t30 = t29;
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_tod6pa33o8u9u t32;
            memset(&t32, 0, sizeof t32);
            ((uint8_t *)&t32)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
            gm_tod6pa33o8u9u t33 = t32;
            uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
            F.ret = t32;
            goto gm_fret;
        }
        gm_tod6pa33o8u9u t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        ((uint8_t *)&t35)[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&t35 + 8), t19);
        gm_tod6pa33o8u9u t36 = t35;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t35;
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
inline __attribute__((aligned(4))) void gm_f_target_for_omoprob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_target_for_omoprob F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* triple */
    gm_b16_8 gm_l2; /* m */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 1467u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t3ujhu6qph7de t3;
        gm_f_macros_hrin9id((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_tod6pa33o8u9u t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l2 = t7;
    }
    {
        gm_b16_8 t8 = gm_l2;
        gm_tod6pa33o8u9u t9;
        gm_f_target_from_macros_hkgk1pc((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tod6pa33o8u9u t10 = t9;
        gm_t0rfe1ou14h21 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_t0rfe1ou14h21 t12 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)&t10) + 8));
            t11 = t12;
        } else {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_l3 = t13;
            gm_t6j6smj7qoctu t15;
            void * t16 = gm_l0;
            gm_b16_8 t17 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t17);
            gm_b16_8 t18 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t15 + 16, t18);
            gm_tabb710d9mq2e t19;
            gm_f_format_gshr6sq((void *)&t19, (void *)NULL, cx, NULL, &t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcmm27gepldtcm, 191, 53);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_tod6pa33o8u9u t24;
            memset(&t24, 0, sizeof t24);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t23);
            gm_tod6pa33o8u9u t25 = t24;
            uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        F.l4 = t11;
    }
    {
        gm_b16_8 t27 = gm_l1;
        gm_b16_8 t28 = gm_l2;
        gm_tl572d7579hns t29;
        gm_f_check_triple_uqaropr((void *)&t29, (void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t30 = t29;
        gm_b0_1 t31;
        if (!((((uint8_t *)&t30))[0] != 0)) {
            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 8));
            t31 = t32;
        } else {
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_l5 = t33;
            gm_t6j6smj7qoctu t35;
            void * t36 = gm_l0;
            gm_b16_8 t37 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t35 + 0, t37);
            gm_b16_8 t38 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t35 + 16, t38);
            gm_tabb710d9mq2e t39;
            gm_f_format_gshr6sq((void *)&t39, (void *)NULL, cx, NULL, &t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t40 = t39;
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcmm27gepldtcm, 192, 49);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_b16_8 t42 = t41;
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            gm_tod6pa33o8u9u t44;
            memset(&t44, 0, sizeof t44);
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 8), t43);
            gm_tod6pa33o8u9u t45 = t44;
            uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
            F.ret = t44;
            goto gm_fret;
        }
    }
    {
        gm_t0rfe1ou14h21 t47 = F.l4;
        gm_tod6pa33o8u9u t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&t48 + 8), t47);
        gm_tod6pa33o8u9u t49 = t48;
        uint8_t t50 = ((((uint8_t *)&t49))[0] != 0);
        F.ret = t48;
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
__attribute__((aligned(4))) void gm_f_target_from_macros_hkgk1pc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_target_from_macros_hkgk1pc F;
    gm_b16_8 gm_l0; /* macros */
    gm_b16_8 gm_l2; /* <tmp> */
    gm_closure gm_l3; /* num */
    uint64_t gm_l4; /* ptr_bytes */
    gm_closure gm_l5; /* value */
    uint8_t gm_l6; /* little_endian */
    gm_b16_8 gm_l7; /* order */
    gm_b16_8 gm_l8; /* big */
    gm_closure gm_l9; /* has */
    gm_b16_8 gm_l10; /* arch */
    gm_b16_8 gm_l11; /* os_name */
    GM_STACK_CHECK(&F, 2976u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_p3t5tj8((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl5iavnkj4ht, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_s30klli((void *)NULL, (&wn5), NULL, t8);
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
        gm_closure t13 = ((gm_closure){(void *)gm_f_num_1gcf1u8, (void *)&F.h, cx});
        gm_l3 = t13;
    }
    {
        gm_closure t14 = gm_l3;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sn2e6t65fstoj));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(18));
        gm_closure t16 = t14;
        gm_node *ccx17 = t16.cx;
        void * t18 = t16.code;
        gm_tf5srh4mp4b3i t19;
        if (GM_PTR_TAGGED(t18)) {
            t19 = ((gm_tf5srh4mp4b3i (*)(gm_b16_8))GM_PTR_UNTAG(t18))(t15);
        } else {
            t19 = ((gm_tf5srh4mp4b3i (*)(void *, gm_node *, gm_node *, gm_b16_8))t18)(t16.env, ccx17, NULL, t15);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t20 = t19;
        uint64_t t21;
        if (((((uint8_t *)&t20))[8] != 0)) {
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_so5q4maooc958));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(34));
            gm_tod6pa33o8u9u t24;
            memset(&t24, 0, sizeof t24);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t23);
            gm_tod6pa33o8u9u t25 = t24;
            uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        gm_l4 = t21;
    }
    {
        uint64_t t28 = gm_l4;
        uint8_t t29 = (!(t28 == ((uint64_t)4ULL)));
        if (!t29) goto ie27;
        uint64_t t30 = gm_l4;
        uint8_t t31 = (!(t30 == ((uint64_t)8ULL)));
        if (!t31) goto ie27;
        {
            uint64_t t33 = gm_l4;
            gm_t4ce0jd3tdro4 t34;
            GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t33);
            gm_tabb710d9mq2e t35;
            gm_f_format_seuq78o((void *)&t35, (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcmm27gepldtcm, 374, 17);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_tod6pa33o8u9u t40;
            memset(&t40, 0, sizeof t40);
            ((uint8_t *)&t40)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 8), t39);
            gm_tod6pa33o8u9u t41 = t40;
            uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
            F.ret = t40;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t44 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_se6fhe7nkildm));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(17));
        gm_th2lqbo77pvsc t46;
        gm_f_get_e3joqcc((void *)&t46, t44, t45);
        gm_th2lqbo77pvsc t47 = t46;
        uint8_t t48;
        if (((((uint8_t *)&t47))[16] != 0)) goto mn50;
        t48 = ((uint8_t)1);
        goto me49;
mn50:;
        t48 = ((uint8_t)0);
        goto me49;
mn51:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 376, 8);
me49:;
        if (!t48) goto ie43;
        {
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sfrku7jk647q0));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(82));
            gm_tod6pa33o8u9u t54;
            memset(&t54, 0, sizeof t54);
            ((uint8_t *)&t54)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
            gm_tod6pa33o8u9u t55 = t54;
            uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
            F.ret = t54;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        gm_closure t57 = ((gm_closure){(void *)gm_f_value_3oqtvqn, (void *)&F.h, cx});
        gm_l5 = t57;
    }
    {
        gm_l6 = ((uint8_t)1);
    }
    {
        gm_closure t59 = gm_l5;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s5lr3gdic3o7e));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(14));
        gm_closure t61 = t59;
        gm_node *ccx62 = t61.cx;
        void * t63 = t61.code;
        gm_th2lqbo77pvsc t64;
        if (GM_PTR_TAGGED(t63)) {
            t64 = ((gm_th2lqbo77pvsc (*)(gm_b16_8))GM_PTR_UNTAG(t63))(t60);
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t63)((void *)&t64, t61.env, ccx62, NULL, t60);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t65 = t64;
        if (!((((uint8_t *)&t65))[16] != 0)) goto ie58;
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        gm_l7 = t66;
        {
            gm_closure t69 = gm_l5;
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_st5oflrv0tmc6));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(20));
            gm_closure t71 = t69;
            gm_node *ccx72 = t71.cx;
            void * t73 = t71.code;
            gm_th2lqbo77pvsc t74;
            if (GM_PTR_TAGGED(t73)) {
                t74 = ((gm_th2lqbo77pvsc (*)(gm_b16_8))GM_PTR_UNTAG(t73))(t70);
            } else {
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t73)((void *)&t74, t71.env, ccx72, NULL, t70);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t75 = t74;
            if (!((((uint8_t *)&t75))[16] != 0)) goto ie68;
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            gm_l8 = t76;
            {
                gm_b16_8 t78 = gm_l7;
                gm_b16_8 t79 = gm_l8;
                gm_b16_8 t80 = t78;
                gm_b16_8 t81 = t79;
                uint8_t t82 = (!gm_eq0iqiubfm7e(((uint8_t *)&t80), ((uint8_t *)&t81)));
                gm_l6 = t82;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_closure t83 = ((gm_closure){(void *)gm_f_has_c7mpl87, (void *)&F.h, cx});
        gm_l9 = t83;
    }
    {
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_svln5ui59rjil));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(5));
        gm_l10 = t84;
    }
    {
        gm_closure t86 = gm_l9;
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_smuveuet82l27));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(10));
        gm_closure t88 = t86;
        gm_node *ccx89 = t88.cx;
        void * t90 = t88.code;
        uint8_t t91;
        if (GM_PTR_TAGGED(t90)) {
            t91 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t90))(t87);
        } else {
            t91 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t90)(t88.env, ccx89, NULL, t87);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t91) goto ie85;
        {
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sje4rbc88csdu));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(6));
            gm_l10 = t93;
        }
        goto ix85;
ie85:;
        gm_closure t95 = gm_l9;
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_s2ct6k0fls3pb));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(11));
        gm_closure t97 = t95;
        gm_node *ccx98 = t97.cx;
        void * t99 = t97.code;
        uint8_t t100;
        if (GM_PTR_TAGGED(t99)) {
            t100 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t99))(t96);
        } else {
            t100 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t99)(t97.env, ccx98, NULL, t96);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t100) goto ie94;
        {
            gm_b16_8 t102;
            GM_ST(void *, (uint8_t *)&t102, (void *)(gm_spervk999ikim));
            GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(7));
            gm_l10 = t102;
        }
        goto ix94;
ie94:;
        gm_closure t104 = gm_l9;
        gm_b16_8 t105;
        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sup67cuqst8bs));
        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(7));
        gm_closure t106 = t104;
        gm_node *ccx107 = t106.cx;
        void * t108 = t106.code;
        uint8_t t109;
        if (GM_PTR_TAGGED(t108)) {
            t109 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t108))(t105);
        } else {
            t109 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t108)(t106.env, ccx107, NULL, t105);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t109) goto ie103;
        gm_closure t110 = gm_l3;
        gm_b16_8 t111;
        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sm3fgo8acha61));
        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(12));
        gm_closure t112 = t110;
        gm_node *ccx113 = t112.cx;
        void * t114 = t112.code;
        gm_tf5srh4mp4b3i t115;
        if (GM_PTR_TAGGED(t114)) {
            t115 = ((gm_tf5srh4mp4b3i (*)(gm_b16_8))GM_PTR_UNTAG(t114))(t111);
        } else {
            t115 = ((gm_tf5srh4mp4b3i (*)(void *, gm_node *, gm_node *, gm_b16_8))t114)(t112.env, ccx113, NULL, t111);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t116;
        GM_ST(uint8_t, (uint8_t *)&t116 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t116 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t116 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t116), ((uint64_t)64ULL));
        ((uint8_t *)&t116)[8] = 1;
        gm_tf5srh4mp4b3i t117 = t115;
        gm_tf5srh4mp4b3i t118 = t116;
        uint8_t t119 = (gm_eqmpqhhfmb90(((uint8_t *)&t117), ((uint8_t *)&t118)));
        if (!t119) goto ie103;
        {
            gm_b16_8 t121;
            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sar7ovt8nasdo));
            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(7));
            gm_l10 = t121;
        }
        goto ix103;
ie103:;
        gm_closure t123 = gm_l9;
        gm_b16_8 t124;
        GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sgfrcimthamdn));
        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(9));
        gm_closure t125 = t123;
        gm_node *ccx126 = t125.cx;
        void * t127 = t125.code;
        uint8_t t128;
        if (GM_PTR_TAGGED(t127)) {
            t128 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t127))(t124);
        } else {
            t128 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t127)(t125.env, ccx126, NULL, t124);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t128) goto ie122;
        {
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sqg9do4ma3giu));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(5));
            gm_l10 = t130;
        }
        goto ix122;
ie122:;
        gm_closure t132 = gm_l9;
        gm_b16_8 t133;
        GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sr389r8t7d0mf));
        GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(10));
        gm_closure t134 = t132;
        gm_node *ccx135 = t134.cx;
        void * t136 = t134.code;
        uint8_t t137;
        if (GM_PTR_TAGGED(t136)) {
            t137 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t136))(t133);
        } else {
            t137 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t136)(t134.env, ccx135, NULL, t133);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t137) goto ie131;
        {
            gm_b16_8 t139;
            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sbrifoc7ea21m));
            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(6));
            gm_l10 = t139;
        }
        goto ix131;
ie131:;
ix131:;
ix122:;
ix103:;
ix94:;
ix85:;
    }
    {
        gm_b16_8 t140;
        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_soo4rtckloi4d));
        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(12));
        gm_l11 = t140;
    }
    {
        gm_closure t142 = gm_l9;
        gm_b16_8 t143;
        GM_ST(void *, (uint8_t *)&t143, (void *)(gm_ssevq7ii4bscf));
        GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(9));
        gm_closure t144 = t142;
        gm_node *ccx145 = t144.cx;
        void * t146 = t144.code;
        uint8_t t147;
        if (GM_PTR_TAGGED(t146)) {
            t147 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t146))(t143);
        } else {
            t147 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t146)(t144.env, ccx145, NULL, t143);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t147) goto ie141;
        {
            gm_b16_8 t149;
            GM_ST(void *, (uint8_t *)&t149, (void *)(gm_scq91tgm9lph4));
            GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(5));
            gm_l11 = t149;
        }
        goto ix141;
ie141:;
        gm_closure t151 = gm_l9;
        gm_b16_8 t152;
        GM_ST(void *, (uint8_t *)&t152, (void *)(gm_smiidqh71uo72));
        GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(9));
        gm_closure t153 = t151;
        gm_node *ccx154 = t153.cx;
        void * t155 = t153.code;
        uint8_t t156;
        if (GM_PTR_TAGGED(t155)) {
            t156 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t155))(t152);
        } else {
            t156 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t155)(t153.env, ccx154, NULL, t152);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t156) goto ie150;
        {
            gm_b16_8 t158;
            GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sjdv8icqpfhc5));
            GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(5));
            gm_l11 = t158;
        }
        goto ix150;
ie150:;
        gm_closure t160 = gm_l9;
        gm_b16_8 t161;
        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sl5rf5cfkcmdk));
        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(6));
        gm_closure t162 = t160;
        gm_node *ccx163 = t162.cx;
        void * t164 = t162.code;
        uint8_t t165;
        if (GM_PTR_TAGGED(t164)) {
            t165 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t164))(t161);
        } else {
            t165 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t164)(t162.env, ccx163, NULL, t161);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t165) goto ie159;
        {
            gm_b16_8 t167;
            GM_ST(void *, (uint8_t *)&t167, (void *)(gm_s048hmhpmbi7c));
            GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(7));
            gm_l11 = t167;
        }
        goto ix159;
ie159:;
        gm_closure t169 = gm_l9;
        gm_b16_8 t170;
        GM_ST(void *, (uint8_t *)&t170, (void *)(gm_s1d20j48bjjln));
        GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(11));
        gm_closure t171 = t169;
        gm_node *ccx172 = t171.cx;
        void * t173 = t171.code;
        uint8_t t174;
        if (GM_PTR_TAGGED(t173)) {
            t174 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t173))(t170);
        } else {
            t174 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t173)(t171.env, ccx172, NULL, t170);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t174) goto ie168;
        {
            gm_b16_8 t176;
            GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sn3ltks10rrv7));
            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(7));
            gm_l11 = t176;
        }
        goto ix168;
ie168:;
        gm_closure t178 = gm_l9;
        gm_b16_8 t179;
        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_scgvr0isof71e));
        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(8));
        gm_closure t180 = t178;
        gm_node *ccx181 = t180.cx;
        void * t182 = t180.code;
        uint8_t t183;
        if (GM_PTR_TAGGED(t182)) {
            t183 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t182))(t179);
        } else {
            t183 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t182)(t180.env, ccx181, NULL, t179);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t183) goto ie177;
        {
            gm_b16_8 t185;
            GM_ST(void *, (uint8_t *)&t185, (void *)(gm_svl13be5ca8rt));
            GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(4));
            gm_l11 = t185;
        }
        goto ix177;
ie177:;
ix177:;
ix168:;
ix159:;
ix150:;
ix141:;
    }
    {
        gm_t0rfe1ou14h21 t186;
        memset((uint8_t *)&t186 + 9, 0, 7);
        memset((uint8_t *)&t186 + 58, 0, 6);
        uint64_t t187 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t186 + 0, t187);
        uint8_t t188 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&t186 + 8, t188);
        uint64_t t189;
        gm_closure t191 = gm_l9;
        gm_b16_8 t192;
        GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sgfrcimthamdn));
        GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(9));
        gm_closure t193 = t191;
        gm_node *ccx194 = t193.cx;
        void * t195 = t193.code;
        uint8_t t196;
        if (GM_PTR_TAGGED(t195)) {
            t196 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t195))(t192);
        } else {
            t196 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t195)(t193.env, ccx194, NULL, t192);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t196) goto ie190;
        uint64_t t198 = ((uint64_t)8ULL);
        t189 = t198;
        goto ix190;
ie190:;
        uint64_t t200 = ((uint64_t)16ULL);
        t189 = t200;
ix190:;
        GM_ST(uint64_t, (uint8_t *)&t186 + 16, t189);
        gm_b16_8 t201 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t186 + 24, t201);
        gm_b16_8 t202 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t186 + 40, t202);
        gm_closure t203 = gm_l9;
        gm_b16_8 t204;
        GM_ST(void *, (uint8_t *)&t204, (void *)(gm_su4retifs0sre));
        GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(8));
        gm_closure t205 = t203;
        gm_node *ccx206 = t205.cx;
        void * t207 = t205.code;
        uint8_t t208;
        if (GM_PTR_TAGGED(t207)) {
            t208 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t207))(t204);
        } else {
            t208 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t207)(t205.env, ccx206, NULL, t204);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&t186 + 56, t208);
        gm_closure t209 = gm_l9;
        gm_b16_8 t210;
        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_s26219ruuua5s));
        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(18));
        gm_closure t211 = t209;
        gm_node *ccx212 = t211.cx;
        void * t213 = t211.code;
        uint8_t t214;
        if (GM_PTR_TAGGED(t213)) {
            t214 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t213))(t210);
        } else {
            t214 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t213)(t211.env, ccx212, NULL, t210);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&t186 + 57, t214);
        gm_tod6pa33o8u9u t215;
        GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
        ((uint8_t *)&t215)[0] = 0;
        GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&t215 + 8), t186);
        gm_tod6pa33o8u9u t216 = t215;
        uint8_t t217 = ((((uint8_t *)&t216))[0] != 0);
        F.ret = t215;
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
inline __attribute__((aligned(4))) void gm_f_value_3oqtvqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_value_3oqtvqn F;
    gm_b16_8 gm_l0; /* m */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_get_e3joqcc((void *)&t4, t2, t3);
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        void * t10 = (void *)((uint8_t *)&((struct gm_fr_target_from_macros_hkgk1pc *)((&F.h)->up))->l1);
        gm_b16_8 t11 = gm_l1;
        gm_th2lqbo77pvsc t12;
        gm_f_get_e3joqcc((void *)&t12, t10, t11);
        gm_th2lqbo77pvsc t13 = t12;
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_b16_8 t16 = gm_l1;
            t14 = t16;
        }
        gm_th2lqbo77pvsc t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t17), t14);
        ((uint8_t *)&t17)[16] = 1;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_with_manifest_flags_objbdaj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_with_manifest_flags_objbdaj F;
    gm_b16_8 gm_l1; /* cflags */
    gm_b16_8 gm_l2; /* ldflags */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tfeab279c16rr t2 = F.l0;
        F.l3 = t2;
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie3;
        {
            gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t7 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t8 = gm_l1;
            gm_tftb1odc739t5 t9 = gm_f_extend_vogsd72(t7, t8);
            gm_tftb1odc739t5 t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcmm27gepldtcm, 166, 9);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        {
            void * t12 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 16));
            gm_tftb1odc739t5 t14 = gm_f_extend_vogsd72(t12, t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcmm27gepldtcm, 167, 9);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        {
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 16), t19);
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t22 = (t21 > ((uint64_t)0ULL));
        if (!t22) goto ie20;
        {
            gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t24 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t25 = gm_l2;
            gm_tftb1odc739t5 t26 = gm_f_extend_vogsd72(t24, t25);
            gm_tftb1odc739t5 t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcmm27gepldtcm, 172, 9);
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
        }
        {
            void * t29 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 32));
            gm_tftb1odc739t5 t31 = gm_f_extend_vogsd72(t29, t30);
            gm_tftb1odc739t5 t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcmm27gepldtcm, 173, 9);
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
        }
        {
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            GM_ST(gm_b16_8, (((uint8_t *)&F.l3) + 32), t36);
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 48);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_words_of_f1gud4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_words_of_f1gud4i F;
    gm_b16_8 gm_l0; /* a */
    gm_th2lqbo77pvsc gm_l1; /* b */
    gm_b16_8 gm_l3; /* w */
    GM_STACK_CHECK(&F, 209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_3ef746b(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmm27gepldtcm, 125, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie7;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l3 = t8;
        {
            void * t10 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t11 = gm_l3;
            gm_tftb1odc739t5 t12 = gm_f_push_3ef746b(t10, t11);
            gm_tftb1odc739t5 t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcmm27gepldtcm, 126, 17);
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
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
