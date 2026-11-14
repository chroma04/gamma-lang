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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
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
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tlivgedjs5ps3 f0; gm_b16_8 f1; gm_tapdr2ag17fh7 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t0kv05e068jrn;
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t0kv05e068jrn) == 152 && _Alignof(gm_t0kv05e068jrn) == 8 && offsetof(gm_t0kv05e068jrn, f0) == 0 && offsetof(gm_t0kv05e068jrn, f1) == 72 && offsetof(gm_t0kv05e068jrn, f2) == 88 && offsetof(gm_t0kv05e068jrn, f3) == 136, "layout of `FnLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3qnk0a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
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
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jvoelso {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qlbp545 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enumerate_loop_5f6m89f {
    gm_frame h;
    gm_tsth9qu1dv0g3 l5; /* callee */
    gm_tl874t2atnlh3 l12; /* span */
    gm_trqfvmjtt4sij l17; /* fields */
    gm_trqfvmjtt4sij l20; /* stmts */
    void * ret;
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
struct gm_fr_extend_43vto3v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_nkql463 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_t9suqla {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_iterator_cc5qo7n {
    gm_frame h;
    gm_tsth9qu1dv0g3 l11; /* callee */
    gm_tddtjfggdat0d l14; /* sig */
    gm_tddtjfggdat0d l15; /* sig */
    gm_trqfvmjtt4sij l16; /* yields */
    gm_trqfvmjtt4sij l27; /* pre */
    gm_trqfvmjtt4sij l32; /* params */
    gm_t0d02142k4tu8 l35; /* pat_ast */
    gm_trqfvmjtt4sij l36; /* args */
    gm_t0d02142k4tu8 l39; /* p */
    gm_trqfvmjtt4sij l40; /* binds */
    gm_trqfvmjtt4sij l43; /* arms */
    gm_tk9n3vlila9qg l45; /* frame */
    gm_trqfvmjtt4sij l46; /* ids */
    gm_trqfvmjtt4sij l47; /* clauses */
    gm_trqfvmjtt4sij l48; /* handlers */
    void * ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_args_po0rnjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hoist_iter_value_cs229ip {
    gm_frame h;
    gm_t0mfir12leg2g l3; /* place */
    gm_tc3oht4ksth9d l7; /* b */
    gm_tc3oht4ksth9d l11; /* b */
    gm_tc3oht4ksth9d l14; /* b */
    gm_tc3oht4ksth9d l15; /* i */
    gm_tc3oht4ksth9d l17; /* v */
    gm_tc3oht4ksth9d l19; /* o */
    gm_tc3oht4ksth9d l20; /* p */
    gm_tc3oht4ksth9d l21; /* expr */
    gm_trqfvmjtt4sij l26; /* pre */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_hoist_iterator_call_5rmgsce {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* new_callee */
    gm_tc3oht4ksth9d l8; /* b */
    gm_trqfvmjtt4sij l9; /* out */
    gm_t7te2tdpebbt4 l12; /* a */
    gm_tc3oht4ksth9d l13; /* e */
    gm_tc3oht4ksth9d l15; /* e */
    gm_tc3oht4ksth9d l16; /* e */
    gm_t22mtv3sd9prf l17; /* kind */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_iter_stable_k9c87oo {
    gm_frame h;
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l14; /* x */
    gm_tc3oht4ksth9d l15; /* x */
    uint8_t ret;
};
struct gm_fr_iterator_arg_r0if28f {
    gm_frame h;
    gm_tddtjfggdat0d l3; /* sig */
    gm_tc3oht4ksth9d l11; /* call */
    gm_tlivgedjs5ps3 l12; /* fsig */
    gm_t0mfir12leg2g ret;
};
struct gm_fr_iterator_call_as_value_00i5mch {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lift_temps_7c6gk6l {
    gm_frame h;
    void * l0; /* cx */
    void * l2; /* pre */
    gm_b0_1 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6l26q4a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p0pjtrh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_c3kjspo {
    gm_frame h;
    void * ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_not_an_iterator_au10b3l {
    gm_frame h;
    void * ret;
};
struct gm_fr_place_iter_pre_qn5c5b5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu0mlkm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f5fsh94 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jd7smj6 {
    gm_frame h;
    gm_tl93vjpcesj48 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8l7j2k6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kqobl40 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s9eh08e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_sqitcho {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
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
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_ua7l21h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void * gm_f_enumerate_loop_5f6m89f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_43vto3v(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_mpnc7rc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t04dp7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_hoist_args_po0rnjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_hoist_iter_value_cs229ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_hoist_iterator_call_5rmgsce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_iter_stable_k9c87oo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cnt7p5u(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_not_an_iterator_au10b3l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqobl40(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remove_vrfdutr(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
extern uint8_t gm_k1umgjqrbkf[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k8gk4puesad[40] __attribute__((aligned(8)));
extern uint8_t gm_k8upcoeidcs[40] __attribute__((aligned(8)));
extern uint8_t gm_kb8poqrtf7g[40] __attribute__((aligned(8)));
extern uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
extern uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6mn01fhkq[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
extern uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
extern uint8_t gm_kukq3ep6ok2[40] __attribute__((aligned(8)));
extern const uint8_t gm_sa39b9bt4jpna[36];
extern const uint8_t gm_sb8airrqr74u5[73];
extern const uint8_t gm_sbr6raqk787l2[10];
extern const uint8_t gm_sc0gdocdik6sv[7];
extern const uint8_t gm_sfvnlkgl40moh[72];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sigq4ahgd0l2a[64];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_so4k586t2v79t[64];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcoimgd0jo62fr[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b) {
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
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    uint8_t t3 = gm_l2;
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    void * t5 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t1, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&t1));
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jvoelso F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qlbp545 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_43vto3v(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_43vto3v F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_s9eh08e(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 40);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_nkql463 F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_h08har5_2(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 32);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 32)));
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 82, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_t9suqla F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_lift_temps_7c6gk6l *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_lift_temps_7c6gk6l *)((&F.h)->up))->l2));
        gm_b0_1 t5 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6l26q4a F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ilis91k();
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fa4sjvq();
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p0pjtrh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qlbp545();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_c3kjspo F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t1 = gm_l1;
    uint32_t t2 = gm_l0;
    gm_teti411eujvf4 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_enumerate_loop_5f6m89f *)((&F.h)->up))->l12));
    void * t5 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1, &t3, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
            gm_t9805qtonbsag t8;
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
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
            gm_td8hfgklo8dil t8;
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
        gm_t66j1im9ip128 t17 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&gm_l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out))[32] = 1;
        memmove((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 0, t29, 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
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
            gm_tqetrgo0q24b5 t8;
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
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t29, 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
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
            gm_te5gbhu3us3k4 t8;
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
        gm_t8enl5g7b22jb t17 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
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
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 0, t29, 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 56);
            gm_tqetrgo0q24b5 t5;
            gm_f_pop_dt2bd2s((void *)&t5, t4);
        }
    }
    {
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 616);
            gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
        }
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 656);
        gm_td8hfgklo8dil t13;
        gm_f_pop_8sodrk9((void *)&t13, t12);
        if (!((((uint8_t *)&t13))[32] != 0)) goto ie10;
        gm_l1 = (*(gm_t66j1im9ip128 *)(((uint8_t *)&t13) + 0));
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_f_restore_scope_edqq1v8(t16, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
        }
        goto ix10;
ie10:;
ix10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_r81t436();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_v0p8ic9(t10, ((uint64_t)1ULL), t12);
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
        gm_tl874t2atnlh3 t21 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)t20 + t18 * 12), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h9tbucc_3(t10, ((uint64_t)1ULL), t12);
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
        gm_tbbqefuhv2r8h t21 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu0mlkm F;
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
            gm_b16_8 t11 = gm_f_empty_slice_c3qnk0a();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8l7j2k6(t10, ((uint64_t)1ULL), t12);
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
        gm_t1cs2fjnrqap1 t21 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)a1));
        GM_ST(gm_t1cs2fjnrqap1, ((uint8_t *)t20 + t18 * 40), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d1r4h19 F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_92arfe8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_m244brh(t10, ((uint64_t)1ULL), t12);
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
        gm_tiilmfd16v3fc t21 = gm_l1;
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h08har5_2(t10, ((uint64_t)1ULL), t12);
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
        gm_trcmmd12cefb1 t21 = gm_l1;
        GM_ST(gm_trcmmd12cefb1, ((uint8_t *)t20 + t18 * 32), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f5fsh94 F;
    void * gm_l0; /* l */
    gm_t4lnf71mruqpt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_qlbp545();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_sqitcho(t10, ((uint64_t)1ULL), t12);
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
        gm_t4lnf71mruqpt t21 = gm_l1;
        GM_ST(gm_t4lnf71mruqpt, ((uint8_t *)t20 + t18 * 32), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_d914i5u();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2onqfca(t10, ((uint64_t)1ULL), t12);
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
        uint32_t t21 = gm_l1;
        GM_ST(uint32_t, ((uint8_t *)t20 + t18 * 4), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jd7smj6 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_jvoelso();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_kqobl40(t10, ((uint64_t)1ULL), t12);
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
        gm_tl93vjpcesj48 t21 = GM_LD(gm_tl93vjpcesj48, ((uint8_t *)a1));
        GM_ST(gm_tl93vjpcesj48, ((uint8_t *)t20 + t18 * 48), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jis5lhu F;
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
            gm_b16_8 t11 = gm_f_empty_slice_fa4sjvq();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_08uc6h5(t10, ((uint64_t)1ULL), t12);
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
        gm_t7te2tdpebbt4 t21 = F.l1;
        GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)t20 + t18 * 144), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 144)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
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
            gm_b16_8 t11 = gm_f_empty_slice_r9g5q6n();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_t5pgl2t(t10, ((uint64_t)1ULL), t12);
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
        gm_tnrgict8sthcv t21 = F.l1;
        GM_ST(gm_tnrgict8sthcv, ((uint8_t *)t20 + t18 * 120), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
            gm_b16_8 t11 = gm_f_empty_slice_he774nl();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_7sj41hq(t10, ((uint64_t)1ULL), t12);
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
        gm_t5v0c8ia6a5mo t21 = F.l1;
        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)t20 + t18 * 56), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_73v4ttb();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_k4j2gj3(t10, ((uint64_t)1ULL), t12);
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
        uint64_t t21 = gm_l1;
        GM_ST(uint64_t, ((uint8_t *)t20 + t18 * 8), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_08uc6h5 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)144ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8l7j2k6 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqobl40(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kqobl40 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_m244brh F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s9eh08e F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_sqitcho F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ua7l21h F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
__attribute__((aligned(4))) void * gm_f_enumerate_loop_5f6m89f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4) {
    struct gm_fr_enumerate_loop_5f6m89f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* call */
    gm_b16_8 gm_l3; /* bound */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    gm_b16_8 gm_l6; /* args */
    uint32_t gm_l7; /* fid */
    void * gm_l8; /* sig */
    void * gm_l9; /* tys */
    gm_t88rcucg1b7uk gm_l10; /* sty */
    gm_t88rcucg1b7uk gm_l11; /* x_ty */
    gm_t88rcucg1b7uk gm_l13; /* usize_t */
    uint32_t gm_l14; /* s */
    uint32_t gm_l15; /* i */
    gm_closure gm_l16; /* local */
    void * gm_l18; /* item */
    void * gm_l19; /* lp */
    GM_STACK_CHECK(&F, 1710u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        gm_tsth9qu1dv0g3 t5 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t3) + 8) + 8) + 0));
        F.l5 = t5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 40));
        gm_l6 = t6;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t9 != ((uint8_t)0ULL)) goto ie8;
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
            gm_l7 = t10;
            {
                uint32_t t12 = gm_l7;
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 344) + 0) + 8);
                if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srcoimgd0jo62fr, 123, 13);
                void * t18 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 344) + 0)) + t14 * 136);
                gm_l8 = t18;
            }
            {
                uint8_t t20 = 0;
                uint8_t t21 = 0;
                void * t22 = gm_l8;
                uint8_t t23 = (!((GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)t22) + 0)), gm_sbr6raqk787l2, 9) == 0));
                if (!t23) {
                    void * t24 = gm_l8;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 32));
                    uint64_t t26 = (uintptr_t)t25;
                    void * t27 = gm_l0;
                    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                    void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                    uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 200) + 0) + 8);
                    if (t26 >= (uintptr_t)(t30)) gm_trap_index(t26, t30, gm_srcoimgd0jo62fr, 124, 43);
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t29) + 200) + 0)) + t26 * 184) + 112));
                    uint8_t t32 = (uint8_t)!t31;
                    t21 = t32;
                } else t21 = 1;
                if (!t21) {
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t34 = (!(t33 == ((uint64_t)1ULL)));
                    t20 = t34;
                } else t20 = 1;
                if (!t20) goto ie19;
                {
                    uint8_t t36 = 0;
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
                goto ix19;
ie19:;
ix19:;
            }
            {
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
                void * t39 = (void *)(((uint8_t *)t38) + 8);
                gm_l9 = t39;
            }
            {
                void * t40 = gm_l0;
                void * t41 = (void *)(((uint8_t *)t40) + 96);
                void * t42 = gm_l0;
                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                uint64_t t44 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcoimgd0jo62fr, 126, 35);
                gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t44 * 88) + 0));
                gm_t88rcucg1b7uk t47 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t41, t43, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t47;
            }
            {
                void * t49 = gm_l9;
                gm_t88rcucg1b7uk t50 = gm_l10;
                gm_t53at80pfdus3 t51;
                gm_f_kind_jcnt4hm((void *)&t51, t49, GM_CONV(gm_csguankko4lmhd, t50));
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t51) + 0));
                if (t52 != ((uint8_t)10ULL)) goto ie48;
                gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t51) + 8) + 4));
                gm_l11 = t53;
                {
                    void * t55 = gm_l1;
                    gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 104));
                    F.l12 = t56;
                }
                {
                    void * t57 = gm_l9;
                    gm_t88rcucg1b7uk t58 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t58;
                }
                {
                    void * t59 = gm_l0;
                    gm_t88rcucg1b7uk t60 = gm_l10;
                    gm_tl874t2atnlh3 t61 = F.l12;
                    uint32_t t62 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t59, t60, t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t62;
                }
                {
                    void * t63 = gm_l0;
                    gm_t88rcucg1b7uk t64 = gm_l13;
                    gm_tl874t2atnlh3 t65 = F.l12;
                    uint32_t t66 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t63, t64, t65);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t66;
                }
                {
                    gm_closure t67 = ((gm_closure){(void *)gm_f_local_c3kjspo, (void *)&F.h, cx});
                    gm_l16 = t67;
                }
                {
                    gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t68 = (void *)((uint8_t *)&F.l17);
                    gm_closure t69 = gm_l16;
                    uint32_t t70 = gm_l15;
                    gm_t88rcucg1b7uk t71 = gm_l13;
                    gm_closure t72 = t69;
                    gm_node *ccx73 = t72.cx;
                    gm_node cn74; { gm_node *f = gm_find(cx, 587034433); if (f) { cn74 = *f; cn74.next = ccx73; ccx73 = &cn74; } }
                    void * t75 = t72.code;
                    void * t76;
                    if (GM_PTR_TAGGED(t75)) {
                        t76 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t75))(t70, GM_CONV(gm_csguankko4lmhd, t71));
                    } else {
                        t76 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t75)(t72.env, ccx73, NULL, t70, t71);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t77 = gm_f_push_d943vgp_2(t68, &(*(gm_t69m56meqefs8 *)((uint8_t *)t76)));
                    if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcoimgd0jo62fr, 134, 5);
                    gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                }
                {
                    void * t79 = (void *)((uint8_t *)&F.l17);
                    gm_t88rcucg1b7uk t80 = gm_l11;
                    gm_tlcl4u558l6p5 t81;
                    memset((uint8_t *)&t81 + 17, 0, 7);
                    gm_closure t82 = gm_l16;
                    uint32_t t83 = gm_l14;
                    gm_t88rcucg1b7uk t84 = gm_l10;
                    gm_closure t85 = t82;
                    gm_node *ccx86 = t85.cx;
                    gm_node cn87; { gm_node *f = gm_find(cx, 587034433); if (f) { cn87 = *f; cn87.next = ccx86; ccx86 = &cn87; } }
                    void * t88 = t85.code;
                    void * t89;
                    if (GM_PTR_TAGGED(t88)) {
                        t89 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t88))(t83, GM_CONV(gm_csguankko4lmhd, t84));
                    } else {
                        t89 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t88)(t85.env, ccx86, NULL, t83, t84);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t81 + 0, t89);
                    gm_closure t90 = gm_l16;
                    uint32_t t91 = gm_l15;
                    gm_t88rcucg1b7uk t92 = gm_l13;
                    gm_closure t93 = t90;
                    gm_node *ccx94 = t93.cx;
                    gm_node cn95; { gm_node *f = gm_find(cx, 587034433); if (f) { cn95 = *f; cn95.next = ccx94; ccx94 = &cn95; } }
                    void * t96 = t93.code;
                    void * t97;
                    if (GM_PTR_TAGGED(t96)) {
                        t97 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t96))(t91, GM_CONV(gm_csguankko4lmhd, t92));
                    } else {
                        t97 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t96)(t93.env, ccx94, NULL, t91, t92);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t81 + 8, t97);
                    GM_ST(uint8_t, (uint8_t *)&t81 + 16, ((uint8_t)0));
                    gm_teti411eujvf4 t98;
                    memset(&t98, 0, sizeof t98);
                    GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t98 + 8), t81);
                    gm_tl874t2atnlh3 t99 = F.l12;
                    void * t100 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t80, &t98, t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t101 = gm_f_push_d943vgp_2(t79, &(*(gm_t69m56meqefs8 *)((uint8_t *)t100)));
                    if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srcoimgd0jo62fr, 135, 5);
                    gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                }
                {
                    gm_t88rcucg1b7uk t103 = gm_l4;
                    gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                    gm_teti411eujvf4 t105;
                    memset(&t105, 0, sizeof t105);
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t105 + 8), t104);
                    gm_tl874t2atnlh3 t106 = F.l12;
                    void * t107 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t103, &t105, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t107;
                }
                {
                    void * t108 = gm_l0;
                    void * t109 = gm_l1;
                    gm_t88rcucg1b7uk t110 = gm_l4;
                    uint32_t t111 = gm_l15;
                    gm_t88rcucg1b7uk t112 = gm_l13;
                    gm_t88rcucg1b7uk t113 = gm_l13;
                    gm_closure t114 = gm_l16;
                    uint32_t t115 = gm_l14;
                    gm_t88rcucg1b7uk t116 = gm_l10;
                    gm_closure t117 = t114;
                    gm_node *ccx118 = t117.cx;
                    gm_node cn119; { gm_node *f = gm_find(cx, 587034433); if (f) { cn119 = *f; cn119.next = ccx118; ccx118 = &cn119; } }
                    void * t120 = t117.code;
                    void * t121;
                    if (GM_PTR_TAGGED(t120)) {
                        t121 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t120))(t115, GM_CONV(gm_csguankko4lmhd, t116));
                    } else {
                        t121 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t120)(t117.env, ccx118, NULL, t115, t116);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_teti411eujvf4 t122;
                    memset(&t122, 0, sizeof t122);
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)22ULL));
                    GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
                    gm_tl874t2atnlh3 t123 = F.l12;
                    void * t124 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t113, &t122, t123);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t125 = gm_l18;
                    void * t126 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t108, t109, t110, t111, t112, t124, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t126;
                }
                {
                    gm_f_list_ir0h3tc_2((void *)&F.l20, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t127 = (void *)((uint8_t *)&F.l20);
                    gm_b16_8 t128 = gm_l3;
                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                    gm_tftb1odc739t5 t130 = gm_f_extend_nkql463(t127, t129);
                    if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_srcoimgd0jo62fr, 139, 5);
                    gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
                }
                {
                    void * t132 = (void *)((uint8_t *)&F.l20);
                    uint32_t t133 = gm_l14;
                    uint64_t t134 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_srcoimgd0jo62fr, 140, 25);
                    void * t136 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t134 * 88);
                    gm_ttggq7ng4kp3p t137;
                    GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t137 + 0), t133);
                    GM_ST(void *, ((uint8_t *)&t137 + 8), t136);
                    gm_trcmmd12cefb1 t138;
                    GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t138 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t138 + 8), t137);
                    gm_tftb1odc739t5 t139 = gm_f_push_d6c76ud_2(t132, &t138);
                    if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srcoimgd0jo62fr, 140, 5);
                    gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
                }
                {
                    void * t141 = (void *)((uint8_t *)&F.l20);
                    uint32_t t142 = gm_l15;
                    gm_t88rcucg1b7uk t143 = gm_l13;
                    gm_tbci5ospk5nnn t144;
                    gm_f_zero_s25l990((void *)&t144, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_teti411eujvf4 t145;
                    GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t145 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t145 + 8), t144);
                    gm_tl874t2atnlh3 t146 = F.l12;
                    void * t147 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t143, &t145, t146);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttggq7ng4kp3p t148;
                    GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t148 + 0), t142);
                    GM_ST(void *, ((uint8_t *)&t148 + 8), t147);
                    gm_trcmmd12cefb1 t149;
                    GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t149 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t149 + 8), t148);
                    gm_tftb1odc739t5 t150 = gm_f_push_d6c76ud_2(t141, &t149);
                    if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srcoimgd0jo62fr, 141, 5);
                    gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                }
                {
                    void * t152 = (void *)((uint8_t *)&F.l20);
                    void * t153 = gm_l19;
                    gm_trcmmd12cefb1 t154;
                    GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t154 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t154 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)4ULL));
                    GM_ST(void *, ((uint8_t *)&t154 + 8), t153);
                    gm_tftb1odc739t5 t155 = gm_f_push_d6c76ud_2(t152, &t154);
                    if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_srcoimgd0jo62fr, 142, 5);
                    gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
                }
                {
                    void * t157 = gm_l0;
                    void * t158 = GM_LD(void *, (((uint8_t *)t157) + 0));
                    gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                    gm_tl874t2atnlh3 t160 = F.l12;
                    void * t161 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t158, t159, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t162 = 0;
                    F.ret = t161;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
                {
                    uint8_t t164 = 0;
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
ix48:;
            }
            goto ix8;
ie8:;
            {
                uint8_t t166 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
ix8:;
        }
        goto ix2;
ie2:;
        {
            uint8_t t168 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_iterator_cc5qo7n F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    uint32_t gm_l4; /* yield_eff */
    uint64_t gm_l5; /* start */
    void * gm_l6; /* call */
    gm_b16_8 gm_l7; /* bound */
    void * gm_l8; /* b */
    void * gm_l9; /* t */
    gm_tnpifv20irnph gm_l10; /* effects */
    uint32_t gm_l12; /* id */
    void * gm_l13; /* c */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tiilmfd16v3fc gm_l19; /* e */
    void * gm_l20; /* e */
    gm_tiilmfd16v3fc gm_l21; /* eref */
    gm_t88rcucg1b7uk gm_l22; /* elem */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint64_t gm_l24; /* i */
    void * gm_l25; /* e */
    gm_t88rcucg1b7uk gm_l26; /* result_ty */
    gm_th2lqbo77pvsc gm_l28; /* label_name */
    gm_t08olq97rscn3 gm_l29; /* l */
    uint32_t gm_l30; /* label */
    uint32_t gm_l31; /* hid */
    uint32_t gm_l33; /* fn_id */
    uint32_t gm_l34; /* xid */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    void * gm_l41; /* pat */
    void * gm_l42; /* body */
    void * gm_l44; /* m */
    void * gm_l49; /* w */
    GM_STACK_CHECK(&F, 4655u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t9805qtonbsag t9 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t8) + 1956));
        uint32_t t10;
        if (((((uint8_t *)&t9))[4] != 0)) {
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            {
                void * t13 = gm_l0;
                gm_tl874t2atnlh3 t14 = gm_l2;
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sa39b9bt4jpna));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(35));
                gm_b0_1 t16 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t13, t14, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_tl874t2atnlh3 t19 = gm_l2;
            void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        gm_l4 = t10;
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 192) + 0) + 8);
        gm_l5 = t23;
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l1;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 56));
        gm_tkn42u7na3t81 t27;
        memset(&t27, 0, sizeof t27);
        void * t28 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t24, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t28;
    }
    {
        gm_b16_8 t29 = gm_f_empty_slice_vj3ckdj_2();
        gm_l7 = t29;
    }
    {
        void * t31 = gm_l6;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
        if (t32 != ((uint8_t)46ULL)) goto ie30;
        void * t33 = GM_LD(void *, ((((uint8_t *)t31) + 8) + 8));
        gm_l8 = t33;
        void * t34 = gm_l8;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t35)) != 0)) goto ie30;
        void * t36 = GM_LD(void *, ((uint8_t *)&t35));
        gm_l9 = t36;
        void * t37 = gm_l9;
        uint8_t t38;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t37) + 8) + 0));
        if (t41 != ((uint8_t)34ULL)) goto mn40;
        t38 = ((uint8_t)1);
        goto me39;
