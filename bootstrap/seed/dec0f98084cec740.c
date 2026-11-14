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
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_t04llc5uen0qo;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint64_t f4; uint32_t f5; uint32_t f6; void * f7; } gm_t1lvkimu9u0hv;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; void * f4; } gm_t3a21dm0j52qn;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; } gm_t4h1llua034m2;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t f5; uint8_t f6; uint8_t p7[2]; uint64_t f7; void * f8; } gm_td7fbab3thbb2;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[1]; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; void * f7; } gm_te4o1hd8337m3;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; void * f7; } gm_thbsr19fadfjo;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[1]; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_ti4ei62u4i1d9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_tn3qmqcq7vsk9;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; uint64_t f3; void * f4; } gm_tu97pd4fjd5oe;
_Static_assert(sizeof(gm_t04llc5uen0qo) == 32 && _Alignof(gm_t04llc5uen0qo) == 8 && offsetof(gm_t04llc5uen0qo, f0) == 0 && offsetof(gm_t04llc5uen0qo, f1) == 4 && offsetof(gm_t04llc5uen0qo, f2) == 8 && offsetof(gm_t04llc5uen0qo, f3) == 12 && offsetof(gm_t04llc5uen0qo, f4) == 16 && offsetof(gm_t04llc5uen0qo, f5) == 24, "layout of `(base: u32, off: u32, size: u32, align: u32, v: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t1lvkimu9u0hv) == 40 && _Alignof(gm_t1lvkimu9u0hv) == 8 && offsetof(gm_t1lvkimu9u0hv, f0) == 0 && offsetof(gm_t1lvkimu9u0hv, f1) == 4 && offsetof(gm_t1lvkimu9u0hv, f2) == 8 && offsetof(gm_t1lvkimu9u0hv, f3) == 12 && offsetof(gm_t1lvkimu9u0hv, f4) == 16 && offsetof(gm_t1lvkimu9u0hv, f5) == 24 && offsetof(gm_t1lvkimu9u0hv, f6) == 28 && offsetof(gm_t1lvkimu9u0hv, f7) == 32, "layout of `(r: u32, p: u32, len: u32, i: u32, esz: u64, size: u32, align: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t3a21dm0j52qn) == 32 && _Alignof(gm_t3a21dm0j52qn) == 8 && offsetof(gm_t3a21dm0j52qn, f0) == 0 && offsetof(gm_t3a21dm0j52qn, f1) == 4 && offsetof(gm_t3a21dm0j52qn, f2) == 8 && offsetof(gm_t3a21dm0j52qn, f3) == 16 && offsetof(gm_t3a21dm0j52qn, f4) == 24, "layout of `(p: u32, len: u32, i: u32, esz: u64, n: ^Node)`");
_Static_assert(sizeof(gm_t4h1llua034m2) == 16 && _Alignof(gm_t4h1llua034m2) == 8 && offsetof(gm_t4h1llua034m2, f0) == 0 && offsetof(gm_t4h1llua034m2, f1) == 4 && offsetof(gm_t4h1llua034m2, f2) == 8, "layout of `(p: u32, i: u32, esz: u64)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; uint16_t f2; uint32_t f3; uint32_t f4; uint32_t f5; } gm_tb1f35qilrmun;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_td7fbab3thbb2) == 40 && _Alignof(gm_td7fbab3thbb2) == 8 && offsetof(gm_td7fbab3thbb2, f0) == 0 && offsetof(gm_td7fbab3thbb2, f1) == 4 && offsetof(gm_td7fbab3thbb2, f2) == 8 && offsetof(gm_td7fbab3thbb2, f3) == 12 && offsetof(gm_td7fbab3thbb2, f4) == 16 && offsetof(gm_td7fbab3thbb2, f5) == 20 && offsetof(gm_td7fbab3thbb2, f6) == 21 && offsetof(gm_td7fbab3thbb2, f7) == 24 && offsetof(gm_td7fbab3thbb2, f8) == 32, "layout of `(at: u32, p: u32, len: u32, lo: u32, hi: u32, has_len: bool, inclusive: bool, esz: u64, n: ^Node)`");
_Static_assert(sizeof(gm_te4o1hd8337m3) == 32 && _Alignof(gm_te4o1hd8337m3) == 8 && offsetof(gm_te4o1hd8337m3, f0) == 0 && offsetof(gm_te4o1hd8337m3, f1) == 1 && offsetof(gm_te4o1hd8337m3, f2) == 2 && offsetof(gm_te4o1hd8337m3, f3) == 4 && offsetof(gm_te4o1hd8337m3, f4) == 8 && offsetof(gm_te4o1hd8337m3, f5) == 12 && offsetof(gm_te4o1hd8337m3, f6) == 16 && offsetof(gm_te4o1hd8337m3, f7) == 24, "layout of `(op: u8, signed: bool, wrap: bool, bits: u32, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thbsr19fadfjo) == 48 && _Alignof(gm_thbsr19fadfjo) == 8 && offsetof(gm_thbsr19fadfjo, f0) == 0 && offsetof(gm_thbsr19fadfjo, f1) == 4 && offsetof(gm_thbsr19fadfjo, f2) == 8 && offsetof(gm_thbsr19fadfjo, f3) == 16 && offsetof(gm_thbsr19fadfjo, f4) == 24 && offsetof(gm_thbsr19fadfjo, f5) == 28 && offsetof(gm_thbsr19fadfjo, f6) == 32 && offsetof(gm_thbsr19fadfjo, f7) == 40, "layout of `(p: u32, len: u32, i: u32, esz: u64, size: u32, align: u32, v: u32, n: ^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; gm_ttovhfee8mf28 f1; uint8_t f2; uint8_t p3[3]; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_thji9nptko1bl;
_Static_assert(sizeof(gm_ti4ei62u4i1d9) == 24 && _Alignof(gm_ti4ei62u4i1d9) == 8 && offsetof(gm_ti4ei62u4i1d9, f0) == 0 && offsetof(gm_ti4ei62u4i1d9, f1) == 1 && offsetof(gm_ti4ei62u4i1d9, f2) == 2 && offsetof(gm_ti4ei62u4i1d9, f3) == 4 && offsetof(gm_ti4ei62u4i1d9, f4) == 8 && offsetof(gm_ti4ei62u4i1d9, f5) == 12 && offsetof(gm_ti4ei62u4i1d9, f6) == 16, "layout of `(op: u8, signed: bool, wrap: bool, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; gm_ttovhfee8mf28 f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_tiag95i83438g;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3qmqcq7vsk9) == 32 && _Alignof(gm_tn3qmqcq7vsk9) == 8 && offsetof(gm_tn3qmqcq7vsk9, f0) == 0 && offsetof(gm_tn3qmqcq7vsk9, f1) == 4 && offsetof(gm_tn3qmqcq7vsk9, f2) == 8 && offsetof(gm_tn3qmqcq7vsk9, f3) == 12 && offsetof(gm_tn3qmqcq7vsk9, f4) == 16 && offsetof(gm_tn3qmqcq7vsk9, f5) == 20 && offsetof(gm_tn3qmqcq7vsk9, f6) == 24, "layout of `(r: u32, levels: u32, local: u32, rel: u32, size: u32, align: u32, n: ^Node)`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tu97pd4fjd5oe) == 32 && _Alignof(gm_tu97pd4fjd5oe) == 8 && offsetof(gm_tu97pd4fjd5oe, f0) == 0 && offsetof(gm_tu97pd4fjd5oe, f1) == 4 && offsetof(gm_tu97pd4fjd5oe, f2) == 8 && offsetof(gm_tu97pd4fjd5oe, f3) == 16 && offsetof(gm_tu97pd4fjd5oe, f4) == 24, "layout of `(p: u32, i: u32, len: u64, esz: u64, n: ^Node)`");
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tufm7evi61pnh;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_t06kd2nmcgb3u;
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint32_t f3; uint32_t f4; } gm_t78m5u6j4a3pq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb1f35qilrmun) == 16 && _Alignof(gm_tb1f35qilrmun) == 4 && offsetof(gm_tb1f35qilrmun, f0) == 0 && offsetof(gm_tb1f35qilrmun, f1) == 1 && offsetof(gm_tb1f35qilrmun, f2) == 2 && offsetof(gm_tb1f35qilrmun, f3) == 4 && offsetof(gm_tb1f35qilrmun, f4) == 8 && offsetof(gm_tb1f35qilrmun, f5) == 12, "layout of `Bc`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[1]; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tfd1kqjeh4les;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thji9nptko1bl) == 40 && _Alignof(gm_thji9nptko1bl) == 8 && offsetof(gm_thji9nptko1bl, f0) == 0 && offsetof(gm_thji9nptko1bl, f1) == 8 && offsetof(gm_thji9nptko1bl, f2) == 16 && offsetof(gm_thji9nptko1bl, f3) == 20 && offsetof(gm_thji9nptko1bl, f4) == 24 && offsetof(gm_thji9nptko1bl, f5) == 28 && offsetof(gm_thji9nptko1bl, f6) == 32, "layout of `(t: IntW, shift: IntW, wrap: bool, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_tiag95i83438g) == 40 && _Alignof(gm_tiag95i83438g) == 8 && offsetof(gm_tiag95i83438g, f0) == 0 && offsetof(gm_tiag95i83438g, f1) == 8 && offsetof(gm_tiag95i83438g, f2) == 16 && offsetof(gm_tiag95i83438g, f3) == 20 && offsetof(gm_tiag95i83438g, f4) == 24 && offsetof(gm_tiag95i83438g, f5) == 32, "layout of `(t: IntW, shift: IntW, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tnkql1rjn1vbj;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint8_t p6[4]; void * f6; uint64_t f7; gm_b16_8 f8; uint32_t f9; uint8_t p10[4]; uint64_t f10; void * f11; uint32_t f12; uint8_t p13[4]; gm_th2lqbo77pvsc f13; uint32_t f14; uint8_t p15[4]; void * f15; } gm_tu1mv36lldn2v;
_Static_assert(sizeof(gm_tufm7evi61pnh) == 24 && _Alignof(gm_tufm7evi61pnh) == 4 && offsetof(gm_tufm7evi61pnh, f0) == 0 && offsetof(gm_tufm7evi61pnh, f1) == 8 && offsetof(gm_tufm7evi61pnh, f2) == 12 && offsetof(gm_tufm7evi61pnh, f3) == 16 && offsetof(gm_tufm7evi61pnh, f4) == 20, "layout of `(how: WordCmp, negated: bool, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t06kd2nmcgb3u) == 40 && _Alignof(gm_t06kd2nmcgb3u) == 8 && offsetof(gm_t06kd2nmcgb3u, f0) == 0 && offsetof(gm_t06kd2nmcgb3u, f1) == 4 && offsetof(gm_t06kd2nmcgb3u, f2) == 12 && offsetof(gm_t06kd2nmcgb3u, f3) == 20 && offsetof(gm_t06kd2nmcgb3u, f4) == 24 && offsetof(gm_t06kd2nmcgb3u, f5) == 28 && offsetof(gm_t06kd2nmcgb3u, f6) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t78m5u6j4a3pq) == 36 && _Alignof(gm_t78m5u6j4a3pq) == 4 && offsetof(gm_t78m5u6j4a3pq, f0) == 0 && offsetof(gm_t78m5u6j4a3pq, f1) == 12 && offsetof(gm_t78m5u6j4a3pq, f2) == 20 && offsetof(gm_t78m5u6j4a3pq, f3) == 28 && offsetof(gm_t78m5u6j4a3pq, f4) == 32, "layout of `(op: Builtin, from: IntW, to: IntW, r: u32, a: u32)`");
_Static_assert(sizeof(gm_tfd1kqjeh4les) == 16 && _Alignof(gm_tfd1kqjeh4les) == 4 && offsetof(gm_tfd1kqjeh4les, f0) == 0 && offsetof(gm_tfd1kqjeh4les, f1) == 2 && offsetof(gm_tfd1kqjeh4les, f2) == 4 && offsetof(gm_tfd1kqjeh4les, f3) == 8 && offsetof(gm_tfd1kqjeh4les, f4) == 12, "layout of `(op: BinOp, wide: bool, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tnkql1rjn1vbj) == 24 && _Alignof(gm_tnkql1rjn1vbj) == 4 && offsetof(gm_tnkql1rjn1vbj, f0) == 0 && offsetof(gm_tnkql1rjn1vbj, f1) == 4 && offsetof(gm_tnkql1rjn1vbj, f2) == 12 && offsetof(gm_tnkql1rjn1vbj, f3) == 16 && offsetof(gm_tnkql1rjn1vbj, f4) == 20, "layout of `(op: BinOp, how: WordCmp, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tu1mv36lldn2v) == 128 && _Alignof(gm_tu1mv36lldn2v) == 8 && offsetof(gm_tu1mv36lldn2v, f0) == 0 && offsetof(gm_tu1mv36lldn2v, f1) == 1 && offsetof(gm_tu1mv36lldn2v, f2) == 4 && offsetof(gm_tu1mv36lldn2v, f3) == 8 && offsetof(gm_tu1mv36lldn2v, f4) == 12 && offsetof(gm_tu1mv36lldn2v, f5) == 16 && offsetof(gm_tu1mv36lldn2v, f6) == 24 && offsetof(gm_tu1mv36lldn2v, f7) == 32 && offsetof(gm_tu1mv36lldn2v, f8) == 40 && offsetof(gm_tu1mv36lldn2v, f9) == 56 && offsetof(gm_tu1mv36lldn2v, f10) == 64 && offsetof(gm_tu1mv36lldn2v, f11) == 72 && offsetof(gm_tu1mv36lldn2v, f12) == 80 && offsetof(gm_tu1mv36lldn2v, f13) == 88 && offsetof(gm_tu1mv36lldn2v, f14) == 112 && offsetof(gm_tu1mv36lldn2v, f15) == 120, "layout of `CallSite`");
struct gm_fr_bc3_m9eakek {
    gm_frame h;
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_align_6s4vol1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bc_at_91tc4br {
    gm_frame h;
    void * ret;
};
struct gm_fr_bc_cmp_hcolbhq {
    gm_frame h;
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_free_v10vouf {
    gm_frame h;
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_of_ga25123 {
    gm_frame h;
    gm_t78m5u6j4a3pq l23; /* x */
    gm_t1lvkimu9u0hv l66; /* x */
    gm_thbsr19fadfjo l67; /* x */
    gm_tiag95i83438g l74; /* x */
    gm_thji9nptko1bl l75; /* x */
    gm_t06kd2nmcgb3u l76; /* x */
    gm_td7fbab3thbb2 l77; /* x */
    gm_tb1f35qilrmun ret;
};
struct gm_fr_bc_pc_vubo137 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_empty_slice_thu1fog {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fits16_1bnmjva {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fits32_a5qt6pu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_memory_t2cr4s3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ld1_adb1ani {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ld2_gce8h7a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ld4_lj8nmpt {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ld8_ecs41ho {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ldn_94iknnm {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_log2_acbegp6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pick_kkl1a7m {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_st1_aqrj474 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_st2_lkdmkcq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_st4_38u38jn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_st8_6aops52 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stn_cbrtno6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_vm_assemble_k5irtcr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vm_view_fo2var1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_word_size_mu6ni08 {
    gm_frame h;
    uint8_t ret;
};
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc3_m9eakek(gm_t9qkh0rsooc8d a0, uint32_t a1, uint32_t a2, uint32_t a3);
inline __attribute__((aligned(4))) uint8_t gm_f_bc_align_6s4vol1(uint32_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_bc_at_91tc4br(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_cmp_hcolbhq(gm_ts6oqppjh0ba3 a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint16_t a7);
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_free_v10vouf(void);
__attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_of_ga25123(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_bc_pc_vubo137(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_thu1fog(void);
inline __attribute__((aligned(4))) uint8_t gm_f_fits16_1bnmjva(uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fits32_a5qt6pu(uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_5tbvv4o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_in_memory_t2cr4s3(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld1_adb1ani(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld2_gce8h7a(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld4_lj8nmpt(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld8_ecs41ho(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ldn_94iknnm(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_log2_acbegp6(uint32_t a0);
__attribute__((aligned(4))) void gm_f_make_fill_0sgj1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, gm_tb1f35qilrmun a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_bqbf5rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_kjs404n(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_pick_kkl1a7m(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st1_aqrj474(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st2_lkdmkcq(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st4_38u38jn(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st8_6aops52(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_stn_cbrtno6(void * a0, uint8_t a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_vm_assemble_k5irtcr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_vm_view_fo2var1(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
inline __attribute__((aligned(4))) uint8_t gm_f_word_size_mu6ni08(uint64_t a0);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cl7gtp51edk[4] __attribute__((aligned(4)));
extern uint8_t gm_k68pmlng4vp[40] __attribute__((aligned(8)));
extern uint8_t gm_kfh7mc23f4a[40] __attribute__((aligned(8)));
extern uint8_t gm_kiiubtvdrqu[40] __attribute__((aligned(8)));
extern uint8_t gm_kojtndgobcj[40] __attribute__((aligned(8)));
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6unagr666v56[];
extern const char gm_srcq7ohh8h77bn7[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc3_m9eakek(gm_t9qkh0rsooc8d a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc3_m9eakek F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    uint32_t gm_l1; /* a */
    uint32_t gm_l2; /* b */
    uint32_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9qkh0rsooc8d t1 = gm_l0;
    uint32_t t2 = gm_l1;
    uint32_t t3 = gm_l2;
    uint32_t t4 = gm_l3;
    gm_tb1f35qilrmun t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5 + 0), t1);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 1), ((uint8_t)0ULL));
    GM_ST(uint16_t, ((uint8_t *)&t5 + 2), ((uint16_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t2);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t3);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 12), t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bc_align_6s4vol1(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_align_6s4vol1 F;
    uint32_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 > ((uint32_t)0ULL));
    if (t4) {
        uint32_t t5 = gm_l0;
        uint8_t t6 = (t5 <= ((uint32_t)32768ULL));
        t2 = t6;
    } else t2 = 0;
    if (t2) {
        uint32_t t7 = gm_l0;
        uint32_t t8 = gm_l0;
        uint32_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint32_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 344, 66);
        uint32_t t11 = r9;
        uint32_t t12 = (uint32_t)(t7 & t11);
        uint8_t t13 = ((t12 == ((uint32_t)0ULL)));
        t1 = t13;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_bc_at_91tc4br(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_at_91tc4br F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* k */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)t3 + t2 * 16);
    void * t5 = t4;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_free_v10vouf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_free_v10vouf F;
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_tb1f35qilrmun t2;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t2 + 0), t1);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)0ULL));
    GM_ST(uint16_t, ((uint8_t *)&t2 + 2), ((uint16_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 4), ((uint32_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 8), ((uint32_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 12), ((uint32_t)0ULL));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_bc_pc_vubo137(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_pc_vubo137 F;
    void * gm_l0; /* start */
    void * gm_l1; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = (uintptr_t)t3;
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    if (o6) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 603, 50);
    uint64_t t7 = r5;
    if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_src6unagr666v56, 603, 49);
    uint64_t t8 = (uint64_t)(t7 / ((uint64_t)16ULL));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_thu1fog(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_thu1fog F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fits16_1bnmjva(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits16_1bnmjva F;
    uint64_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint8_t t2 = (t1 <= ((uint64_t)65535ULL));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fits32_a5qt6pu(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits32_a5qt6pu F;
    uint64_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint8_t t2 = (t1 <= ((uint64_t)4294967295ULL));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_in_memory_t2cr4s3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_in_memory_t2cr4s3 F;
    void * gm_l0; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t7 != ((uint8_t)1ULL)) goto pa6;
    goto po5;
pa6:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto pa8;
    goto po5;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)59ULL)) goto pa10;
    goto po5;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t13 != ((uint8_t)51ULL)) goto pa12;
    goto po5;
pa12:;
    goto mn4;
po5:;
    t2 = ((uint8_t)1);
    goto me3;
mn4:;
    t2 = ((uint8_t)0);
    goto me3;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 538, 43);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld1_adb1ani(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld1_adb1ani F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = (uintptr_t)((uint64_t)0ULL);
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)t2 + t1 * 1));
    uint64_t t4 = (uint64_t)t3;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld2_gce8h7a(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld2_gce8h7a F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = (uintptr_t)((uint64_t)0ULL);
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)t2 + t1 * 1));
    uint64_t t4 = (uint64_t)t3;
    uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
    void * t6 = gm_l0;
    uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)t6 + t5 * 1));
    uint64_t t8 = (uint64_t)t7;
    unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_src6unagr666v56, 267, 50);
    uint64_t r10 = (uint64_t)((uint64_t)t8 << (int)n9);
    bool o11 = (uint64_t)(r10 >> (int)n9) != t8;
    if (o11) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 267, 50);
    uint64_t t12 = r10;
    uint64_t t13 = (uint64_t)(t4 | t12);
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld4_lj8nmpt(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld4_lj8nmpt F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = (uintptr_t)((uint64_t)0ULL);
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)t2 + t1 * 1));
    uint64_t t4 = (uint64_t)t3;
    uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
    void * t6 = gm_l0;
    uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)t6 + t5 * 1));
    uint64_t t8 = (uint64_t)t7;
    unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_src6unagr666v56, 270, 50);
    uint64_t r10 = (uint64_t)((uint64_t)t8 << (int)n9);
    bool o11 = (uint64_t)(r10 >> (int)n9) != t8;
    if (o11) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 270, 50);
    uint64_t t12 = r10;
    uint64_t t13 = (uint64_t)(t4 | t12);
    uint64_t t14 = (uintptr_t)((uint64_t)2ULL);
    void * t15 = gm_l0;
    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)t15 + t14 * 1));
    uint64_t t17 = (uint64_t)t16;
    unsigned __int128 n18 = (unsigned __int128)((uint64_t)16ULL);
    if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_src6unagr666v56, 270, 69);
    uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
    bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
    if (o20) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 270, 69);
    uint64_t t21 = r19;
    uint64_t t22 = (uint64_t)(t13 | t21);
    uint64_t t23 = (uintptr_t)((uint64_t)3ULL);
    void * t24 = gm_l0;
    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)t24 + t23 * 1));
    uint64_t t26 = (uint64_t)t25;
    unsigned __int128 n27 = (unsigned __int128)((uint64_t)24ULL);
    if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_src6unagr666v56, 270, 89);
    uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
    bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
    if (o29) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 270, 89);
    uint64_t t30 = r28;
    uint64_t t31 = (uint64_t)(t22 | t30);
    F.ret = t31;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ld8_ecs41ho(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ld8_ecs41ho F;
    void * gm_l0; /* p */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = (uintptr_t)((uint64_t)0ULL);
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)t2 + t1 * 1));
    uint64_t t4 = (uint64_t)t3;
    uint64_t t5 = (uintptr_t)((uint64_t)1ULL);
    void * t6 = gm_l0;
    uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)t6 + t5 * 1));
    uint64_t t8 = (uint64_t)t7;
    unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_src6unagr666v56, 273, 50);
    uint64_t r10 = (uint64_t)((uint64_t)t8 << (int)n9);
    bool o11 = (uint64_t)(r10 >> (int)n9) != t8;
    if (o11) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 50);
    uint64_t t12 = r10;
    uint64_t t13 = (uint64_t)(t4 | t12);
    uint64_t t14 = (uintptr_t)((uint64_t)2ULL);
    void * t15 = gm_l0;
    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)t15 + t14 * 1));
    uint64_t t17 = (uint64_t)t16;
    unsigned __int128 n18 = (unsigned __int128)((uint64_t)16ULL);
    if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_src6unagr666v56, 273, 69);
    uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
    bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
    if (o20) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 69);
    uint64_t t21 = r19;
    uint64_t t22 = (uint64_t)(t13 | t21);
    uint64_t t23 = (uintptr_t)((uint64_t)3ULL);
    void * t24 = gm_l0;
    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)t24 + t23 * 1));
    uint64_t t26 = (uint64_t)t25;
    unsigned __int128 n27 = (unsigned __int128)((uint64_t)24ULL);
    if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_src6unagr666v56, 273, 89);
    uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
    bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
    if (o29) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 89);
    uint64_t t30 = r28;
    uint64_t t31 = (uint64_t)(t22 | t30);
    uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
    void * t33 = gm_l0;
    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)t33 + t32 * 1));
    uint64_t t35 = (uint64_t)t34;
    unsigned __int128 n36 = (unsigned __int128)((uint64_t)32ULL);
    if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_src6unagr666v56, 273, 109);
    uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
    bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
    if (o38) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 109);
    uint64_t t39 = r37;
    uint64_t t40 = (uint64_t)(t31 | t39);
    uint64_t t41 = (uintptr_t)((uint64_t)5ULL);
    void * t42 = gm_l0;
    uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)t42 + t41 * 1));
    uint64_t t44 = (uint64_t)t43;
    unsigned __int128 n45 = (unsigned __int128)((uint64_t)40ULL);
    if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_src6unagr666v56, 273, 129);
    uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
    bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
    if (o47) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 129);
    uint64_t t48 = r46;
    uint64_t t49 = (uint64_t)(t40 | t48);
    uint64_t t50 = (uintptr_t)((uint64_t)6ULL);
    void * t51 = gm_l0;
    uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)t51 + t50 * 1));
    uint64_t t53 = (uint64_t)t52;
    unsigned __int128 n54 = (unsigned __int128)((uint64_t)48ULL);
    if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_src6unagr666v56, 273, 149);
    uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
    bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
    if (o56) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 149);
    uint64_t t57 = r55;
    uint64_t t58 = (uint64_t)(t49 | t57);
    uint64_t t59 = (uintptr_t)((uint64_t)7ULL);
    void * t60 = gm_l0;
    uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)t60 + t59 * 1));
    uint64_t t62 = (uint64_t)t61;
    unsigned __int128 n63 = (unsigned __int128)((uint64_t)56ULL);
    if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_src6unagr666v56, 273, 169);
    uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
    bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
    if (o65) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 273, 169);
    uint64_t t66 = r64;
    uint64_t t67 = (uint64_t)(t58 | t66);
    F.ret = t67;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_ldn_94iknnm(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ldn_94iknnm F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint8_t t3 = gm_l1;
    uint8_t t4 = ((t3 == ((uint8_t)8ULL)));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    uint64_t t7 = gm_f_ld8_ecs41ho(t6);
    uint64_t t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    uint64_t t9;
    uint8_t t11 = gm_l1;
    uint8_t t12 = ((t11 == ((uint8_t)4ULL)));
    if (!t12) goto ie10;
    void * t14 = gm_l0;
    uint64_t t15 = gm_f_ld4_lj8nmpt(t14);
    uint64_t t16 = t15;
    t9 = t16;
    goto ix10;
ie10:;
    uint64_t t17;
    uint8_t t19 = gm_l1;
    uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
    if (!t20) goto ie18;
    void * t22 = gm_l0;
    uint64_t t23 = gm_f_ld1_adb1ani(t22);
    uint64_t t24 = t23;
    t17 = t24;
    goto ix18;
ie18:;
    void * t26 = gm_l0;
    uint64_t t27 = gm_f_ld2_gce8h7a(t26);
    uint64_t t28 = t27;
    t17 = t28;
ix18:;
    t9 = t17;
ix10:;
    t1 = t9;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_log2_acbegp6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_log2_acbegp6 F;
    uint32_t gm_l0; /* v */
    uint8_t gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint8_t)0ULL);
    }
    {
        for (;;) {
            uint8_t t3 = gm_l1;
            uint32_t t4 = (uint32_t)t3;
            unsigned __int128 n5 = (unsigned __int128)t4;
            if (n5 >= 32) gm_trap_shift((uint64_t)n5, "u32", gm_src6unagr666v56, 325, 12);
            uint32_t r6 = (uint32_t)((uint32_t)((uint32_t)1ULL) << (int)n5);
            bool o7 = (uint32_t)(r6 >> (int)n5) != ((uint32_t)1ULL);
            if (o7) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 325, 12);
            uint32_t t8 = r6;
            uint32_t t9 = gm_l0;
            uint8_t t10 = (t8 < t9);
            if (!t10) goto lb2;
            {
                uint8_t t13 = gm_l1;
                uint8_t r14; bool o15 = __builtin_add_overflow(t13, ((uint8_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 325, 36);
                uint8_t t16 = r14;
                gm_l1 = t16;
            }
lc2:;
        }
lb2:;
    }
    {
        uint8_t t17 = gm_l1;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_pick_kkl1a7m(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, gm_t9qkh0rsooc8d a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pick_kkl1a7m F;
    uint8_t gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* add */
    gm_t9qkh0rsooc8d gm_l2; /* sub */
    gm_t9qkh0rsooc8d gm_l3; /* mul */
    GM_STACK_CHECK_SHALLOW(&F, 18u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9qkh0rsooc8d t1;
    uint8_t t3 = gm_l0;
    uint8_t t4 = ((t3 == ((uint8_t)0ULL)));
    if (!t4) goto ie2;
    gm_t9qkh0rsooc8d t6 = gm_l1;
    gm_t9qkh0rsooc8d t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_t9qkh0rsooc8d t8;
    uint8_t t10 = gm_l0;
    uint8_t t11 = ((t10 == ((uint8_t)1ULL)));
    if (!t11) goto ie9;
    gm_t9qkh0rsooc8d t13 = gm_l2;
    gm_t9qkh0rsooc8d t14 = t13;
    t8 = t14;
    goto ix9;
ie9:;
    gm_t9qkh0rsooc8d t16 = gm_l3;
    gm_t9qkh0rsooc8d t17 = t16;
    t8 = t17;
ix9:;
    t1 = t8;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st1_aqrj474(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st1_aqrj474 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (uint8_t)t4;
        GM_ST(uint8_t, ((uint8_t *)t3 + t2 * 1), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st2_lkdmkcq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st2_lkdmkcq F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (uint8_t)t4;
        GM_ST(uint8_t, ((uint8_t *)t3 + t2 * 1), t5);
    }
    {
        uint64_t t6 = (uintptr_t)((uint64_t)1ULL);
        void * t7 = gm_l0;
        uint64_t t8 = gm_l1;
        unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_src6unagr666v56, 281, 13);
        uint64_t t10 = (uint64_t)(t8 >> (int)n9);
        uint8_t t11 = (uint8_t)t10;
        GM_ST(uint8_t, ((uint8_t *)t7 + t6 * 1), t11);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st4_38u38jn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st4_38u38jn F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (uint8_t)t4;
        GM_ST(uint8_t, ((uint8_t *)t3 + t2 * 1), t5);
    }
    {
        uint64_t t6 = (uintptr_t)((uint64_t)1ULL);
        void * t7 = gm_l0;
        uint64_t t8 = gm_l1;
        unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_src6unagr666v56, 287, 13);
        uint64_t t10 = (uint64_t)(t8 >> (int)n9);
        uint8_t t11 = (uint8_t)t10;
        GM_ST(uint8_t, ((uint8_t *)t7 + t6 * 1), t11);
    }
    {
        uint64_t t12 = (uintptr_t)((uint64_t)2ULL);
        void * t13 = gm_l0;
        uint64_t t14 = gm_l1;
        unsigned __int128 n15 = (unsigned __int128)((uint64_t)16ULL);
        if (n15 >= 64) gm_trap_shift((uint64_t)n15, "u64", gm_src6unagr666v56, 288, 13);
        uint64_t t16 = (uint64_t)(t14 >> (int)n15);
        uint8_t t17 = (uint8_t)t16;
        GM_ST(uint8_t, ((uint8_t *)t13 + t12 * 1), t17);
    }
    {
        uint64_t t18 = (uintptr_t)((uint64_t)3ULL);
        void * t19 = gm_l0;
        uint64_t t20 = gm_l1;
        unsigned __int128 n21 = (unsigned __int128)((uint64_t)24ULL);
        if (n21 >= 64) gm_trap_shift((uint64_t)n21, "u64", gm_src6unagr666v56, 289, 13);
        uint64_t t22 = (uint64_t)(t20 >> (int)n21);
        uint8_t t23 = (uint8_t)t22;
        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t23);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_st8_6aops52(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_st8_6aops52 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (uint8_t)t4;
        GM_ST(uint8_t, ((uint8_t *)t3 + t2 * 1), t5);
    }
    {
        uint64_t t6 = (uintptr_t)((uint64_t)1ULL);
        void * t7 = gm_l0;
        uint64_t t8 = gm_l1;
        unsigned __int128 n9 = (unsigned __int128)((uint64_t)8ULL);
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_src6unagr666v56, 295, 13);
        uint64_t t10 = (uint64_t)(t8 >> (int)n9);
        uint8_t t11 = (uint8_t)t10;
        GM_ST(uint8_t, ((uint8_t *)t7 + t6 * 1), t11);
    }
    {
        uint64_t t12 = (uintptr_t)((uint64_t)2ULL);
        void * t13 = gm_l0;
        uint64_t t14 = gm_l1;
        unsigned __int128 n15 = (unsigned __int128)((uint64_t)16ULL);
        if (n15 >= 64) gm_trap_shift((uint64_t)n15, "u64", gm_src6unagr666v56, 296, 13);
        uint64_t t16 = (uint64_t)(t14 >> (int)n15);
        uint8_t t17 = (uint8_t)t16;
        GM_ST(uint8_t, ((uint8_t *)t13 + t12 * 1), t17);
    }
    {
        uint64_t t18 = (uintptr_t)((uint64_t)3ULL);
        void * t19 = gm_l0;
        uint64_t t20 = gm_l1;
        unsigned __int128 n21 = (unsigned __int128)((uint64_t)24ULL);
        if (n21 >= 64) gm_trap_shift((uint64_t)n21, "u64", gm_src6unagr666v56, 297, 13);
        uint64_t t22 = (uint64_t)(t20 >> (int)n21);
        uint8_t t23 = (uint8_t)t22;
        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t23);
    }
    {
        uint64_t t24 = (uintptr_t)((uint64_t)4ULL);
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        unsigned __int128 n27 = (unsigned __int128)((uint64_t)32ULL);
        if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_src6unagr666v56, 298, 13);
        uint64_t t28 = (uint64_t)(t26 >> (int)n27);
        uint8_t t29 = (uint8_t)t28;
        GM_ST(uint8_t, ((uint8_t *)t25 + t24 * 1), t29);
    }
    {
        uint64_t t30 = (uintptr_t)((uint64_t)5ULL);
        void * t31 = gm_l0;
        uint64_t t32 = gm_l1;
        unsigned __int128 n33 = (unsigned __int128)((uint64_t)40ULL);
        if (n33 >= 64) gm_trap_shift((uint64_t)n33, "u64", gm_src6unagr666v56, 299, 13);
        uint64_t t34 = (uint64_t)(t32 >> (int)n33);
        uint8_t t35 = (uint8_t)t34;
        GM_ST(uint8_t, ((uint8_t *)t31 + t30 * 1), t35);
    }
    {
        uint64_t t36 = (uintptr_t)((uint64_t)6ULL);
        void * t37 = gm_l0;
        uint64_t t38 = gm_l1;
        unsigned __int128 n39 = (unsigned __int128)((uint64_t)48ULL);
        if (n39 >= 64) gm_trap_shift((uint64_t)n39, "u64", gm_src6unagr666v56, 300, 13);
        uint64_t t40 = (uint64_t)(t38 >> (int)n39);
        uint8_t t41 = (uint8_t)t40;
        GM_ST(uint8_t, ((uint8_t *)t37 + t36 * 1), t41);
    }
    {
        uint64_t t42 = (uintptr_t)((uint64_t)7ULL);
        void * t43 = gm_l0;
        uint64_t t44 = gm_l1;
        unsigned __int128 n45 = (unsigned __int128)((uint64_t)56ULL);
        if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_src6unagr666v56, 301, 13);
        uint64_t t46 = (uint64_t)(t44 >> (int)n45);
        uint8_t t47 = (uint8_t)t46;
        GM_ST(uint8_t, ((uint8_t *)t43 + t42 * 1), t47);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_stn_cbrtno6(void * a0, uint8_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stn_cbrtno6 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* size */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint8_t)8ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            uint64_t t7 = gm_l2;
            gm_f_st8_6aops52(t6, t7);
        }
        goto ix2;
ie2:;
        uint8_t t9 = gm_l1;
        uint8_t t10 = ((t9 == ((uint8_t)4ULL)));
        if (!t10) goto ie8;
        {
            void * t12 = gm_l0;
            uint64_t t13 = gm_l2;
            gm_f_st4_38u38jn(t12, t13);
        }
        goto ix8;
ie8:;
        uint8_t t15 = gm_l1;
        uint8_t t16 = ((t15 == ((uint8_t)1ULL)));
        if (!t16) goto ie14;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l2;
            gm_f_st1_aqrj474(t18, t19);
        }
        goto ix14;
ie14:;
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l2;
            gm_f_st2_lkdmkcq(t21, t22);
        }
ix14:;
ix8:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
    GM_STACK_CHECK_SHALLOW(&F, 561u);
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
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t28;
    }
    {
        uint64_t t31 = gm_l1;
        void * t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
        uint64_t r34; bool o35 = __builtin_sub_overflow(t31, t33, &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t36 = r34;
        gm_l7 = t36;
    }
    {
        uint64_t t37 = gm_l7;
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t41 = (uint64_t)(t37 >> (int)n40);
        gm_l8 = t41;
    }
    {
        void * t42 = gm_l6;
        void * t43 = t42;
        uint64_t t44 = gm_l8;
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = (void *)((uint8_t *)t43 + t46 * 24);
        gm_l9 = t47;
    }
    {
        uint64_t t48 = gm_l7;
        uint64_t t49 = gm_l8;
        void * t50 = gm_l5;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
        unsigned __int128 n52 = (unsigned __int128)t51;
        if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r53 = (uint64_t)((uint64_t)t49 << (int)n52);
        bool o54 = (uint64_t)(r53 >> (int)n52) != t49;
        if (o54) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t55 = r53;
        uint64_t r56; bool o57 = __builtin_sub_overflow(t48, t55, &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t58 = r56;
        gm_l10 = t58;
    }
    {
        uint8_t t60 = 0;
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        void * t63 = gm_l9;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 17));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) {
            uint64_t t66 = gm_l2;
            void * t67 = gm_l9;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
            uint8_t t69 = (t66 > t68);
            t62 = t69;
        } else t62 = 1;
        if (!t62) {
            uint64_t t70 = gm_l10;
            void * t71 = gm_l9;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
            uint64_t t73 = gm_l2;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t76 = r74;
            uint8_t t77 = (t70 > t76);
            t61 = t77;
        } else t61 = 1;
        if (!t61) {
            uint8_t t78 = 0;
            uint8_t t79 = gm_l3;
            if (t79) {
                void * t80 = gm_l9;
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 18));
                uint8_t t82 = (uint8_t)!t81;
                t78 = t82;
            } else t78 = 0;
            t60 = t78;
        } else t60 = 1;
        if (!t60) goto ie59;
        {
            uint8_t t84 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t85 = gm_l9;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = t86;
        uint64_t t88 = gm_l10;
        uint64_t t89 = gm_f_us_cuv01fp(t88);
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = (void *)((uint8_t *)t87 + t90 * 1);
        void * t92 = t91;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_vm_view_fo2var1(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vm_view_fo2var1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* size */
    uint64_t gm_l3; /* align */
    uint8_t gm_l4; /* write */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_l3;
        uint64_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 318, 16);
        uint64_t t7 = r5;
        uint64_t t8 = (uint64_t)(t3 & t7);
        uint8_t t9 = (!(t8 == ((uint64_t)0ULL)));
        if (!t9) goto ie2;
        {
            uint8_t t11 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 184);
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_l2;
        uint8_t t16 = gm_l4;
        void * t17 = gm_f_view_qalkbv1(t13, t14, t15, t16);
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_word_size_mu6ni08(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_size_mu6ni08 F;
    uint64_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    uint64_t t4 = gm_l0;
    uint8_t t5 = ((t4 == ((uint64_t)1ULL)));
    if (!t5) {
        uint64_t t6 = gm_l0;
        uint8_t t7 = ((t6 == ((uint64_t)2ULL)));
        t3 = t7;
    } else t3 = 1;
    if (!t3) {
        uint64_t t8 = gm_l0;
        uint8_t t9 = ((t8 == ((uint64_t)4ULL)));
        t2 = t9;
    } else t2 = 1;
    if (!t2) {
        uint64_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint64_t)8ULL)));
        t1 = t11;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc3_m9eakek(gm_t9qkh0rsooc8d a0, uint32_t a1, uint32_t a2, uint32_t a3);
extern __attribute__((aligned(4))) uint8_t gm_f_bc_align_6s4vol1(uint32_t a0);
extern __attribute__((aligned(4))) void * gm_f_bc_at_91tc4br(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_cmp_hcolbhq(gm_ts6oqppjh0ba3 a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint16_t a7) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_cmp_hcolbhq F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* lt */
    gm_t9qkh0rsooc8d gm_l2; /* le */
    uint8_t gm_l3; /* jump */
    uint32_t gm_l4; /* a */
    uint32_t gm_l5; /* b */
    uint32_t gm_l6; /* r */
    uint16_t gm_l7; /* w */
    uint8_t gm_l8; /* swap */
    gm_t9qkh0rsooc8d gm_l9; /* which */
    uint32_t gm_l10; /* x */
    uint32_t gm_l11; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 179u);
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
    gm_l7 = a7;
    {
        uint8_t t2 = 0;
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)12ULL)));
        if (!t3) {
            uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)13ULL)));
            t2 = t4;
        } else t2 = 1;
        gm_l8 = t2;
    }
    {
        gm_t9qkh0rsooc8d t5;
        uint8_t t7 = 0;
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)10ULL)));
        if (!t8) {
            uint8_t t9 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)12ULL)));
            t7 = t9;
        } else t7 = 1;
        if (!t7) goto ie6;
        gm_t9qkh0rsooc8d t11 = gm_l1;
        gm_t9qkh0rsooc8d t12 = t11;
        t5 = t12;
        goto ix6;
ie6:;
        gm_t9qkh0rsooc8d t14 = gm_l2;
        gm_t9qkh0rsooc8d t15 = t14;
        t5 = t15;
ix6:;
        gm_l9 = t5;
    }
    {
        uint32_t t16;
        uint8_t t18 = gm_l8;
        if (!t18) goto ie17;
        uint32_t t20 = gm_l5;
        uint32_t t21 = t20;
        t16 = t21;
        goto ix17;
ie17:;
        uint32_t t23 = gm_l4;
        uint32_t t24 = t23;
        t16 = t24;
ix17:;
        gm_l10 = t16;
    }
    {
        uint32_t t25;
        uint8_t t27 = gm_l8;
        if (!t27) goto ie26;
        uint32_t t29 = gm_l4;
        uint32_t t30 = t29;
        t25 = t30;
        goto ix26;
ie26:;
        uint32_t t32 = gm_l5;
        uint32_t t33 = t32;
        t25 = t33;
ix26:;
        gm_l11 = t25;
    }
    {
        uint8_t t35 = gm_l3;
        if (!t35) goto ie34;
        {
            gm_t9qkh0rsooc8d t37 = gm_l9;
            uint16_t t38 = gm_l7;
            uint32_t t39 = gm_l10;
            uint32_t t40 = gm_l11;
            uint32_t t41 = gm_l6;
            gm_tb1f35qilrmun t42;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t42 + 0), t37);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t42 + 2), t38);
            GM_ST(uint32_t, ((uint8_t *)&t42 + 4), t39);
            GM_ST(uint32_t, ((uint8_t *)&t42 + 8), t40);
            GM_ST(uint32_t, ((uint8_t *)&t42 + 12), t41);
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        gm_t9qkh0rsooc8d t44 = gm_l9;
        uint16_t t45 = gm_l7;
        uint32_t t46 = gm_l6;
        uint32_t t47 = gm_l10;
        uint32_t t48 = gm_l11;
        gm_tb1f35qilrmun t49;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t49 + 0), t44);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 1), ((uint8_t)0ULL));
        GM_ST(uint16_t, ((uint8_t *)&t49 + 2), t45);
        GM_ST(uint32_t, ((uint8_t *)&t49 + 4), t46);
        GM_ST(uint32_t, ((uint8_t *)&t49 + 8), t47);
        GM_ST(uint32_t, ((uint8_t *)&t49 + 12), t48);
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_free_v10vouf(void);
__attribute__((aligned(4))) gm_tb1f35qilrmun gm_f_bc_of_ga25123(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bc_of_ga25123 F;
    void * gm_l0; /* i */
    uint8_t gm_l1; /* words */
    uint32_t gm_l2; /* r */
    uint64_t gm_l3; /* v */
    uint32_t gm_l4; /* r */
    uint32_t gm_l5; /* x */
    gm_ti4ei62u4i1d9 gm_l6; /* x */
    gm_t9qkh0rsooc8d gm_l7; /* op */
    gm_te4o1hd8337m3 gm_l8; /* x */
    gm_t9qkh0rsooc8d gm_l9; /* op */
    gm_tfd1kqjeh4les gm_l10; /* x */
    gm_t9qkh0rsooc8d gm_l11; /* op */
    uint8_t gm_l12; /* op */
    uint32_t gm_l13; /* r */
    uint32_t gm_l14; /* a */
    uint32_t gm_l15; /* b */
    uint32_t gm_l16; /* r */
    uint32_t gm_l17; /* x */
    uint32_t gm_l18; /* r */
    uint64_t gm_l19; /* k */
    uint32_t gm_l20; /* r */
    uint32_t gm_l21; /* x */
    uint32_t gm_l22; /* r */
    gm_tnkql1rjn1vbj gm_l24; /* x */
    uint32_t gm_l25; /* bits */
    gm_tnkql1rjn1vbj gm_l26; /* x */
    uint32_t gm_l27; /* bits */
    gm_tufm7evi61pnh gm_l28; /* x */
    gm_tufm7evi61pnh gm_l29; /* x */
    uint32_t gm_l30; /* to */
    uint32_t gm_l31; /* r */
    uint32_t gm_l32; /* to */
    uint32_t gm_l33; /* r */
    uint32_t gm_l34; /* to */
    uint32_t gm_l35; /* to */
    uint32_t gm_l36; /* r */
    uint32_t gm_l37; /* at */
    uint32_t gm_l38; /* size */
    uint32_t gm_l39; /* r */
    uint32_t gm_l40; /* at */
    uint32_t gm_l41; /* size */
    uint32_t gm_l42; /* at */
    uint32_t gm_l43; /* size */
    uint32_t gm_l44; /* v */
    uint32_t gm_l45; /* at */
    uint32_t gm_l46; /* size */
    uint32_t gm_l47; /* r */
    uint32_t gm_l48; /* r */
    uint32_t gm_l49; /* at */
    uint32_t gm_l50; /* d */
    uint32_t gm_l51; /* size */
    uint32_t gm_l52; /* at */
    uint32_t gm_l53; /* d */
    uint32_t gm_l54; /* size */
    uint32_t gm_l55; /* v */
    uint32_t gm_l56; /* at */
    uint32_t gm_l57; /* size */
    uint8_t gm_l58; /* nonzero */
    uint32_t gm_l59; /* to */
    uint32_t gm_l60; /* at */
    uint32_t gm_l61; /* size */
    uint64_t gm_l62; /* v */
    uint32_t gm_l63; /* to */
    gm_t04llc5uen0qo gm_l64; /* x */
    gm_t04llc5uen0qo gm_l65; /* x */
    uint32_t gm_l68; /* ri */
    uint64_t gm_l69; /* len */
    uint64_t gm_l70; /* esz */
    gm_t3a21dm0j52qn gm_l71; /* x */
    gm_tu97pd4fjd5oe gm_l72; /* x */
    gm_t4h1llua034m2 gm_l73; /* x */
    uint8_t gm_l78; /* flags */
    uint32_t gm_l79; /* r */
    uint64_t gm_l80; /* align */
    gm_t04llc5uen0qo gm_l81; /* x */
    gm_t04llc5uen0qo gm_l82; /* x */
    uint32_t gm_l83; /* d */
    uint32_t gm_l84; /* src */
    uint32_t gm_l85; /* size */
    uint32_t gm_l86; /* at */
    uint32_t gm_l87; /* size */
    uint32_t gm_l88; /* r */
    uint32_t gm_l89; /* r */
    gm_tn3qmqcq7vsk9 gm_l90; /* x */
    gm_tn3qmqcq7vsk9 gm_l91; /* x */
    uint32_t gm_l92; /* r */
    uint32_t gm_l93; /* at */
    uint32_t gm_l94; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 5263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        switch (t59) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma25; case ((uint8_t)5ULL): goto ma51; case ((uint8_t)6ULL): goto ma27; case ((uint8_t)7ULL): goto ma28; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma39; case ((uint8_t)12ULL): goto ma40; case ((uint8_t)13ULL): goto ma52; case ((uint8_t)14ULL): goto ma57; case ((uint8_t)15ULL): goto ma57; case ((uint8_t)16ULL): goto ma57; case ((uint8_t)17ULL): goto ma31; case ((uint8_t)18ULL): goto ma32; case ((uint8_t)19ULL): goto ma36; case ((uint8_t)20ULL): goto ma37; case ((uint8_t)21ULL): goto ma33; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma38; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma41; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma16; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma15; case ((uint8_t)31ULL): goto ma17; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma57; case ((uint8_t)35ULL): goto ma14; case ((uint8_t)36ULL): goto ma57; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma19; case ((uint8_t)39ULL): goto ma20; case ((uint8_t)40ULL): goto ma21; case ((uint8_t)41ULL): goto ma44; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma43; case ((uint8_t)44ULL): goto ma57; case ((uint8_t)45ULL): goto ma57; case ((uint8_t)46ULL): goto ma57; case ((uint8_t)47ULL): goto ma50; case ((uint8_t)48ULL): goto ma50; case ((uint8_t)49ULL): goto ma57; case ((uint8_t)50ULL): goto ma50; case ((uint8_t)51ULL): goto ma56; case ((uint8_t)52ULL): goto ma46; case ((uint8_t)53ULL): goto ma24; case ((uint8_t)54ULL): goto ma26; case ((uint8_t)55ULL): goto ma29; case ((uint8_t)56ULL): goto ma57; case ((uint8_t)57ULL): goto ma47; case ((uint8_t)58ULL): goto ma42; case ((uint8_t)59ULL): goto ma56; case ((uint8_t)60ULL): goto ma30; case ((uint8_t)61ULL): goto ma56; case ((uint8_t)62ULL): goto ma56; case ((uint8_t)63ULL): goto ma56; case ((uint8_t)64ULL): goto ma56; case ((uint8_t)65ULL): goto ma5; case ((uint8_t)66ULL): goto ma56; case ((uint8_t)67ULL): goto ma56; case ((uint8_t)68ULL): goto ma56; case ((uint8_t)69ULL): goto ma56; case ((uint8_t)70ULL): goto ma56; case ((uint8_t)71ULL): goto ma57; case ((uint8_t)72ULL): goto ma53; case ((uint8_t)73ULL): goto ma56; case ((uint8_t)74ULL): goto ma55; case ((uint8_t)75ULL): goto ma54; case ((uint8_t)76ULL): goto ma48; case ((uint8_t)77ULL): goto ma49; case ((uint8_t)78ULL): goto ma22; case ((uint8_t)79ULL): goto ma57; case ((uint8_t)80ULL): goto ma56; case ((uint8_t)81ULL): goto ma56; case ((uint8_t)82ULL): goto ma56; case ((uint8_t)83ULL): goto ma56; case ((uint8_t)84ULL): goto ma56; default: goto ma57; }
ma4:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t61 != ((uint8_t)0ULL)) goto mn60;
        uint32_t t62 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l2 = t62;
        uint64_t t63 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t63;
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)3ULL));
        uint32_t t65 = gm_l2;
        uint64_t t66 = gm_l3;
        uint64_t t67 = (uint64_t)(t66 & ((uint64_t)4294967295ULL));
        uint32_t t68 = (uint32_t)t67;
        uint64_t t69 = gm_l3;
        unsigned __int128 n70 = (unsigned __int128)((uint64_t)32ULL);
        if (n70 >= 64) gm_trap_shift((uint64_t)n70, "u64", gm_src6unagr666v56, 362, 74);
        uint64_t t71 = (uint64_t)(t69 >> (int)n70);
        uint32_t t72 = (uint32_t)t71;
        gm_tb1f35qilrmun t73 = gm_f_bc3_m9eakek(t64, t65, t68, t72);
        uint8_t t74 = 0;
        F.ret = t73;
        goto gm_fret;
        goto me3;
mn60:;
ma5:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t76 != ((uint8_t)65ULL)) goto mn75;
        uint32_t t77 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l4 = t77;
        uint32_t t78 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l5 = t78;
        gm_t9qkh0rsooc8d t79;
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)2ULL));
        uint32_t t80 = gm_l4;
        uint32_t t81 = gm_l5;
        gm_tb1f35qilrmun t82 = gm_f_bc3_m9eakek(t79, t80, t81, ((uint32_t)0ULL));
        uint8_t t83 = 0;
        F.ret = t82;
        goto gm_fret;
        goto me3;
mn75:;
ma6:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t85 != ((uint8_t)26ULL)) goto mn84;
        gm_ti4ei62u4i1d9 t86 = GM_LD(gm_ti4ei62u4i1d9, (((uint8_t *)t2) + 8));
        gm_l6 = t86;
        {
            gm_t9qkh0rsooc8d t88;
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 2));
            if (!t90) goto ie89;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            gm_t9qkh0rsooc8d t93;
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)10ULL));
            gm_t9qkh0rsooc8d t94;
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)11ULL));
            gm_t9qkh0rsooc8d t95;
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)12ULL));
            gm_t9qkh0rsooc8d t96 = gm_f_pick_kkl1a7m(t92, t93, t94, t95);
            gm_t9qkh0rsooc8d t97 = t96;
            t88 = t97;
            goto ix89;
ie89:;
            gm_t9qkh0rsooc8d t98;
            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 1));
            if (!t100) goto ie99;
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            gm_t9qkh0rsooc8d t103;
            GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)4ULL));
            gm_t9qkh0rsooc8d t104;
            GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)5ULL));
            gm_t9qkh0rsooc8d t105;
            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)6ULL));
            gm_t9qkh0rsooc8d t106 = gm_f_pick_kkl1a7m(t102, t103, t104, t105);
            gm_t9qkh0rsooc8d t107 = t106;
            t98 = t107;
            goto ix99;
ie99:;
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            gm_t9qkh0rsooc8d t110;
            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)7ULL));
            gm_t9qkh0rsooc8d t111;
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)8ULL));
            gm_t9qkh0rsooc8d t112;
            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)9ULL));
            gm_t9qkh0rsooc8d t113 = gm_f_pick_kkl1a7m(t109, t110, t111, t112);
            gm_t9qkh0rsooc8d t114 = t113;
            t98 = t114;
ix99:;
            t88 = t98;
ix89:;
            gm_l7 = t88;
        }
        {
            gm_t9qkh0rsooc8d t115 = gm_l7;
            uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 12));
            gm_tb1f35qilrmun t119 = gm_f_bc3_m9eakek(t115, t116, t117, t118);
            uint8_t t120 = 0;
            F.ret = t119;
            goto gm_fret;
        }
        goto me3;
mn84:;
ma7:;
        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t122 != ((uint8_t)27ULL)) goto mn121;
        gm_te4o1hd8337m3 t123 = GM_LD(gm_te4o1hd8337m3, (((uint8_t *)t2) + 8));
        gm_l8 = t123;
        {
            gm_t9qkh0rsooc8d t125;
            uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 2));
            if (!t127) goto ie126;
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            gm_t9qkh0rsooc8d t130;
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)19ULL));
            gm_t9qkh0rsooc8d t131;
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)20ULL));
            gm_t9qkh0rsooc8d t132;
            GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)21ULL));
            gm_t9qkh0rsooc8d t133 = gm_f_pick_kkl1a7m(t129, t130, t131, t132);
            gm_t9qkh0rsooc8d t134 = t133;
            t125 = t134;
            goto ix126;
ie126:;
            gm_t9qkh0rsooc8d t135;
            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 1));
            if (!t137) goto ie136;
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            gm_t9qkh0rsooc8d t140;
            GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)13ULL));
            gm_t9qkh0rsooc8d t141;
            GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)14ULL));
            gm_t9qkh0rsooc8d t142;
            GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)15ULL));
            gm_t9qkh0rsooc8d t143 = gm_f_pick_kkl1a7m(t139, t140, t141, t142);
            gm_t9qkh0rsooc8d t144 = t143;
            t135 = t144;
            goto ix136;
ie136:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            gm_t9qkh0rsooc8d t147;
            GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)16ULL));
            gm_t9qkh0rsooc8d t148;
            GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)17ULL));
            gm_t9qkh0rsooc8d t149;
            GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)18ULL));
            gm_t9qkh0rsooc8d t150 = gm_f_pick_kkl1a7m(t146, t147, t148, t149);
            gm_t9qkh0rsooc8d t151 = t150;
            t135 = t151;
ix136:;
            t125 = t135;
ix126:;
            gm_l9 = t125;
        }
        {
            gm_t9qkh0rsooc8d t152 = gm_l9;
            uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
            uint16_t t154 = (uint16_t)t153;
            uint32_t t155 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 8));
            uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 12));
            uint32_t t157 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 16));
            gm_tb1f35qilrmun t158;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t158 + 0), t152);
            GM_ST(uint8_t, ((uint8_t *)&t158 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t158 + 2), t154);
            GM_ST(uint32_t, ((uint8_t *)&t158 + 4), t155);
            GM_ST(uint32_t, ((uint8_t *)&t158 + 8), t156);
            GM_ST(uint32_t, ((uint8_t *)&t158 + 12), t157);
            uint8_t t159 = 0;
            F.ret = t158;
            goto gm_fret;
        }
        goto me3;
mn121:;
ma8:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t161 != ((uint8_t)37ULL)) goto mn160;
        gm_tfd1kqjeh4les t162 = GM_LD(gm_tfd1kqjeh4les, (((uint8_t *)t2) + 8));
        gm_l10 = t162;
        {
            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 2));
            uint8_t t166 = (uint8_t)!t165;
            if (!t166) goto ie164;
            {
                gm_tb1f35qilrmun t168 = gm_f_bc_free_v10vouf();
                uint8_t t169 = 0;
                F.ret = t168;
                goto gm_fret;
            }
            goto ix164;
ie164:;
ix164:;
        }
        {
            gm_t9qkh0rsooc8d t170;
            uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)&gm_l10) + 0) + 0));
            switch (t177) { case ((uint8_t)0ULL): goto ma172; case ((uint8_t)1ULL): goto ma173; case ((uint8_t)2ULL): goto ma174; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; case ((uint8_t)7ULL): goto ma175; case ((uint8_t)8ULL): goto ma175; case ((uint8_t)9ULL): goto ma175; case ((uint8_t)10ULL): goto ma175; case ((uint8_t)11ULL): goto ma175; case ((uint8_t)12ULL): goto ma175; case ((uint8_t)13ULL): goto ma175; default: goto ma175; }
ma172:;
            uint8_t t179 = GM_LD(uint8_t, ((((uint8_t *)&gm_l10) + 0) + 0));
            if (t179 != ((uint8_t)0ULL)) goto mn178;
            gm_t9qkh0rsooc8d t180;
            GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)22ULL));
            t170 = t180;
            goto me171;
mn178:;
ma173:;
            uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)&gm_l10) + 0) + 0));
            if (t182 != ((uint8_t)1ULL)) goto mn181;
            gm_t9qkh0rsooc8d t183;
            GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)23ULL));
            t170 = t183;
            goto me171;
mn181:;
ma174:;
            uint8_t t185 = GM_LD(uint8_t, ((((uint8_t *)&gm_l10) + 0) + 0));
            if (t185 != ((uint8_t)2ULL)) goto mn184;
            gm_t9qkh0rsooc8d t186;
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)24ULL));
            t170 = t186;
            goto me171;
mn184:;
ma175:;
            gm_t9qkh0rsooc8d t188;
            GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)25ULL));
            t170 = t188;
            goto me171;
mn187:;
ma176:;
            gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 374, 24);
me171:;
            gm_l11 = t170;
        }
        {
            gm_t9qkh0rsooc8d t189 = gm_l11;
            uint32_t t190 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
            uint32_t t191 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            uint32_t t192 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 12));
            gm_tb1f35qilrmun t193 = gm_f_bc3_m9eakek(t189, t190, t191, t192);
            uint8_t t194 = 0;
            F.ret = t193;
            goto gm_fret;
        }
        goto me3;
mn160:;
ma9:;
        uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t196 != ((uint8_t)10ULL)) goto mn195;
        uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l12 = t197;
        uint32_t t198 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l13 = t198;
        uint32_t t199 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t199;
        uint32_t t200 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 12));
        gm_l15 = t200;
        gm_t9qkh0rsooc8d t201;
        uint8_t t203 = gm_l12;
        uint8_t t204 = ((t203 == ((uint8_t)0ULL)));
        if (!t204) goto ie202;
        gm_t9qkh0rsooc8d t206;
        GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)26ULL));
        gm_t9qkh0rsooc8d t207 = t206;
        t201 = t207;
        goto ix202;
ie202:;
        gm_t9qkh0rsooc8d t208;
        uint8_t t210 = gm_l12;
        uint8_t t211 = ((t210 == ((uint8_t)1ULL)));
        if (!t211) goto ie209;
        gm_t9qkh0rsooc8d t213;
        GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)27ULL));
        gm_t9qkh0rsooc8d t214 = t213;
        t208 = t214;
        goto ix209;
ie209:;
        gm_t9qkh0rsooc8d t216;
        GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)28ULL));
        gm_t9qkh0rsooc8d t217 = t216;
        t208 = t217;
ix209:;
        t201 = t208;
ix202:;
        uint32_t t218 = gm_l13;
        uint32_t t219 = gm_l14;
        uint32_t t220 = gm_l15;
        gm_tb1f35qilrmun t221 = gm_f_bc3_m9eakek(t201, t218, t219, t220);
        uint8_t t222 = 0;
        F.ret = t221;
        goto gm_fret;
        goto me3;
mn195:;
ma10:;
        uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t224 != ((uint8_t)9ULL)) goto mn223;
        uint32_t t225 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l16 = t225;
        uint32_t t226 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l17 = t226;
        gm_t9qkh0rsooc8d t227;
        GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)29ULL));
        uint32_t t228 = gm_l16;
        uint32_t t229 = gm_l17;
        gm_tb1f35qilrmun t230 = gm_f_bc3_m9eakek(t227, t228, t229, ((uint32_t)0ULL));
        uint8_t t231 = 0;
        F.ret = t230;
        goto gm_fret;
        goto me3;
mn223:;
ma11:;
        uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t233 != ((uint8_t)24ULL)) goto mn232;
        uint32_t t234 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l18 = t234;
        uint64_t t235 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l19 = t235;
        gm_t9qkh0rsooc8d t236;
        GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)30ULL));
        uint32_t t237 = gm_l18;
        uint64_t t238 = gm_l19;
        uint64_t t239 = (uint64_t)(t238 & ((uint64_t)4294967295ULL));
        uint32_t t240 = (uint32_t)t239;
        uint64_t t241 = gm_l19;
        unsigned __int128 n242 = (unsigned __int128)((uint64_t)32ULL);
        if (n242 >= 64) gm_trap_shift((uint64_t)n242, "u64", gm_src6unagr666v56, 384, 76);
        uint64_t t243 = (uint64_t)(t241 >> (int)n242);
        uint32_t t244 = (uint32_t)t243;
        gm_tb1f35qilrmun t245 = gm_f_bc3_m9eakek(t236, t237, t240, t244);
        uint8_t t246 = 0;
        F.ret = t245;
        goto gm_fret;
        goto me3;
mn232:;
ma12:;
        uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t248 != ((uint8_t)32ULL)) goto mn247;
        uint32_t t249 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l20 = t249;
        uint32_t t250 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l21 = t250;
        gm_t9qkh0rsooc8d t251;
        GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)31ULL));
        uint32_t t252 = gm_l20;
        uint32_t t253 = gm_l21;
        gm_tb1f35qilrmun t254 = gm_f_bc3_m9eakek(t251, t252, t253, ((uint32_t)0ULL));
        uint8_t t255 = 0;
        F.ret = t254;
        goto gm_fret;
        goto me3;
mn247:;
ma13:;
        uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t257 != ((uint8_t)33ULL)) goto mn256;
        uint32_t t258 = GM_LD(uint32_t, (((uint8_t *)t2) + 8));
        gm_l22 = t258;
        gm_t9qkh0rsooc8d t259;
        GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)32ULL));
        uint32_t t260 = gm_l22;
        gm_tb1f35qilrmun t261 = gm_f_bc3_m9eakek(t259, t260, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t262 = 0;
        F.ret = t261;
        goto gm_fret;
        goto me3;
mn256:;
ma14:;
        uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t264 != ((uint8_t)35ULL)) goto mn263;
        gm_t78m5u6j4a3pq t265 = GM_LD(gm_t78m5u6j4a3pq, (((uint8_t *)t2) + 8));
        F.l23 = t265;
        {
            uint8_t t268 = ((GM_LD(uint8_t, (((uint8_t *)&F.l23) + 0) + 0) == ((uint8_t)6ULL)));
            if (!t268) goto ie267;
            {
                gm_tb1f35qilrmun t270 = gm_f_bc_free_v10vouf();
                uint8_t t271 = 0;
                F.ret = t270;
                goto gm_fret;
            }
            goto ix267;
ie267:;
ix267:;
        }
        {
            uint8_t t273 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 12) + 0));
            if (!t273) goto ie272;
            {
                gm_t9qkh0rsooc8d t275;
                GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)34ULL));
                uint32_t t276 = GM_LD(uint32_t, ((((uint8_t *)&F.l23) + 12) + 4));
                uint8_t t277 = (uint8_t)t276;
                uint32_t t278 = GM_LD(uint32_t, ((((uint8_t *)&F.l23) + 20) + 4));
                uint16_t t279 = (uint16_t)t278;
                uint32_t t280 = GM_LD(uint32_t, (((uint8_t *)&F.l23) + 28));
                uint32_t t281 = GM_LD(uint32_t, (((uint8_t *)&F.l23) + 32));
                gm_tb1f35qilrmun t282;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t282 + 0), t275);
                GM_ST(uint8_t, ((uint8_t *)&t282 + 1), t277);
                GM_ST(uint16_t, ((uint8_t *)&t282 + 2), t279);
                GM_ST(uint32_t, ((uint8_t *)&t282 + 4), t280);
                GM_ST(uint32_t, ((uint8_t *)&t282 + 8), t281);
                GM_ST(uint32_t, ((uint8_t *)&t282 + 12), ((uint32_t)0ULL));
                uint8_t t283 = 0;
                F.ret = t282;
                goto gm_fret;
            }
            goto ix272;
ie272:;
ix272:;
        }
        {
            gm_t9qkh0rsooc8d t284;
            GM_ST(uint8_t, ((uint8_t *)&t284 + 0), ((uint8_t)33ULL));
            uint32_t t285 = GM_LD(uint32_t, ((((uint8_t *)&F.l23) + 20) + 4));
            uint16_t t286 = (uint16_t)t285;
            uint32_t t287 = GM_LD(uint32_t, (((uint8_t *)&F.l23) + 28));
            uint32_t t288 = GM_LD(uint32_t, (((uint8_t *)&F.l23) + 32));
            gm_tb1f35qilrmun t289;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t289 + 0), t284);
            GM_ST(uint8_t, ((uint8_t *)&t289 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t289 + 2), t286);
            GM_ST(uint32_t, ((uint8_t *)&t289 + 4), t287);
            GM_ST(uint32_t, ((uint8_t *)&t289 + 8), t288);
            GM_ST(uint32_t, ((uint8_t *)&t289 + 12), ((uint32_t)0ULL));
            uint8_t t290 = 0;
            F.ret = t289;
            goto gm_fret;
        }
        goto me3;
mn263:;
ma15:;
        uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t292 != ((uint8_t)30ULL)) goto mn291;
        gm_tnkql1rjn1vbj t293 = GM_LD(gm_tnkql1rjn1vbj, (((uint8_t *)t2) + 8));
        gm_l24 = t293;
        uint8_t t300 = GM_LD(uint8_t, ((((uint8_t *)&gm_l24) + 4) + 0));
        switch (t300) { case ((uint8_t)0ULL): goto ma295; case ((uint8_t)1ULL): goto ma296; case ((uint8_t)2ULL): goto ma298; case ((uint8_t)3ULL): goto ma297; case ((uint8_t)4ULL): goto ma298; default: goto ma298; }
ma295:;
        uint8_t t302 = GM_LD(uint8_t, ((((uint8_t *)&gm_l24) + 4) + 0));
        if (t302 != ((uint8_t)0ULL)) goto mn301;
        gm_ts6oqppjh0ba3 t303 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l24) + 0));
        gm_t9qkh0rsooc8d t304;
        GM_ST(uint8_t, ((uint8_t *)&t304 + 0), ((uint8_t)35ULL));
        gm_t9qkh0rsooc8d t305;
        GM_ST(uint8_t, ((uint8_t *)&t305 + 0), ((uint8_t)36ULL));
        uint32_t t306 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 16));
        uint32_t t307 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 20));
        uint32_t t308 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 12));
        gm_tb1f35qilrmun t309 = gm_f_bc_cmp_hcolbhq(t303, t304, t305, ((uint8_t)0), t306, t307, t308, ((uint16_t)0ULL));
        uint8_t t310 = 0;
        F.ret = t309;
        goto gm_fret;
        goto me294;
mn301:;
ma296:;
        uint8_t t312 = GM_LD(uint8_t, ((((uint8_t *)&gm_l24) + 4) + 0));
        if (t312 != ((uint8_t)1ULL)) goto mn311;
        uint32_t t313 = GM_LD(uint32_t, ((((uint8_t *)&gm_l24) + 4) + 4));
        gm_l25 = t313;
        {
            uint32_t t316 = gm_l25;
            uint8_t t317 = ((t316 == ((uint32_t)64ULL)));
            if (!t317) goto ie315;
            {
                gm_ts6oqppjh0ba3 t319 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l24) + 0));
                gm_t9qkh0rsooc8d t320;
                GM_ST(uint8_t, ((uint8_t *)&t320 + 0), ((uint8_t)37ULL));
                gm_t9qkh0rsooc8d t321;
                GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)38ULL));
                uint32_t t322 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 16));
                uint32_t t323 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 20));
                uint32_t t324 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 12));
                gm_tb1f35qilrmun t325 = gm_f_bc_cmp_hcolbhq(t319, t320, t321, ((uint8_t)0), t322, t323, t324, ((uint16_t)0ULL));
                uint8_t t326 = 0;
                F.ret = t325;
                goto gm_fret;
            }
            goto ix315;
ie315:;
ix315:;
        }
        {
            gm_ts6oqppjh0ba3 t327 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l24) + 0));
            gm_t9qkh0rsooc8d t328;
            GM_ST(uint8_t, ((uint8_t *)&t328 + 0), ((uint8_t)39ULL));
            gm_t9qkh0rsooc8d t329;
            GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)40ULL));
            uint32_t t330 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 16));
            uint32_t t331 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 20));
            uint32_t t332 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 12));
            uint32_t t333 = gm_l25;
            uint16_t t334 = (uint16_t)t333;
            gm_tb1f35qilrmun t335 = gm_f_bc_cmp_hcolbhq(t327, t328, t329, ((uint8_t)0), t330, t331, t332, t334);
            uint8_t t336 = 0;
            F.ret = t335;
            goto gm_fret;
        }
        goto me294;
mn311:;
ma297:;
        uint8_t t338 = GM_LD(uint8_t, ((((uint8_t *)&gm_l24) + 4) + 0));
        if (t338 != ((uint8_t)3ULL)) goto mn337;
        gm_ts6oqppjh0ba3 t339 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l24) + 0));
        gm_t9qkh0rsooc8d t340;
        GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)41ULL));
        gm_t9qkh0rsooc8d t341;
        GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)42ULL));
        uint32_t t342 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 16));
        uint32_t t343 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 20));
        uint32_t t344 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 12));
        gm_tb1f35qilrmun t345 = gm_f_bc_cmp_hcolbhq(t339, t340, t341, ((uint8_t)0), t342, t343, t344, ((uint16_t)0ULL));
        uint8_t t346 = 0;
        F.ret = t345;
        goto gm_fret;
        goto me294;
mn337:;
ma298:;
        gm_tb1f35qilrmun t348 = gm_f_bc_free_v10vouf();
        uint8_t t349 = 0;
        F.ret = t348;
        goto gm_fret;
        goto me294;
mn347:;
ma299:;
        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 392, 20);
me294:;
        goto me3;
mn291:;
ma16:;
        uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t351 != ((uint8_t)28ULL)) goto mn350;
        gm_tnkql1rjn1vbj t352 = GM_LD(gm_tnkql1rjn1vbj, (((uint8_t *)t2) + 8));
        gm_l26 = t352;
        uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)&gm_l26) + 4) + 0));
        switch (t359) { case ((uint8_t)0ULL): goto ma354; case ((uint8_t)1ULL): goto ma355; case ((uint8_t)2ULL): goto ma357; case ((uint8_t)3ULL): goto ma356; case ((uint8_t)4ULL): goto ma357; default: goto ma357; }
ma354:;
        uint8_t t361 = GM_LD(uint8_t, ((((uint8_t *)&gm_l26) + 4) + 0));
        if (t361 != ((uint8_t)0ULL)) goto mn360;
        gm_ts6oqppjh0ba3 t362 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l26) + 0));
        gm_t9qkh0rsooc8d t363;
        GM_ST(uint8_t, ((uint8_t *)&t363 + 0), ((uint8_t)45ULL));
        gm_t9qkh0rsooc8d t364;
        GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)46ULL));
        uint32_t t365 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 12));
        uint32_t t366 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 16));
        uint32_t t367 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 20));
        gm_tb1f35qilrmun t368 = gm_f_bc_cmp_hcolbhq(t362, t363, t364, ((uint8_t)1), t365, t366, t367, ((uint16_t)0ULL));
        uint8_t t369 = 0;
        F.ret = t368;
        goto gm_fret;
        goto me353;
mn360:;
ma355:;
        uint8_t t371 = GM_LD(uint8_t, ((((uint8_t *)&gm_l26) + 4) + 0));
        if (t371 != ((uint8_t)1ULL)) goto mn370;
        uint32_t t372 = GM_LD(uint32_t, ((((uint8_t *)&gm_l26) + 4) + 4));
        gm_l27 = t372;
        {
            uint32_t t375 = gm_l27;
            uint8_t t376 = ((t375 == ((uint32_t)64ULL)));
            if (!t376) goto ie374;
            {
                gm_ts6oqppjh0ba3 t378 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l26) + 0));
                gm_t9qkh0rsooc8d t379;
                GM_ST(uint8_t, ((uint8_t *)&t379 + 0), ((uint8_t)47ULL));
                gm_t9qkh0rsooc8d t380;
                GM_ST(uint8_t, ((uint8_t *)&t380 + 0), ((uint8_t)48ULL));
                uint32_t t381 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 12));
                uint32_t t382 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 16));
                uint32_t t383 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 20));
                gm_tb1f35qilrmun t384 = gm_f_bc_cmp_hcolbhq(t378, t379, t380, ((uint8_t)1), t381, t382, t383, ((uint16_t)0ULL));
                uint8_t t385 = 0;
                F.ret = t384;
                goto gm_fret;
            }
            goto ix374;
ie374:;
ix374:;
        }
        {
            gm_ts6oqppjh0ba3 t386 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l26) + 0));
            gm_t9qkh0rsooc8d t387;
            GM_ST(uint8_t, ((uint8_t *)&t387 + 0), ((uint8_t)49ULL));
            gm_t9qkh0rsooc8d t388;
            GM_ST(uint8_t, ((uint8_t *)&t388 + 0), ((uint8_t)50ULL));
            uint32_t t389 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 12));
            uint32_t t390 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 16));
            uint32_t t391 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 20));
            uint32_t t392 = gm_l27;
            uint16_t t393 = (uint16_t)t392;
            gm_tb1f35qilrmun t394 = gm_f_bc_cmp_hcolbhq(t386, t387, t388, ((uint8_t)1), t389, t390, t391, t393);
            uint8_t t395 = 0;
            F.ret = t394;
            goto gm_fret;
        }
        goto me353;
mn370:;
ma356:;
        uint8_t t397 = GM_LD(uint8_t, ((((uint8_t *)&gm_l26) + 4) + 0));
        if (t397 != ((uint8_t)3ULL)) goto mn396;
        gm_ts6oqppjh0ba3 t398 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l26) + 0));
        gm_t9qkh0rsooc8d t399;
        GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)51ULL));
        gm_t9qkh0rsooc8d t400;
        GM_ST(uint8_t, ((uint8_t *)&t400 + 0), ((uint8_t)52ULL));
        uint32_t t401 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 12));
        uint32_t t402 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 16));
        uint32_t t403 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 20));
        gm_tb1f35qilrmun t404 = gm_f_bc_cmp_hcolbhq(t398, t399, t400, ((uint8_t)1), t401, t402, t403, ((uint16_t)0ULL));
        uint8_t t405 = 0;
        F.ret = t404;
        goto gm_fret;
        goto me353;
mn396:;
ma357:;
        gm_tb1f35qilrmun t407 = gm_f_bc_free_v10vouf();
        uint8_t t408 = 0;
        F.ret = t407;
        goto gm_fret;
        goto me353;
mn406:;
ma358:;
        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 401, 21);
me353:;
        goto me3;
mn350:;
ma17:;
        uint8_t t410 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t410 != ((uint8_t)31ULL)) goto mn409;
        gm_tufm7evi61pnh t411 = GM_LD(gm_tufm7evi61pnh, (((uint8_t *)t2) + 8));
        gm_l28 = t411;
        uint8_t t416 = GM_LD(uint8_t, ((((uint8_t *)&gm_l28) + 0) + 0));
        if (t416 != ((uint8_t)0ULL)) goto pa415;
        goto po414;
pa415:;
        uint8_t t418 = GM_LD(uint8_t, ((((uint8_t *)&gm_l28) + 0) + 0));
        if (t418 != ((uint8_t)1ULL)) goto pa417;
        goto po414;
pa417:;
        goto mn413;
po414:;
        gm_t9qkh0rsooc8d t419;
        uint8_t t421 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 8));
        if (!t421) goto ie420;
        gm_t9qkh0rsooc8d t423;
        GM_ST(uint8_t, ((uint8_t *)&t423 + 0), ((uint8_t)44ULL));
        gm_t9qkh0rsooc8d t424 = t423;
        t419 = t424;
        goto ix420;
ie420:;
        gm_t9qkh0rsooc8d t426;
        GM_ST(uint8_t, ((uint8_t *)&t426 + 0), ((uint8_t)43ULL));
        gm_t9qkh0rsooc8d t427 = t426;
        t419 = t427;
ix420:;
        uint32_t t428 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 12));
        uint32_t t429 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 16));
        uint32_t t430 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 20));
        gm_tb1f35qilrmun t431 = gm_f_bc3_m9eakek(t419, t428, t429, t430);
        uint8_t t432 = 0;
        F.ret = t431;
        goto gm_fret;
        goto me412;
mn413:;
        gm_tb1f35qilrmun t434 = gm_f_bc_free_v10vouf();
        uint8_t t435 = 0;
        F.ret = t434;
        goto gm_fret;
        goto me412;
mn433:;
        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 410, 20);
me412:;
        goto me3;
mn409:;
ma18:;
        uint8_t t437 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t437 != ((uint8_t)29ULL)) goto mn436;
        gm_tufm7evi61pnh t438 = GM_LD(gm_tufm7evi61pnh, (((uint8_t *)t2) + 8));
        gm_l29 = t438;
        uint8_t t443 = GM_LD(uint8_t, ((((uint8_t *)&gm_l29) + 0) + 0));
        if (t443 != ((uint8_t)0ULL)) goto pa442;
        goto po441;
pa442:;
        uint8_t t445 = GM_LD(uint8_t, ((((uint8_t *)&gm_l29) + 0) + 0));
        if (t445 != ((uint8_t)1ULL)) goto pa444;
        goto po441;
pa444:;
        goto mn440;
po441:;
        gm_t9qkh0rsooc8d t446;
        uint8_t t448 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 8));
        if (!t448) goto ie447;
        gm_t9qkh0rsooc8d t450;
        GM_ST(uint8_t, ((uint8_t *)&t450 + 0), ((uint8_t)53ULL));
        gm_t9qkh0rsooc8d t451 = t450;
        t446 = t451;
        goto ix447;
ie447:;
        gm_t9qkh0rsooc8d t453;
        GM_ST(uint8_t, ((uint8_t *)&t453 + 0), ((uint8_t)54ULL));
        gm_t9qkh0rsooc8d t454 = t453;
        t446 = t454;
ix447:;
        uint32_t t455 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 12));
        uint32_t t456 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 16));
        uint32_t t457 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 20));
        gm_tb1f35qilrmun t458 = gm_f_bc3_m9eakek(t446, t455, t456, t457);
        uint8_t t459 = 0;
        F.ret = t458;
        goto gm_fret;
        goto me439;
mn440:;
        gm_tb1f35qilrmun t461 = gm_f_bc_free_v10vouf();
        uint8_t t462 = 0;
        F.ret = t461;
        goto gm_fret;
        goto me439;
mn460:;
        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 415, 20);
me439:;
        goto me3;
mn436:;
ma19:;
        uint8_t t464 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t464 != ((uint8_t)38ULL)) goto mn463;
        uint32_t t465 = GM_LD(uint32_t, (((uint8_t *)t2) + 8));
        gm_l30 = t465;
        gm_t9qkh0rsooc8d t466;
        GM_ST(uint8_t, ((uint8_t *)&t466 + 0), ((uint8_t)55ULL));
        uint32_t t467 = gm_l30;
        gm_tb1f35qilrmun t468 = gm_f_bc3_m9eakek(t466, t467, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t469 = 0;
        F.ret = t468;
        goto gm_fret;
        goto me3;
mn463:;
ma20:;
        uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t471 != ((uint8_t)39ULL)) goto mn470;
        uint32_t t472 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l31 = t472;
        uint32_t t473 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l32 = t473;
        gm_t9qkh0rsooc8d t474;
        GM_ST(uint8_t, ((uint8_t *)&t474 + 0), ((uint8_t)56ULL));
        uint32_t t475 = gm_l31;
        uint32_t t476 = gm_l32;
        gm_tb1f35qilrmun t477 = gm_f_bc3_m9eakek(t474, t475, ((uint32_t)0ULL), t476);
        uint8_t t478 = 0;
        F.ret = t477;
        goto gm_fret;
        goto me3;
mn470:;
ma21:;
        uint8_t t480 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t480 != ((uint8_t)40ULL)) goto mn479;
        uint32_t t481 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l33 = t481;
        uint32_t t482 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l34 = t482;
        gm_t9qkh0rsooc8d t483;
        GM_ST(uint8_t, ((uint8_t *)&t483 + 0), ((uint8_t)57ULL));
        uint32_t t484 = gm_l33;
        uint32_t t485 = gm_l34;
        gm_tb1f35qilrmun t486 = gm_f_bc3_m9eakek(t483, t484, ((uint32_t)0ULL), t485);
        uint8_t t487 = 0;
        F.ret = t486;
        goto gm_fret;
        goto me3;
mn479:;
ma22:;
        uint8_t t489 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t489 != ((uint8_t)78ULL)) goto mn488;
        uint32_t t490 = GM_LD(uint32_t, (((uint8_t *)t2) + 8));
        gm_l35 = t490;
        gm_t9qkh0rsooc8d t491;
        GM_ST(uint8_t, ((uint8_t *)&t491 + 0), ((uint8_t)58ULL));
        uint32_t t492 = gm_l35;
        gm_tb1f35qilrmun t493 = gm_f_bc3_m9eakek(t491, t492, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t494 = 0;
        F.ret = t493;
        goto gm_fret;
        goto me3;
mn488:;
ma23:;
        uint8_t t496 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t496 != ((uint8_t)3ULL)) goto mn495;
        uint32_t t497 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l36 = t497;
        uint32_t t498 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l37 = t498;
        uint32_t t499 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l38 = t499;
        {
            uint8_t t502 = 0;
            uint8_t t503 = gm_l1;
            uint8_t t504 = (uint8_t)!t503;
            if (!t504) {
                uint32_t t505 = gm_l38;
                uint64_t t506 = (uint64_t)t505;
                uint8_t t507 = gm_f_word_size_mu6ni08(t506);
                uint8_t t508 = (uint8_t)!t507;
                t502 = t508;
            } else t502 = 1;
            if (!t502) goto ie501;
            {
                gm_tb1f35qilrmun t510 = gm_f_bc_free_v10vouf();
                uint8_t t511 = 0;
                F.ret = t510;
                goto gm_fret;
            }
            goto ix501;
ie501:;
ix501:;
        }
        {
            gm_t9qkh0rsooc8d t512;
            uint32_t t514 = gm_l38;
            uint8_t t515 = ((t514 == ((uint32_t)8ULL)));
            if (!t515) goto ie513;
            gm_t9qkh0rsooc8d t517;
            GM_ST(uint8_t, ((uint8_t *)&t517 + 0), ((uint8_t)62ULL));
            gm_t9qkh0rsooc8d t518 = t517;
            t512 = t518;
            goto ix513;
ie513:;
            gm_t9qkh0rsooc8d t519;
            uint32_t t521 = gm_l38;
            uint8_t t522 = ((t521 == ((uint32_t)4ULL)));
            if (!t522) goto ie520;
            gm_t9qkh0rsooc8d t524;
            GM_ST(uint8_t, ((uint8_t *)&t524 + 0), ((uint8_t)61ULL));
            gm_t9qkh0rsooc8d t525 = t524;
            t519 = t525;
            goto ix520;
ie520:;
            gm_t9qkh0rsooc8d t526;
            uint32_t t528 = gm_l38;
            uint8_t t529 = ((t528 == ((uint32_t)1ULL)));
            if (!t529) goto ie527;
            gm_t9qkh0rsooc8d t531;
            GM_ST(uint8_t, ((uint8_t *)&t531 + 0), ((uint8_t)59ULL));
            gm_t9qkh0rsooc8d t532 = t531;
            t526 = t532;
            goto ix527;
ie527:;
            gm_t9qkh0rsooc8d t534;
            GM_ST(uint8_t, ((uint8_t *)&t534 + 0), ((uint8_t)60ULL));
            gm_t9qkh0rsooc8d t535 = t534;
            t526 = t535;
ix527:;
            t519 = t526;
ix520:;
            t512 = t519;
ix513:;
            uint32_t t536 = gm_l36;
            uint32_t t537 = gm_l37;
            gm_tb1f35qilrmun t538 = gm_f_bc3_m9eakek(t512, t536, t537, ((uint32_t)0ULL));
            uint8_t t539 = 0;
            F.ret = t538;
            goto gm_fret;
        }
        goto me3;
mn495:;
ma24:;
        uint8_t t541 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t541 != ((uint8_t)53ULL)) goto mn540;
        uint32_t t542 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l39 = t542;
        uint32_t t543 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l40 = t543;
        uint32_t t544 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l41 = t544;
        {
            uint8_t t547 = 0;
            uint8_t t548 = gm_l1;
            uint8_t t549 = (uint8_t)!t548;
            if (!t549) {
                uint32_t t550 = gm_l41;
                uint64_t t551 = (uint64_t)t550;
                uint8_t t552 = gm_f_word_size_mu6ni08(t551);
                uint8_t t553 = (uint8_t)!t552;
                t547 = t553;
            } else t547 = 1;
            if (!t547) goto ie546;
            {
                gm_tb1f35qilrmun t555 = gm_f_bc_free_v10vouf();
                uint8_t t556 = 0;
                F.ret = t555;
                goto gm_fret;
            }
            goto ix546;
ie546:;
ix546:;
        }
        {
            gm_t9qkh0rsooc8d t557;
            uint32_t t559 = gm_l41;
            uint8_t t560 = ((t559 == ((uint32_t)8ULL)));
            if (!t560) goto ie558;
            gm_t9qkh0rsooc8d t562;
            GM_ST(uint8_t, ((uint8_t *)&t562 + 0), ((uint8_t)62ULL));
            gm_t9qkh0rsooc8d t563 = t562;
            t557 = t563;
            goto ix558;
ie558:;
            gm_t9qkh0rsooc8d t564;
            uint32_t t566 = gm_l41;
            uint8_t t567 = ((t566 == ((uint32_t)4ULL)));
            if (!t567) goto ie565;
            gm_t9qkh0rsooc8d t569;
            GM_ST(uint8_t, ((uint8_t *)&t569 + 0), ((uint8_t)61ULL));
            gm_t9qkh0rsooc8d t570 = t569;
            t564 = t570;
            goto ix565;
ie565:;
            gm_t9qkh0rsooc8d t571;
            uint32_t t573 = gm_l41;
            uint8_t t574 = ((t573 == ((uint32_t)1ULL)));
            if (!t574) goto ie572;
            gm_t9qkh0rsooc8d t576;
            GM_ST(uint8_t, ((uint8_t *)&t576 + 0), ((uint8_t)59ULL));
            gm_t9qkh0rsooc8d t577 = t576;
            t571 = t577;
            goto ix572;
ie572:;
            gm_t9qkh0rsooc8d t579;
            GM_ST(uint8_t, ((uint8_t *)&t579 + 0), ((uint8_t)60ULL));
            gm_t9qkh0rsooc8d t580 = t579;
            t571 = t580;
ix572:;
            t564 = t571;
ix565:;
            t557 = t564;
ix558:;
            uint32_t t581 = gm_l39;
            uint32_t t582 = gm_l40;
            gm_tb1f35qilrmun t583 = gm_f_bc3_m9eakek(t557, t581, t582, ((uint32_t)0ULL));
            uint8_t t584 = 0;
            F.ret = t583;
            goto gm_fret;
        }
        goto me3;
mn540:;
ma25:;
        uint8_t t586 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t586 != ((uint8_t)4ULL)) goto mn585;
        uint32_t t587 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l42 = t587;
        uint32_t t588 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l43 = t588;
        uint32_t t589 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l44 = t589;
        {
            uint8_t t592 = 0;
            uint8_t t593 = gm_l1;
            uint8_t t594 = (uint8_t)!t593;
            if (!t594) {
                uint32_t t595 = gm_l43;
                uint64_t t596 = (uint64_t)t595;
                uint8_t t597 = gm_f_word_size_mu6ni08(t596);
                uint8_t t598 = (uint8_t)!t597;
                t592 = t598;
            } else t592 = 1;
            if (!t592) goto ie591;
            {
                gm_tb1f35qilrmun t600 = gm_f_bc_free_v10vouf();
                uint8_t t601 = 0;
                F.ret = t600;
                goto gm_fret;
            }
            goto ix591;
ie591:;
ix591:;
        }
        {
            gm_t9qkh0rsooc8d t602;
            uint32_t t604 = gm_l43;
            uint8_t t605 = ((t604 == ((uint32_t)8ULL)));
            if (!t605) goto ie603;
            gm_t9qkh0rsooc8d t607;
            GM_ST(uint8_t, ((uint8_t *)&t607 + 0), ((uint8_t)66ULL));
            gm_t9qkh0rsooc8d t608 = t607;
            t602 = t608;
            goto ix603;
ie603:;
            gm_t9qkh0rsooc8d t609;
            uint32_t t611 = gm_l43;
            uint8_t t612 = ((t611 == ((uint32_t)4ULL)));
            if (!t612) goto ie610;
            gm_t9qkh0rsooc8d t614;
            GM_ST(uint8_t, ((uint8_t *)&t614 + 0), ((uint8_t)65ULL));
            gm_t9qkh0rsooc8d t615 = t614;
            t609 = t615;
            goto ix610;
ie610:;
            gm_t9qkh0rsooc8d t616;
            uint32_t t618 = gm_l43;
            uint8_t t619 = ((t618 == ((uint32_t)1ULL)));
            if (!t619) goto ie617;
            gm_t9qkh0rsooc8d t621;
            GM_ST(uint8_t, ((uint8_t *)&t621 + 0), ((uint8_t)63ULL));
            gm_t9qkh0rsooc8d t622 = t621;
            t616 = t622;
            goto ix617;
ie617:;
            gm_t9qkh0rsooc8d t624;
            GM_ST(uint8_t, ((uint8_t *)&t624 + 0), ((uint8_t)64ULL));
            gm_t9qkh0rsooc8d t625 = t624;
            t616 = t625;
ix617:;
            t609 = t616;
ix610:;
            t602 = t609;
ix603:;
            uint32_t t626 = gm_l42;
            uint32_t t627 = gm_l44;
            gm_tb1f35qilrmun t628 = gm_f_bc3_m9eakek(t602, t626, t627, ((uint32_t)0ULL));
            uint8_t t629 = 0;
            F.ret = t628;
            goto gm_fret;
        }
        goto me3;
mn585:;
ma26:;
        uint8_t t631 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t631 != ((uint8_t)54ULL)) goto mn630;
        uint32_t t632 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l45 = t632;
        uint32_t t633 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l46 = t633;
        uint32_t t634 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l47 = t634;
        {
            uint8_t t637 = 0;
            uint8_t t638 = gm_l1;
            uint8_t t639 = (uint8_t)!t638;
            if (!t639) {
                uint32_t t640 = gm_l46;
                uint64_t t641 = (uint64_t)t640;
                uint8_t t642 = gm_f_word_size_mu6ni08(t641);
                uint8_t t643 = (uint8_t)!t642;
                t637 = t643;
            } else t637 = 1;
            if (!t637) goto ie636;
            {
                gm_tb1f35qilrmun t645 = gm_f_bc_free_v10vouf();
                uint8_t t646 = 0;
                F.ret = t645;
                goto gm_fret;
            }
            goto ix636;
ie636:;
ix636:;
        }
        {
            gm_t9qkh0rsooc8d t647;
            uint32_t t649 = gm_l46;
            uint8_t t650 = ((t649 == ((uint32_t)8ULL)));
            if (!t650) goto ie648;
            gm_t9qkh0rsooc8d t652;
            GM_ST(uint8_t, ((uint8_t *)&t652 + 0), ((uint8_t)66ULL));
            gm_t9qkh0rsooc8d t653 = t652;
            t647 = t653;
            goto ix648;
ie648:;
            gm_t9qkh0rsooc8d t654;
            uint32_t t656 = gm_l46;
            uint8_t t657 = ((t656 == ((uint32_t)4ULL)));
            if (!t657) goto ie655;
            gm_t9qkh0rsooc8d t659;
            GM_ST(uint8_t, ((uint8_t *)&t659 + 0), ((uint8_t)65ULL));
            gm_t9qkh0rsooc8d t660 = t659;
            t654 = t660;
            goto ix655;
ie655:;
            gm_t9qkh0rsooc8d t661;
            uint32_t t663 = gm_l46;
            uint8_t t664 = ((t663 == ((uint32_t)1ULL)));
            if (!t664) goto ie662;
            gm_t9qkh0rsooc8d t666;
            GM_ST(uint8_t, ((uint8_t *)&t666 + 0), ((uint8_t)63ULL));
            gm_t9qkh0rsooc8d t667 = t666;
            t661 = t667;
            goto ix662;
ie662:;
            gm_t9qkh0rsooc8d t669;
            GM_ST(uint8_t, ((uint8_t *)&t669 + 0), ((uint8_t)64ULL));
            gm_t9qkh0rsooc8d t670 = t669;
            t661 = t670;
ix662:;
            t654 = t661;
ix655:;
            t647 = t654;
ix648:;
            uint32_t t671 = gm_l45;
            uint32_t t672 = gm_l47;
            gm_tb1f35qilrmun t673 = gm_f_bc3_m9eakek(t647, t671, t672, ((uint32_t)0ULL));
            uint8_t t674 = 0;
            F.ret = t673;
            goto gm_fret;
        }
        goto me3;
mn630:;
ma27:;
        uint8_t t676 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t676 != ((uint8_t)6ULL)) goto mn675;
        uint32_t t677 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l48 = t677;
        uint32_t t678 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l49 = t678;
        uint32_t t679 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l50 = t679;
        uint32_t t680 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 12));
        gm_l51 = t680;
        {
            uint8_t t683 = 0;
            uint8_t t684 = gm_l1;
            uint8_t t685 = (uint8_t)!t684;
            if (!t685) {
                uint32_t t686 = gm_l51;
                uint64_t t687 = (uint64_t)t686;
                uint8_t t688 = gm_f_word_size_mu6ni08(t687);
                uint8_t t689 = (uint8_t)!t688;
                t683 = t689;
            } else t683 = 1;
            if (!t683) goto ie682;
            {
                gm_tb1f35qilrmun t691 = gm_f_bc_free_v10vouf();
                uint8_t t692 = 0;
                F.ret = t691;
                goto gm_fret;
            }
            goto ix682;
ie682:;
ix682:;
        }
        {
            gm_t9qkh0rsooc8d t693;
            GM_ST(uint8_t, ((uint8_t *)&t693 + 0), ((uint8_t)67ULL));
            uint32_t t694 = gm_l51;
            uint8_t t695 = (uint8_t)t694;
            uint32_t t696 = gm_l48;
            uint32_t t697 = gm_l49;
            uint32_t t698 = gm_l50;
            gm_tb1f35qilrmun t699;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t699 + 0), t693);
            GM_ST(uint8_t, ((uint8_t *)&t699 + 1), t695);
            GM_ST(uint16_t, ((uint8_t *)&t699 + 2), ((uint16_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t699 + 4), t696);
            GM_ST(uint32_t, ((uint8_t *)&t699 + 8), t697);
            GM_ST(uint32_t, ((uint8_t *)&t699 + 12), t698);
            uint8_t t700 = 0;
            F.ret = t699;
            goto gm_fret;
        }
        goto me3;
mn675:;
ma28:;
        uint8_t t702 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t702 != ((uint8_t)7ULL)) goto mn701;
        uint32_t t703 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l52 = t703;
        uint32_t t704 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l53 = t704;
        uint32_t t705 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l54 = t705;
        uint32_t t706 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 12));
        gm_l55 = t706;
        {
            uint8_t t709 = 0;
            uint8_t t710 = gm_l1;
            uint8_t t711 = (uint8_t)!t710;
            if (!t711) {
                uint32_t t712 = gm_l54;
                uint64_t t713 = (uint64_t)t712;
                uint8_t t714 = gm_f_word_size_mu6ni08(t713);
                uint8_t t715 = (uint8_t)!t714;
                t709 = t715;
            } else t709 = 1;
            if (!t709) goto ie708;
            {
                gm_tb1f35qilrmun t717 = gm_f_bc_free_v10vouf();
                uint8_t t718 = 0;
                F.ret = t717;
                goto gm_fret;
            }
            goto ix708;
ie708:;
ix708:;
        }
        {
            gm_t9qkh0rsooc8d t719;
            GM_ST(uint8_t, ((uint8_t *)&t719 + 0), ((uint8_t)68ULL));
            uint32_t t720 = gm_l54;
            uint8_t t721 = (uint8_t)t720;
            uint32_t t722 = gm_l52;
            uint32_t t723 = gm_l53;
            uint32_t t724 = gm_l55;
            gm_tb1f35qilrmun t725;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t725 + 0), t719);
            GM_ST(uint8_t, ((uint8_t *)&t725 + 1), t721);
            GM_ST(uint16_t, ((uint8_t *)&t725 + 2), ((uint16_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t725 + 4), t722);
            GM_ST(uint32_t, ((uint8_t *)&t725 + 8), t723);
            GM_ST(uint32_t, ((uint8_t *)&t725 + 12), t724);
            uint8_t t726 = 0;
            F.ret = t725;
            goto gm_fret;
        }
        goto me3;
mn701:;
ma29:;
        uint8_t t728 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t728 != ((uint8_t)55ULL)) goto mn727;
        uint32_t t729 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l56 = t729;
        uint32_t t730 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l57 = t730;
        uint8_t t731 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l58 = t731;
        uint32_t t732 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 12));
        gm_l59 = t732;
        {
            uint8_t t735 = 0;
            uint8_t t736 = gm_l1;
            uint8_t t737 = (uint8_t)!t736;
            if (!t737) {
                uint32_t t738 = gm_l57;
                uint64_t t739 = (uint64_t)t738;
                uint8_t t740 = gm_f_word_size_mu6ni08(t739);
                uint8_t t741 = (uint8_t)!t740;
                t735 = t741;
            } else t735 = 1;
            if (!t735) goto ie734;
            {
                gm_tb1f35qilrmun t743 = gm_f_bc_free_v10vouf();
                uint8_t t744 = 0;
                F.ret = t743;
                goto gm_fret;
            }
            goto ix734;
ie734:;
ix734:;
        }
        {
            gm_t9qkh0rsooc8d t745;
            uint8_t t747 = gm_l58;
            if (!t747) goto ie746;
            gm_t9qkh0rsooc8d t749;
            GM_ST(uint8_t, ((uint8_t *)&t749 + 0), ((uint8_t)70ULL));
            gm_t9qkh0rsooc8d t750 = t749;
            t745 = t750;
            goto ix746;
ie746:;
            gm_t9qkh0rsooc8d t752;
            GM_ST(uint8_t, ((uint8_t *)&t752 + 0), ((uint8_t)69ULL));
            gm_t9qkh0rsooc8d t753 = t752;
            t745 = t753;
ix746:;
            uint32_t t754 = gm_l57;
            uint8_t t755 = (uint8_t)t754;
            uint32_t t756 = gm_l56;
            uint32_t t757 = gm_l59;
            gm_tb1f35qilrmun t758;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t758 + 0), t745);
            GM_ST(uint8_t, ((uint8_t *)&t758 + 1), t755);
            GM_ST(uint16_t, ((uint8_t *)&t758 + 2), ((uint16_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t758 + 4), t756);
            GM_ST(uint32_t, ((uint8_t *)&t758 + 8), ((uint32_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t758 + 12), t757);
            uint8_t t759 = 0;
            F.ret = t758;
            goto gm_fret;
        }
        goto me3;
mn727:;
ma30:;
        uint8_t t761 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t761 != ((uint8_t)60ULL)) goto mn760;
        uint32_t t762 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l60 = t762;
        uint32_t t763 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l61 = t763;
        uint64_t t764 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l62 = t764;
        uint32_t t765 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 16));
        gm_l63 = t765;
        {
            uint8_t t768 = 0;
            uint8_t t769 = 0;
            uint8_t t770 = gm_l1;
            uint8_t t771 = (uint8_t)!t770;
            if (!t771) {
                uint32_t t772 = gm_l61;
                uint64_t t773 = (uint64_t)t772;
                uint8_t t774 = gm_f_word_size_mu6ni08(t773);
                uint8_t t775 = (uint8_t)!t774;
                t769 = t775;
            } else t769 = 1;
            if (!t769) {
                uint64_t t776 = gm_l62;
                uint8_t t777 = gm_f_fits32_a5qt6pu(t776);
                uint8_t t778 = (uint8_t)!t777;
                t768 = t778;
            } else t768 = 1;
            if (!t768) goto ie767;
            {
                gm_tb1f35qilrmun t780 = gm_f_bc_free_v10vouf();
                uint8_t t781 = 0;
                F.ret = t780;
                goto gm_fret;
            }
            goto ix767;
ie767:;
ix767:;
        }
        {
            gm_t9qkh0rsooc8d t782;
            GM_ST(uint8_t, ((uint8_t *)&t782 + 0), ((uint8_t)71ULL));
            uint32_t t783 = gm_l61;
            uint8_t t784 = (uint8_t)t783;
            uint32_t t785 = gm_l60;
            uint64_t t786 = gm_l62;
            uint32_t t787 = (uint32_t)t786;
            uint32_t t788 = gm_l63;
            gm_tb1f35qilrmun t789;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t789 + 0), t782);
            GM_ST(uint8_t, ((uint8_t *)&t789 + 1), t784);
            GM_ST(uint16_t, ((uint8_t *)&t789 + 2), ((uint16_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t789 + 4), t785);
            GM_ST(uint32_t, ((uint8_t *)&t789 + 8), t787);
            GM_ST(uint32_t, ((uint8_t *)&t789 + 12), t788);
            uint8_t t790 = 0;
            F.ret = t789;
            goto gm_fret;
        }
        goto me3;
mn760:;
ma31:;
        uint8_t t792 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t792 != ((uint8_t)17ULL)) goto mn791;
        gm_t04llc5uen0qo t793 = GM_LD(gm_t04llc5uen0qo, (((uint8_t *)t2) + 8));
        gm_l64 = t793;
        {
            uint8_t t796 = 0;
            uint8_t t797 = 0;
            uint8_t t798 = gm_l1;
            uint8_t t799 = (uint8_t)!t798;
            if (!t799) {
                uint32_t t800 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 12));
                uint64_t t801 = (uint64_t)t800;
                uint8_t t802 = gm_f_word_size_mu6ni08(t801);
                uint8_t t803 = (uint8_t)!t802;
                t797 = t803;
            } else t797 = 1;
            if (!t797) {
                uint32_t t804 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 16));
                uint8_t t805 = gm_f_bc_align_6s4vol1(t804);
                uint8_t t806 = (uint8_t)!t805;
                t796 = t806;
            } else t796 = 1;
            if (!t796) goto ie795;
            {
                gm_tb1f35qilrmun t808 = gm_f_bc_free_v10vouf();
                uint8_t t809 = 0;
                F.ret = t808;
                goto gm_fret;
            }
            goto ix795;
ie795:;
ix795:;
        }
        {
            gm_t9qkh0rsooc8d t810;
            uint32_t t812 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 12));
            uint8_t t813 = ((t812 == ((uint32_t)8ULL)));
            if (!t813) goto ie811;
            gm_t9qkh0rsooc8d t815;
            GM_ST(uint8_t, ((uint8_t *)&t815 + 0), ((uint8_t)75ULL));
            gm_t9qkh0rsooc8d t816 = t815;
            t810 = t816;
            goto ix811;
ie811:;
            gm_t9qkh0rsooc8d t817;
            uint32_t t819 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 12));
            uint8_t t820 = ((t819 == ((uint32_t)4ULL)));
            if (!t820) goto ie818;
            gm_t9qkh0rsooc8d t822;
            GM_ST(uint8_t, ((uint8_t *)&t822 + 0), ((uint8_t)74ULL));
            gm_t9qkh0rsooc8d t823 = t822;
            t817 = t823;
            goto ix818;
ie818:;
            gm_t9qkh0rsooc8d t824;
            uint32_t t826 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 12));
            uint8_t t827 = ((t826 == ((uint32_t)1ULL)));
            if (!t827) goto ie825;
            gm_t9qkh0rsooc8d t829;
            GM_ST(uint8_t, ((uint8_t *)&t829 + 0), ((uint8_t)72ULL));
            gm_t9qkh0rsooc8d t830 = t829;
            t824 = t830;
            goto ix825;
ie825:;
            gm_t9qkh0rsooc8d t832;
            GM_ST(uint8_t, ((uint8_t *)&t832 + 0), ((uint8_t)73ULL));
            gm_t9qkh0rsooc8d t833 = t832;
            t824 = t833;
ix825:;
            t817 = t824;
ix818:;
            t810 = t817;
ix811:;
            uint32_t t834 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 16));
            uint16_t t835 = (uint16_t)t834;
            uint32_t t836 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 0));
            uint32_t t837 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 4));
            uint32_t t838 = GM_LD(uint32_t, (((uint8_t *)&gm_l64) + 8));
            gm_tb1f35qilrmun t839;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t839 + 0), t810);
            GM_ST(uint8_t, ((uint8_t *)&t839 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t839 + 2), t835);
            GM_ST(uint32_t, ((uint8_t *)&t839 + 4), t836);
            GM_ST(uint32_t, ((uint8_t *)&t839 + 8), t837);
            GM_ST(uint32_t, ((uint8_t *)&t839 + 12), t838);
            uint8_t t840 = 0;
            F.ret = t839;
            goto gm_fret;
        }
        goto me3;
mn791:;
ma32:;
        uint8_t t842 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t842 != ((uint8_t)18ULL)) goto mn841;
        gm_t04llc5uen0qo t843 = GM_LD(gm_t04llc5uen0qo, (((uint8_t *)t2) + 8));
        gm_l65 = t843;
        {
            uint8_t t846 = 0;
            uint8_t t847 = 0;
            uint8_t t848 = gm_l1;
            uint8_t t849 = (uint8_t)!t848;
            if (!t849) {
                uint32_t t850 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 8));
                uint64_t t851 = (uint64_t)t850;
                uint8_t t852 = gm_f_word_size_mu6ni08(t851);
                uint8_t t853 = (uint8_t)!t852;
                t847 = t853;
            } else t847 = 1;
            if (!t847) {
                uint32_t t854 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 12));
                uint8_t t855 = gm_f_bc_align_6s4vol1(t854);
                uint8_t t856 = (uint8_t)!t855;
                t846 = t856;
            } else t846 = 1;
            if (!t846) goto ie845;
            {
                gm_tb1f35qilrmun t858 = gm_f_bc_free_v10vouf();
                uint8_t t859 = 0;
                F.ret = t858;
                goto gm_fret;
            }
            goto ix845;
ie845:;
ix845:;
        }
        {
            gm_t9qkh0rsooc8d t860;
            uint32_t t862 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 8));
            uint8_t t863 = ((t862 == ((uint32_t)8ULL)));
            if (!t863) goto ie861;
            gm_t9qkh0rsooc8d t865;
            GM_ST(uint8_t, ((uint8_t *)&t865 + 0), ((uint8_t)79ULL));
            gm_t9qkh0rsooc8d t866 = t865;
            t860 = t866;
            goto ix861;
ie861:;
            gm_t9qkh0rsooc8d t867;
            uint32_t t869 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 8));
            uint8_t t870 = ((t869 == ((uint32_t)4ULL)));
            if (!t870) goto ie868;
            gm_t9qkh0rsooc8d t872;
            GM_ST(uint8_t, ((uint8_t *)&t872 + 0), ((uint8_t)78ULL));
            gm_t9qkh0rsooc8d t873 = t872;
            t867 = t873;
            goto ix868;
ie868:;
            gm_t9qkh0rsooc8d t874;
            uint32_t t876 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 8));
            uint8_t t877 = ((t876 == ((uint32_t)1ULL)));
            if (!t877) goto ie875;
            gm_t9qkh0rsooc8d t879;
            GM_ST(uint8_t, ((uint8_t *)&t879 + 0), ((uint8_t)76ULL));
            gm_t9qkh0rsooc8d t880 = t879;
            t874 = t880;
            goto ix875;
ie875:;
            gm_t9qkh0rsooc8d t882;
            GM_ST(uint8_t, ((uint8_t *)&t882 + 0), ((uint8_t)77ULL));
            gm_t9qkh0rsooc8d t883 = t882;
            t874 = t883;
ix875:;
            t867 = t874;
ix868:;
            t860 = t867;
ix861:;
            uint32_t t884 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 12));
            uint16_t t885 = (uint16_t)t884;
            uint32_t t886 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 0));
            uint32_t t887 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 16));
            uint32_t t888 = GM_LD(uint32_t, (((uint8_t *)&gm_l65) + 4));
            gm_tb1f35qilrmun t889;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t889 + 0), t860);
            GM_ST(uint8_t, ((uint8_t *)&t889 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t889 + 2), t885);
            GM_ST(uint32_t, ((uint8_t *)&t889 + 4), t886);
            GM_ST(uint32_t, ((uint8_t *)&t889 + 8), t887);
            GM_ST(uint32_t, ((uint8_t *)&t889 + 12), t888);
            uint8_t t890 = 0;
            F.ret = t889;
            goto gm_fret;
        }
        goto me3;
mn841:;
ma33:;
        uint8_t t892 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t892 != ((uint8_t)21ULL)) goto mn891;
        gm_t1lvkimu9u0hv t893 = GM_LD(gm_t1lvkimu9u0hv, (((uint8_t *)t2) + 8));
        F.l66 = t893;
        {
            uint8_t t896 = 0;
            uint8_t t897 = 0;
            uint8_t t898 = 0;
            uint8_t t899 = 0;
            uint8_t t900 = gm_l1;
            uint8_t t901 = (uint8_t)!t900;
            if (!t901) {
                uint32_t t902 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 24));
                uint64_t t903 = (uint64_t)t902;
                uint8_t t904 = gm_f_word_size_mu6ni08(t903);
                uint8_t t905 = (uint8_t)!t904;
                t899 = t905;
            } else t899 = 1;
            if (!t899) {
                uint32_t t906 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 28));
                uint32_t t907 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 24));
                uint8_t t908 = (!(t906 == t907));
                t898 = t908;
            } else t898 = 1;
            if (!t898) {
                uint32_t t909 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 8));
                uint32_t t910 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 4));
                uint32_t r911; bool o912 = __builtin_add_overflow(t910, ((uint32_t)1ULL), &r911);
                if (o912) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 464, 97);
                uint32_t t913 = r911;
                uint8_t t914 = (!(t909 == t913));
                t897 = t914;
            } else t897 = 1;
            if (!t897) {
                uint64_t t915 = GM_LD(uint64_t, (((uint8_t *)&F.l66) + 16));
                uint8_t t916 = gm_f_fits16_1bnmjva(t915);
                uint8_t t917 = (uint8_t)!t916;
                t896 = t917;
            } else t896 = 1;
            if (!t896) goto ie895;
            {
                gm_tb1f35qilrmun t919 = gm_f_bc_free_v10vouf();
                uint8_t t920 = 0;
                F.ret = t919;
                goto gm_fret;
            }
            goto ix895;
ie895:;
ix895:;
        }
        {
            gm_t9qkh0rsooc8d t921;
            GM_ST(uint8_t, ((uint8_t *)&t921 + 0), ((uint8_t)80ULL));
            uint32_t t922 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 24));
            uint8_t t923 = (uint8_t)t922;
            uint64_t t924 = GM_LD(uint64_t, (((uint8_t *)&F.l66) + 16));
            uint16_t t925 = (uint16_t)t924;
            uint32_t t926 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 0));
            uint32_t t927 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 4));
            uint32_t t928 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 12));
            gm_tb1f35qilrmun t929;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t929 + 0), t921);
            GM_ST(uint8_t, ((uint8_t *)&t929 + 1), t923);
            GM_ST(uint16_t, ((uint8_t *)&t929 + 2), t925);
            GM_ST(uint32_t, ((uint8_t *)&t929 + 4), t926);
            GM_ST(uint32_t, ((uint8_t *)&t929 + 8), t927);
            GM_ST(uint32_t, ((uint8_t *)&t929 + 12), t928);
            uint8_t t930 = 0;
            F.ret = t929;
            goto gm_fret;
        }
        goto me3;
mn891:;
ma34:;
        uint8_t t932 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t932 != ((uint8_t)22ULL)) goto mn931;
        gm_thbsr19fadfjo t933 = GM_LD(gm_thbsr19fadfjo, (((uint8_t *)t2) + 8));
        F.l67 = t933;
        {
            uint8_t t936 = 0;
            uint8_t t937 = 0;
            uint8_t t938 = 0;
            uint8_t t939 = 0;
            uint8_t t940 = gm_l1;
            uint8_t t941 = (uint8_t)!t940;
            if (!t941) {
                uint32_t t942 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 24));
                uint64_t t943 = (uint64_t)t942;
                uint8_t t944 = gm_f_word_size_mu6ni08(t943);
                uint8_t t945 = (uint8_t)!t944;
                t939 = t945;
            } else t939 = 1;
            if (!t939) {
                uint32_t t946 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 28));
                uint32_t t947 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 24));
                uint8_t t948 = (!(t946 == t947));
                t938 = t948;
            } else t938 = 1;
            if (!t938) {
                uint32_t t949 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 4));
                uint32_t t950 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 0));
                uint32_t r951; bool o952 = __builtin_add_overflow(t950, ((uint32_t)1ULL), &r951);
                if (o952) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 468, 97);
                uint32_t t953 = r951;
                uint8_t t954 = (!(t949 == t953));
                t937 = t954;
            } else t937 = 1;
            if (!t937) {
                uint64_t t955 = GM_LD(uint64_t, (((uint8_t *)&F.l67) + 16));
                uint8_t t956 = gm_f_fits16_1bnmjva(t955);
                uint8_t t957 = (uint8_t)!t956;
                t936 = t957;
            } else t936 = 1;
            if (!t936) goto ie935;
            {
                gm_tb1f35qilrmun t959 = gm_f_bc_free_v10vouf();
                uint8_t t960 = 0;
                F.ret = t959;
                goto gm_fret;
            }
            goto ix935;
ie935:;
ix935:;
        }
        {
            gm_t9qkh0rsooc8d t961;
            GM_ST(uint8_t, ((uint8_t *)&t961 + 0), ((uint8_t)81ULL));
            uint32_t t962 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 24));
            uint8_t t963 = (uint8_t)t962;
            uint64_t t964 = GM_LD(uint64_t, (((uint8_t *)&F.l67) + 16));
            uint16_t t965 = (uint16_t)t964;
            uint32_t t966 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 0));
            uint32_t t967 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 32));
            uint32_t t968 = GM_LD(uint32_t, (((uint8_t *)&F.l67) + 8));
            gm_tb1f35qilrmun t969;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t969 + 0), t961);
            GM_ST(uint8_t, ((uint8_t *)&t969 + 1), t963);
            GM_ST(uint16_t, ((uint8_t *)&t969 + 2), t965);
            GM_ST(uint32_t, ((uint8_t *)&t969 + 4), t966);
            GM_ST(uint32_t, ((uint8_t *)&t969 + 8), t967);
            GM_ST(uint32_t, ((uint8_t *)&t969 + 12), t968);
            uint8_t t970 = 0;
            F.ret = t969;
            goto gm_fret;
        }
        goto me3;
mn931:;
ma35:;
        uint8_t t972 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t972 != ((uint8_t)8ULL)) goto mn971;
        uint32_t t973 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l68 = t973;
        uint64_t t974 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l69 = t974;
        uint64_t t975 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 16));
        gm_l70 = t975;
        {
            uint8_t t978 = 0;
            uint64_t t979 = gm_l69;
            uint8_t t980 = gm_f_fits32_a5qt6pu(t979);
            uint8_t t981 = (uint8_t)!t980;
            if (!t981) {
                uint64_t t982 = gm_l70;
                uint8_t t983 = gm_f_fits32_a5qt6pu(t982);
                uint8_t t984 = (uint8_t)!t983;
                t978 = t984;
            } else t978 = 1;
            if (!t978) goto ie977;
            {
                gm_tb1f35qilrmun t986 = gm_f_bc_free_v10vouf();
                uint8_t t987 = 0;
                F.ret = t986;
                goto gm_fret;
            }
            goto ix977;
ie977:;
ix977:;
        }
        {
            gm_t9qkh0rsooc8d t988;
            GM_ST(uint8_t, ((uint8_t *)&t988 + 0), ((uint8_t)82ULL));
            uint32_t t989 = gm_l68;
            uint64_t t990 = gm_l69;
            uint32_t t991 = (uint32_t)t990;
            uint64_t t992 = gm_l70;
            uint32_t t993 = (uint32_t)t992;
            gm_tb1f35qilrmun t994 = gm_f_bc3_m9eakek(t988, t989, t991, t993);
            uint8_t t995 = 0;
            F.ret = t994;
            goto gm_fret;
        }
        goto me3;
mn971:;
ma36:;
        uint8_t t997 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t997 != ((uint8_t)19ULL)) goto mn996;
        gm_t3a21dm0j52qn t998 = GM_LD(gm_t3a21dm0j52qn, (((uint8_t *)t2) + 8));
        gm_l71 = t998;
        {
            uint64_t t1001 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 16));
            uint8_t t1002 = gm_f_fits16_1bnmjva(t1001);
            uint8_t t1003 = (uint8_t)!t1002;
            if (!t1003) goto ie1000;
            {
                gm_tb1f35qilrmun t1005 = gm_f_bc_free_v10vouf();
                uint8_t t1006 = 0;
                F.ret = t1005;
                goto gm_fret;
            }
            goto ix1000;
ie1000:;
ix1000:;
        }
        {
            gm_t9qkh0rsooc8d t1007;
            GM_ST(uint8_t, ((uint8_t *)&t1007 + 0), ((uint8_t)83ULL));
            uint64_t t1008 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 16));
            uint16_t t1009 = (uint16_t)t1008;
            uint32_t t1010 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 0));
            uint32_t t1011 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 4));
            uint32_t t1012 = GM_LD(uint32_t, (((uint8_t *)&gm_l71) + 8));
            gm_tb1f35qilrmun t1013;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1013 + 0), t1007);
            GM_ST(uint8_t, ((uint8_t *)&t1013 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t1013 + 2), t1009);
            GM_ST(uint32_t, ((uint8_t *)&t1013 + 4), t1010);
            GM_ST(uint32_t, ((uint8_t *)&t1013 + 8), t1011);
            GM_ST(uint32_t, ((uint8_t *)&t1013 + 12), t1012);
            uint8_t t1014 = 0;
            F.ret = t1013;
            goto gm_fret;
        }
        goto me3;
mn996:;
ma37:;
        uint8_t t1016 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1016 != ((uint8_t)20ULL)) goto mn1015;
        gm_tu97pd4fjd5oe t1017 = GM_LD(gm_tu97pd4fjd5oe, (((uint8_t *)t2) + 8));
        gm_l72 = t1017;
        {
            uint8_t t1020 = 0;
            uint64_t t1021 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 16));
            uint8_t t1022 = gm_f_fits16_1bnmjva(t1021);
            uint8_t t1023 = (uint8_t)!t1022;
            if (!t1023) {
                uint64_t t1024 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 8));
                uint8_t t1025 = gm_f_fits32_a5qt6pu(t1024);
                uint8_t t1026 = (uint8_t)!t1025;
                t1020 = t1026;
            } else t1020 = 1;
            if (!t1020) goto ie1019;
            {
                gm_tb1f35qilrmun t1028 = gm_f_bc_free_v10vouf();
                uint8_t t1029 = 0;
                F.ret = t1028;
                goto gm_fret;
            }
            goto ix1019;
ie1019:;
ix1019:;
        }
        {
            gm_t9qkh0rsooc8d t1030;
            GM_ST(uint8_t, ((uint8_t *)&t1030 + 0), ((uint8_t)84ULL));
            uint64_t t1031 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 16));
            uint16_t t1032 = (uint16_t)t1031;
            uint32_t t1033 = GM_LD(uint32_t, (((uint8_t *)&gm_l72) + 0));
            uint32_t t1034 = GM_LD(uint32_t, (((uint8_t *)&gm_l72) + 4));
            uint64_t t1035 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 8));
            uint32_t t1036 = (uint32_t)t1035;
            gm_tb1f35qilrmun t1037;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1037 + 0), t1030);
            GM_ST(uint8_t, ((uint8_t *)&t1037 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t1037 + 2), t1032);
            GM_ST(uint32_t, ((uint8_t *)&t1037 + 4), t1033);
            GM_ST(uint32_t, ((uint8_t *)&t1037 + 8), t1034);
            GM_ST(uint32_t, ((uint8_t *)&t1037 + 12), t1036);
            uint8_t t1038 = 0;
            F.ret = t1037;
            goto gm_fret;
        }
        goto me3;
mn1015:;
ma38:;
        uint8_t t1040 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1040 != ((uint8_t)23ULL)) goto mn1039;
        gm_t4h1llua034m2 t1041 = GM_LD(gm_t4h1llua034m2, (((uint8_t *)t2) + 8));
        gm_l73 = t1041;
        {
            uint64_t t1044 = GM_LD(uint64_t, (((uint8_t *)&gm_l73) + 8));
            uint8_t t1045 = gm_f_fits32_a5qt6pu(t1044);
            uint8_t t1046 = (uint8_t)!t1045;
            if (!t1046) goto ie1043;
            {
                gm_tb1f35qilrmun t1048 = gm_f_bc_free_v10vouf();
                uint8_t t1049 = 0;
                F.ret = t1048;
                goto gm_fret;
            }
            goto ix1043;
ie1043:;
ix1043:;
        }
        {
            gm_t9qkh0rsooc8d t1050;
            GM_ST(uint8_t, ((uint8_t *)&t1050 + 0), ((uint8_t)85ULL));
            uint32_t t1051 = GM_LD(uint32_t, (((uint8_t *)&gm_l73) + 0));
            uint32_t t1052 = GM_LD(uint32_t, (((uint8_t *)&gm_l73) + 4));
            uint64_t t1053 = GM_LD(uint64_t, (((uint8_t *)&gm_l73) + 8));
            uint32_t t1054 = (uint32_t)t1053;
            gm_tb1f35qilrmun t1055 = gm_f_bc3_m9eakek(t1050, t1051, t1052, t1054);
            uint8_t t1056 = 0;
            F.ret = t1055;
            goto gm_fret;
        }
        goto me3;
mn1039:;
ma39:;
        uint8_t t1058 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1058 != ((uint8_t)11ULL)) goto mn1057;
        gm_tiag95i83438g t1059 = GM_LD(gm_tiag95i83438g, (((uint8_t *)t2) + 8));
        F.l74 = t1059;
        gm_t9qkh0rsooc8d t1060;
        uint8_t t1062 = GM_LD(uint8_t, ((((uint8_t *)&F.l74) + 0) + 0));
        if (!t1062) goto ie1061;
        gm_t9qkh0rsooc8d t1064;
        GM_ST(uint8_t, ((uint8_t *)&t1064 + 0), ((uint8_t)87ULL));
        gm_t9qkh0rsooc8d t1065 = t1064;
        t1060 = t1065;
        goto ix1061;
ie1061:;
        gm_t9qkh0rsooc8d t1067;
        GM_ST(uint8_t, ((uint8_t *)&t1067 + 0), ((uint8_t)86ULL));
        gm_t9qkh0rsooc8d t1068 = t1067;
        t1060 = t1068;
ix1061:;
        uint32_t t1069 = GM_LD(uint32_t, ((((uint8_t *)&F.l74) + 0) + 4));
        uint16_t t1070 = (uint16_t)t1069;
        uint32_t t1071 = GM_LD(uint32_t, (((uint8_t *)&F.l74) + 16));
        uint32_t t1072 = GM_LD(uint32_t, (((uint8_t *)&F.l74) + 20));
        uint32_t t1073 = GM_LD(uint32_t, (((uint8_t *)&F.l74) + 24));
        gm_tb1f35qilrmun t1074;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1074 + 0), t1060);
        GM_ST(uint8_t, ((uint8_t *)&t1074 + 1), ((uint8_t)0ULL));
        GM_ST(uint16_t, ((uint8_t *)&t1074 + 2), t1070);
        GM_ST(uint32_t, ((uint8_t *)&t1074 + 4), t1071);
        GM_ST(uint32_t, ((uint8_t *)&t1074 + 8), t1072);
        GM_ST(uint32_t, ((uint8_t *)&t1074 + 12), t1073);
        uint8_t t1075 = 0;
        F.ret = t1074;
        goto gm_fret;
        goto me3;
mn1057:;
ma40:;
        uint8_t t1077 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1077 != ((uint8_t)12ULL)) goto mn1076;
        gm_thji9nptko1bl t1078 = GM_LD(gm_thji9nptko1bl, (((uint8_t *)t2) + 8));
        F.l75 = t1078;
        {
            uint8_t t1081 = GM_LD(uint8_t, (((uint8_t *)&F.l75) + 16));
            uint8_t t1082 = (uint8_t)!t1081;
            if (!t1082) goto ie1080;
            uint8_t t1083 = GM_LD(uint8_t, ((((uint8_t *)&F.l75) + 0) + 0));
            if (!t1083) goto ie1080;
            {
                gm_tb1f35qilrmun t1085 = gm_f_bc_free_v10vouf();
                uint8_t t1086 = 0;
                F.ret = t1085;
                goto gm_fret;
            }
            goto ix1080;
ie1080:;
ix1080:;
        }
        {
            gm_t9qkh0rsooc8d t1087;
            uint8_t t1089 = GM_LD(uint8_t, (((uint8_t *)&F.l75) + 16));
            if (!t1089) goto ie1088;
            gm_t9qkh0rsooc8d t1091;
            GM_ST(uint8_t, ((uint8_t *)&t1091 + 0), ((uint8_t)88ULL));
            gm_t9qkh0rsooc8d t1092 = t1091;
            t1087 = t1092;
            goto ix1088;
ie1088:;
            gm_t9qkh0rsooc8d t1094;
            GM_ST(uint8_t, ((uint8_t *)&t1094 + 0), ((uint8_t)89ULL));
            gm_t9qkh0rsooc8d t1095 = t1094;
            t1087 = t1095;
ix1088:;
            uint32_t t1096 = GM_LD(uint32_t, ((((uint8_t *)&F.l75) + 0) + 4));
            uint16_t t1097 = (uint16_t)t1096;
            uint32_t t1098 = GM_LD(uint32_t, (((uint8_t *)&F.l75) + 20));
            uint32_t t1099 = GM_LD(uint32_t, (((uint8_t *)&F.l75) + 24));
            uint32_t t1100 = GM_LD(uint32_t, (((uint8_t *)&F.l75) + 28));
            gm_tb1f35qilrmun t1101;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1101 + 0), t1087);
            GM_ST(uint8_t, ((uint8_t *)&t1101 + 1), ((uint8_t)0ULL));
            GM_ST(uint16_t, ((uint8_t *)&t1101 + 2), t1097);
            GM_ST(uint32_t, ((uint8_t *)&t1101 + 4), t1098);
            GM_ST(uint32_t, ((uint8_t *)&t1101 + 8), t1099);
            GM_ST(uint32_t, ((uint8_t *)&t1101 + 12), t1100);
            uint8_t t1102 = 0;
            F.ret = t1101;
            goto gm_fret;
        }
        goto me3;
mn1076:;
ma41:;
        uint8_t t1104 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1104 != ((uint8_t)25ULL)) goto mn1103;
        gm_t06kd2nmcgb3u t1105 = GM_LD(gm_t06kd2nmcgb3u, (((uint8_t *)t2) + 8));
        F.l76 = t1105;
        {
            uint8_t t1108 = ((GM_LD(uint8_t, (((uint8_t *)&F.l76) + 0) + 0) == ((uint8_t)4ULL)));
            if (!t1108) goto ie1107;
            {
                gm_t9qkh0rsooc8d t1110;
                uint8_t t1112 = GM_LD(uint8_t, ((((uint8_t *)&F.l76) + 4) + 0));
                if (!t1112) goto ie1111;
                gm_t9qkh0rsooc8d t1114;
                GM_ST(uint8_t, ((uint8_t *)&t1114 + 0), ((uint8_t)92ULL));
                gm_t9qkh0rsooc8d t1115 = t1114;
                t1110 = t1115;
                goto ix1111;
ie1111:;
                gm_t9qkh0rsooc8d t1117;
                GM_ST(uint8_t, ((uint8_t *)&t1117 + 0), ((uint8_t)90ULL));
                gm_t9qkh0rsooc8d t1118 = t1117;
                t1110 = t1118;
ix1111:;
                uint32_t t1119 = GM_LD(uint32_t, ((((uint8_t *)&F.l76) + 4) + 4));
                uint16_t t1120 = (uint16_t)t1119;
                uint32_t t1121 = GM_LD(uint32_t, (((uint8_t *)&F.l76) + 20));
                uint32_t t1122 = GM_LD(uint32_t, (((uint8_t *)&F.l76) + 24));
                uint32_t t1123 = GM_LD(uint32_t, (((uint8_t *)&F.l76) + 28));
                gm_tb1f35qilrmun t1124;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1124 + 0), t1110);
                GM_ST(uint8_t, ((uint8_t *)&t1124 + 1), ((uint8_t)0ULL));
                GM_ST(uint16_t, ((uint8_t *)&t1124 + 2), t1120);
                GM_ST(uint32_t, ((uint8_t *)&t1124 + 4), t1121);
                GM_ST(uint32_t, ((uint8_t *)&t1124 + 8), t1122);
                GM_ST(uint32_t, ((uint8_t *)&t1124 + 12), t1123);
                uint8_t t1125 = 0;
                F.ret = t1124;
                goto gm_fret;
            }
            goto ix1107;
ie1107:;
ix1107:;
        }
        {
            uint8_t t1127 = ((GM_LD(uint8_t, (((uint8_t *)&F.l76) + 0) + 0) == ((uint8_t)5ULL)));
            if (!t1127) goto ie1126;
            {
                gm_t9qkh0rsooc8d t1129;
                uint8_t t1131 = GM_LD(uint8_t, ((((uint8_t *)&F.l76) + 4) + 0));
                if (!t1131) goto ie1130;
                gm_t9qkh0rsooc8d t1133;
                GM_ST(uint8_t, ((uint8_t *)&t1133 + 0), ((uint8_t)93ULL));
                gm_t9qkh0rsooc8d t1134 = t1133;
                t1129 = t1134;
                goto ix1130;
ie1130:;
                gm_t9qkh0rsooc8d t1136;
                GM_ST(uint8_t, ((uint8_t *)&t1136 + 0), ((uint8_t)91ULL));
                gm_t9qkh0rsooc8d t1137 = t1136;
                t1129 = t1137;
ix1130:;
                uint32_t t1138 = GM_LD(uint32_t, ((((uint8_t *)&F.l76) + 4) + 4));
                uint16_t t1139 = (uint16_t)t1138;
                uint32_t t1140 = GM_LD(uint32_t, (((uint8_t *)&F.l76) + 20));
                uint32_t t1141 = GM_LD(uint32_t, (((uint8_t *)&F.l76) + 24));
                uint32_t t1142 = GM_LD(uint32_t, (((uint8_t *)&F.l76) + 28));
                gm_tb1f35qilrmun t1143;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1143 + 0), t1129);
                GM_ST(uint8_t, ((uint8_t *)&t1143 + 1), ((uint8_t)0ULL));
                GM_ST(uint16_t, ((uint8_t *)&t1143 + 2), t1139);
                GM_ST(uint32_t, ((uint8_t *)&t1143 + 4), t1140);
                GM_ST(uint32_t, ((uint8_t *)&t1143 + 8), t1141);
                GM_ST(uint32_t, ((uint8_t *)&t1143 + 12), t1142);
                uint8_t t1144 = 0;
                F.ret = t1143;
                goto gm_fret;
            }
            goto ix1126;
ie1126:;
ix1126:;
        }
        {
            gm_tb1f35qilrmun t1145 = gm_f_bc_free_v10vouf();
            uint8_t t1146 = 0;
            F.ret = t1145;
            goto gm_fret;
        }
        goto me3;
mn1103:;
ma42:;
        uint8_t t1148 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1148 != ((uint8_t)58ULL)) goto mn1147;
        gm_td7fbab3thbb2 t1149 = GM_LD(gm_td7fbab3thbb2, (((uint8_t *)t2) + 8));
        F.l77 = t1149;
        {
            uint8_t t1152 = 0;
            uint8_t t1153 = 0;
            uint8_t t1154 = 0;
            uint8_t t1155 = 0;
            uint8_t t1156 = gm_l1;
            uint8_t t1157 = (uint8_t)!t1156;
            if (!t1157) {
                uint32_t t1158 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 12));
                uint64_t t1159 = (uint64_t)t1158;
                uint8_t t1160 = gm_f_fits16_1bnmjva(t1159);
                uint8_t t1161 = (uint8_t)!t1160;
                t1155 = t1161;
            } else t1155 = 1;
            if (!t1155) {
                uint32_t t1162 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 16));
                uint64_t t1163 = (uint64_t)t1162;
                uint8_t t1164 = gm_f_fits16_1bnmjva(t1163);
                uint8_t t1165 = (uint8_t)!t1164;
                t1154 = t1165;
            } else t1154 = 1;
            if (!t1154) {
                uint64_t t1166 = GM_LD(uint64_t, (((uint8_t *)&F.l77) + 24));
                uint8_t t1167 = gm_f_fits16_1bnmjva(t1166);
                uint8_t t1168 = (uint8_t)!t1167;
                t1153 = t1168;
            } else t1153 = 1;
            if (!t1153) {
                uint8_t t1169 = 0;
                uint8_t t1170 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 20));
                if (t1170) {
                    uint32_t t1171 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 8));
                    uint32_t t1172 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 4));
                    uint32_t r1173; bool o1174 = __builtin_add_overflow(t1172, ((uint32_t)1ULL), &r1173);
                    if (o1174) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 498, 128);
                    uint32_t t1175 = r1173;
                    uint8_t t1176 = (!(t1171 == t1175));
                    t1169 = t1176;
                } else t1169 = 0;
                t1152 = t1169;
            } else t1152 = 1;
            if (!t1152) goto ie1151;
            {
                gm_tb1f35qilrmun t1178 = gm_f_bc_free_v10vouf();
                uint8_t t1179 = 0;
                F.ret = t1178;
                goto gm_fret;
            }
            goto ix1151;
ie1151:;
ix1151:;
        }
        {
            uint8_t t1180;
            uint8_t t1182 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 20));
            if (!t1182) goto ie1181;
            uint8_t t1184 = ((uint8_t)1ULL);
            t1180 = t1184;
            goto ix1181;
ie1181:;
            uint8_t t1186 = ((uint8_t)0ULL);
            t1180 = t1186;
ix1181:;
            uint8_t t1187;
            uint8_t t1189 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 21));
            if (!t1189) goto ie1188;
            uint8_t t1191 = ((uint8_t)2ULL);
            t1187 = t1191;
            goto ix1188;
ie1188:;
            uint8_t t1193 = ((uint8_t)0ULL);
            t1187 = t1193;
ix1188:;
            uint8_t t1194 = (uint8_t)(t1180 | t1187);
            gm_l78 = t1194;
        }
        {
            gm_t9qkh0rsooc8d t1195;
            GM_ST(uint8_t, ((uint8_t *)&t1195 + 0), ((uint8_t)97ULL));
            uint8_t t1196 = gm_l78;
            uint64_t t1197 = GM_LD(uint64_t, (((uint8_t *)&F.l77) + 24));
            uint16_t t1198 = (uint16_t)t1197;
            uint32_t t1199 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 0));
            uint32_t t1200 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 4));
            uint32_t t1201 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 12));
            uint32_t t1202 = GM_LD(uint32_t, (((uint8_t *)&F.l77) + 16));
            unsigned __int128 n1203 = (unsigned __int128)((uint64_t)16ULL);
            if (n1203 >= 32) gm_trap_shift((uint64_t)n1203, "u32", gm_src6unagr666v56, 500, 98);
            uint32_t r1204 = (uint32_t)((uint32_t)t1202 << (int)n1203);
            bool o1205 = (uint32_t)(r1204 >> (int)n1203) != t1202;
            if (o1205) gm_trap("integer overflow in `<<`", gm_src6unagr666v56, 500, 98);
            uint32_t t1206 = r1204;
            uint32_t t1207 = (uint32_t)(t1201 | t1206);
            gm_tb1f35qilrmun t1208;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1208 + 0), t1195);
            GM_ST(uint8_t, ((uint8_t *)&t1208 + 1), t1196);
            GM_ST(uint16_t, ((uint8_t *)&t1208 + 2), t1198);
            GM_ST(uint32_t, ((uint8_t *)&t1208 + 4), t1199);
            GM_ST(uint32_t, ((uint8_t *)&t1208 + 8), t1200);
            GM_ST(uint32_t, ((uint8_t *)&t1208 + 12), t1207);
            uint8_t t1209 = 0;
            F.ret = t1208;
            goto gm_fret;
        }
        goto me3;
mn1147:;
ma43:;
        uint8_t t1211 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1211 != ((uint8_t)43ULL)) goto mn1210;
        uint32_t t1212 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l79 = t1212;
        uint64_t t1213 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l80 = t1213;
        {
            uint8_t t1216 = 0;
            uint8_t t1217 = 0;
            uint64_t t1218 = gm_l80;
            uint8_t t1219 = ((t1218 == ((uint64_t)0ULL)));
            if (!t1219) {
                uint64_t t1220 = gm_l80;
                uint64_t t1221 = gm_l80;
                uint64_t r1222; bool o1223 = __builtin_sub_overflow(t1221, ((uint64_t)1ULL), &r1222);
                if (o1223) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 503, 39);
                uint64_t t1224 = r1222;
                uint64_t t1225 = (uint64_t)(t1220 & t1224);
                uint8_t t1226 = (!(t1225 == ((uint64_t)0ULL)));
                t1217 = t1226;
            } else t1217 = 1;
            if (!t1217) {
                uint64_t t1227 = gm_l80;
                uint8_t t1228 = gm_f_fits32_a5qt6pu(t1227);
                uint8_t t1229 = (uint8_t)!t1228;
                t1216 = t1229;
            } else t1216 = 1;
            if (!t1216) goto ie1215;
            {
                gm_tb1f35qilrmun t1231 = gm_f_bc_free_v10vouf();
                uint8_t t1232 = 0;
                F.ret = t1231;
                goto gm_fret;
            }
            goto ix1215;
ie1215:;
ix1215:;
        }
        {
            gm_t9qkh0rsooc8d t1233;
            GM_ST(uint8_t, ((uint8_t *)&t1233 + 0), ((uint8_t)98ULL));
            uint32_t t1234 = gm_l79;
            uint64_t t1235 = gm_l80;
            uint64_t r1236; bool o1237 = __builtin_sub_overflow(t1235, ((uint64_t)1ULL), &r1236);
            if (o1237) gm_trap("integer overflow in `-`", gm_src6unagr666v56, 504, 39);
            uint64_t t1238 = r1236;
            uint32_t t1239 = (uint32_t)t1238;
            gm_tb1f35qilrmun t1240 = gm_f_bc3_m9eakek(t1233, t1234, t1239, ((uint32_t)0ULL));
            uint8_t t1241 = 0;
            F.ret = t1240;
            goto gm_fret;
        }
        goto me3;
mn1210:;
ma44:;
        uint8_t t1243 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1243 != ((uint8_t)41ULL)) goto mn1242;
        gm_t04llc5uen0qo t1244 = GM_LD(gm_t04llc5uen0qo, (((uint8_t *)t2) + 8));
        gm_l81 = t1244;
        {
            uint8_t t1247 = 0;
            uint32_t t1248 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 12));
            uint64_t t1249 = (uint64_t)t1248;
            uint8_t t1250 = gm_f_fits16_1bnmjva(t1249);
            uint8_t t1251 = (uint8_t)!t1250;
            if (!t1251) {
                uint32_t t1252 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 16));
                uint8_t t1253 = gm_f_bc_align_6s4vol1(t1252);
                uint8_t t1254 = (uint8_t)!t1253;
                t1247 = t1254;
            } else t1247 = 1;
            if (!t1247) goto ie1246;
            {
                gm_tb1f35qilrmun t1256 = gm_f_bc_free_v10vouf();
                uint8_t t1257 = 0;
                F.ret = t1256;
                goto gm_fret;
            }
            goto ix1246;
ie1246:;
ix1246:;
        }
        {
            gm_t9qkh0rsooc8d t1258;
            GM_ST(uint8_t, ((uint8_t *)&t1258 + 0), ((uint8_t)99ULL));
            uint32_t t1259 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 16));
            uint8_t t1260 = gm_f_log2_acbegp6(t1259);
            uint32_t t1261 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 12));
            uint16_t t1262 = (uint16_t)t1261;
            uint32_t t1263 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 0));
            uint32_t t1264 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 4));
            uint32_t t1265 = GM_LD(uint32_t, (((uint8_t *)&gm_l81) + 8));
            gm_tb1f35qilrmun t1266;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1266 + 0), t1258);
            GM_ST(uint8_t, ((uint8_t *)&t1266 + 1), t1260);
            GM_ST(uint16_t, ((uint8_t *)&t1266 + 2), t1262);
            GM_ST(uint32_t, ((uint8_t *)&t1266 + 4), t1263);
            GM_ST(uint32_t, ((uint8_t *)&t1266 + 8), t1264);
            GM_ST(uint32_t, ((uint8_t *)&t1266 + 12), t1265);
            uint8_t t1267 = 0;
            F.ret = t1266;
            goto gm_fret;
        }
        goto me3;
mn1242:;
ma45:;
        uint8_t t1269 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1269 != ((uint8_t)42ULL)) goto mn1268;
        gm_t04llc5uen0qo t1270 = GM_LD(gm_t04llc5uen0qo, (((uint8_t *)t2) + 8));
        gm_l82 = t1270;
        {
            uint8_t t1273 = 0;
            uint32_t t1274 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 12));
            uint64_t t1275 = (uint64_t)t1274;
            uint8_t t1276 = gm_f_fits16_1bnmjva(t1275);
            uint8_t t1277 = (uint8_t)!t1276;
            if (!t1277) {
                uint32_t t1278 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 16));
                uint8_t t1279 = gm_f_bc_align_6s4vol1(t1278);
                uint8_t t1280 = (uint8_t)!t1279;
                t1273 = t1280;
            } else t1273 = 1;
            if (!t1273) goto ie1272;
            {
                gm_tb1f35qilrmun t1282 = gm_f_bc_free_v10vouf();
                uint8_t t1283 = 0;
                F.ret = t1282;
                goto gm_fret;
            }
            goto ix1272;
ie1272:;
ix1272:;
        }
        {
            gm_t9qkh0rsooc8d t1284;
            GM_ST(uint8_t, ((uint8_t *)&t1284 + 0), ((uint8_t)100ULL));
            uint32_t t1285 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 16));
            uint8_t t1286 = gm_f_log2_acbegp6(t1285);
            uint32_t t1287 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 12));
            uint16_t t1288 = (uint16_t)t1287;
            uint32_t t1289 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 0));
            uint32_t t1290 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 8));
            uint32_t t1291 = GM_LD(uint32_t, (((uint8_t *)&gm_l82) + 4));
            gm_tb1f35qilrmun t1292;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1292 + 0), t1284);
            GM_ST(uint8_t, ((uint8_t *)&t1292 + 1), t1286);
            GM_ST(uint16_t, ((uint8_t *)&t1292 + 2), t1288);
            GM_ST(uint32_t, ((uint8_t *)&t1292 + 4), t1289);
            GM_ST(uint32_t, ((uint8_t *)&t1292 + 8), t1290);
            GM_ST(uint32_t, ((uint8_t *)&t1292 + 12), t1291);
            uint8_t t1293 = 0;
            F.ret = t1292;
            goto gm_fret;
        }
        goto me3;
mn1268:;
ma46:;
        uint8_t t1295 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1295 != ((uint8_t)52ULL)) goto mn1294;
        uint32_t t1296 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l83 = t1296;
        uint32_t t1297 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l84 = t1297;
        uint32_t t1298 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l85 = t1298;
        gm_t9qkh0rsooc8d t1299;
        GM_ST(uint8_t, ((uint8_t *)&t1299 + 0), ((uint8_t)101ULL));
        uint32_t t1300 = gm_l83;
        uint32_t t1301 = gm_l84;
        uint32_t t1302 = gm_l85;
        gm_tb1f35qilrmun t1303 = gm_f_bc3_m9eakek(t1299, t1300, t1301, t1302);
        uint8_t t1304 = 0;
        F.ret = t1303;
        goto gm_fret;
        goto me3;
mn1294:;
ma47:;
        uint8_t t1306 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1306 != ((uint8_t)57ULL)) goto mn1305;
        uint32_t t1307 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l86 = t1307;
        uint32_t t1308 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l87 = t1308;
        gm_t9qkh0rsooc8d t1309;
        GM_ST(uint8_t, ((uint8_t *)&t1309 + 0), ((uint8_t)102ULL));
        uint32_t t1310 = gm_l86;
        uint32_t t1311 = gm_l87;
        gm_tb1f35qilrmun t1312 = gm_f_bc3_m9eakek(t1309, t1310, t1311, ((uint32_t)0ULL));
        uint8_t t1313 = 0;
        F.ret = t1312;
        goto gm_fret;
        goto me3;
mn1305:;
ma48:;
        uint8_t t1315 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1315 != ((uint8_t)76ULL)) goto mn1314;
        uint32_t t1316 = GM_LD(uint32_t, (((uint8_t *)t2) + 8));
        gm_l88 = t1316;
        gm_t9qkh0rsooc8d t1317;
        GM_ST(uint8_t, ((uint8_t *)&t1317 + 0), ((uint8_t)103ULL));
        uint32_t t1318 = gm_l88;
        gm_tb1f35qilrmun t1319 = gm_f_bc3_m9eakek(t1317, t1318, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t1320 = 0;
        F.ret = t1319;
        goto gm_fret;
        goto me3;
mn1314:;
ma49:;
        uint8_t t1322 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1322 != ((uint8_t)77ULL)) goto mn1321;
        uint32_t t1323 = GM_LD(uint32_t, (((uint8_t *)t2) + 8));
        gm_l89 = t1323;
        gm_t9qkh0rsooc8d t1324;
        GM_ST(uint8_t, ((uint8_t *)&t1324 + 0), ((uint8_t)104ULL));
        uint32_t t1325 = gm_l89;
        gm_tb1f35qilrmun t1326 = gm_f_bc3_m9eakek(t1324, t1325, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t1327 = 0;
        F.ret = t1326;
        goto gm_fret;
        goto me3;
mn1321:;
ma50:;
        uint8_t t1331 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1331 != ((uint8_t)47ULL)) goto pa1330;
        goto po1329;
pa1330:;
        uint8_t t1333 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1333 != ((uint8_t)50ULL)) goto pa1332;
        goto po1329;
pa1332:;
        uint8_t t1335 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1335 != ((uint8_t)48ULL)) goto pa1334;
        goto po1329;
pa1334:;
        goto mn1328;
po1329:;
        gm_t9qkh0rsooc8d t1336;
        GM_ST(uint8_t, ((uint8_t *)&t1336 + 0), ((uint8_t)105ULL));
        uint32_t t1337 = GM_LD(uint32_t, gm_cl7gtp51edk);
        gm_tb1f35qilrmun t1338 = gm_f_bc3_m9eakek(t1336, t1337, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t1339 = 0;
        F.ret = t1338;
        goto gm_fret;
        goto me3;
mn1328:;
ma51:;
        uint8_t t1341 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1341 != ((uint8_t)5ULL)) goto mn1340;
        gm_tn3qmqcq7vsk9 t1342 = GM_LD(gm_tn3qmqcq7vsk9, (((uint8_t *)t2) + 8));
        gm_l90 = t1342;
        {
            uint8_t t1345 = 0;
            uint8_t t1346 = 0;
            uint8_t t1347 = gm_l1;
            uint8_t t1348 = (uint8_t)!t1347;
            if (!t1348) {
                uint32_t t1349 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 4));
                uint8_t t1350 = (!(t1349 == ((uint32_t)1ULL)));
                t1346 = t1350;
            } else t1346 = 1;
            if (!t1346) {
                uint32_t t1351 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 16));
                uint64_t t1352 = (uint64_t)t1351;
                uint8_t t1353 = gm_f_word_size_mu6ni08(t1352);
                uint8_t t1354 = (uint8_t)!t1353;
                t1345 = t1354;
            } else t1345 = 1;
            if (!t1345) goto ie1344;
            {
                gm_tb1f35qilrmun t1356 = gm_f_bc_free_v10vouf();
                uint8_t t1357 = 0;
                F.ret = t1356;
                goto gm_fret;
            }
            goto ix1344;
ie1344:;
ix1344:;
        }
        {
            gm_t9qkh0rsooc8d t1358;
            GM_ST(uint8_t, ((uint8_t *)&t1358 + 0), ((uint8_t)94ULL));
            uint32_t t1359 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 16));
            uint8_t t1360 = (uint8_t)t1359;
            uint32_t t1361 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 0));
            uint32_t t1362 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 8));
            uint32_t t1363 = GM_LD(uint32_t, (((uint8_t *)&gm_l90) + 12));
            gm_tb1f35qilrmun t1364;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1364 + 0), t1358);
            GM_ST(uint8_t, ((uint8_t *)&t1364 + 1), t1360);
            GM_ST(uint16_t, ((uint8_t *)&t1364 + 2), ((uint16_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t1364 + 4), t1361);
            GM_ST(uint32_t, ((uint8_t *)&t1364 + 8), t1362);
            GM_ST(uint32_t, ((uint8_t *)&t1364 + 12), t1363);
            uint8_t t1365 = 0;
            F.ret = t1364;
            goto gm_fret;
        }
        goto me3;
mn1340:;
ma52:;
        uint8_t t1367 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1367 != ((uint8_t)13ULL)) goto mn1366;
        gm_tn3qmqcq7vsk9 t1368 = GM_LD(gm_tn3qmqcq7vsk9, (((uint8_t *)t2) + 8));
        gm_l91 = t1368;
        {
            uint8_t t1371 = 0;
            uint8_t t1372 = 0;
            uint8_t t1373 = gm_l1;
            uint8_t t1374 = (uint8_t)!t1373;
            if (!t1374) {
                uint32_t t1375 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 0));
                uint8_t t1376 = (!(t1375 == ((uint32_t)1ULL)));
                t1372 = t1376;
            } else t1372 = 1;
            if (!t1372) {
                uint32_t t1377 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 12));
                uint64_t t1378 = (uint64_t)t1377;
                uint8_t t1379 = gm_f_word_size_mu6ni08(t1378);
                uint8_t t1380 = (uint8_t)!t1379;
                t1371 = t1380;
            } else t1371 = 1;
            if (!t1371) goto ie1370;
            {
                gm_tb1f35qilrmun t1382 = gm_f_bc_free_v10vouf();
                uint8_t t1383 = 0;
                F.ret = t1382;
                goto gm_fret;
            }
            goto ix1370;
ie1370:;
ix1370:;
        }
        {
            gm_t9qkh0rsooc8d t1384;
            GM_ST(uint8_t, ((uint8_t *)&t1384 + 0), ((uint8_t)95ULL));
            uint32_t t1385 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 12));
            uint8_t t1386 = (uint8_t)t1385;
            uint32_t t1387 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 4));
            uint32_t t1388 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 20));
            uint32_t t1389 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 8));
            gm_tb1f35qilrmun t1390;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1390 + 0), t1384);
            GM_ST(uint8_t, ((uint8_t *)&t1390 + 1), t1386);
            GM_ST(uint16_t, ((uint8_t *)&t1390 + 2), ((uint16_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t1390 + 4), t1387);
            GM_ST(uint32_t, ((uint8_t *)&t1390 + 8), t1388);
            GM_ST(uint32_t, ((uint8_t *)&t1390 + 12), t1389);
            uint8_t t1391 = 0;
            F.ret = t1390;
            goto gm_fret;
        }
        goto me3;
mn1366:;
ma53:;
        uint8_t t1393 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1393 != ((uint8_t)72ULL)) goto mn1392;
        uint32_t t1394 = GM_LD(uint32_t, (((uint8_t *)t2) + 8));
        gm_l92 = t1394;
        gm_t9qkh0rsooc8d t1395;
        GM_ST(uint8_t, ((uint8_t *)&t1395 + 0), ((uint8_t)106ULL));
        uint32_t t1396 = gm_l92;
        gm_tb1f35qilrmun t1397 = gm_f_bc3_m9eakek(t1395, t1396, ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t1398 = 0;
        F.ret = t1397;
        goto gm_fret;
        goto me3;
mn1392:;
ma54:;
        uint8_t t1400 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1400 != ((uint8_t)75ULL)) goto mn1399;
        gm_t9qkh0rsooc8d t1401;
        GM_ST(uint8_t, ((uint8_t *)&t1401 + 0), ((uint8_t)107ULL));
        gm_tb1f35qilrmun t1402 = gm_f_bc3_m9eakek(t1401, ((uint32_t)0ULL), ((uint32_t)0ULL), ((uint32_t)0ULL));
        uint8_t t1403 = 0;
        F.ret = t1402;
        goto gm_fret;
        goto me3;
mn1399:;
ma55:;
        uint8_t t1405 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1405 != ((uint8_t)74ULL)) goto mn1404;
        uint32_t t1406 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 0));
        gm_l93 = t1406;
        uint32_t t1407 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 4));
        gm_l94 = t1407;
        gm_t9qkh0rsooc8d t1408;
        GM_ST(uint8_t, ((uint8_t *)&t1408 + 0), ((uint8_t)108ULL));
        uint32_t t1409 = gm_l93;
        uint32_t t1410 = gm_l94;
        gm_tb1f35qilrmun t1411 = gm_f_bc3_m9eakek(t1408, t1409, t1410, ((uint32_t)0ULL));
        uint8_t t1412 = 0;
        F.ret = t1411;
        goto gm_fret;
        goto me3;
mn1404:;
ma56:;
        uint8_t t1416 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1416 != ((uint8_t)1ULL)) goto pa1415;
        goto po1414;
pa1415:;
        uint8_t t1418 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1418 != ((uint8_t)2ULL)) goto pa1417;
        goto po1414;
pa1417:;
        uint8_t t1420 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1420 != ((uint8_t)59ULL)) goto pa1419;
        goto po1414;
pa1419:;
        uint8_t t1422 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1422 != ((uint8_t)51ULL)) goto pa1421;
        goto po1414;
pa1421:;
        uint8_t t1424 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1424 != ((uint8_t)61ULL)) goto pa1423;
        goto po1414;
pa1423:;
        uint8_t t1426 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1426 != ((uint8_t)62ULL)) goto pa1425;
        goto po1414;
pa1425:;
        uint8_t t1428 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1428 != ((uint8_t)63ULL)) goto pa1427;
        goto po1414;
pa1427:;
        uint8_t t1430 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1430 != ((uint8_t)64ULL)) goto pa1429;
        goto po1414;
pa1429:;
        uint8_t t1432 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1432 != ((uint8_t)69ULL)) goto pa1431;
        goto po1414;
pa1431:;
        uint8_t t1434 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1434 != ((uint8_t)70ULL)) goto pa1433;
        goto po1414;
pa1433:;
        uint8_t t1436 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1436 != ((uint8_t)73ULL)) goto pa1435;
        goto po1414;
pa1435:;
        uint8_t t1438 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1438 != ((uint8_t)66ULL)) goto pa1437;
        goto po1414;
pa1437:;
        uint8_t t1440 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1440 != ((uint8_t)67ULL)) goto pa1439;
        goto po1414;
pa1439:;
        uint8_t t1442 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1442 != ((uint8_t)68ULL)) goto pa1441;
        goto po1414;
pa1441:;
        uint8_t t1444 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1444 != ((uint8_t)80ULL)) goto pa1443;
        goto po1414;
pa1443:;
        uint8_t t1446 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1446 != ((uint8_t)81ULL)) goto pa1445;
        goto po1414;
pa1445:;
        uint8_t t1448 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1448 != ((uint8_t)82ULL)) goto pa1447;
        goto po1414;
pa1447:;
        uint8_t t1450 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1450 != ((uint8_t)83ULL)) goto pa1449;
        goto po1414;
pa1449:;
        uint8_t t1452 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t1452 != ((uint8_t)84ULL)) goto pa1451;
        goto po1414;
pa1451:;
        goto mn1413;
po1414:;
        gm_t9qkh0rsooc8d t1453;
        GM_ST(uint8_t, ((uint8_t *)&t1453 + 0), ((uint8_t)1ULL));
        gm_tb1f35qilrmun t1454;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t1454 + 0), t1453);
        GM_ST(uint8_t, ((uint8_t *)&t1454 + 1), ((uint8_t)0ULL));
        GM_ST(uint16_t, ((uint8_t *)&t1454 + 2), ((uint16_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t1454 + 4), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t1454 + 8), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t1454 + 12), ((uint32_t)0ULL));
        uint8_t t1455 = 0;
        F.ret = t1454;
        goto gm_fret;
        goto me3;
mn1413:;
ma57:;
        gm_tb1f35qilrmun t1457 = gm_f_bc_free_v10vouf();
        uint8_t t1458 = 0;
        F.ret = t1457;
        goto gm_fret;
        goto me3;
mn1456:;
ma58:;
        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 361, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_bc_pc_vubo137(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fits16_1bnmjva(uint64_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fits32_a5qt6pu(uint64_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_in_memory_t2cr4s3(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld1_adb1ani(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld2_gce8h7a(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld4_lj8nmpt(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ld8_ecs41ho(void * a0);
extern __attribute__((aligned(4))) uint64_t gm_f_ldn_94iknnm(void * a0, uint8_t a1);
extern __attribute__((aligned(4))) uint8_t gm_f_log2_acbegp6(uint32_t a0);
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_pick_kkl1a7m(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2, gm_t9qkh0rsooc8d a3);
extern __attribute__((aligned(4))) void gm_f_st1_aqrj474(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_st2_lkdmkcq(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_st4_38u38jn(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_st8_6aops52(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_stn_cbrtno6(void * a0, uint8_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vm_assemble_k5irtcr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vm_assemble_k5irtcr F;
    void * gm_l0; /* m */
    void * gm_l1; /* info */
    void * gm_l2; /* code */
    gm_b16_8 gm_l3; /* out */
    uint8_t gm_l4; /* frameless */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint32_t gm_l8; /* nsites */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* k */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    gm_b16_8 gm_l15; /* hidden */
    uint64_t gm_l16; /* n */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* l */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    uint32_t gm_l23; /* r */
    uint32_t gm_l24; /* local */
    GM_STACK_CHECK(&F, 1819u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_tb1f35qilrmun t4 = gm_f_bc_free_v10vouf();
        gm_tabb710d9mq2e t5;
        gm_f_make_fill_0sgj1dq((void *)&t5, (void *)NULL, cx, NULL, t3, t4, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kfh7mc23f4a)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src6unagr666v56, 548, 12);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l3 = t6;
    }
    {
        gm_l4 = ((uint8_t)1);
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t8 = gm_l2;
            uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 0) + 8);
            gm_l5 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l6;
                uint64_t t12 = gm_l5;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l6;
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                    gm_l7 = t19;
                    {
                        uint64_t t21 = gm_l7;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src6unagr666v56, 551, 9);
                        uint8_t *t24 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 16);
                        uint64_t t25 = gm_l7;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l2;
                        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
                        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_src6unagr666v56, 551, 25);
                        void * t29 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 0)) + t26 * 56);
                        void * t30 = gm_l0;
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t30) + 90));
                        gm_tb1f35qilrmun t32 = gm_f_bc_of_ga25123(t29, t31);
                        GM_ST(gm_tb1f35qilrmun, t24, t32);
                    }
                    {
                        uint64_t t34 = gm_l7;
                        uint64_t t35 = (uintptr_t)t34;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_src6unagr666v56, 552, 12);
                        uint8_t t37 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t35 * 16) + 0) + 0) == ((uint8_t)1ULL)));
                        if (!t37) goto ie33;
                        uint64_t t38 = gm_l7;
                        uint64_t t39 = (uintptr_t)t38;
                        void * t40 = gm_l2;
                        uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 0) + 8);
                        if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_src6unagr666v56, 552, 52);
                        void * t42 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t40) + 0)) + t39 * 56);
                        uint8_t t43 = gm_f_in_memory_t2cr4s3(t42);
                        uint8_t t44 = (uint8_t)!t43;
                        if (!t44) goto ie33;
                        {
                            gm_l4 = ((uint8_t)0);
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 550, 5);
me17:;
                }
lc10:;
                {
                    uint64_t t47 = gm_l6;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l6 = t50;
                }
            }
lb10:;
        }
    }
    {
        gm_l8 = ((uint32_t)0ULL);
    }
    {
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            void * t52 = gm_l2;
            uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 0) + 8);
            gm_l9 = t53;
        }
        {
            for (;;) {
                uint64_t t55 = gm_l10;
                uint64_t t56 = gm_l9;
                uint8_t t57 = (t55 < t56);
                if (!t57) goto lb54;
                {
                    uint64_t t60 = gm_l10;
                    uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&t60));
                    gm_l11 = t63;
                    {
                        uint8_t t66 = 0;
                        uint8_t t67 = 0;
                        uint64_t t68 = gm_l11;
                        uint64_t t69 = (uintptr_t)t68;
                        void * t70 = gm_l2;
                        uint64_t t71 = GM_LD(uintptr_t, (((uint8_t *)t70) + 0) + 8);
                        if (t69 >= (uintptr_t)(t71)) gm_trap_index(t69, t71, gm_src6unagr666v56, 557, 12);
                        uint8_t t72;
                        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t70) + 0)) + t69 * 56) + 0));
                        if (t75 != ((uint8_t)47ULL)) goto mn74;
                        t72 = ((uint8_t)1);
                        goto me73;
mn74:;
                        t72 = ((uint8_t)0);
                        goto me73;
mn76:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 557, 12);
me73:;
                        if (!t72) {
                            uint64_t t77 = gm_l11;
                            uint64_t t78 = (uintptr_t)t77;
                            void * t79 = gm_l2;
                            uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)t79) + 0) + 8);
                            if (t78 >= (uintptr_t)(t80)) gm_trap_index(t78, t80, gm_src6unagr666v56, 557, 39);
                            uint8_t t81;
                            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t79) + 0)) + t78 * 56) + 0));
                            if (t84 != ((uint8_t)50ULL)) goto mn83;
                            t81 = ((uint8_t)1);
                            goto me82;
mn83:;
                            t81 = ((uint8_t)0);
                            goto me82;
mn85:;
                            gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 557, 39);
me82:;
                            t67 = t81;
                        } else t67 = 1;
                        if (!t67) {
                            uint64_t t86 = gm_l11;
                            uint64_t t87 = (uintptr_t)t86;
                            void * t88 = gm_l2;
                            uint64_t t89 = GM_LD(uintptr_t, (((uint8_t *)t88) + 0) + 8);
                            if (t87 >= (uintptr_t)(t89)) gm_trap_index(t87, t89, gm_src6unagr666v56, 557, 69);
                            uint8_t t90;
                            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t88) + 0)) + t87 * 56) + 0));
                            if (t93 != ((uint8_t)48ULL)) goto mn92;
                            t90 = ((uint8_t)1);
                            goto me91;
mn92:;
                            t90 = ((uint8_t)0);
                            goto me91;
mn94:;
                            gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 557, 69);
me91:;
                            t66 = t90;
                        } else t66 = 1;
                        if (!t66) goto ie65;
                        {
                            uint64_t t96 = gm_l11;
                            uint64_t t97 = (uintptr_t)t96;
                            uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_src6unagr666v56, 558, 13);
                            uint32_t t99 = gm_l8;
                            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t97 * 16) + 4), t99);
                        }
                        {
                            uint32_t t100 = gm_l8;
                            uint32_t r101; bool o102 = __builtin_add_overflow(t100, ((uint32_t)1ULL), &r101);
                            if (o102) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 559, 13);
                            uint32_t t103 = r101;
                            gm_l8 = t103;
                        }
                        goto ix65;
ie65:;
ix65:;
                    }
                    goto me61;
mn62:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 556, 5);
me61:;
                }
lc54:;
                {
                    uint64_t t105 = gm_l10;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l10 = t108;
                }
            }
lb54:;
        }
    }
    {
        void * t109 = gm_l2;
        uint8_t *t110 = (((uint8_t *)t109) + 72);
        uint32_t t111 = gm_l8;
        uint64_t t112 = (uint64_t)t111;
        gm_tu1mv36lldn2v t113;
        memset((uint8_t *)&t113 + 2, 0, 2);
        memset((uint8_t *)&t113 + 20, 0, 4);
        memset((uint8_t *)&t113 + 60, 0, 4);
        memset((uint8_t *)&t113 + 84, 0, 4);
        memset((uint8_t *)&t113 + 116, 0, 4);
        GM_ST(uint8_t, (uint8_t *)&t113 + 0, ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&t113 + 1, ((uint8_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t113 + 4, ((uint32_t)0ULL));
        uint32_t t114 = GM_LD(uint32_t, gm_cl7gtp51edk);
        GM_ST(uint32_t, (uint8_t *)&t113 + 8, t114);
        GM_ST(uint32_t, (uint8_t *)&t113 + 12, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t113 + 16, ((uint32_t)0ULL));
        GM_ST(void *, (uint8_t *)&t113 + 24, ((void *)0));
        GM_ST(uint64_t, (uint8_t *)&t113 + 32, ((uint64_t)0ULL));
        gm_b16_8 t115 = gm_f_empty_slice_thu1fog();
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 40, t115);
        GM_ST(uint32_t, (uint8_t *)&t113 + 56, ((uint32_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&t113 + 64, ((uint64_t)0ULL));
        GM_ST(void *, (uint8_t *)&t113 + 72, ((void *)0));
        GM_ST(uint32_t, (uint8_t *)&t113 + 80, ((uint32_t)0ULL));
        gm_th2lqbo77pvsc t116;
        memset(&t116, 0, sizeof t116);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t113 + 88, t116);
        GM_ST(uint32_t, (uint8_t *)&t113 + 112, ((uint32_t)0ULL));
        GM_ST(void *, (uint8_t *)&t113 + 120, ((void *)0));
        gm_tabb710d9mq2e t117;
        gm_f_make_fill_kjs404n((void *)&t117, (void *)NULL, cx, NULL, t112, &t113, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k68pmlng4vp)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src6unagr666v56, 562, 18);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
        GM_ST(gm_b16_8, t110, t118);
    }
    {
        void * t119 = gm_l2;
        gm_b16_8 t120 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t119) + 16), t120);
    }
    {
        void * t121 = gm_l2;
        uint8_t t122 = gm_l4;
        GM_ST(uint8_t, (((uint8_t *)t121) + 32), t122);
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            void * t124 = gm_l2;
            uint64_t t125 = GM_LD(uintptr_t, (((uint8_t *)t124) + 0) + 8);
            gm_l12 = t125;
        }
        {
            for (;;) {
                uint64_t t127 = gm_l13;
                uint64_t t128 = gm_l12;
                uint8_t t129 = (t127 < t128);
                if (!t129) goto lb126;
                {
                    uint64_t t132 = gm_l13;
                    uint64_t t135 = GM_LD(uint64_t, ((uint8_t *)&t132));
                    gm_l14 = t135;
                    {
                        uint8_t t138 = 0;
                        uint8_t t139 = 0;
                        uint64_t t140 = gm_l14;
                        uint64_t t141 = (uintptr_t)t140;
                        void * t142 = gm_l2;
                        uint64_t t143 = GM_LD(uintptr_t, (((uint8_t *)t142) + 0) + 8);
                        if (t141 >= (uintptr_t)(t143)) gm_trap_index(t141, t143, gm_src6unagr666v56, 567, 12);
                        uint8_t t144;
                        uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t142) + 0)) + t141 * 56) + 0));
                        if (t147 != ((uint8_t)5ULL)) goto mn146;
                        t144 = ((uint8_t)1);
                        goto me145;
mn146:;
                        t144 = ((uint8_t)0);
                        goto me145;
mn148:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 567, 12);
me145:;
                        if (!t144) {
                            uint64_t t149 = gm_l14;
                            uint64_t t150 = (uintptr_t)t149;
                            void * t151 = gm_l2;
                            uint64_t t152 = GM_LD(uintptr_t, (((uint8_t *)t151) + 0) + 8);
                            if (t150 >= (uintptr_t)(t152)) gm_trap_index(t150, t152, gm_src6unagr666v56, 567, 42);
                            uint8_t t153;
                            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t151) + 0)) + t150 * 56) + 0));
                            if (t156 != ((uint8_t)13ULL)) goto mn155;
                            t153 = ((uint8_t)1);
                            goto me154;
mn155:;
                            t153 = ((uint8_t)0);
                            goto me154;
mn157:;
                            gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 567, 42);
me154:;
                            t139 = t153;
                        } else t139 = 1;
                        if (!t139) {
                            uint64_t t158 = gm_l14;
                            uint64_t t159 = (uintptr_t)t158;
                            void * t160 = gm_l2;
                            uint64_t t161 = GM_LD(uintptr_t, (((uint8_t *)t160) + 0) + 8);
                            if (t159 >= (uintptr_t)(t161)) gm_trap_index(t159, t161, gm_src6unagr666v56, 567, 75);
                            uint8_t t162;
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t160) + 0)) + t159 * 56) + 0));
                            if (t165 != ((uint8_t)14ULL)) goto mn164;
                            t162 = ((uint8_t)1);
                            goto me163;
mn164:;
                            t162 = ((uint8_t)0);
                            goto me163;
mn166:;
                            gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 567, 75);
me163:;
                            t138 = t162;
                        } else t138 = 1;
                        if (!t138) goto ie137;
                        {
                            void * t168 = gm_l2;
                            void * t169 = gm_l2;
                            uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)t169) + 104));
                            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src6unagr666v56, 568, 29);
                            uint64_t t171 = (uint64_t)(t170 / ((uint64_t)8ULL));
                            uint32_t t172 = (uint32_t)t171;
                            GM_ST(uint32_t, (((uint8_t *)t168) + 88), t172);
                        }
                        {
                            void * t173 = gm_l2;
                            void * t174 = gm_l2;
                            uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)t174) + 104));
                            uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)8ULL), &r176);
                            if (o177) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 569, 13);
                            uint64_t t178 = r176;
                            GM_ST(uint64_t, (((uint8_t *)t173) + 104), t178);
                        }
                        {
                            goto lb126;
                        }
                        goto ix137;
ie137:;
ix137:;
                    }
                    goto me133;
mn134:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 566, 5);
me133:;
                }
lc126:;
                {
                    uint64_t t180 = gm_l13;
                    uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                    uint64_t t183 = r181;
                    gm_l13 = t183;
                }
            }
lb126:;
        }
    }
    {
        uint8_t t185 = gm_l4;
        uint8_t t186 = (uint8_t)!t185;
        if (!t186) goto ie184;
        {
            gm_b0_1 t188;
            uint8_t t189 = 0;
            F.ret = t188;
            goto gm_fret;
        }
        goto ix184;
ie184:;
ix184:;
    }
    {
        void * t190 = gm_l1;
        uint64_t t191 = GM_LD(uintptr_t, (((uint8_t *)t190) + 120) + 8);
        uint32_t t192 = GM_LD(uint32_t, gm_cl7gtp51edk);
        gm_tabb710d9mq2e t193;
        gm_f_make_fill_bqbf5rp((void *)&t193, (void *)NULL, cx, NULL, t191, t192, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kiiubtvdrqu)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_src6unagr666v56, 574, 15);
        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
        gm_l15 = t194;
    }
    {
        gm_l16 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            void * t196 = gm_l1;
            uint64_t t197 = GM_LD(uintptr_t, (((uint8_t *)t196) + 120) + 8);
            gm_l17 = t197;
        }
        {
            for (;;) {
                uint64_t t199 = gm_l18;
                uint64_t t200 = gm_l17;
                uint8_t t201 = (t199 < t200);
                if (!t201) goto lb198;
                {
                    uint64_t t204 = gm_l18;
                    uint64_t t207 = GM_LD(uint64_t, ((uint8_t *)&t204));
                    gm_l19 = t207;
                    {
                        uint64_t t210 = gm_l19;
                        uint64_t t211 = (uintptr_t)t210;
                        void * t212 = gm_l1;
                        uint64_t t213 = GM_LD(uintptr_t, (((uint8_t *)t212) + 120) + 8);
                        if (t211 >= (uintptr_t)(t213)) gm_trap_index(t211, t213, gm_src6unagr666v56, 577, 12);
                        uint8_t t214 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t212) + 120)) + t211 * 1));
                        if (!t214) goto ie209;
                        {
                            uint64_t t216 = gm_l19;
                            uint64_t t217 = (uintptr_t)t216;
                            uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            if (t217 >= (uintptr_t)(t218)) gm_trap_index(t217, t218, gm_src6unagr666v56, 578, 13);
                            void * t219 = gm_l2;
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)t219) + 104));
                            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src6unagr666v56, 578, 26);
                            uint64_t t221 = (uint64_t)(t220 / ((uint64_t)8ULL));
                            uint64_t t222 = gm_l16;
                            uint64_t r223; bool o224 = __builtin_add_overflow(t221, t222, &r223);
                            if (o224) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 578, 26);
                            uint64_t t225 = r223;
                            uint32_t t226 = (uint32_t)t225;
                            GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t217 * 4), t226);
                        }
                        {
                            uint64_t t227 = gm_l16;
                            uint64_t r228; bool o229 = __builtin_add_overflow(t227, ((uint64_t)1ULL), &r228);
                            if (o229) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 579, 13);
                            uint64_t t230 = r228;
                            gm_l16 = t230;
                        }
                        goto ix209;
ie209:;
ix209:;
                    }
                    goto me205;
mn206:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 576, 5);
me205:;
                }
lc198:;
                {
                    uint64_t t232 = gm_l18;
                    uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                    uint64_t t235 = r233;
                    gm_l18 = t235;
                }
            }
lb198:;
        }
    }
    {
        uint64_t t237 = gm_l16;
        uint8_t t238 = ((t237 == ((uint64_t)0ULL)));
        if (!t238) goto ie236;
        {
            gm_b16_8 t240 = gm_l15;
            gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
            gm_b0_1 t242 = gm_f_free_5tbvv4o((void *)NULL, cx, NULL, t241, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kojtndgobcj)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t243;
            uint8_t t244 = 0;
            F.ret = t243;
            goto gm_fret;
        }
        goto ix236;
ie236:;
ix236:;
    }
    {
        void * t245 = gm_l2;
        gm_b16_8 t246 = gm_l15;
        GM_ST(gm_b16_8, (((uint8_t *)t245) + 48), t246);
    }
    {
        void * t247 = gm_l2;
        void * t248 = gm_l2;
        uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)t248) + 104));
        uint64_t t250 = gm_l16;
        uint64_t r251; bool o252 = __builtin_mul_overflow(((uint64_t)8ULL), t250, &r251);
        if (o252) gm_trap("integer overflow in `*`", gm_src6unagr666v56, 587, 18);
        uint64_t t253 = r251;
        uint64_t r254; bool o255 = __builtin_add_overflow(t249, t253, &r254);
        if (o255) gm_trap("integer overflow in `+`", gm_src6unagr666v56, 587, 5);
        uint64_t t256 = r254;
        GM_ST(uint64_t, (((uint8_t *)t247) + 104), t256);
    }
    {
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            void * t258 = gm_l2;
            uint64_t t259 = GM_LD(uintptr_t, (((uint8_t *)t258) + 0) + 8);
            gm_l20 = t259;
        }
        {
            for (;;) {
                uint64_t t261 = gm_l21;
                uint64_t t262 = gm_l20;
                uint8_t t263 = (t261 < t262);
                if (!t263) goto lb260;
                {
                    uint64_t t266 = gm_l21;
                    uint64_t t269 = GM_LD(uint64_t, ((uint8_t *)&t266));
                    gm_l22 = t269;
                    {
                        uint64_t t271 = gm_l22;
                        uint64_t t272 = (uintptr_t)t271;
                        void * t273 = gm_l2;
                        uint64_t t274 = GM_LD(uintptr_t, (((uint8_t *)t273) + 0) + 8);
                        if (t272 >= (uintptr_t)(t274)) gm_trap_index(t272, t274, gm_src6unagr666v56, 589, 15);
                        uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 0));
                        switch (t280) { case ((uint8_t)0ULL): goto ma278; case ((uint8_t)1ULL): goto ma277; case ((uint8_t)2ULL): goto ma277; case ((uint8_t)3ULL): goto ma278; case ((uint8_t)4ULL): goto ma278; case ((uint8_t)5ULL): goto ma278; case ((uint8_t)6ULL): goto ma278; case ((uint8_t)7ULL): goto ma278; case ((uint8_t)8ULL): goto ma278; case ((uint8_t)9ULL): goto ma278; case ((uint8_t)10ULL): goto ma278; case ((uint8_t)11ULL): goto ma278; case ((uint8_t)12ULL): goto ma278; case ((uint8_t)13ULL): goto ma278; case ((uint8_t)14ULL): goto ma278; case ((uint8_t)15ULL): goto ma278; case ((uint8_t)16ULL): goto ma278; case ((uint8_t)17ULL): goto ma278; case ((uint8_t)18ULL): goto ma278; case ((uint8_t)19ULL): goto ma278; case ((uint8_t)20ULL): goto ma278; case ((uint8_t)21ULL): goto ma278; case ((uint8_t)22ULL): goto ma278; case ((uint8_t)23ULL): goto ma278; case ((uint8_t)24ULL): goto ma278; case ((uint8_t)25ULL): goto ma278; case ((uint8_t)26ULL): goto ma278; case ((uint8_t)27ULL): goto ma278; case ((uint8_t)28ULL): goto ma278; case ((uint8_t)29ULL): goto ma278; case ((uint8_t)30ULL): goto ma278; case ((uint8_t)31ULL): goto ma278; case ((uint8_t)32ULL): goto ma278; case ((uint8_t)33ULL): goto ma278; case ((uint8_t)34ULL): goto ma278; case ((uint8_t)35ULL): goto ma278; case ((uint8_t)36ULL): goto ma278; case ((uint8_t)37ULL): goto ma278; case ((uint8_t)38ULL): goto ma278; case ((uint8_t)39ULL): goto ma278; case ((uint8_t)40ULL): goto ma278; case ((uint8_t)41ULL): goto ma278; case ((uint8_t)42ULL): goto ma278; case ((uint8_t)43ULL): goto ma278; case ((uint8_t)44ULL): goto ma278; case ((uint8_t)45ULL): goto ma278; case ((uint8_t)46ULL): goto ma278; case ((uint8_t)47ULL): goto ma278; case ((uint8_t)48ULL): goto ma278; case ((uint8_t)49ULL): goto ma278; case ((uint8_t)50ULL): goto ma278; case ((uint8_t)51ULL): goto ma277; case ((uint8_t)52ULL): goto ma278; case ((uint8_t)53ULL): goto ma278; case ((uint8_t)54ULL): goto ma278; case ((uint8_t)55ULL): goto ma278; case ((uint8_t)56ULL): goto ma278; case ((uint8_t)57ULL): goto ma278; case ((uint8_t)58ULL): goto ma278; case ((uint8_t)59ULL): goto ma276; case ((uint8_t)60ULL): goto ma278; case ((uint8_t)61ULL): goto ma278; case ((uint8_t)62ULL): goto ma278; case ((uint8_t)63ULL): goto ma278; case ((uint8_t)64ULL): goto ma278; case ((uint8_t)65ULL): goto ma278; case ((uint8_t)66ULL): goto ma278; case ((uint8_t)67ULL): goto ma278; case ((uint8_t)68ULL): goto ma278; case ((uint8_t)69ULL): goto ma278; case ((uint8_t)70ULL): goto ma278; case ((uint8_t)71ULL): goto ma278; case ((uint8_t)72ULL): goto ma278; case ((uint8_t)73ULL): goto ma278; case ((uint8_t)74ULL): goto ma278; case ((uint8_t)75ULL): goto ma278; case ((uint8_t)76ULL): goto ma278; case ((uint8_t)77ULL): goto ma278; case ((uint8_t)78ULL): goto ma278; case ((uint8_t)79ULL): goto ma278; case ((uint8_t)80ULL): goto ma278; case ((uint8_t)81ULL): goto ma278; case ((uint8_t)82ULL): goto ma278; case ((uint8_t)83ULL): goto ma278; case ((uint8_t)84ULL): goto ma278; default: goto ma278; }
ma276:;
                        uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 0));
                        if (t282 != ((uint8_t)59ULL)) goto mn281;
                        uint32_t t283 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 8) + 0));
                        gm_l23 = t283;
                        uint32_t t284 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 8) + 4));
                        gm_l24 = t284;
                        {
                            uint64_t t286 = gm_l22;
                            uint64_t t287 = (uintptr_t)t286;
                            uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t287 >= (uintptr_t)(t288)) gm_trap_index(t287, t288, gm_src6unagr666v56, 590, 42);
                            uint8_t *t289 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t287 * 16);
                            gm_t9qkh0rsooc8d t290;
                            GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)96ULL));
                            uint32_t t291 = gm_l23;
                            uint32_t t292 = gm_l24;
                            uint32_t t293 = gm_l24;
                            uint64_t t294 = (uint64_t)t293;
                            uint64_t t295 = (uintptr_t)t294;
                            uint64_t t296 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            if (t295 >= (uintptr_t)(t296)) gm_trap_index(t295, t296, gm_src6unagr666v56, 590, 78);
                            uint32_t t297 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t295 * 4));
                            gm_tb1f35qilrmun t298 = gm_f_bc3_m9eakek(t290, t291, t292, t297);
                            GM_ST(gm_tb1f35qilrmun, t289, t298);
                        }
                        goto me275;
mn281:;
ma277:;
                        uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 0));
                        if (t302 != ((uint8_t)1ULL)) goto pa301;
                        goto po300;
pa301:;
                        uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 0));
                        if (t304 != ((uint8_t)2ULL)) goto pa303;
                        goto po300;
pa303:;
                        uint8_t t306 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t273) + 0)) + t272 * 56) + 0));
                        if (t306 != ((uint8_t)51ULL)) goto pa305;
                        goto po300;
pa305:;
                        goto mn299;
po300:;
                        {
                            uint64_t t308 = gm_l22;
                            uint64_t t309 = (uintptr_t)t308;
                            uint64_t t310 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t309 >= (uintptr_t)(t310)) gm_trap_index(t309, t310, gm_src6unagr666v56, 591, 60);
                            uint8_t *t311 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t309 * 16);
                            gm_tb1f35qilrmun t312 = gm_f_bc_free_v10vouf();
                            GM_ST(gm_tb1f35qilrmun, t311, t312);
                        }
                        goto me275;
mn299:;
ma278:;
                        goto me275;
mn313:;
ma279:;
                        gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 589, 9);
me275:;
                    }
                    goto me267;
mn268:;
                    gm_trap("no match arm matched the scrutinee", gm_src6unagr666v56, 588, 5);
me267:;
                }
lc260:;
                {
                    uint64_t t316 = gm_l21;
                    uint64_t r317; bool o318 = __builtin_add_overflow(t316, ((uint64_t)1ULL), &r317);
                    uint64_t t319 = r317;
                    gm_l21 = t319;
                }
            }
lb260:;
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
extern __attribute__((aligned(4))) void * gm_f_vm_view_fo2var1(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
extern __attribute__((aligned(4))) uint8_t gm_f_word_size_mu6ni08(uint64_t a0);