mn40:;
        t38 = ((uint8_t)0);
        goto me39;
mn42:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 17, 51);
me39:;
        if (!t38) goto ie30;
        {
            void * t44 = gm_l8;
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t44) + 0));
            gm_l7 = t45;
        }
        {
            void * t46 = gm_l9;
            gm_l6 = t46;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_tnpifv20irnph t47 = gm_f_empty_effects_r0iv3pv();
        gm_l10 = t47;
    }
    {
        void * t48 = gm_l6;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t48) + 8) + 0));
        if (t51 != ((uint8_t)34ULL)) goto mn50;
        gm_tsth9qu1dv0g3 t52 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t48) + 8) + 8) + 0));
        F.l11 = t52;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
        switch (t58) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma55; default: goto ma56; }
ma54:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
        if (t60 != ((uint8_t)0ULL)) goto mn59;
        uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 8));
        gm_l12 = t61;
        {
            uint32_t t63 = gm_l12;
            uint64_t t64 = (uint64_t)t63;
            uint64_t t65 = (uintptr_t)t64;
            void * t66 = gm_l0;
            void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
            uint64_t t68 = GM_LD(uintptr_t, ((((uint8_t *)t67) + 344) + 0) + 8);
            if (t65 >= (uintptr_t)(t68)) gm_trap_index(t65, t68, gm_srcoimgd0jo62fr, 24, 36);
            gm_tnpifv20irnph t69 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t67) + 344) + 0)) + t65 * 136) + 64));
            gm_l10 = t69;
        }
        goto me53;
mn59:;
ma55:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
        if (t71 != ((uint8_t)2ULL)) goto mn70;
        void * t72 = GM_LD(void *, (((uint8_t *)&F.l11) + 8));
        gm_l13 = t72;
        void * t73 = gm_l3;
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 96);
        void * t76 = gm_l0;
        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
        void * t78 = gm_l13;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t78) + 0));
        gm_t88rcucg1b7uk t80 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t75, t77, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t81;
        gm_f_kind_jcnt4hm((void *)&t81, t73, GM_CONV(gm_csguankko4lmhd, t80));
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t81) + 0));
        if (t86 != ((uint8_t)15ULL)) goto pa85;
        F.l14 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t81) + 8));
        goto po84;
pa85:;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t81) + 0));
        if (t88 != ((uint8_t)16ULL)) goto pa87;
        F.l14 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t81) + 8));
        goto po84;
pa87:;
        goto mn83;
po84:;
        {
            gm_tnpifv20irnph t90 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l14) + 24));
            gm_l10 = t90;
        }
        goto me82;
mn83:;
        goto me82;
mn91:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 25, 29);
me82:;
        goto me53;
mn70:;
ma56:;
        void * t94 = gm_l0;
        void * t95 = gm_l1;
        void * t96 = gm_l6;
        void * t97 = gm_f_not_an_iterator_au10b3l((void *)NULL, cx, NULL, t94, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t98 = 0;
        F.ret = t97;
        goto gm_fret;
        goto me53;
mn93:;
ma57:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 23, 29);
me53:;
        goto me49;
mn50:;
        void * t100 = gm_l0;
        void * t101 = gm_l1;
        void * t102 = gm_l6;
        void * t103 = gm_f_not_an_iterator_au10b3l((void *)NULL, cx, NULL, t100, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t104 = 0;
        F.ret = t103;
        goto gm_fret;
        goto me49;
mn99:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 22, 5);
me49:;
    }
    {
        gm_f_list_084fcd6((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
            gm_l17 = t106;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t108 = gm_l18;
                uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t110 = (t108 < t109);
                if (!t110) goto lb107;
                {
                    uint64_t t113 = gm_l18;
                    uint64_t t114 = (uintptr_t)t113;
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_tiilmfd16v3fc t118 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t114 * 24));
                    gm_l19 = t118;
                    {
                        uint32_t t121 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 0));
                        uint32_t t122 = gm_l4;
                        uint8_t t123 = ((t121 == t122));
                        if (!t123) goto ie120;
                        {
                            void * t125 = (void *)((uint8_t *)&F.l16);
                            gm_tftb1odc739t5 t126 = gm_f_push_d1r4h19(t125, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l19)));
                            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srcoimgd0jo62fr, 35, 33);
                            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                        }
                        goto ix120;
ie120:;
ix120:;
                    }
                    goto me116;
mn117:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 34, 5);
me116:;
                }
lc107:;
                {
                    uint64_t t129 = gm_l18;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    uint64_t t132 = r130;
                    gm_l18 = t132;
                }
            }
lb107:;
        }
    }
    {
        uint64_t t134 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint8_t t135 = ((t134 == ((uint64_t)0ULL)));
        if (!t135) goto ie133;
        {
            void * t138 = gm_l0;
            void * t139 = gm_l1;
            void * t140 = gm_l6;
            void * t141 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t138, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t141)) != 0)) goto ie137;
            void * t142 = GM_LD(void *, ((uint8_t *)&t141));
            gm_l20 = t142;
            {
                void * t144 = gm_l20;
                uint8_t t145 = 0;
                F.ret = t144;
                goto gm_fret;
            }
            goto ix137;
ie137:;
ix137:;
        }
        {
            void * t146 = gm_l0;
            void * t147 = gm_l1;
            void * t148 = GM_LD(void *, (((uint8_t *)t147) + 56));
            gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t148) + 72));
            gm_b16_8 t150;
            GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sigq4ahgd0l2a));
            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(63));
            gm_b0_1 t151 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t146, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t152 = gm_l0;
            void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
            gm_tl874t2atnlh3 t154 = gm_l2;
            void * t155 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t156 = 0;
            F.ret = t155;
            goto gm_fret;
        }
        goto ix133;
ie133:;
ix133:;
    }
    {
        uint64_t t158 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint8_t t159 = (t158 > ((uint64_t)1ULL));
        if (!t159) goto ie157;
        {
            void * t161 = gm_l0;
            void * t162 = gm_l1;
            void * t163 = GM_LD(void *, (((uint8_t *)t162) + 56));
            gm_tl874t2atnlh3 t164 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t163) + 72));
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sfvnlkgl40moh));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(71));
            gm_b0_1 t166 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t161, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t167 = gm_l0;
            void * t168 = GM_LD(void *, (((uint8_t *)t167) + 0));
            gm_tl874t2atnlh3 t169 = gm_l2;
            void * t170 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t168, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t171 = 0;
            F.ret = t170;
            goto gm_fret;
        }
        goto ix157;
ie157:;
ix157:;
    }
    {
        uint64_t t172 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_srcoimgd0jo62fr, 47, 13);
        gm_tiilmfd16v3fc t174 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t172 * 24));
        gm_l21 = t174;
    }
    {
        void * t175 = gm_l3;
        gm_t88rcucg1b7uk t176 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t175);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t176;
    }
    {
        uint64_t t178 = GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 8) + 8);
        uint8_t t179 = (t178 > ((uint64_t)0ULL));
        if (!t179) goto ie177;
        uint64_t t180 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t181 = GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 8) + 8);
        if (t180 >= (uintptr_t)(t181)) gm_trap_index(t180, t181, gm_srcoimgd0jo62fr, 49, 42);
        uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l21) + 8)) + t180 * 40) + 0));
        if (t182 != ((uint8_t)0ULL)) goto ie177;
        gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l21) + 8)) + t180 * 40) + 8));
        gm_l23 = t183;
        {
            gm_t88rcucg1b7uk t185 = gm_l23;
            gm_l22 = t185;
        }
        goto ix177;
ie177:;
ix177:;
    }
    {
        uint64_t t186 = gm_l5;
        gm_l24 = t186;
    }
    {
        for (;;) {
            uint64_t t188 = gm_l24;
            void * t189 = gm_l0;
            uint64_t t190 = GM_LD(uintptr_t, ((((uint8_t *)t189) + 192) + 0) + 8);
            uint8_t t191 = (t188 < t190);
            if (!t191) goto lb187;
            {
                uint64_t t195 = gm_l24;
                uint64_t t196 = (uintptr_t)t195;
                void * t197 = gm_l0;
                uint64_t t198 = GM_LD(uintptr_t, ((((uint8_t *)t197) + 192) + 0) + 8);
                if (t196 >= (uintptr_t)(t198)) gm_trap_index(t196, t198, gm_srcoimgd0jo62fr, 53, 27);
                void * t199 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t197) + 192) + 0)) + t196 * 40) + 0);
                void * t200 = (void *)((uint8_t *)&gm_l21);
                int32_t t201 = gm_f_cmp_ref_8ntrqll(t199, t200);
                uint8_t t202 = ((t201 == ((int32_t)0LL)));
                if (!t202) goto ie194;
                {
                    {
                        void * t205 = gm_l0;
                        void * t206 = (void *)(((uint8_t *)t205) + 192);
                        uint64_t t207 = gm_l24;
                        gm_tj44oivvogvtl t208;
                        gm_f_remove_vrfdutr((void *)&t208, t206, t207);
                    }
                }
                goto ix194;
ie194:;
                {
                    uint64_t t210 = gm_l24;
                    uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                    if (o212) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 56, 13);
                    uint64_t t213 = r211;
                    gm_l24 = t213;
                }
ix194:;
            }
lc187:;
        }
lb187:;
    }
    {
        void * t215 = gm_l0;
        void * t216 = gm_l1;
        void * t217 = gm_l6;
        gm_b16_8 t218 = gm_l7;
        gm_t88rcucg1b7uk t219 = gm_l22;
        void * t220 = gm_f_enumerate_loop_5f6m89f((void *)NULL, cx, NULL, t215, t216, t217, t218, t219);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!(GM_LD(void *, ((uint8_t *)&t220)) != 0)) goto ie214;
        void * t221 = GM_LD(void *, ((uint8_t *)&t220));
        gm_l25 = t221;
        {
            void * t223 = gm_l25;
            uint8_t t224 = 0;
            F.ret = t223;
            goto gm_fret;
        }
        goto ix214;
ie214:;
ix214:;
    }
    {
        void * t225 = gm_l6;
        gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t225) + 0));
        gm_l26 = t226;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t227 = (void *)((uint8_t *)&F.l27);
        gm_b16_8 t228 = gm_l7;
        gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
        gm_tftb1odc739t5 t230 = gm_f_extend_nkql463(t227, t229);
        if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 1, gm_srcoimgd0jo62fr, 63, 5);
        gm_b0_1 t231 = GM_LD(gm_b0_1, (((uint8_t *)&t230) + 1));
    }
    {
        void * t232 = gm_l0;
        void * t233 = gm_l6;
        void * t234 = (void *)((uint8_t *)&F.l27);
        gm_b0_1 t235 = gm_f_hoist_args_po0rnjr((void *)NULL, cx, NULL, t232, t233, t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t236 = gm_l0;
        gm_b0_1 t237 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t238;
        memset(&t238, 0, sizeof t238);
        gm_l28 = t238;
    }
    {
        void * t240 = gm_l1;
        if (!(((((uint8_t *)t240) + 0))[32] != 0)) goto ie239;
        gm_t08olq97rscn3 t241 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t240) + 0));
        gm_l29 = t241;
        {
            void * t243 = gm_l0;
            gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 0));
            gm_t930q5l38hdap t245;
            memset(&t245, 0, sizeof t245);
            GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)2ULL));
            gm_tl874t2atnlh3 t246 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l29) + 16));
            gm_b0_1 t247 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t243, t244, &t245, t246);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 0, t248);
        }
        goto ix239;
ie239:;
ix239:;
    }
    {
        void * t249 = gm_l0;
        gm_t88rcucg1b7uk t250 = gm_l26;
        gm_t1qr0p6kln6tf t251;
        GM_ST(uint8_t, (uint8_t *)&t251 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t251 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t251 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t251 + 4), t250);
        uint32_t t252 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t249, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)), ((uint8_t)1), t251);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l30 = t252;
    }
    {
        void * t253 = gm_l0;
        void * t254 = GM_LD(void *, (((uint8_t *)t253) + 0));
        uint64_t t255 = GM_LD(uintptr_t, ((((uint8_t *)t254) + 504) + 0) + 8);
        uint32_t t256 = (uint32_t)t255;
        gm_l31 = t256;
    }
    {
        void * t257 = gm_l0;
        void * t258 = GM_LD(void *, (((uint8_t *)t257) + 0));
        void * t259 = (void *)(((uint8_t *)t258) + 504);
        gm_t1cs2fjnrqap1 t260;
        memcpy((uint8_t *)&t260 + 0, ((uint8_t *)&gm_l21), 24);
        gm_b16_8 t261 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t260 + 24, t261);
        gm_tftb1odc739t5 t262 = gm_f_push_bu0mlkm(t259, &t260);
        if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 1, gm_srcoimgd0jo62fr, 73, 5);
        gm_b0_1 t263 = GM_LD(gm_b0_1, (((uint8_t *)&t262) + 1));
    }
    {
        gm_f_list_af74s0q((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t264 = (void *)((uint8_t *)&F.l32);
        gm_t5v0c8ia6a5mo t265;
        memset((uint8_t *)&t265 + 49, 0, 7);
        gm_b16_8 t266;
        GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sc0gdocdik6sv));
        GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t265 + 0, t266);
        gm_t88rcucg1b7uk t267 = gm_l22;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t265 + 16, t267);
        gm_tl874t2atnlh3 t268 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t265 + 20, t268);
        GM_ST(void *, (uint8_t *)&t265 + 32, ((void *)0));
        gm_t9805qtonbsag t269;
        memset(&t269, 0, sizeof t269);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t265 + 40, t269);
        GM_ST(uint8_t, (uint8_t *)&t265 + 48, ((uint8_t)0));
        gm_tftb1odc739t5 t270 = gm_f_push_o8u01sh(t264, &t265);
        if (((((uint8_t *)&t270))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t270) + 1, gm_srcoimgd0jo62fr, 76, 5);
        gm_b0_1 t271 = GM_LD(gm_b0_1, (((uint8_t *)&t270) + 1));
    }
    {
        void * t272 = gm_l0;
        void * t273 = GM_LD(void *, (((uint8_t *)t272) + 0));
        gm_t9qkh0rsooc8d t274;
        GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)2ULL));
        gm_b16_8 t275;
        GM_ST(void *, (uint8_t *)&t275, (void *)(gm_ss7qhfi2s00na));
        GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(10));
        gm_tl874t2atnlh3 t276 = gm_l2;
        void * t277 = gm_l0;
        uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)t277) + 8));
        gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
        void * t280 = gm_l3;
        gm_t88rcucg1b7uk t281 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t280);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t282 = gm_f_empty_effects_r0iv3pv();
        uint32_t t283 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t273, t274, t275, t276, t278, t279, t281, t282);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l33 = t283;
    }
    {
        void * t284 = gm_l0;
        uint32_t t285 = gm_l33;
        void * t286 = gm_l3;
        gm_t88rcucg1b7uk t287 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t286);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t288 = gm_f_empty_effects_r0iv3pv();
        gm_b0_1 t289 = gm_f_enter_fn_vo5kel1((void *)NULL, cx, NULL, t284, t285, t287, t288);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t290 = gm_l0;
        gm_tnqmlaad6uchl t291;
        memset((uint8_t *)&t291 + 1, 0, 3);
        memset((uint8_t *)&t291 + 28, 0, 4);
        gm_t9qkh0rsooc8d t292;
        GM_ST(uint8_t, ((uint8_t *)&t292 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t291 + 0, t292);
        uint32_t t293 = gm_l31;
        gm_t9805qtonbsag t294;
        GM_ST(uint8_t, (uint8_t *)&t294 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t294 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t294), t293);
        ((uint8_t *)&t294)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t291 + 4, t294);
        gm_t9805qtonbsag t295;
        memset(&t295, 0, sizeof t295);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t291 + 12, t295);
        gm_tkn42u7na3t81 t296;
        memset(&t296, 0, sizeof t296);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t291 + 20, t296);
        gm_tuanp0jbrlb6j t297;
        memset((uint8_t *)&t297 + 28, 0, 4);
        memcpy((uint8_t *)&t297 + 0, ((uint8_t *)&gm_l28), 24);
        uint32_t t298 = gm_l30;
        GM_ST(uint32_t, (uint8_t *)&t297 + 24, t298);
        gm_tjvtgq0p0sehv t299;
        GM_ST(uint8_t, (uint8_t *)&t299 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t299 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t299 + 36, 0);
        GM_ST(gm_tuanp0jbrlb6j, ((uint8_t *)&t299), t297);
        ((uint8_t *)&t299)[32] = 1;
        GM_ST(gm_tjvtgq0p0sehv, (uint8_t *)&t291 + 32, t299);
        gm_t88rcucg1b7uk t300 = gm_l26;
        gm_tkn42u7na3t81 t301;
        GM_ST(uint8_t, (uint8_t *)&t301 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t301 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t301), t300);
        ((uint8_t *)&t301)[4] = 1;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t291 + 72, t301);
        gm_tiilmfd16v3fc t302 = gm_l21;
        gm_tqu3m2l3n8mnc t303;
        GM_ST(uint8_t, (uint8_t *)&t303 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t303 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t303 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t303), t302);
        ((uint8_t *)&t303)[24] = 1;
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t291 + 80, t303);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)t290) + 800)) + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)t290) + 800)) + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)t290) + 800)) + 116, 0);
        ((uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)t290) + 800)))[112] = 1;
        GM_ST(gm_tnqmlaad6uchl, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)t290) + 800)) + 0, t291);
    }
    {
        void * t304 = gm_l0;
        gm_b16_8 t305;
        GM_ST(void *, (uint8_t *)&t305, (void *)(gm_sc0gdocdik6sv));
        GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(6));
        gm_t88rcucg1b7uk t306 = gm_l22;
        gm_tl874t2atnlh3 t307 = gm_l2;
        uint32_t t308 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t304, t305, t306, ((uint8_t)0), t307);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l34 = t308;
    }
    {
        uint64_t t309 = (uintptr_t)((uint64_t)0ULL);
        void * t310 = gm_l1;
        uint64_t t311 = GM_LD(uintptr_t, (((uint8_t *)t310) + 40) + 8);
        if (t309 >= (uintptr_t)(t311)) gm_trap_index(t309, t311, gm_srcoimgd0jo62fr, 82, 16);
        gm_t0d02142k4tu8 t312 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t310) + 40)) + t309 * 64));
        F.l35 = t312;
    }
    {
        void * t314 = gm_l1;
        uint64_t t315 = GM_LD(uintptr_t, (((uint8_t *)t314) + 40) + 8);
        uint8_t t316 = (!(t315 == ((uint64_t)1ULL)));
        if (!t316) goto ie313;
        {
            gm_f_list_ttksl9g((void *)&F.l36, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t319 = gm_l1;
                gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)t319) + 40));
                gm_l37 = t320;
            }
            {
                gm_l38 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t322 = gm_l38;
                    uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                    uint8_t t324 = (t322 < t323);
                    if (!t324) goto lb321;
                    {
                        uint64_t t327 = gm_l38;
                        uint64_t t328 = (uintptr_t)t327;
                        uint64_t t329 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        gm_t0d02142k4tu8 t332 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t328 * 64));
                        F.l39 = t332;
                        {
                            void * t334 = (void *)((uint8_t *)&F.l36);
                            gm_tnrgict8sthcv t335;
                            memset((uint8_t *)&t335 + 116, 0, 4);
                            gm_t1s75i08qtptd t336;
                            memset(&t336, 0, sizeof t336);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t335 + 0, t336);
                            memcpy((uint8_t *)&t335 + 40, ((uint8_t *)&F.l39), 64);
                            gm_tl874t2atnlh3 t337 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l39) + 48));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t335 + 104, t337);
                            gm_tftb1odc739t5 t338 = gm_f_push_l74t9rq(t334, &t335);
                            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srcoimgd0jo62fr, 85, 27);
                            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
                        }
                        goto me330;
mn331:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 85, 9);
me330:;
                    }
lc321:;
                    {
                        uint64_t t341 = gm_l38;
                        uint64_t r342; bool o343 = __builtin_add_overflow(t341, ((uint64_t)1ULL), &r342);
                        uint64_t t344 = r342;
                        gm_l38 = t344;
                    }
                }
lb321:;
            }
        }
        {
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 60, 0, 4);
            uint64_t t345 = (uintptr_t)((uint64_t)0ULL);
            void * t346 = gm_l1;
            uint64_t t347 = GM_LD(uintptr_t, (((uint8_t *)t346) + 40) + 8);
            if (t345 >= (uintptr_t)(t347)) gm_trap_index(t345, t347, gm_srcoimgd0jo62fr, 86, 35);
            uint32_t t348 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t346) + 40)) + t345 * 64) + 0));
            gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
            gm_to361lgknsu69 t350;
            GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t350 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t350 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)8ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t350 + 8), t349);
            uint64_t t351 = (uintptr_t)((uint64_t)0ULL);
            void * t352 = gm_l1;
            uint64_t t353 = GM_LD(uintptr_t, (((uint8_t *)t352) + 40) + 8);
            if (t351 >= (uintptr_t)(t353)) gm_trap_index(t351, t353, gm_srcoimgd0jo62fr, 86, 81);
            gm_tl874t2atnlh3 t354 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t352) + 40)) + t351 * 64) + 48));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 0, t348);
            GM_ST(gm_to361lgknsu69, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 8, t350);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 48, t354);
        }
        goto ix313;
ie313:;
ix313:;
    }
    {
        gm_f_list_6l26q4a((void *)&F.l40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t355 = gm_l0;
        void * t356 = (void *)((uint8_t *)&F.l35);
        gm_t88rcucg1b7uk t357 = gm_l22;
        gm_t9qkh0rsooc8d t358;
        GM_ST(uint8_t, ((uint8_t *)&t358 + 0), ((uint8_t)1ULL));
        void * t359 = (void *)((uint8_t *)&F.l40);
        void * t360 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t355, t356, t357, t358, t359);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l41 = t360;
    }
    {
        void * t362 = gm_l3;
        gm_t88rcucg1b7uk t363 = gm_l22;
        uint8_t t364 = gm_f_is_error_8k27gd1(t362, GM_CONV(gm_csguankko4lmhd, t363));
        uint8_t t365 = (uint8_t)!t364;
        if (!t365) goto ie361;
        void * t366 = gm_l0;
        void * t367 = GM_LD(void *, (((uint8_t *)t366) + 0));
        void * t368 = gm_l41;
        uint8_t t369 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t367, t368);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t370 = (uint8_t)!t369;
        if (!t370) goto ie361;
        {
            void * t372 = gm_l0;
            gm_tl874t2atnlh3 t373 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 48));
            gm_b16_8 t374;
            GM_ST(void *, (uint8_t *)&t374, (void *)(gm_so4k586t2v79t));
            GM_ST(uintptr_t, (uint8_t *)&t374 + 8, (uintptr_t)(63));
            gm_b0_1 t375 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t372, t373, t374);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix361;
ie361:;
ix361:;
    }
    {
        void * t376 = gm_l0;
        void * t377 = (void *)((uint8_t *)&F.l40);
        gm_b0_1 t378 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t376, t377);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t379 = gm_l0;
        void * t380 = gm_l1;
        void * t381 = (void *)(((uint8_t *)t380) + 64);
        void * t382 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t379, t381, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l42 = t382;
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t383 = (void *)((uint8_t *)&F.l43);
        gm_tbbqefuhv2r8h t384;
        void * t385 = gm_l41;
        GM_ST(void *, (uint8_t *)&t384 + 0, t385);
        GM_ST(void *, (uint8_t *)&t384 + 8, ((void *)0));
        void * t386 = gm_l42;
        GM_ST(void *, (uint8_t *)&t384 + 16, t386);
        gm_tftb1odc739t5 t387 = gm_f_push_bpovr2k_3(t383, &t384);
        if (((((uint8_t *)&t387))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t387) + 1, gm_srcoimgd0jo62fr, 96, 5);
        gm_b0_1 t388 = GM_LD(gm_b0_1, (((uint8_t *)&t387) + 1));
    }
    {
        void * t389 = gm_l3;
        gm_t88rcucg1b7uk t390 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t389);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t391;
        gm_t88rcucg1b7uk t392 = gm_l22;
        uint32_t t393 = gm_l34;
        gm_teti411eujvf4 t394;
        memset(&t394, 0, sizeof t394);
        GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t394 + 8), t393);
        gm_tl874t2atnlh3 t395 = gm_l2;
        void * t396 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t392, &t394, t395);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t391 + 0, t396);
        gm_b16_8 t397 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t391 + 8, t397);
        gm_teti411eujvf4 t398;
        memset(&t398, 0, sizeof t398);
        GM_ST(uint8_t, ((uint8_t *)&t398 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t398 + 8), t391);
        gm_tl874t2atnlh3 t399 = gm_l2;
        void * t400 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t390, &t398, t399);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l44 = t400;
    }
    {
        void * t401 = gm_l0;
        gm_f_exit_fn_kj6vbu8((void *)&F.l45, t401);
    }
    {
        void * t402 = gm_l0;
        void * t403 = (void *)(((uint8_t *)t402) + 192);
        gm_b16_8 t404 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
        gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
        gm_tftb1odc739t5 t406 = gm_f_extend_43vto3v(t403, t405);
        if (((((uint8_t *)&t406))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t406) + 1, gm_srcoimgd0jo62fr, 99, 5);
        gm_b0_1 t407 = GM_LD(gm_b0_1, (((uint8_t *)&t406) + 1));
    }
    {
        uint32_t t408 = gm_l33;
        uint64_t t409 = (uint64_t)t408;
        uint64_t t410 = (uintptr_t)t409;
        void * t411 = gm_l0;
        void * t412 = GM_LD(void *, (((uint8_t *)t411) + 0));
        uint64_t t413 = GM_LD(uintptr_t, ((((uint8_t *)t412) + 384) + 0) + 8);
        if (t410 >= (uintptr_t)(t413)) gm_trap_index(t410, t413, gm_srcoimgd0jo62fr, 100, 5);
        void * t414 = gm_l0;
        gm_t9805qtonbsag t415 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t414) + 400));
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t412) + 384) + 0)) + t410 * 64) + 48), t415);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t416 = (void *)((uint8_t *)&F.l46);
        uint32_t t417 = gm_l34;
        gm_tftb1odc739t5 t418 = gm_f_push_im9d4ro(t416, t417);
        if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 1, gm_srcoimgd0jo62fr, 102, 5);
        gm_b0_1 t419 = GM_LD(gm_b0_1, (((uint8_t *)&t418) + 1));
    }
    {
        void * t420 = gm_l0;
        void * t421 = (void *)(((uint8_t *)t420) + 704);
        gm_tl93vjpcesj48 t422;
        memset((uint8_t *)&t422 + 4, 0, 4);
        uint32_t t423 = gm_l33;
        GM_ST(uint32_t, (uint8_t *)&t422 + 0, t423);
        gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t422 + 8, t424);
        gm_b16_8 t425 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t422 + 24, t425);
        void * t426 = gm_l44;
        GM_ST(void *, (uint8_t *)&t422 + 40, t426);
        gm_tftb1odc739t5 t427 = gm_f_push_jd7smj6(t421, &t422);
        if (((((uint8_t *)&t427))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t427) + 1, gm_srcoimgd0jo62fr, 103, 5);
        gm_b0_1 t428 = GM_LD(gm_b0_1, (((uint8_t *)&t427) + 1));
    }
    {
        {
            void * t430 = gm_l0;
            void * t431 = (void *)(((uint8_t *)t430) + 232);
            gm_te5gbhu3us3k4 t432;
            gm_f_pop_ruvva9t((void *)&t432, t431);
        }
    }
    {
        {
            void * t434 = gm_l0;
            uint32_t t435 = gm_l30;
            uint8_t t436 = gm_f_forget_label_breaks_vl4mn72(t434, t435);
        }
    }
    {
        void * t437 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t437);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t438 = (void *)((uint8_t *)&F.l47);
        uint32_t t439 = gm_l33;
        gm_tftb1odc739t5 t440 = gm_f_push_im9d4ro(t438, t439);
        if (((((uint8_t *)&t440))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t440) + 1, gm_srcoimgd0jo62fr, 108, 5);
        gm_b0_1 t441 = GM_LD(gm_b0_1, (((uint8_t *)&t440) + 1));
    }
    {
        uint32_t t442 = gm_l31;
        uint64_t t443 = (uint64_t)t442;
        uint64_t t444 = (uintptr_t)t443;
        void * t445 = gm_l0;
        void * t446 = GM_LD(void *, (((uint8_t *)t445) + 0));
        uint64_t t447 = GM_LD(uintptr_t, ((((uint8_t *)t446) + 504) + 0) + 8);
        if (t444 >= (uintptr_t)(t447)) gm_trap_index(t444, t447, gm_srcoimgd0jo62fr, 109, 5);
        gm_b16_8 t448 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t446) + 504) + 0)) + t444 * 40) + 24), t448);
    }
    {
        gm_f_list_p0pjtrh((void *)&F.l48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t449 = (void *)((uint8_t *)&F.l48);
        void * t450 = gm_l3;
        uint32_t t451 = gm_l31;
        gm_t53at80pfdus3 t452;
        memset(&t452, 0, sizeof t452);
        GM_ST(uint8_t, ((uint8_t *)&t452 + 0), ((uint8_t)25ULL));
        GM_ST(uint32_t, ((uint8_t *)&t452 + 8), t451);
        gm_t88rcucg1b7uk t453 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t450, &t452);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t454 = gm_l31;
        gm_teti411eujvf4 t455;
        memset(&t455, 0, sizeof t455);
        GM_ST(uint8_t, ((uint8_t *)&t455 + 0), ((uint8_t)28ULL));
        GM_ST(uint32_t, ((uint8_t *)&t455 + 8), t454);
        gm_tl874t2atnlh3 t456 = gm_l2;
        void * t457 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t453, &t455, t456);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4lnf71mruqpt t458;
        GM_ST(uint8_t, (uint8_t *)&t458 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t458 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t458 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t458 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t458 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t458 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t458 + 8), t457);
        gm_tftb1odc739t5 t459 = gm_f_push_f5fsh94(t449, &t458);
        if (((((uint8_t *)&t459))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t459) + 1, gm_srcoimgd0jo62fr, 111, 5);
        gm_b0_1 t460 = GM_LD(gm_b0_1, (((uint8_t *)&t459) + 1));
    }
    {
        gm_t88rcucg1b7uk t461 = gm_l26;
        gm_t710lsrq1m8gk t462;
        memset((uint8_t *)&t462 + 4, 0, 4);
        uint32_t t463 = gm_l30;
        GM_ST(uint32_t, (uint8_t *)&t462 + 0, t463);
        gm_b16_8 t464 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t462 + 8, t464);
        GM_ST(void *, (uint8_t *)&t462 + 24, ((void *)0));
        void * t465 = gm_l6;
        GM_ST(void *, (uint8_t *)&t462 + 32, t465);
        gm_teti411eujvf4 t466;
        GM_ST(uint8_t, (uint8_t *)&t466 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t466 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t466 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t466 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t466 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t466 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t466 + 8), t462);
        gm_tl874t2atnlh3 t467 = gm_l2;
        void * t468 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t461, &t466, t467);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t468;
    }
    {
        uint64_t t470 = GM_LD(uintptr_t, (((uint8_t *)&F.l27) + 0) + 8);
        uint8_t t471 = ((t470 == ((uint64_t)0ULL)));
        if (!t471) goto ie469;
        {
            void * t473 = gm_l49;
            uint8_t t474 = 0;
            F.ret = t473;
            goto gm_fret;
        }
        goto ix469;
ie469:;
ix469:;
    }
    {
        gm_t88rcucg1b7uk t475 = gm_l26;
        gm_b16_8 t476 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
        void * t477 = gm_l49;
        gm_tl874t2atnlh3 t478 = gm_l2;
        void * t479 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t475, t476, t477, t478);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t480 = 0;
        F.ret = t479;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_hoist_args_po0rnjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_hoist_args_po0rnjr F;
    void * gm_l0; /* cx */
    void * gm_l1; /* call */
    void * gm_l2; /* pre */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* a */
    uint32_t gm_l8; /* t */
    void * gm_l9; /* v */
    GM_STACK_CHECK(&F, 594u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 40));
        gm_l3 = t5;
        {
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l4 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = gm_l4;
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    {
                        uint64_t t15 = gm_l5;
                        uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&t15));
                        gm_l6 = t18;
                        {
                            uint64_t t20 = gm_l6;
                            uint64_t t21 = (uintptr_t)t20;
                            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcoimgd0jo62fr, 159, 15);
                            void * t23 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t21 * 88);
                            gm_l7 = t23;
                        }
                        {
                            void * t25 = gm_l7;
                            uint8_t t26;
                            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                            switch (t33) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma31; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma29; case ((uint8_t)6ULL): goto ma31; case ((uint8_t)7ULL): goto ma31; case ((uint8_t)8ULL): goto ma31; case ((uint8_t)9ULL): goto ma31; case ((uint8_t)10ULL): goto ma31; case ((uint8_t)11ULL): goto ma31; case ((uint8_t)12ULL): goto ma31; case ((uint8_t)13ULL): goto ma31; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma31; case ((uint8_t)16ULL): goto ma31; case ((uint8_t)17ULL): goto ma31; case ((uint8_t)18ULL): goto ma31; case ((uint8_t)19ULL): goto ma31; case ((uint8_t)20ULL): goto ma31; case ((uint8_t)21ULL): goto ma31; case ((uint8_t)22ULL): goto ma31; case ((uint8_t)23ULL): goto ma31; case ((uint8_t)24ULL): goto ma31; case ((uint8_t)25ULL): goto ma31; case ((uint8_t)26ULL): goto ma31; case ((uint8_t)27ULL): goto ma31; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma31; case ((uint8_t)30ULL): goto ma31; case ((uint8_t)31ULL): goto ma31; case ((uint8_t)32ULL): goto ma31; case ((uint8_t)33ULL): goto ma31; case ((uint8_t)34ULL): goto ma31; case ((uint8_t)35ULL): goto ma31; case ((uint8_t)36ULL): goto ma31; case ((uint8_t)37ULL): goto ma31; case ((uint8_t)38ULL): goto ma31; case ((uint8_t)39ULL): goto ma31; case ((uint8_t)40ULL): goto ma31; case ((uint8_t)41ULL): goto ma31; case ((uint8_t)42ULL): goto ma31; case ((uint8_t)43ULL): goto ma31; case ((uint8_t)44ULL): goto ma31; case ((uint8_t)45ULL): goto ma31; case ((uint8_t)46ULL): goto ma31; case ((uint8_t)47ULL): goto ma31; case ((uint8_t)48ULL): goto ma31; case ((uint8_t)49ULL): goto ma31; case ((uint8_t)50ULL): goto ma31; case ((uint8_t)51ULL): goto ma31; case ((uint8_t)52ULL): goto ma31; case ((uint8_t)53ULL): goto ma31; case ((uint8_t)54ULL): goto ma31; case ((uint8_t)55ULL): goto ma31; case ((uint8_t)56ULL): goto ma31; case ((uint8_t)57ULL): goto ma31; case ((uint8_t)58ULL): goto ma31; default: goto ma31; }
ma28:;
                            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                            if (t35 != ((uint8_t)0ULL)) goto mn34;
                            t26 = ((uint8_t)1);
                            goto me27;
mn34:;
ma29:;
                            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                            if (t37 != ((uint8_t)5ULL)) goto mn36;
                            t26 = ((uint8_t)1);
                            goto me27;
mn36:;
ma30:;
                            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                            if (t39 != ((uint8_t)4ULL)) goto mn38;
                            t26 = ((uint8_t)1);
                            goto me27;
mn38:;
ma31:;
                            t26 = ((uint8_t)0);
                            goto me27;
mn40:;
ma32:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 160, 12);
me27:;
                            if (!t26) goto ie24;
                            {
                                goto lc9;
                            }
                            goto ix24;
ie24:;
ix24:;
                        }
                        {
                            void * t42 = gm_l0;
                            void * t43 = gm_l7;
                            void * t44 = gm_l2;
                            gm_b0_1 t45 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t42, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            void * t46 = gm_l0;
                            void * t47 = gm_l7;
                            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 0));
                            void * t49 = gm_l7;
                            gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
                            uint32_t t51 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t46, t48, t50);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_l8 = t51;
                        }
                        {
                            void * t52 = gm_l7;
                            gm_tq7s98ihonkju t53 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_t69m56meqefs8 *)((uint8_t *)t52)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1umgjqrbkf)));
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcoimgd0jo62fr, 166, 14);
                            void * t54 = GM_LD(void *, (((uint8_t *)&t53) + 8));
                            gm_l9 = t54;
                        }
                        {
                            void * t55 = gm_l7;
                            uint8_t *t56 = ((uint8_t *)t55);
                            void * t57 = gm_l7;
                            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t57) + 0));
                            uint32_t t59 = gm_l8;
                            gm_teti411eujvf4 t60;
                            memset(&t60, 0, sizeof t60);
                            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t60 + 8), t59);
                            void * t61 = gm_l7;
                            gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 72));
                            void * t63 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t58, &t60, t62);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_t69m56meqefs8 t64 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t63));
                            GM_ST(gm_t69m56meqefs8, t56, t64);
                        }
                        {
                            void * t65 = gm_l2;
                            uint32_t t66 = gm_l8;
                            void * t67 = gm_l9;
                            gm_ttggq7ng4kp3p t68;
                            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t68 + 0), t66);
                            GM_ST(void *, ((uint8_t *)&t68 + 8), t67);
                            gm_trcmmd12cefb1 t69;
                            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t69 + 8), t68);
                            gm_tftb1odc739t5 t70 = gm_f_push_d6c76ud_2(t65, &t69);
                            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcoimgd0jo62fr, 168, 9);
                            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                        }
                        goto me16;
mn17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 158, 5);
me16:;
                    }
lc9:;
                    {
                        uint64_t t73 = gm_l5;
                        uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                        uint64_t t76 = r74;
                        gm_l5 = t76;
                    }
                }
gm_ll13:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
                }
                goto gm_lad;
lb9:;
            }
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t78;
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) void gm_f_hoist_iter_value_cs229ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_hoist_iter_value_cs229ip F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* receiver */
    void * gm_l4; /* x */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    void * gm_l8; /* base */
    uint32_t gm_l9; /* index */
    gm_tl874t2atnlh3 gm_l10; /* index_span */
    void * gm_l12; /* base */
    void * gm_l13; /* index */
    void * gm_l16; /* x */
    void * gm_l18; /* operand */
    uint64_t gm_l22; /* mark */
    void * gm_l23; /* v */
    gm_t88rcucg1b7uk gm_l24; /* ty */
    uint8_t gm_l25; /* keep_place */
    gm_b16_8 gm_l27; /* name */
    uint32_t gm_l28; /* local */
    GM_STACK_CHECK(&F, 3258u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        uint8_t t5 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t5) goto ie2;
        {
            void * t7 = gm_l1;
            memmove(gm_out, ((uint8_t *)t7), 88);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t0mfir12leg2g t8;
        memset(&t8, 0, sizeof t8);
        F.l3 = t8;
    }
    {
        void * t9 = gm_l1;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma17; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma18; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma18; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma18; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma18; case ((uint8_t)27ULL): goto ma18; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma18; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma18; case ((uint8_t)33ULL): goto ma18; case ((uint8_t)34ULL): goto ma18; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma18; case ((uint8_t)37ULL): goto ma18; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma18; case ((uint8_t)40ULL): goto ma18; case ((uint8_t)41ULL): goto ma18; case ((uint8_t)42ULL): goto ma18; case ((uint8_t)43ULL): goto ma18; case ((uint8_t)44ULL): goto ma18; case ((uint8_t)45ULL): goto ma18; case ((uint8_t)46ULL): goto ma18; case ((uint8_t)47ULL): goto ma18; case ((uint8_t)48ULL): goto ma18; case ((uint8_t)49ULL): goto ma18; case ((uint8_t)50ULL): goto ma18; default: goto ma18; }
ma11:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t22 != ((uint8_t)9ULL)) goto mn21;
        void * t23 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l4 = t23;
        void * t24 = gm_l0;
        void * t25 = gm_l4;
        uint8_t t26 = gm_l2;
        gm_f_hoist_iter_value_cs229ip((void *)&(*(gm_tc3oht4ksth9d *)gm_out), (void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me10;
mn21:;
ma12:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t28 != ((uint8_t)6ULL)) goto mn27;
        {
            void * t30 = gm_l1;
            const uint8_t *t31 = ((uint8_t *)t30);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)))[88] = 1;
            memmove((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 0, t31, 88);
        }
        goto me10;
mn27:;
ma13:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t33 != ((uint8_t)22ULL)) goto mn32;
        void * t34 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l5 = t34;
        gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l6 = t35;
        {
            void * t37 = gm_l0;
            void * t38 = gm_l5;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l7, (void *)NULL, cx, NULL, t37, t38, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tc3oht4ksth9d t39;
            memset((uint8_t *)&t39 + 4, 0, 4);
            memset((uint8_t *)&t39 + 84, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t39 + 0, ((uint32_t)4294967295ULL));
            gm_tioie2ju3q4ul t40;
            gm_tq7s98ihonkju t41 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq6te48ps2f)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcoimgd0jo62fr, 314, 78);
            void * t42 = GM_LD(void *, (((uint8_t *)&t41) + 8));
            GM_ST(void *, (uint8_t *)&t40 + 0, t42);
            memcpy((uint8_t *)&t40 + 8, ((uint8_t *)&gm_l6), 32);
            gm_t2gtbh39ptekd t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t43 + 8), t40);
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&t39 + 8, t43);
            void * t44 = gm_l1;
            gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t44) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t39 + 72, t45);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)))[88] = 1;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 0, t39);
        }
        goto me10;
mn32:;
ma14:;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t47 != ((uint8_t)23ULL)) goto mn46;
        void * t48 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l8 = t48;
        uint32_t t49 = GM_LD(uint32_t, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l9 = t49;
        gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t9) + 8) + 8) + 12));
        gm_l10 = t50;
        {
            void * t52 = gm_l0;
            void * t53 = gm_l8;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l11, (void *)NULL, cx, NULL, t52, t53, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tc3oht4ksth9d t54;
            memset((uint8_t *)&t54 + 4, 0, 4);
            memset((uint8_t *)&t54 + 84, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t54 + 0, ((uint32_t)4294967295ULL));
            gm_tupk62cr6lbdu t55;
            gm_tq7s98ihonkju t56 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l11)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kukq3ep6ok2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcoimgd0jo62fr, 318, 83);
            void * t57 = GM_LD(void *, (((uint8_t *)&t56) + 8));
            GM_ST(void *, (uint8_t *)&t55 + 0, t57);
            uint32_t t58 = gm_l9;
            GM_ST(uint32_t, (uint8_t *)&t55 + 8, t58);
            gm_tl874t2atnlh3 t59 = gm_l10;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t55 + 12, t59);
            gm_t2gtbh39ptekd t60;
            memset(&t60, 0, sizeof t60);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)23ULL));
            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t60 + 8), t55);
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&t54 + 8, t60);
            void * t61 = gm_l1;
            gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t54 + 72, t62);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)))[88] = 1;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 0, t54);
        }
        goto me10;
mn46:;
ma15:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t64 != ((uint8_t)20ULL)) goto mn63;
        void * t65 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l12 = t65;
        void * t66 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l13 = t66;
        {
            void * t68 = gm_l0;
            void * t69 = gm_l12;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l14, (void *)NULL, cx, NULL, t68, t69, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t70 = gm_l0;
            void * t71 = gm_l13;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l15, (void *)NULL, cx, NULL, t70, t71, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tc3oht4ksth9d t72;
            memset((uint8_t *)&t72 + 4, 0, 4);
            memset((uint8_t *)&t72 + 84, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t72 + 0, ((uint32_t)4294967295ULL));
            gm_tq7s98ihonkju t73 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l14)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8gk4puesad)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcoimgd0jo62fr, 323, 78);
            void * t74 = GM_LD(void *, (((uint8_t *)&t73) + 8));
            gm_tq7s98ihonkju t75 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l15)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbng43moaud)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srcoimgd0jo62fr, 323, 94);
            void * t76 = GM_LD(void *, (((uint8_t *)&t75) + 8));
            gm_t3mf58gv9kf3n t77;
            GM_ST(void *, ((uint8_t *)&t77 + 0), t74);
            GM_ST(void *, ((uint8_t *)&t77 + 8), t76);
            gm_t2gtbh39ptekd t78;
            memset(&t78, 0, sizeof t78);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)20ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t78 + 8), t77);
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&t72 + 8, t78);
            void * t79 = gm_l1;
            gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t79) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t72 + 72, t80);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)))[88] = 1;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 0, t72);
        }
        goto me10;
mn63:;
ma16:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t82 != ((uint8_t)18ULL)) goto mn81;
        void * t83 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l16 = t83;
        {
            void * t85 = gm_l0;
            void * t86 = gm_l16;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l17, (void *)NULL, cx, NULL, t85, t86, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tc3oht4ksth9d t87;
            memset((uint8_t *)&t87 + 4, 0, 4);
            memset((uint8_t *)&t87 + 84, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t87 + 0, ((uint32_t)4294967295ULL));
            gm_tq7s98ihonkju t88 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l17)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krlms0mdefs)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_srcoimgd0jo62fr, 327, 72);
            void * t89 = GM_LD(void *, (((uint8_t *)&t88) + 8));
            gm_t2gtbh39ptekd t90;
            memset(&t90, 0, sizeof t90);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t90 + 8), t89);
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&t87 + 8, t90);
            void * t91 = gm_l1;
            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t91) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t87 + 72, t92);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)))[88] = 1;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l3)) + 0, t87);
        }
        goto me10;
mn81:;
ma17:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t94 != ((uint8_t)12ULL)) goto mn93;
        uint8_t t95 = GM_LD(uint8_t, ((((((uint8_t *)t9) + 8) + 8) + 0) + 0));
        if (t95 != ((uint8_t)2ULL)) goto mn93;
        void * t96 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l18 = t96;
        {
            void * t98 = gm_l0;
            void * t99 = gm_l18;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l19, (void *)NULL, cx, NULL, t98, t99, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
            gm_t9qkh0rsooc8d t100;
            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)2ULL));
            gm_tq7s98ihonkju t101 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l19)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kb8poqrtf7g)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_srcoimgd0jo62fr, 331, 93);
            void * t102 = GM_LD(void *, (((uint8_t *)&t101) + 8));
            gm_t8ghgl2jj0qha t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t103 + 0), t100);
            GM_ST(void *, ((uint8_t *)&t103 + 8), t102);
            gm_t2gtbh39ptekd t104;
            memset(&t104, 0, sizeof t104);
            GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t104 + 8), t103);
            void * t105 = gm_l1;
            gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 72));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, ((uint32_t)4294967295ULL));
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t104);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t106);
            goto gm_fdone;
        }
        goto me10;
mn93:;
ma18:;
        goto me10;
mn107:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 309, 5);
me10:;
    }
    {
        gm_tc3oht4ksth9d t109;
        if (!((((uint8_t *)&F.l3))[88] != 0)) goto ie110;
        gm_tc3oht4ksth9d t111 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
        F.l20 = t111;
        gm_tc3oht4ksth9d t113 = F.l20;
        gm_tc3oht4ksth9d t114 = t113;
        t109 = t114;
        goto ix110;
ie110:;
        void * t116 = gm_l1;
        gm_tc3oht4ksth9d t117 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t116));
        gm_tc3oht4ksth9d t118 = t117;
        t109 = t118;
ix110:;
        F.l21 = t109;
    }
    {
        void * t119 = gm_l0;
        uint64_t t120 = GM_LD(uintptr_t, ((((uint8_t *)t119) + 192) + 0) + 8);
        gm_l22 = t120;
    }
    {
        void * t121 = gm_l0;
        void * t122 = (void *)((uint8_t *)&F.l21);
        gm_tkn42u7na3t81 t123;
        memset(&t123, 0, sizeof t123);
        void * t124 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t121, t122, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t124;
    }
    {
        uint8_t t126;
        uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 8) + 0));
        if (t129 != ((uint8_t)19ULL)) goto mn128;
        t126 = ((uint8_t)1);
        goto me127;
mn128:;
        t126 = ((uint8_t)0);
        goto me127;
mn130:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 340, 8);
me127:;
        if (!t126) goto ie125;
        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 72));
        void * t132 = gm_l0;
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((((uint8_t *)t132) + 960) + 0));
        uint8_t t134 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t133) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t133))) + i * 12, ((uint8_t *)&t131))) { t134 = 1; break; }
        uint8_t t135 = (uint8_t)(t134);
        if (!t135) goto ie125;
        {
            void * t137 = gm_l0;
            void * t138 = (void *)(((uint8_t *)t137) + 192);
            uint64_t t139 = gm_l22;
            gm_f_truncate_ua7l21h(t138, t139);
        }
        {
            void * t140 = gm_l0;
            void * t141 = (void *)((uint8_t *)&F.l21);
            gm_f_hoist_iterator_call_5rmgsce((void *)&(*(gm_tc3oht4ksth9d *)gm_out), (void *)NULL, cx, NULL, t140, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix125;
ie125:;
ix125:;
    }
    {
        void * t142 = gm_l0;
        void * t143 = (void *)(((uint8_t *)t142) + 96);
        void * t144 = gm_l0;
        void * t145 = GM_LD(void *, (((uint8_t *)t144) + 0));
        void * t146 = gm_l23;
        gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t146) + 0));
        gm_t88rcucg1b7uk t148 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t143, t145, t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t148;
    }
    {
        uint8_t t149 = 0;
        uint8_t t150;
        if (((((uint8_t *)&F.l3))[88] != 0)) goto mn152;
        t150 = ((uint8_t)0);
        goto me151;
mn152:;
        t150 = ((uint8_t)1);
        goto me151;
mn153:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 345, 19);
me151:;
        if (t150) {
            uint8_t t154 = 0;
            uint8_t t155 = gm_l2;
            if (!t155) {
                void * t156 = gm_l0;
                void * t157 = GM_LD(void *, (((uint8_t *)t156) + 0));
                void * t158 = (void *)(((uint8_t *)t157) + 8);
                gm_t88rcucg1b7uk t159 = gm_l24;
                gm_t53at80pfdus3 t160;
                gm_f_kind_jcnt4hm((void *)&t160, t158, GM_CONV(gm_csguankko4lmhd, t159));
                uint8_t t161;
                uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&t160) + 0));
                if (t164 != ((uint8_t)11ULL)) goto mn163;
                t161 = ((uint8_t)1);
                goto me162;
mn163:;
                t161 = ((uint8_t)0);
                goto me162;
mn165:;
                gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 345, 54);
me162:;
                t154 = t161;
            } else t154 = 1;
            t149 = t154;
        } else t149 = 0;
        gm_l25 = t149;
    }
    {
        uint8_t t167 = 0;
        uint8_t t168 = 0;
        uint8_t t169 = 0;
        void * t170 = gm_l0;
        void * t171 = GM_LD(void *, (((uint8_t *)t170) + 0));
        void * t172 = (void *)(((uint8_t *)t171) + 8);
        gm_t88rcucg1b7uk t173 = gm_l24;
        uint8_t t174 = gm_f_is_error_8k27gd1(t172, GM_CONV(gm_csguankko4lmhd, t173));
        if (!t174) {
            void * t175 = gm_l0;
            void * t176 = GM_LD(void *, (((uint8_t *)t175) + 0));
            void * t177 = (void *)(((uint8_t *)t176) + 8);
            gm_t88rcucg1b7uk t178 = gm_l24;
            uint8_t t179 = gm_f_has_infer_3o82a0b(t177, GM_CONV(gm_csguankko4lmhd, t178));
            t169 = t179;
        } else t169 = 1;
        if (!t169) {
            void * t180 = gm_l0;
            void * t181 = GM_LD(void *, (((uint8_t *)t180) + 0));
            void * t182 = (void *)(((uint8_t *)t181) + 8);
            gm_t88rcucg1b7uk t183 = gm_l24;
            gm_t53at80pfdus3 t184;
            gm_f_kind_jcnt4hm((void *)&t184, t182, GM_CONV(gm_csguankko4lmhd, t183));
            uint8_t t185;
            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&t184) + 0));
            if (t188 != ((uint8_t)15ULL)) goto mn187;
            t185 = ((uint8_t)1);
            goto me186;
mn187:;
            t185 = ((uint8_t)0);
            goto me186;
mn189:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 348, 59);
me186:;
            t168 = t185;
        } else t168 = 1;
        if (!t168) {
            uint8_t t190 = gm_l25;
            t167 = t190;
        } else t167 = 1;
        if (!t167) goto ie166;
        {
            memmove(gm_out, ((uint8_t *)&F.l21), 88);
            goto gm_fdone;
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        void * t192 = gm_l0;
        gm_trqfvmjtt4sij t193 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t192) + 1160));
        F.l26 = t193;
    }
    {
        void * t194 = gm_l0;
        uint8_t *t195 = (((uint8_t *)t194) + 1160);
        gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t195), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t196 = gm_l0;
        void * t197 = gm_l23;
        void * t198 = (void *)((uint8_t *)&F.l26);
        gm_b0_1 t199 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t196, t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t200 = gm_l0;
        uint64_t t201 = GM_LD(uintptr_t, ((((uint8_t *)t200) + 16) + 0) + 8);
        gm_t4ce0jd3tdro4 t202;
        GM_ST(uint64_t, ((uint8_t *)&t202 + 0), t201);
        gm_tabb710d9mq2e t203;
        gm_f_format_t04dp7b((void *)&t203, (void *)NULL, cx, NULL, t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 8, gm_srcoimgd0jo62fr, 352, 20);
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&t203) + 8));
        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
        gm_l27 = t205;
    }
    {
        void * t206 = gm_l0;
        gm_b16_8 t207 = gm_l27;
        gm_t88rcucg1b7uk t208 = gm_l24;
        void * t209 = gm_l1;
        gm_tl874t2atnlh3 t210 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t209) + 72));
        uint32_t t211 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t206, t207, t208, ((uint8_t)1), t210);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l28 = t211;
    }
    {
        void * t212 = (void *)((uint8_t *)&F.l26);
        uint32_t t213 = gm_l28;
        void * t214 = gm_l23;
        gm_ttggq7ng4kp3p t215;
        GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t215 + 0), t213);
        GM_ST(void *, ((uint8_t *)&t215 + 8), t214);
        gm_trcmmd12cefb1 t216;
        GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t216 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t216 + 8), t215);
        gm_tftb1odc739t5 t217 = gm_f_push_d6c76ud_2(t212, &t216);
        if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srcoimgd0jo62fr, 354, 5);
        gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
    }
    {
        void * t219 = gm_l0;
        gm_trqfvmjtt4sij t220 = F.l26;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t219) + 1160), t220);
    }
    {
        void * t221 = gm_l0;
        gm_b16_8 t222 = gm_l27;
        uint32_t t223 = gm_l28;
        gm_t930q5l38hdap t224;
        memset(&t224, 0, sizeof t224);
        GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t224 + 8), t223);
        void * t225 = gm_l1;
        gm_tl874t2atnlh3 t226 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t225) + 72));
        gm_b0_1 t227 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t221, t222, &t224, t226);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
        memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
        gm_t08olq97rscn3 t228;
        memset((uint8_t *)&t228 + 28, 0, 4);
        gm_b16_8 t229 = gm_l27;
        GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t229);
        void * t230 = gm_l1;
        gm_tl874t2atnlh3 t231 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t230) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t228 + 16, t231);
        gm_t2gtbh39ptekd t232;
        GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t232 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t232 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t232 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)6ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t232 + 8), t228);
        void * t233 = gm_l1;
        gm_tl874t2atnlh3 t234 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t233) + 72));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, ((uint32_t)4294967295ULL));
        GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t232);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t234);
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
__attribute__((aligned(4))) void gm_f_hoist_iterator_call_5rmgsce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_hoist_iterator_call_5rmgsce F;
    void * gm_l0; /* cx */
    void * gm_l1; /* call */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    uint8_t gm_l4; /* trailing_comma */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l14; /* n */
    GM_STACK_CHECK(&F, 2210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)19ULL)) goto ie2;
        void * t5 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        uint8_t t7 = GM_LD(uint8_t, (((((uint8_t *)t3) + 8) + 8) + 24));
        gm_l4 = t7;
        {
            void * t9 = gm_l2;
            gm_tc3oht4ksth9d t10 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t9));
            F.l5 = t10;
        }
        {
            void * t12 = gm_l2;
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            if (t13 != ((uint8_t)22ULL)) goto ie11;
            void * t14 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 0));
            gm_l6 = t14;
            gm_t08olq97rscn3 t15 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t12) + 8) + 8) + 8));
            gm_l7 = t15;
            {
                void * t18 = gm_l0;
                void * t19 = gm_l6;
                uint8_t t20 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t21 = (uint8_t)!t20;
                if (!t21) goto ie17;
                {
                    void * t23 = gm_l0;
                    void * t24 = gm_l6;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l8, (void *)NULL, cx, NULL, t23, t24, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)) + 4, 0, 4);
                    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)) + 84, 0, 4);
                    void * t25 = gm_l2;
                    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 0));
                    gm_tioie2ju3q4ul t27;
                    gm_tq7s98ihonkju t28 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l8)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl5b02juaoa)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcoimgd0jo62fr, 261, 69);
                    void * t29 = GM_LD(void *, (((uint8_t *)&t28) + 8));
                    GM_ST(void *, (uint8_t *)&t27 + 0, t29);
                    memcpy((uint8_t *)&t27 + 8, ((uint8_t *)&gm_l7), 32);
                    gm_t2gtbh39ptekd t30;
                    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t30 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t30 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)22ULL));
                    GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t30 + 8), t27);
                    void * t31 = gm_l2;
                    gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)) + 0, t26);
                    GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)) + 8, t30);
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)) + 72, t32);
                }
                goto ix17;
ie17:;
ix17:;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            gm_f_list_6oos54v((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t34 = gm_l3;
                gm_l10 = t34;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t36 = gm_l11;
                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t38 = (t36 < t37);
                    if (!t38) goto lb35;
                    {
                        uint64_t t41 = gm_l11;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t7te2tdpebbt4 t46 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t42 * 144));
                        F.l12 = t46;
                        {
                            gm_t22mtv3sd9prf t48;
                            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            switch (t54) { case ((uint8_t)0ULL): goto ma50; case ((uint8_t)1ULL): goto ma51; case ((uint8_t)2ULL): goto ma52; default: goto ma53; }
ma50:;
                            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            if (t56 != ((uint8_t)0ULL)) goto mn55;
                            gm_tc3oht4ksth9d t57 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l12) + 0) + 8));
                            F.l13 = t57;
                            void * t58 = gm_l0;
                            void * t59 = (void *)((uint8_t *)&F.l13);
                            gm_tc3oht4ksth9d t60;
                            gm_f_hoist_iter_value_cs229ip((void *)&t60, (void *)NULL, cx, NULL, t58, t59, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll39;
                            }
                            gm_t22mtv3sd9prf t61;
                            memset(&t61, 0, sizeof t61);
                            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t61 + 8), t60);
                            t48 = t61;
                            goto me49;
mn55:;
ma51:;
                            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            if (t63 != ((uint8_t)1ULL)) goto mn62;
                            gm_t08olq97rscn3 t64 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l12) + 0) + 8) + 0));
                            gm_l14 = t64;
                            gm_tc3oht4ksth9d t65 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l12) + 0) + 8) + 32));
                            F.l15 = t65;
                            gm_t56mj5hnm2uc6 t66;
                            memcpy((uint8_t *)&t66 + 0, ((uint8_t *)&gm_l14), 32);
                            void * t67 = gm_l0;
                            void * t68 = (void *)((uint8_t *)&F.l15);
                            gm_f_hoist_iter_value_cs229ip((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&t66 + 32)), (void *)NULL, cx, NULL, t67, t68, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll39;
                            }
                            gm_t22mtv3sd9prf t69;
                            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t69 + 8), t66);
                            t48 = t69;
                            goto me49;
mn62:;
ma52:;
                            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            if (t71 != ((uint8_t)2ULL)) goto mn70;
                            gm_tc3oht4ksth9d t72 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l12) + 0) + 8));
                            F.l16 = t72;
                            gm_tc3oht4ksth9d t73 = F.l16;
                            gm_t22mtv3sd9prf t74;
                            memset(&t74, 0, sizeof t74);
                            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t74 + 8), t73);
                            t48 = t74;
                            goto me49;
mn70:;
ma53:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 266, 29);
me49:;
                            F.l17 = t48;
                        }
                        {
                            void * t75 = (void *)((uint8_t *)&F.l9);
                            gm_t7te2tdpebbt4 t76;
                            memset((uint8_t *)&t76 + 140, 0, 4);
                            memcpy((uint8_t *)&t76 + 0, ((uint8_t *)&F.l17), 128);
                            gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t76 + 128, t77);
                            gm_tftb1odc739t5 t78 = gm_f_push_jis5lhu(t75, &t76);
                            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcoimgd0jo62fr, 271, 9);
                            gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                        }
                        goto me44;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 265, 5);
me44:;
                    }
lc35:;
                    {
                        uint64_t t81 = gm_l11;
                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                        uint64_t t84 = r82;
                        gm_l11 = t84;
                    }
                }
gm_ll39:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
                }
                goto gm_lad;
lb35:;
            }
        }
        {
            memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
            void * t85 = gm_l1;
            uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)t85) + 0));
            gm_tlljg61a2ctgq t87;
            memset((uint8_t *)&t87 + 25, 0, 7);
            gm_tq7s98ihonkju t88 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq6mn01fhkq)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_srcoimgd0jo62fr, 273, 54);
            void * t89 = GM_LD(void *, (((uint8_t *)&t88) + 8));
            GM_ST(void *, (uint8_t *)&t87 + 0, t89);
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 8, t90);
            uint8_t t91 = gm_l4;
            GM_ST(uint8_t, (uint8_t *)&t87 + 24, t91);
            gm_t2gtbh39ptekd t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t92 + 8), t87);
            void * t93 = gm_l1;
            gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, t86);
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t92);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t94);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            void * t96 = gm_l1;
            memmove(gm_out, ((uint8_t *)t96), 88);
            goto gm_fdone;
        }
ix2:;
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
__attribute__((aligned(4))) uint8_t gm_f_iter_stable_k9c87oo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_iter_stable_k9c87oo F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    void * gm_l3; /* x */
    void * gm_l4; /* base */
    gm_t9qkh0rsooc8d gm_l5; /* op */
    void * gm_l6; /* operand */
    void * gm_l7; /* lhs */
    void * gm_l8; /* rhs */
    void * gm_l9; /* callee */
    gm_b16_8 gm_l10; /* args */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 1068u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)0ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)1ULL)) goto pa18;
        goto po15;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)2ULL)) goto pa20;
        goto po15;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)3ULL)) goto pa22;
        goto po15;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)4ULL)) goto pa24;
        goto po15;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)5ULL)) goto pa26;
        goto po15;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)7ULL)) goto pa28;
        goto po15;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)8ULL)) goto pa30;
        goto po15;
pa30:;
        goto mn14;
po15:;
        uint8_t t32 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn14:;
ma5:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)6ULL)) goto mn33;
        gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t35;
        void * t36 = gm_l0;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_tqvu78rpi6q1h t38;
        gm_f_lookup_local_6f38t7p((void *)&t38, t36, t37);
        uint8_t t39;
        if (((((uint8_t *)&t38))[208] != 0)) goto mn41;
        t39 = ((uint8_t)1);
        goto me40;
mn41:;
        t39 = ((uint8_t)0);
        goto me40;
mn42:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 281, 30);
me40:;
        uint8_t t43 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me3;
mn33:;
ma6:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)9ULL)) goto mn44;
        void * t46 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t46;
        void * t47 = gm_l0;
        void * t48 = gm_l3;
        uint8_t t49 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
        goto me3;
mn44:;
ma7:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)22ULL)) goto mn51;
        void * t53 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t53;
        void * t54 = gm_l0;
        void * t55 = gm_l4;
        uint8_t t56 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
        goto me3;
mn51:;
ma8:;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t59 != ((uint8_t)12ULL)) goto mn58;
        gm_t9qkh0rsooc8d t60 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t60;
        void * t61 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t61;
        uint8_t t62 = 0;
        gm_t9qkh0rsooc8d t63 = gm_l5;
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t65;
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t66;
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)5ULL));
        gm_tk22flr9mh3ql t67;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 0), t64);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 1), t65);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 2), t66);
        uint8_t t68 = 0;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t67) + 0, ((uint8_t *)&t63))) t68 = 1;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t67) + 1, ((uint8_t *)&t63))) t68 = 1;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t67) + 2, ((uint8_t *)&t63))) t68 = 1;
        uint8_t t69 = (uint8_t)(t68);
        if (t69) {
            void * t70 = gm_l0;
            void * t71 = gm_l6;
            uint8_t t72 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t62 = t72;
        } else t62 = 0;
        uint8_t t73 = 0;
        F.ret = t62;
        goto gm_fret;
        goto me3;
mn58:;
ma9:;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t75 != ((uint8_t)13ULL)) goto mn74;
        void * t76 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t76;
        void * t77 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l8 = t77;
        uint8_t t78 = 0;
        void * t79 = gm_l0;
        void * t80 = gm_l7;
        uint8_t t81 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t81) {
            void * t82 = gm_l0;
            void * t83 = gm_l8;
            uint8_t t84 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t78 = t84;
        } else t78 = 0;
        uint8_t t85 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me3;
mn74:;
ma10:;
        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t87 != ((uint8_t)19ULL)) goto mn86;
        void * t88 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t88;
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t89;
        {
            void * t92 = gm_l9;
            uint8_t t93;
            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t92) + 8) + 0));
            if (t96 != ((uint8_t)8ULL)) goto mn95;
            t93 = ((uint8_t)0);
            goto me94;
mn95:;
            t93 = ((uint8_t)1);
            goto me94;
mn97:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 288, 16);
me94:;
            if (!t93) goto ie91;
            {
                uint8_t t99 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix91;
ie91:;
ix91:;
        }
        {
            {
                gm_b16_8 t101 = gm_l10;
                gm_l11 = t101;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t103 = gm_l12;
                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t105 = (t103 < t104);
                    if (!t105) goto lb102;
                    {
                        uint64_t t108 = gm_l12;
                        uint64_t t109 = (uintptr_t)t108;
                        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t7te2tdpebbt4 t113 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t109 * 144));
                        F.l13 = t113;
                        {
                            uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            switch (t120) { case ((uint8_t)0ULL): goto ma116; case ((uint8_t)1ULL): goto ma117; case ((uint8_t)2ULL): goto ma118; default: goto ma119; }
ma116:;
                            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t122 != ((uint8_t)0ULL)) goto mn121;
                            gm_tc3oht4ksth9d t123 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l13) + 0) + 8));
                            F.l14 = t123;
                            void * t125 = gm_l0;
                            void * t126 = (void *)((uint8_t *)&F.l14);
                            uint8_t t127 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t125, t126);
                            if (gm_unw.kind) {
                                goto gm_ll106;
                            }
                            uint8_t t128 = (uint8_t)!t127;
                            if (!t128) goto ie124;
                            {
                                uint8_t t130 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix124;
ie124:;
ix124:;
                            goto me115;
mn121:;
ma117:;
                            uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t132 != ((uint8_t)1ULL)) goto mn131;
                            gm_tc3oht4ksth9d t133 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l13) + 0) + 8) + 32));
                            F.l15 = t133;
                            void * t135 = gm_l0;
                            void * t136 = (void *)((uint8_t *)&F.l15);
                            uint8_t t137 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t135, t136);
                            if (gm_unw.kind) {
                                goto gm_ll106;
                            }
                            uint8_t t138 = (uint8_t)!t137;
                            if (!t138) goto ie134;
                            {
                                uint8_t t140 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix134;
ie134:;
ix134:;
                            goto me115;
mn131:;
ma118:;
                            uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t142 != ((uint8_t)2ULL)) goto mn141;
                            uint8_t t143 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                            goto me115;
mn141:;
ma119:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 290, 17);
me115:;
                        }
                        goto me111;
mn112:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 289, 13);
me111:;
                    }
lc102:;
                    {
                        uint64_t t145 = gm_l12;
                        uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                        uint64_t t148 = r146;
                        gm_l12 = t148;
                    }
                }
gm_ll106:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb102; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc102; }
                }
                goto gm_lad;
lb102:;
            }
        }
        {
            uint8_t t149 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn86:;
ma11:;
        void * t151 = gm_l0;
        void * t152 = gm_l1;
        uint8_t t153 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t151, t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t154 = 0;
        F.ret = t153;
        goto gm_fret;
        goto me3;
mn150:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 279, 5);
me3:;
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
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_iterator_arg_r0if28f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l4; /* y */
    uint8_t gm_l5; /* yields */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tiilmfd16v3fc gm_l8; /* x */
    void * gm_l9; /* inner */
    void * gm_l10; /* x */
    void * gm_l13; /* lit */
    GM_STACK_CHECK(&F, 1449u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)15ULL)) goto ie2;
        F.l3 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t7) + 8));
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            gm_t9805qtonbsag t12 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t11) + 1956));
            uint32_t t13;
            if (((((uint8_t *)&t12))[4] != 0)) {
                uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&t12) + 0));
                t13 = t14;
            } else {
                gm_t0mfir12leg2g t15;
                memset(&t15, 0, sizeof t15);
                uint8_t t16 = 0;
                F.ret = t15;
                goto gm_fret;
            }
            gm_l4 = t13;
        }
        {
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            uint8_t t19 = (t18 > ((uint64_t)0ULL));
            if (!t19) goto ie17;
            {
                gm_t0mfir12leg2g t21;
                memset(&t21, 0, sizeof t21);
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 24) + 0));
                gm_l6 = t24;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l7;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l7;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tiilmfd16v3fc t36 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t32 * 24));
                        gm_l8 = t36;
                        {
                            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
                            uint32_t t40 = gm_l4;
                            uint8_t t41 = ((t39 == t40));
                            if (!t41) goto ie38;
                            {
                                gm_l5 = ((uint8_t)1);
                            }
                            goto ix38;
ie38:;
ix38:;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 221, 5);
me34:;
                    }
lc25:;
                    {
                        uint64_t t44 = gm_l7;
                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                        uint64_t t47 = r45;
                        gm_l7 = t47;
                    }
                }
lb25:;
            }
        }
        {
            uint8_t t49 = gm_l5;
            uint8_t t50 = (uint8_t)!t49;
            if (!t50) goto ie48;
            {
                gm_t0mfir12leg2g t52;
                memset(&t52, 0, sizeof t52);
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            void * t54 = gm_l1;
            gm_l9 = t54;
        }
        {
            for (;;) {
                void * t56 = gm_l9;
                uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
                if (t57 != ((uint8_t)9ULL)) goto lb55;
                void * t58 = GM_LD(void *, ((((uint8_t *)t56) + 8) + 8));
                gm_l10 = t58;
                {
                    void * t61 = gm_l10;
                    gm_l9 = t61;
                }
lc55:;
            }
lb55:;
        }
        {
            void * t63 = gm_l9;
            uint8_t t64;
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
            if (t67 != ((uint8_t)19ULL)) goto mn66;
            t64 = ((uint8_t)0);
            goto me65;
mn66:;
            t64 = ((uint8_t)1);
            goto me65;
mn68:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 227, 8);
me65:;
            if (!t64) goto ie62;
            {
                gm_t0mfir12leg2g t70;
                memset(&t70, 0, sizeof t70);
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
            goto ix62;
ie62:;
ix62:;
        }
        {
            void * t72 = gm_l0;
            void * t73 = gm_l9;
            gm_f_hoist_iterator_call_5rmgsce((void *)&F.l11, (void *)NULL, cx, NULL, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset((uint8_t *)&F.l12 + 17, 0, 7);
            memset((uint8_t *)&F.l12 + 68, 0, 4);
            gm_b16_8 t74 = gm_f_empty_slice_ito0pap();
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 0, t74);
            GM_ST(uint8_t, (uint8_t *)&F.l12 + 16, ((uint8_t)0));
            GM_ST(void *, (uint8_t *)&F.l12 + 24, ((void *)0));
            gm_th2lqbo77pvsc t75;
            memset(&t75, 0, sizeof t75);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l12 + 32, t75);
            void * t76 = gm_l1;
            gm_tl874t2atnlh3 t77 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t76) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l12 + 56, t77);
        }
        {
            gm_t0kv05e068jrn t78;
            memset((uint8_t *)&t78 + 148, 0, 4);
            memcpy((uint8_t *)&t78 + 0, ((uint8_t *)&F.l12), 72);
            gm_b16_8 t79 = gm_f_empty_slice_3jt8vgk();
            GM_ST(gm_b16_8, (uint8_t *)&t78 + 72, t79);
            gm_tq7s98ihonkju t80 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l11)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0gn7sac5pt)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srcoimgd0jo62fr, 232, 86);
            void * t81 = GM_LD(void *, (((uint8_t *)&t80) + 8));
            gm_tapdr2ag17fh7 t82;
            memset(&t82, 0, sizeof t82);
            GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)1ULL));
            GM_ST(void *, ((uint8_t *)&t82 + 8), t81);
            GM_ST(gm_tapdr2ag17fh7, (uint8_t *)&t78 + 88, t82);
            void * t83 = gm_l1;
            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t78 + 136, t84);
            gm_tq7s98ihonkju t85 = gm_f_new_cnt7p5u((void *)NULL, cx, NULL, &t78, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8upcoeidcs)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srcoimgd0jo62fr, 232, 12);
            void * t86 = GM_LD(void *, (((uint8_t *)&t85) + 8));
            gm_l13 = t86;
        }
        {
            void * t87 = gm_l0;
            void * t88 = (void *)(((uint8_t *)t87) + 1248);
            void * t89 = gm_l1;
            gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t89) + 72));
            gm_tftb1odc739t5 t91 = gm_f_push_0mk7udh(t88, GM_CONV(gm_cseahvg8v7p0vs, t90));
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcoimgd0jo62fr, 233, 5);
            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
        }
        {
            gm_tc3oht4ksth9d t93;
            memset((uint8_t *)&t93 + 4, 0, 4);
            memset((uint8_t *)&t93 + 84, 0, 4);
            void * t94 = gm_l1;
            uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)t94) + 0));
            GM_ST(uint32_t, (uint8_t *)&t93 + 0, t95);
            void * t96 = gm_l13;
            gm_t2gtbh39ptekd t97;
            memset(&t97, 0, sizeof t97);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)36ULL));
            GM_ST(void *, ((uint8_t *)&t97 + 8), t96);
            GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&t93 + 8, t97);
            void * t98 = gm_l1;
            gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t98) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t93 + 72, t99);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 92, 0);
            ((uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out))[88] = 1;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 0, t93);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_t0mfir12leg2g t101;
            memset(&t101, 0, sizeof t101);
            uint8_t t102 = 0;
            F.ret = t101;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_iterator_call_as_value_00i5mch F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e0 */
    void * gm_l2; /* v */
    gm_t88rcucg1b7uk gm_l3; /* param */
    void * gm_l4; /* e */
    void * gm_l5; /* x */
    void * gm_l6; /* tys */
    gm_b16_8 gm_l7; /* text */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* p */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 1185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_l4 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l4;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            if (t5 != ((uint8_t)9ULL)) goto lb3;
            void * t6 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l5 = t6;
            {
                void * t9 = gm_l5;
                gm_l4 = t9;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_l6 = t12;
    }
    {
        uint8_t t14 = 0;
        uint8_t t15 = 0;
        void * t16 = gm_l4;
        uint8_t t17;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
        if (t20 != ((uint8_t)19ULL)) goto mn19;
        t17 = ((uint8_t)0);
        goto me18;
mn19:;
        t17 = ((uint8_t)1);
        goto me18;
mn21:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 195, 8);
me18:;
        if (!t17) {
            void * t22 = gm_l6;
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 96);
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            void * t27 = gm_l2;
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
            gm_t88rcucg1b7uk t29 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t24, t26, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = gm_f_is_unit_n83ttnb(t22, GM_CONV(gm_csguankko4lmhd, t29));
            uint8_t t31 = (uint8_t)!t30;
            t15 = t31;
        } else t15 = 1;
        if (!t15) {
            void * t32 = gm_l6;
            void * t33 = gm_l0;
            void * t34 = (void *)(((uint8_t *)t33) + 96);
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
            gm_t88rcucg1b7uk t37 = gm_l3;
            gm_t88rcucg1b7uk t38 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t34, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = gm_f_is_unit_n83ttnb(t32, GM_CONV(gm_csguankko4lmhd, t38));
            t14 = t39;
        } else t14 = 1;
        if (!t14) goto ie13;
        {
            uint8_t t41 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t43 = gm_l4;
        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
        void * t45 = gm_l0;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((((uint8_t *)t45) + 960) + 0));
        uint8_t t47 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t46) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t46))) + i * 12, ((uint8_t *)&t44))) { t47 = 1; break; }
        uint8_t t48 = (uint8_t)(!t47);
        if (!t48) goto ie42;
        {
            uint8_t t50 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
        void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
        void * t55 = gm_l4;
        uint32_t t56 = GM_LD(uint32_t, ((((uint8_t *)t55) + 72) + 0));
        void * t57 = gm_f_file_o15fqku(t54, t56);
        uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 16) + 8);
        void * t59 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 16)));
        void * t60 = gm_l4;
        uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)t60) + 72) + 4));
        uint64_t t62 = (uint64_t)t61;
        uint64_t t63 = (uintptr_t)t62;
        void * t64 = gm_l4;
        uint32_t t65 = GM_LD(uint32_t, ((((uint8_t *)t64) + 72) + 8));
        uint64_t t66 = (uint64_t)t65;
        uint64_t t67 = (uintptr_t)t66;
        if (t63 > t67 || t67 > (uintptr_t)t58) gm_trap_slice(t63, t67, t58, gm_srcoimgd0jo62fr, 197, 13);
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t59 + t63 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t63));
        gm_l7 = t68;
    }
    {
        gm_b16_8 t70 = gm_l7;
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_smooaouqmm8dj));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t72 = gm_f_find_nab29co(t70, t71);
        if (!((((uint8_t *)&t72))[8] != 0)) goto ie69;
        uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t72));
        gm_l8 = t73;
        {
            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            void * t76 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
            uint64_t t77 = gm_l8;
            uint64_t t78 = (uintptr_t)t77;
            if ((uintptr_t)0 > t78 || t78 > (uintptr_t)t75) gm_trap_slice((uintptr_t)0, t78, t75, gm_srcoimgd0jo62fr, 198, 42);
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(((uint8_t *)t76 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(t78 - (uintptr_t)0));
            gm_l7 = t79;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        gm_b16_8 t81 = gm_l7;
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t83 = gm_f_rfind_ai5s2js(t81, t82);
        if (!((((uint8_t *)&t83))[8] != 0)) goto ie80;
        uint64_t t84 = GM_LD(uint64_t, ((uint8_t *)&t83));
        gm_l9 = t84;
        {
            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            void * t87 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
            uint64_t t88 = gm_l9;
            uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
            if (o90) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 199, 48);
            uint64_t t91 = r89;
            uint64_t t92 = (uintptr_t)t91;
            if (t92 > (uintptr_t)t86 || (uintptr_t)t86 > (uintptr_t)t86) gm_trap_slice(t92, (uintptr_t)t86, t86, gm_srcoimgd0jo62fr, 199, 43);
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t87 + t92 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)t86 - t92));
            gm_l7 = t93;
        }
        goto ix80;
ie80:;
ix80:;
    }
    {
        gm_b16_8 t94 = gm_l7;
        gm_b16_8 t95 = gm_f_trim_fdrfe6i(t94);
        gm_l10 = t95;
    }
    {
        void * t96 = gm_l0;
        void * t97 = gm_l0;
        void * t98 = (void *)(((uint8_t *)t97) + 96);
        void * t99 = gm_l0;
        void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
        gm_t88rcucg1b7uk t101 = gm_l3;
        gm_t88rcucg1b7uk t102 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t98, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t103 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t96, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t103;
    }
    {
        void * t104 = gm_l0;
        void * t105 = gm_l4;
        gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 72));
        gm_t0uuhaljsf410 t107;
        gm_b16_8 t108 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t108);
        gm_b16_8 t109 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t109);
        gm_b16_8 t110 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t107 + 32, t110);
        gm_b16_8 t111 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t107 + 48, t111);
        gm_tabb710d9mq2e t112;
        gm_f_format_mpnc7rc((void *)&t112, (void *)NULL, cx, NULL, &t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcoimgd0jo62fr, 202, 22);
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
        gm_b0_1 t115 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t104, t106, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l12 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t117 = gm_l12;
            void * t118 = gm_l0;
            uint64_t t119 = GM_LD(uintptr_t, ((((uint8_t *)t118) + 192) + 0) + 8);
            uint8_t t120 = (t117 < t119);
            if (!t120) goto lb116;
            {
                uint64_t t124 = gm_l12;
                uint64_t t125 = (uintptr_t)t124;
                void * t126 = gm_l0;
                uint64_t t127 = GM_LD(uintptr_t, ((((uint8_t *)t126) + 192) + 0) + 8);
                if (t125 >= (uintptr_t)(t127)) gm_trap_index(t125, t127, gm_srcoimgd0jo62fr, 205, 12);
                gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t126) + 192) + 0)) + t125 * 40) + 24));
                void * t129 = gm_l4;
                gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t129) + 72));
                uint8_t t131 = (gm_eqff85ac10o3(((uint8_t *)&t128), ((uint8_t *)&t130)));
                if (!t131) goto ie123;
                {
                    {
                        void * t134 = gm_l0;
                        void * t135 = (void *)(((uint8_t *)t134) + 192);
                        uint64_t t136 = gm_l12;
                        gm_tj44oivvogvtl t137;
                        gm_f_remove_vrfdutr((void *)&t137, t135, t136);
                    }
                }
                goto ix123;
ie123:;
                {
                    uint64_t t139 = gm_l12;
                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                    if (o141) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 208, 13);
                    uint64_t t142 = r140;
                    gm_l12 = t142;
                }
ix123:;
            }
lc116:;
        }
lb116:;
    }
    {
        uint8_t t143 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_lift_temps_7c6gk6l F;
    void * gm_l1; /* e */
    void * gm_l3; /* inner */
    uint32_t gm_l4; /* t */
    void * gm_l5; /* v */
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t12 != ((uint8_t)34ULL)) goto mn11;
        t4 = ((uint8_t)1);
        goto me5;
mn11:;
ma7:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t14 != ((uint8_t)27ULL)) goto mn13;
        t4 = ((uint8_t)1);
        goto me5;
mn13:;
ma8:;
        t4 = ((uint8_t)0);
        goto me5;
mn15:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 175, 8);
me5:;
        if (!t4) goto ie2;
        {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l1;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
        if (t21 != ((uint8_t)25ULL)) goto ie19;
        void * t22 = GM_LD(void *, ((((uint8_t *)t20) + 8) + 8));
        gm_l3 = t22;
        {
            void * t24 = F.l0;
            void * t25 = gm_l3;
            void * t26 = F.l2;
            gm_b0_1 t27 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t28 = F.l0;
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
            void * t31 = gm_l3;
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
            uint32_t t33 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t28, t30, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t33;
        }
        {
            void * t34 = gm_l3;
            gm_tq7s98ihonkju t35 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_t69m56meqefs8 *)((uint8_t *)t34)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmnip3ksasl)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcoimgd0jo62fr, 179, 14);
            void * t36 = GM_LD(void *, (((uint8_t *)&t35) + 8));
            gm_l5 = t36;
        }
        {
            void * t37 = gm_l3;
            uint8_t *t38 = ((uint8_t *)t37);
            void * t39 = gm_l3;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
            uint32_t t41 = gm_l4;
            gm_teti411eujvf4 t42;
            memset(&t42, 0, sizeof t42);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t42 + 8), t41);
            void * t43 = gm_l3;
            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
            void * t45 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t40, &t42, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t46 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t45));
            GM_ST(gm_t69m56meqefs8, t38, t46);
        }
        {
            void * t47 = F.l2;
            uint32_t t48 = gm_l4;
            void * t49 = gm_l5;
            gm_ttggq7ng4kp3p t50;
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t50 + 0), t48);
            GM_ST(void *, ((uint8_t *)&t50 + 8), t49);
            gm_trcmmd12cefb1 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t51 + 8), t50);
            gm_tftb1odc739t5 t52 = gm_f_push_d6c76ud_2(t47, &t51);
            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcoimgd0jo62fr, 181, 9);
            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
        }
        {
            void * t54 = gm_l1;
            void * t55 = gm_l3;
            memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t54) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t54) + 8)));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t54) + 8)) + 0, ((uint8_t)24ULL));
            GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t54) + 8)) + 8, t55);
        }
        {
            gm_b0_1 t56;
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t58 = gm_l1;
        gm_closure t59 = ((gm_closure){(void *)gm_f_fn_t9suqla, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t58, t59);
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
extern __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void * gm_f_not_an_iterator_au10b3l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_not_an_iterator_au10b3l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* call */
    void * gm_l3; /* e */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        void * t5 = gm_l2;
        void * t6 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!(GM_LD(void *, ((uint8_t *)&t6)) != 0)) goto ie2;
        void * t7 = GM_LD(void *, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            void * t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        void * t15 = gm_l2;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 0));
        uint8_t t17 = gm_f_is_error_8k27gd1(t14, GM_CONV(gm_csguankko4lmhd, t16));
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie11;
        {
            void * t20 = gm_l0;
            void * t21 = gm_l1;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 56));
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sb8airrqr74u5));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(72));
            gm_b0_1 t25 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t20, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28 = gm_l1;
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 104));
        void * t30 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t27, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = 0;
        F.ret = t30;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4) {
    struct gm_fr_place_iter_pre_qn5c5b5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* bound */
    void * gm_l2; /* written */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* stmts */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* j */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1160) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            {
                void * t8 = gm_l2;
                gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 0));
                gm_l5 = t9;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l6;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t17 * 8));
                        uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&t19));
                        gm_l7 = t22;
                        {
                            uint64_t t25 = gm_l7;
                            uint64_t t26 = (uintptr_t)t25;
                            void * t27 = gm_l1;
                            uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
                            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcoimgd0jo62fr, 244, 21);
                            void * t29 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 0)) + t26 * 8));
                            if (!(GM_LD(void *, ((uint8_t *)&t29)) != 0)) goto ie24;
                            void * t30 = GM_LD(void *, ((uint8_t *)&t29));
                            gm_l8 = t30;
                            {
                                uint64_t t32 = gm_l7;
                                uint64_t t33 = (uintptr_t)t32;
                                void * t34 = gm_l1;
                                uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 0) + 8);
                                if (t33 >= (uintptr_t)(t35)) gm_trap_index(t33, t35, gm_srcoimgd0jo62fr, 244, 38);
                                uint8_t *t36 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t34) + 0)) + t33 * 8);
                                void * t37 = gm_l0;
                                void * t38 = gm_l8;
                                void * t39 = gm_l4;
                                void * t40 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t37, t38, t39);
                                if (gm_unw.kind) {
                                    goto gm_ll14;
                                }
                                GM_ST(void *, t36, t40);
                            }
                            goto ix24;
ie24:;
ix24:;
                        }
                        goto me20;
mn21:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 243, 9);
me20:;
                    }
lc10:;
                    {
                        uint64_t t42 = gm_l6;
                        uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                        uint64_t t45 = r43;
                        gm_l6 = t45;
                    }
                }
gm_ll14:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
                }
                goto gm_lad;
lb10:;
            }
        }
        {
            void * t46 = gm_l4;
            void * t47 = gm_l0;
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((((uint8_t *)t47) + 1160) + 0));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_tftb1odc739t5 t50 = gm_f_extend_nkql463(t46, t49);
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srcoimgd0jo62fr, 246, 9);
            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
        }
        {
            void * t52 = gm_l0;
            uint8_t *t53 = (((uint8_t *)t52) + 1160);
            gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t53), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t54 = gm_l2;
        uint64_t t55 = gm_l3;
        gm_tftb1odc739t5 t56 = gm_f_push_ucgmlo2(t54, t55);
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcoimgd0jo62fr, 249, 5);
        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
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
