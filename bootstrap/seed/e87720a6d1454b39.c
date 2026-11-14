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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csia7oevr2u0fk { _Bool f0; unsigned __int128 f1; } gm_csia7oevr2u0fk;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csia7oevr2u0fk) == 32 && _Alignof(gm_csia7oevr2u0fk) == 16 && offsetof(gm_csia7oevr2u0fk, f0) == 0 && offsetof(gm_csia7oevr2u0fk, f1) == 16, "layout of `NumVal`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { gm_t86h6hhm6d76n f0; uint8_t f1; uint8_t pe[15]; } gm_tsp9ul70rnkg6;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsp9ul70rnkg6) == 48 && _Alignof(gm_tsp9ul70rnkg6) == 16 && offsetof(gm_tsp9ul70rnkg6, f0) == 0 && offsetof(gm_tsp9ul70rnkg6, f1) == 32, "layout of `?NumVal`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_apply_builtin_41dffti {
    gm_frame h;
    gm_tckdobunaulee l32; /* d */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_array_ptr_to_slice_7dg3m1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coerce_bytes_9kf3u24 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_convert_is6an2k {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_hash_bytes_2qnf0cp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_int_26mf5j0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_u64_32bripn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_val_nr2e99r {
    gm_frame h;
    gm_t2jeg3bg7sfqa l17; /* s */
    gm_tpautr4br7h3l l31; /* def */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_text_vrjnuu2 {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_is_nan_b0ma7mo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_mask_do3naah {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_round_2pna7m3 {
    gm_frame h;
    double ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_inner_q4ga84u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_align_lavuopj {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
__attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0);
__attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_tvdg94bk7aa22 a1);
__attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1);
__attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s064htvgs8b59[69];
extern const uint8_t gm_s26c80h2pm6a7[26];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3gnqv0up712m[61];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_sartclcojin4f[52];
extern const uint8_t gm_sb9352gc5i144[21];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sge745dj3cnrb[73];
extern const uint8_t gm_sh01rb1u2jpfs[19];
extern const uint8_t gm_spqf88ftq0gqk[29];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src9pvgnpibl2nm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sti4tkgslrkgo[26];
extern const uint8_t gm_stk678hojgu6q[24];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
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
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_int_26mf5j0 F;
    void * gm_l0; /* h */
    gm_t86h6hhm6d76n gm_l1; /* x */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* low */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        unsigned __int128 t2 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)a1)));
        gm_l3 = t2;
    }
    {
        void * t3 = gm_l0;
        unsigned __int128 t4 = gm_l3;
        uint64_t t5 = (uint64_t)t4;
        gm_f_hash_u64_32bripn(t3, t5);
    }
    {
        uint32_t t7 = gm_l2;
        uint8_t t8 = (t7 > ((uint32_t)64ULL));
        if (!t8) goto ie6;
        {
            void * t10 = gm_l0;
            unsigned __int128 t11 = gm_l3;
            unsigned __int128 n12 = (unsigned __int128)((uint64_t)64ULL);
            if (n12 >= 128) gm_trap_shift((uint64_t)n12, "u128", gm_srcl8pn2pjtmj6k, 427, 33);
            unsigned __int128 t13 = (unsigned __int128)(t11 >> (int)n12);
            uint64_t t14 = (uint64_t)t13;
            gm_f_hash_u64_32bripn(t10, t14);
        }
        goto ix6;
ie6:;
ix6:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_u64_32bripn F;
    void * gm_l0; /* h */
    uint64_t gm_l1; /* x */
    unsigned __int128 gm_l2; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
        uint64_t t4 = gm_l1;
        uint64_t t5 = (uint64_t)(t3 ^ t4);
        unsigned __int128 t6 = (unsigned __int128)t5;
        unsigned __int128 r7; bool o8 = __builtin_mul_overflow(t6, ((unsigned __int128)(((unsigned __int128)0u << 64) | 11400714819323198485u)), &r7);
        unsigned __int128 t9 = r7;
        gm_l2 = t9;
    }
    {
        void * t10 = gm_l0;
        unsigned __int128 t11 = gm_l2;
        uint64_t t12 = (uint64_t)t11;
        unsigned __int128 t13 = gm_l2;
        unsigned __int128 n14 = (unsigned __int128)((uint64_t)64ULL);
        if (n14 >= 128) gm_trap_shift((uint64_t)n14, "u128", gm_srcl8pn2pjtmj6k, 407, 26);
        unsigned __int128 t15 = (unsigned __int128)(t13 >> (int)n14);
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uint64_t)(t12 ^ t16);
        GM_ST(uint64_t, ((uint8_t *)t10), t17);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_bits_onl28vp F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 t1;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (!t3) goto ie2;
    unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
    unsigned __int128 r6; bool o7 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t5, &r6);
    unsigned __int128 t8 = r6;
    unsigned __int128 t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
    unsigned __int128 t12 = t11;
    t1 = t12;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_is_nan_b0ma7mo F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t1 = gm_l0;
    double t2 = gm_l0;
    uint8_t t3 = (!(t1 == t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_mask_do3naah F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)128ULL));
    if (!t4) goto ie2;
    unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    unsigned __int128 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    unsigned __int128 t10 = (unsigned __int128)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 128) gm_trap_shift((uint64_t)n11, "u128", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 r12 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n11);
    bool o13 = (unsigned __int128)(r12 >> (int)n11) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    if (o13) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 t14 = r12;
    unsigned __int128 r15; bool o16 = __builtin_sub_overflow(t14, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 8, 73);
    unsigned __int128 t17 = r15;
    unsigned __int128 t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_round_2pna7m3 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* width */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint16_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint16_t)32ULL)));
        if (!t4) goto ie2;
        {
            double t6 = gm_l0;
            float t7 = (float)((double)t6);
            double t8 = (double)t7;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint16_t t11 = gm_l1;
        uint8_t t12 = ((t11 == ((uint16_t)16ULL)));
        if (!t12) goto ie10;
        {
            double t14 = gm_l0;
            uint16_t t15 = gm_f_num_f64_to_f16_3g33m05(t14);
            double t16 = gm_f_num_f16_to_f64_h70suv4(t15);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        double t18 = gm_l0;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_inner_q4ga84u F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)13ULL)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l2 = t8;
        {
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t88rcucg1b7uk t13 = gm_l1;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_apply_builtin_41dffti F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* al */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    __int128 gm_l12; /* stride */
    __int128 gm_l13; /* p */
    uint64_t gm_l14; /* pbits */
    uint64_t gm_l15; /* mask */
    int64_t gm_l16; /* d */
    int64_t gm_l17; /* q */
    gm_ttovhfee8mf28 gm_l18; /* info */
    unsigned __int128 gm_l19; /* raw */
    __int128 gm_l20; /* n */
    __int128 gm_l21; /* off */
    __int128 gm_l22; /* r */
    gm_tcmkompbl09il gm_l23; /* u */
    uint64_t gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* def */
    gm_ttovhfee8mf28 gm_l27; /* info */
    gm_t86h6hhm6d76n gm_l28; /* x */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    unsigned __int128 gm_l33; /* tb */
    unsigned __int128 gm_l34; /* mag */
    gm_t2lqu8jsj511d gm_l35; /* mk */
    gm_b16_8 gm_l36; /* vb */
    double gm_l37; /* x */
    double gm_l38; /* y */
    double gm_l39; /* r */
    gm_ttovhfee8mf28 gm_l40; /* info */
    unsigned __int128 gm_l41; /* x */
    unsigned __int128 gm_l42; /* y */
    uint64_t gm_l43; /* pick */
    unsigned __int128 gm_l44; /* hi */
    gm_ttovhfee8mf28 gm_l45; /* info */
    unsigned __int128 gm_l46; /* x */
    unsigned __int128 gm_l47; /* r */
    gm_ttovhfee8mf28 gm_l48; /* info */
    unsigned __int128 gm_l49; /* x */
    unsigned __int128 gm_l50; /* y */
    unsigned __int128 gm_l51; /* d */
    gm_ttovhfee8mf28 gm_l52; /* r */
    gm_t9qkh0rsooc8d gm_l53; /* fl */
    double gm_l54; /* x */
    uint64_t gm_l55; /* raw */
    uint64_t gm_l56; /* exp */
    uint16_t gm_l57; /* w */
    double gm_l58; /* x */
    double gm_l59; /* y */
    double gm_l60; /* z */
    gm_ttovhfee8mf28 gm_l61; /* info */
    gm_ttovhfee8mf28 gm_l62; /* ainfo */
    uint32_t gm_l63; /* n */
    unsigned __int128 gm_l64; /* x */
    unsigned __int128 gm_l65; /* araw */
    uint32_t gm_l66; /* k */
    unsigned __int128 gm_l67; /* r */
    gm_ttovhfee8mf28 gm_l68; /* info */
    uint32_t gm_l69; /* n */
    unsigned __int128 gm_l70; /* x */
    unsigned __int128 gm_l71; /* r */
    uint32_t gm_l72; /* <tmp> */
    uint32_t gm_l73; /* <tmp> */
    uint32_t gm_l74; /* i */
    uint32_t gm_l75; /* <tmp> */
    uint32_t gm_l76; /* <tmp> */
    uint32_t gm_l77; /* i */
    gm_ttovhfee8mf28 gm_l78; /* info */
    gm_t86h6hhm6d76n gm_l79; /* seed */
    uint64_t gm_l80; /* h */
    GM_STACK_CHECK(&F, 13299u);
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
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t23) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; case ((uint8_t)27ULL): goto ma16; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma18; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma18; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma21; case ((uint8_t)38ULL): goto ma21; case ((uint8_t)39ULL): goto ma21; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; case ((uint8_t)51ULL): goto ma20; case ((uint8_t)52ULL): goto ma20; case ((uint8_t)53ULL): goto ma20; case ((uint8_t)54ULL): goto ma20; case ((uint8_t)55ULL): goto ma20; case ((uint8_t)56ULL): goto ma20; case ((uint8_t)57ULL): goto ma20; case ((uint8_t)58ULL): goto ma21; case ((uint8_t)59ULL): goto ma21; case ((uint8_t)60ULL): goto ma21; case ((uint8_t)61ULL): goto ma21; case ((uint8_t)62ULL): goto ma19; case ((uint8_t)63ULL): goto ma21; case ((uint8_t)64ULL): goto ma21; case ((uint8_t)65ULL): goto ma21; case ((uint8_t)66ULL): goto ma21; case ((uint8_t)67ULL): goto ma21; case ((uint8_t)68ULL): goto ma21; case ((uint8_t)69ULL): goto ma21; case ((uint8_t)70ULL): goto ma21; case ((uint8_t)71ULL): goto ma21; case ((uint8_t)72ULL): goto ma21; case ((uint8_t)73ULL): goto ma21; case ((uint8_t)74ULL): goto ma21; case ((uint8_t)75ULL): goto ma21; case ((uint8_t)76ULL): goto ma21; case ((uint8_t)77ULL): goto ma21; case ((uint8_t)78ULL): goto ma21; case ((uint8_t)79ULL): goto ma21; case ((uint8_t)80ULL): goto ma21; case ((uint8_t)81ULL): goto ma21; default: goto ma21; }
ma4:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t27 != ((uint8_t)0ULL)) goto pa26;
            goto po25;
pa26:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t29 != ((uint8_t)1ULL)) goto pa28;
            goto po25;
pa28:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t31 != ((uint8_t)2ULL)) goto pa30;
            goto po25;
pa30:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t33 != ((uint8_t)3ULL)) goto pa32;
            goto po25;
pa32:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t35 != ((uint8_t)4ULL)) goto pa34;
            goto po25;
pa34:;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t37 != ((uint8_t)5ULL)) goto pa36;
            goto po25;
pa36:;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t39 != ((uint8_t)6ULL)) goto pa38;
            goto po25;
pa38:;
            goto mn24;
po25:;
            void * t40 = gm_l0;
            gm_tl874t2atnlh3 t41 = gm_l1;
            gm_tvdg94bk7aa22 t42 = gm_l2;
            gm_t88rcucg1b7uk t43 = gm_l4;
            gm_t88rcucg1b7uk t44 = gm_l3;
            uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcl8pn2pjtmj6k, 192, 107);
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t45 * 16));
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_b16_8 t50 = gm_l7;
            gm_f_convert_is6an2k((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t40, t41, t42, t43, t44, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn24:;
ma5:;
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t52 != ((uint8_t)7ULL)) goto mn51;
            {
                gm_b16_8 t54 = gm_l7;
                gm_f_fill_abmf5g5(t54, ((uint8_t)0ULL));
            }
            {
                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcl8pn2pjtmj6k, 195, 30);
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t56 * 16) + 8);
                uint64_t t59 = (t55 < t58 ? t55 : t58);
                gm_l8 = t59;
            }
            {
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = gm_l8;
                uint64_t t64 = (uintptr_t)t63;
                if (t62 > t64 || t64 > (uintptr_t)t60) gm_trap_slice(t62, t64, t60, gm_srcl8pn2pjtmj6k, 196, 13);
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t61 + t62 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t62));
                uint64_t t66 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcl8pn2pjtmj6k, 196, 33);
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t66 * 16) + 8);
                void * t69 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t66 * 16)));
                uint64_t t70 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t71 = gm_l8;
                uint64_t t72 = (uintptr_t)t71;
                if (t70 > t72 || t72 > (uintptr_t)t68) gm_trap_slice(t70, t72, t68, gm_srcl8pn2pjtmj6k, 196, 33);
                gm_b16_8 t73;
                GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t69 + t70 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72 - t70));
                gm_b16_8 t74 = t73;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_f_copy_from_f7j6p05(t65, t75);
            }
            goto me3;
mn51:;
ma6:;
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t77 != ((uint8_t)8ULL)) goto mn76;
            {
                void * t79 = gm_l0;
                uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcl8pn2pjtmj6k, 199, 30);
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t80 * 16));
                gm_b16_8 t83 = t82;
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                uint64_t t85 = gm_f_ptr_of_b77b4c9(t79, t84);
                gm_l9 = t85;
            }
            {
                void * t86 = gm_l0;
                gm_t88rcucg1b7uk t87 = gm_l3;
                uint64_t t88 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t88;
            }
            {
                uint64_t t90 = gm_l9;
                uint8_t t91 = (!(t90 == ((uint64_t)0ULL)));
                if (!t91) goto ie89;
                uint64_t t92 = gm_l10;
                uint8_t t93 = (t92 > ((uint64_t)1ULL));
                if (!t93) goto ie89;
                uint64_t t94 = gm_l9;
                uint64_t t95 = gm_l10;
                if (t95 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 201, 45);
                uint64_t t96 = (uint64_t)(t94 % t95);
                uint8_t t97 = (!(t96 == ((uint64_t)0ULL)));
                if (!t97) goto ie89;
                void * t98 = gm_l0;
                gm_t88rcucg1b7uk t99 = gm_l4;
                uint64_t t100 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t98, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t101 = gm_l10;
                uint8_t t102 = (t100 < t101);
                if (!t102) goto ie89;
                void * t103 = gm_l0;
                uint8_t t104 = gm_f_fast_23em7sv(t103);
                uint8_t t105 = (uint8_t)!t104;
                if (!t105) goto ie89;
                {
                    void * t107 = gm_l0;
                    gm_tl874t2atnlh3 t108 = gm_l1;
                    uint64_t t109 = gm_l9;
                    uint64_t t110 = gm_l10;
                    gm_t2lqu8jsj511d t111;
                    GM_ST(uint64_t, ((uint8_t *)&t111 + 0), t109);
                    GM_ST(uint64_t, ((uint8_t *)&t111 + 8), t110);
                    gm_tabb710d9mq2e t112;
                    gm_f_format_l7c15sv((void *)&t112, (void *)NULL, cx, NULL, t111);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t113 = t112;
                    if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_srcl8pn2pjtmj6k, 202, 38);
                    gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
                    gm_b16_8 t115 = t114;
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                    gm_t76123gfet73g t117;
                    gm_f_trap_qh84l9d((void *)&t117, (void *)NULL, cx, NULL, t107, t108, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t118;
                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                    ((uint8_t *)&t118)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t118 + 8), t117);
                    gm_tcfa6l7reoosn t119 = t118;
                    uint8_t t120 = ((((uint8_t *)&t119))[0] != 0);
                    F.ret = t118;
                    goto gm_fret;
                }
                goto ix89;
ie89:;
ix89:;
            }
            {
                gm_b16_8 t121 = gm_l7;
                uint64_t t122 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t122 >= (uintptr_t)(t123)) gm_trap_index(t122, t123, gm_srcl8pn2pjtmj6k, 204, 27);
                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t122 * 16));
                gm_b16_8 t125 = t124;
                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                gm_f_copy_from_f7j6p05(t121, t126);
            }
            goto me3;
mn76:;
ma7:;
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t130 != ((uint8_t)9ULL)) goto pa129;
            goto po128;
pa129:;
            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t132 != ((uint8_t)13ULL)) goto pa131;
            goto po128;
pa131:;
            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t134 != ((uint8_t)35ULL)) goto pa133;
            goto po128;
pa133:;
            goto mn127;
po128:;
            gm_b16_8 t135 = gm_l7;
            uint64_t t136 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_srcl8pn2pjtmj6k, 206, 51);
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t136 * 16));
            gm_b16_8 t139 = t138;
            gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
            gm_f_copy_from_f7j6p05(t135, t140);
            goto me3;
mn127:;
ma8:;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t144 != ((uint8_t)10ULL)) goto pa143;
            goto po142;
pa143:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t146 != ((uint8_t)11ULL)) goto pa145;
            goto po142;
pa145:;
            uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t148 != ((uint8_t)12ULL)) goto pa147;
            goto po142;
pa147:;
            goto mn141;
po142:;
            {
                void * t151 = gm_l0;
                gm_t88rcucg1b7uk t152 = gm_l4;
                gm_t53at80pfdus3 t153;
                gm_f_kind_htv6093((void *)&t153, t151, GM_CONV(gm_csguankko4lmhd, t152));
                gm_t53at80pfdus3 t154 = t153;
                uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&t154) + 0));
                if (t155 != ((uint8_t)9ULL)) goto ie150;
                gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t154) + 8) + 4));
                gm_l11 = t156;
                {
                    void * t158 = gm_l0;
                    gm_t88rcucg1b7uk t159 = gm_l11;
                    uint64_t t160 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t158, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    __int128 t161 = (__int128)t160;
                    gm_l12 = t161;
                }
                {
                    void * t162 = gm_l0;
                    uint64_t t163 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_srcl8pn2pjtmj6k, 210, 32);
                    gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t163 * 16));
                    gm_b16_8 t166 = t165;
                    gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                    uint64_t t168 = gm_f_ptr_of_b77b4c9(t162, t167);
                    __int128 t169 = (__int128)t168;
                    gm_l13 = t169;
                }
                {
                    void * t170 = gm_l0;
                    uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)t170) + 176));
                    uint64_t r172; bool o173 = __builtin_mul_overflow(t171, ((uint64_t)8ULL), &r172);
                    if (o173) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 211, 22);
                    uint64_t t174 = r172;
                    gm_l14 = t174;
                }
                {
                    uint64_t t175;
                    uint64_t t177 = gm_l14;
                    uint8_t t178 = (t177 >= ((uint64_t)64ULL));
                    if (!t178) goto ie176;
                    uint64_t t180 = (uint64_t)~((uint64_t)0ULL);
                    uint64_t t181 = t180;
                    t175 = t181;
                    goto ix176;
ie176:;
                    uint64_t t183 = gm_l14;
                    unsigned __int128 n184 = (unsigned __int128)t183;
                    if (n184 >= 64) gm_trap_shift((uint64_t)n184, "u64", gm_srcl8pn2pjtmj6k, 212, 60);
                    uint64_t r185 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n184);
                    bool o186 = (uint64_t)(r185 >> (int)n184) != ((uint64_t)1ULL);
                    if (o186) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 212, 60);
                    uint64_t t187 = r185;
                    uint64_t r188; bool o189 = __builtin_sub_overflow(t187, ((uint64_t)1ULL), &r188);
                    if (o189) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 212, 59);
                    uint64_t t190 = r188;
                    uint64_t t191 = t190;
                    t175 = t191;
ix176:;
                    gm_l15 = t175;
                }
                {
                    uint8_t t193 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)12ULL)));
                    if (!t193) goto ie192;
                    {
                        __int128 t195 = gm_l13;
                        void * t196 = gm_l0;
                        uint64_t t197 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t197 >= (uintptr_t)(t198)) gm_trap_index(t197, t198, gm_srcl8pn2pjtmj6k, 214, 41);
                        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t197 * 16));
                        gm_b16_8 t200 = t199;
                        gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                        uint64_t t202 = gm_f_ptr_of_b77b4c9(t196, t201);
                        __int128 t203 = (__int128)t202;
                        __int128 r204; bool o205 = __builtin_sub_overflow(t195, t203, &r204);
                        if (o205) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 214, 23);
                        __int128 t206 = r204;
                        int64_t t207 = (int64_t)t206;
                        gm_l16 = t207;
                    }
                    {
                        int64_t t208;
                        __int128 t210 = gm_l12;
                        uint8_t t211 = ((t210 == ((__int128)(((unsigned __int128)0u << 64) | 0u))));
                        if (!t211) goto ie209;
                        int64_t t213 = ((int64_t)0LL);
                        t208 = t213;
                        goto ix209;
ie209:;
                        int64_t t215 = gm_l16;
                        __int128 t216 = gm_l12;
                        int64_t t217 = (int64_t)t216;
                        if (t217 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 215, 54);
                        if (t215 == ((int64_t)(-9223372036854775807LL - 1)) && t217 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srcl8pn2pjtmj6k, 215, 54);
                        int64_t t218 = (int64_t)(t215 / t217);
                        int64_t t219 = t218;
                        t208 = t219;
ix209:;
                        gm_l17 = t208;
                    }
                    {
                        void * t220 = gm_l0;
                        gm_b16_8 t221 = gm_l7;
                        int64_t t222 = gm_l17;
                        unsigned __int128 t223 = (unsigned __int128)t222;
                        gm_f_put_uint_cddin99(t220, t221, t223);
                    }
                    {
                        gm_b0_1 t224;
                        gm_tcfa6l7reoosn t225;
                        GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t225 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t225 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t225 + 24, 0);
                        ((uint8_t *)&t225)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t225 + 8), t224);
                        gm_tcfa6l7reoosn t226 = t225;
                        uint8_t t227 = ((((uint8_t *)&t226))[0] != 0);
                        F.ret = t225;
                        goto gm_fret;
                    }
                    goto ix192;
ie192:;
ix192:;
                }
                {
                    void * t228 = gm_l0;
                    gm_t88rcucg1b7uk t229 = gm_l5;
                    gm_tg5lql6itufk7 t230 = gm_f_int_info_ulcevdv(t228, GM_CONV(gm_csguankko4lmhd, t229));
                    gm_tg5lql6itufk7 t231 = t230;
                    gm_ttovhfee8mf28 t232;
                    if (((((uint8_t *)&t231))[8] != 0)) {
                        gm_ttovhfee8mf28 t233 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t231) + 0));
                        t232 = t233;
                    } else {
                        void * t234 = gm_l0;
                        gm_tl874t2atnlh3 t235 = gm_l1;
                        gm_b16_8 t236;
                        GM_ST(void *, (uint8_t *)&t236, (void *)(gm_s064htvgs8b59));
                        GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(68));
                        gm_t76123gfet73g t237;
                        gm_f_trap_qh84l9d((void *)&t237, (void *)NULL, cx, NULL, t234, t235, t236);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t238;
                        GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                        ((uint8_t *)&t238)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t238 + 8), t237);
                        gm_tcfa6l7reoosn t239 = t238;
                        uint8_t t240 = ((((uint8_t *)&t239))[0] != 0);
                        F.ret = t238;
                        goto gm_fret;
                    }
                    gm_l18 = t232;
                }
                {
                    void * t241 = gm_l0;
                    gm_t88rcucg1b7uk t242 = gm_l5;
                    uint64_t t243 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t243 >= (uintptr_t)(t244)) gm_trap_index(t243, t244, gm_srcl8pn2pjtmj6k, 220, 34);
                    gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t243 * 16));
                    gm_b16_8 t246 = t245;
                    gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
                    unsigned __int128 t248 = gm_f_get_int_pg7p4gc(t241, GM_CONV(gm_csguankko4lmhd, t242), t247);
                    gm_l19 = t248;
                }
                {
                    gm_l20 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                }
                {
                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    if (!t250) goto ie249;
                    {
                        unsigned __int128 t252 = gm_l19;
                        unsigned __int128 t253 = t252;
                        __int128 t254;
                        memset(&t254, 0, sizeof t254); memcpy(&t254, ((uint8_t *)&t253), sizeof t254 < 16 ? sizeof t254 : 16);
                        gm_l20 = t254;
                    }
                    goto ix249;
ie249:;
                    unsigned __int128 t256 = gm_l19;
                    unsigned __int128 t257 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                    unsigned __int128 n258 = (unsigned __int128)((uint64_t)1ULL);
                    if (n258 >= 128) gm_trap_shift((uint64_t)n258, "u128", gm_srcl8pn2pjtmj6k, 224, 31);
                    unsigned __int128 t259 = (unsigned __int128)(t257 >> (int)n258);
                    uint8_t t260 = (t256 <= t259);
                    if (!t260) goto ie255;
                    {
                        unsigned __int128 t262 = gm_l19;
                        unsigned __int128 t263 = t262;
                        __int128 t264;
                        memset(&t264, 0, sizeof t264); memcpy(&t264, ((uint8_t *)&t263), sizeof t264 < 16 ? sizeof t264 : 16);
                        gm_l20 = t264;
                    }
                    goto ix255;
ie255:;
ix255:;
ix249:;
                }
                {
                    __int128 t265 = gm_l20;
                    __int128 t266 = gm_l12;
                    __int128 r267; bool o268 = __builtin_mul_overflow(t265, t266, &r267);
                    __int128 t269 = r267;
                    gm_l21 = t269;
                }
                {
                    __int128 t270;
                    uint8_t t272 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
                    if (!t272) goto ie271;
                    __int128 t274 = gm_l13;
                    __int128 t275 = gm_l21;
                    __int128 r276; bool o277 = __builtin_add_overflow(t274, t275, &r276);
                    __int128 t278 = r276;
                    __int128 t279 = t278;
                    t270 = t279;
                    goto ix271;
ie271:;
                    __int128 t281 = gm_l13;
                    __int128 t282 = gm_l21;
                    __int128 r283; bool o284 = __builtin_sub_overflow(t281, t282, &r283);
                    __int128 t285 = r283;
                    __int128 t286 = t285;
                    t270 = t286;
ix271:;
                    gm_l22 = t270;
                }
                {
                    void * t287 = gm_l0;
                    gm_b16_8 t288 = gm_l7;
                    __int128 t289 = gm_l22;
                    uint64_t t290 = (uint64_t)t289;
                    uint64_t t291 = gm_l15;
                    uint64_t t292 = (uint64_t)(t290 & t291);
                    unsigned __int128 t293 = (unsigned __int128)t292;
                    gm_f_put_uint_cddin99(t287, t288, t293);
                }
                goto ix150;
ie150:;
                {
                    void * t295 = gm_l0;
                    gm_tl874t2atnlh3 t296 = gm_l1;
                    gm_b16_8 t297;
                    GM_ST(void *, (uint8_t *)&t297, (void *)(gm_sge745dj3cnrb));
                    GM_ST(uintptr_t, (uint8_t *)&t297 + 8, (uintptr_t)(72));
                    gm_t76123gfet73g t298;
                    gm_f_trap_qh84l9d((void *)&t298, (void *)NULL, cx, NULL, t295, t296, t297);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t299;
                    GM_ST(uint8_t, (uint8_t *)&t299 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t299 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t299 + 4, 0);
                    ((uint8_t *)&t299)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t299 + 8), t298);
                    gm_tcfa6l7reoosn t300 = t299;
                    uint8_t t301 = ((((uint8_t *)&t300))[0] != 0);
                    F.ret = t299;
                    goto gm_fret;
                }
ix150:;
            }
            goto me3;
mn141:;
ma9:;
            uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t303 != ((uint8_t)14ULL)) goto mn302;
            {
                void * t305 = gm_l0;
                gm_t88rcucg1b7uk t306 = gm_l4;
                gm_t57euh612ligr t307;
                gm_f_union_def_h8mvf4t((void *)&t307, t305, GM_CONV(gm_csguankko4lmhd, t306));
                gm_t57euh612ligr t308 = t307;
                gm_tcmkompbl09il t309;
                if (((((uint8_t *)&t308))[24] != 0)) {
                    gm_tcmkompbl09il t310 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t308) + 0));
                    t309 = t310;
                } else {
                    void * t311 = gm_l0;
                    gm_tl874t2atnlh3 t312 = gm_l1;
                    gm_b16_8 t313;
                    GM_ST(void *, (uint8_t *)&t313, (void *)(gm_sb9352gc5i144));
                    GM_ST(uintptr_t, (uint8_t *)&t313 + 8, (uintptr_t)(20));
                    gm_t76123gfet73g t314;
                    gm_f_trap_qh84l9d((void *)&t314, (void *)NULL, cx, NULL, t311, t312, t313);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t315;
                    GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                    ((uint8_t *)&t315)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t315 + 8), t314);
                    gm_tcfa6l7reoosn t316 = t315;
                    uint8_t t317 = ((((uint8_t *)&t316))[0] != 0);
                    F.ret = t315;
                    goto gm_fret;
                }
                gm_l23 = t309;
            }
            {
                void * t318 = gm_l0;
                gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l23) + 0));
                uint64_t t320 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t318, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t321 = gm_f_us_cuv01fp(t320);
                gm_l24 = t321;
            }
            {
                gm_b16_8 t322 = gm_l7;
                gm_f_fill_abmf5g5(t322, ((uint8_t)0ULL));
            }
            {
                uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t324 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t325 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t326 = gm_l24;
                uint64_t t327 = (uintptr_t)t326;
                if (t325 > t327 || t327 > (uintptr_t)t323) gm_trap_slice(t325, t327, t323, gm_srcl8pn2pjtmj6k, 235, 13);
                gm_b16_8 t328;
                GM_ST(void *, (uint8_t *)&t328, (void *)(((uint8_t *)t324 + t325 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(t327 - t325));
                uint64_t t329 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t330 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t329 >= (uintptr_t)(t330)) gm_trap_index(t329, t330, gm_srcl8pn2pjtmj6k, 235, 33);
                uint64_t t331 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t329 * 16) + 8);
                void * t332 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t329 * 16)));
                uint64_t t333 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t334 = gm_l24;
                uint64_t t335 = (uintptr_t)t334;
                if (t333 > t335 || t335 > (uintptr_t)t331) gm_trap_slice(t333, t335, t331, gm_srcl8pn2pjtmj6k, 235, 33);
                gm_b16_8 t336;
                GM_ST(void *, (uint8_t *)&t336, (void *)(((uint8_t *)t332 + t333 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(t335 - t333));
                gm_b16_8 t337 = t336;
                gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
                gm_f_copy_from_f7j6p05(t328, t338);
            }
            goto me3;
mn302:;
ma10:;
            uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t340 != ((uint8_t)15ULL)) goto mn339;
            {
                void * t342 = gm_l0;
                gm_t88rcucg1b7uk t343 = gm_l3;
                gm_t88rcucg1b7uk t344 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t342, GM_CONV(gm_csguankko4lmhd, t343)));
                gm_l25 = t344;
            }
            {
                void * t345 = gm_l0;
                gm_t88rcucg1b7uk t346 = gm_l25;
                gm_t57euh612ligr t347;
                gm_f_union_def_h8mvf4t((void *)&t347, t345, GM_CONV(gm_csguankko4lmhd, t346));
                gm_t57euh612ligr t348 = t347;
                gm_tcmkompbl09il t349;
                if (((((uint8_t *)&t348))[24] != 0)) {
                    gm_tcmkompbl09il t350 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t348) + 0));
                    t349 = t350;
                } else {
                    void * t351 = gm_l0;
                    gm_tl874t2atnlh3 t352 = gm_l1;
                    gm_b16_8 t353;
                    GM_ST(void *, (uint8_t *)&t353, (void *)(gm_stk678hojgu6q));
                    GM_ST(uintptr_t, (uint8_t *)&t353 + 8, (uintptr_t)(23));
                    gm_t76123gfet73g t354;
                    gm_f_trap_qh84l9d((void *)&t354, (void *)NULL, cx, NULL, t351, t352, t353);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t355;
                    GM_ST(uint8_t, (uint8_t *)&t355 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t355 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t355 + 4, 0);
                    ((uint8_t *)&t355)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t355 + 8), t354);
                    gm_tcfa6l7reoosn t356 = t355;
                    uint8_t t357 = ((((uint8_t *)&t356))[0] != 0);
                    F.ret = t355;
                    goto gm_fret;
                }
                gm_l26 = t349;
            }
            {
                void * t358 = gm_l0;
                gm_t88rcucg1b7uk t359 = gm_l4;
                gm_tg5lql6itufk7 t360 = gm_f_int_info_ulcevdv(t358, GM_CONV(gm_csguankko4lmhd, t359));
                gm_tg5lql6itufk7 t361 = t360;
                gm_ttovhfee8mf28 t362;
                if (((((uint8_t *)&t361))[8] != 0)) {
                    gm_ttovhfee8mf28 t363 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t361) + 0));
                    t362 = t363;
                } else {
                    void * t364 = gm_l0;
                    gm_tl874t2atnlh3 t365 = gm_l1;
                    gm_b16_8 t366;
                    GM_ST(void *, (uint8_t *)&t366, (void *)(gm_s3ergh2il1tjf));
                    GM_ST(uintptr_t, (uint8_t *)&t366 + 8, (uintptr_t)(42));
                    gm_t76123gfet73g t367;
                    gm_f_trap_qh84l9d((void *)&t367, (void *)NULL, cx, NULL, t364, t365, t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t368;
                    GM_ST(uint8_t, (uint8_t *)&t368 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t368 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t368 + 4, 0);
                    ((uint8_t *)&t368)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t368 + 8), t367);
                    gm_tcfa6l7reoosn t369 = t368;
                    uint8_t t370 = ((((uint8_t *)&t369))[0] != 0);
                    F.ret = t368;
                    goto gm_fret;
                }
                gm_l27 = t362;
            }
            {
                uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                void * t372 = gm_l0;
                gm_t88rcucg1b7uk t373 = gm_l4;
                uint64_t t374 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t375 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t374 >= (uintptr_t)(t375)) gm_trap_index(t374, t375, gm_srcl8pn2pjtmj6k, 241, 53);
                gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t374 * 16));
                gm_b16_8 t377 = t376;
                gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&t377));
                unsigned __int128 t379 = gm_f_get_int_pg7p4gc(t372, GM_CONV(gm_csguankko4lmhd, t373), t378);
                gm_f_num_val_i3upeur((void *)&gm_l28, t371, t379);
            }
            {
                {
                    gm_b16_8 t381 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t382 = t381;
                    gm_b16_8 t383 = GM_LD(gm_b16_8, ((uint8_t *)&t382));
                    gm_l29 = t383;
                }
                {
                    gm_l30 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t385 = gm_l30;
                        uint64_t t386 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        uint8_t t387 = (t385 < t386);
                        if (!t387) goto lb384;
                        {
                            gm_t3l8idb2m6c1r t390;
                            uint64_t t391 = gm_l30;
                            GM_ST(uint64_t, (uint8_t *)&t390 + 0, t391);
                            uint64_t t392 = gm_l30;
                            uint64_t t393 = (uintptr_t)t392;
                            uint64_t t394 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            memcpy((uint8_t *)&t390 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t393 * 72), 72);
                            gm_t3l8idb2m6c1r t395 = t390;
                            uint64_t t398 = GM_LD(uint64_t, (((uint8_t *)&t395) + 0));
                            gm_l31 = t398;
                            gm_tckdobunaulee t399 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t395) + 8));
                            F.l32 = t399;
                            {
                                uint8_t t402;
                                if ((((((uint8_t *)&F.l32) + 16))[4] != 0)) goto mn404;
                                t402 = ((uint8_t)1);
                                goto me403;
mn404:;
                                t402 = ((uint8_t)0);
                                goto me403;
mn405:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 243, 20);
me403:;
                                if (!t402) goto ie401;
                                uint8_t t406 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 24) + 0));
                                uint8_t t407 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                                uint8_t t408 = ((t406 == t407));
                                if (!t408) goto ie401;
                                {
                                    unsigned __int128 t410 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l32) + 24)));
                                    gm_l33 = t410;
                                }
                                {
                                    unsigned __int128 t411;
                                    uint8_t t413 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 24) + 0));
                                    if (!t413) goto ie412;
                                    unsigned __int128 t415 = gm_l33;
                                    unsigned __int128 r416; bool o417 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t415, &r416);
                                    unsigned __int128 t418 = r416;
                                    unsigned __int128 t419 = t418;
                                    t411 = t419;
                                    goto ix412;
ie412:;
                                    unsigned __int128 t421 = gm_l33;
                                    unsigned __int128 t422 = t421;
                                    t411 = t422;
ix412:;
                                    gm_l34 = t411;
                                }
                                {
                                    unsigned __int128 t424 = gm_l34;
                                    unsigned __int128 t425 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l28) + 16));
                                    uint8_t t426 = ((t424 == t425));
                                    if (!t426) goto ie423;
                                    {
                                        void * t428 = gm_l0;
                                        gm_t2lqu8jsj511d t429 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t428));
                                        gm_l35 = t429;
                                    }
                                    {
                                        void * t430 = gm_l0;
                                        void * t431 = gm_l0;
                                        gm_t88rcucg1b7uk t432 = gm_l25;
                                        uint64_t t433 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t431, t432);
                                        if (gm_unw.kind) {
                                            goto gm_ll388;
                                        }
                                        gm_b16_8 t434 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t430, t433);
                                        if (gm_unw.kind) {
                                            goto gm_ll388;
                                        }
                                        gm_l36 = t434;
                                    }
                                    {
                                        void * t435 = gm_l0;
                                        gm_t88rcucg1b7uk t436 = gm_l25;
                                        gm_b16_8 t437 = gm_l36;
                                        uint64_t t438 = gm_l31;
                                        uint32_t t439 = (uint32_t)t438;
                                        gm_b16_8 t440 = gm_f_empty_slice_ijtp9lo();
                                        gm_b16_8 t441 = t440;
                                        gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&t441));
                                        gm_b0_1 t443 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t435, t436, t437, t439, t442);
                                        if (gm_unw.kind) {
                                            goto gm_ll388;
                                        }
                                    }
                                    {
                                        void * t444 = gm_l0;
                                        gm_t88rcucg1b7uk t445 = gm_l3;
                                        gm_b16_8 t446 = gm_l7;
                                        gm_b16_8 t447 = gm_l36;
                                        gm_b16_8 t448 = t447;
                                        gm_b16_8 t449 = GM_LD(gm_b16_8, ((uint8_t *)&t448));
                                        gm_b0_1 t450 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t444, t445, t446, t449);
                                        if (gm_unw.kind) {
                                            goto gm_ll388;
                                        }
                                    }
                                    {
                                        void * t451 = gm_l0;
                                        gm_t2lqu8jsj511d t452 = gm_l35;
                                        gm_f_release_jn5kqdi(t451, GM_CONV(gm_csc7lc6jlkqkuq, t452));
                                    }
                                    {
                                        gm_b0_1 t453;
                                        gm_tcfa6l7reoosn t454;
                                        GM_ST(uint8_t, (uint8_t *)&t454 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t454 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t454 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t454 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t454 + 16, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t454 + 24, 0);
                                        ((uint8_t *)&t454)[0] = 0;
                                        GM_ST(gm_b0_1, ((uint8_t *)&t454 + 8), t453);
                                        gm_tcfa6l7reoosn t455 = t454;
                                        uint8_t t456 = ((((uint8_t *)&t455))[0] != 0);
                                        F.ret = t454;
                                        goto gm_fret;
                                    }
                                    goto ix423;
ie423:;
ix423:;
                                }
                                goto ix401;
ie401:;
ix401:;
                            }
                            goto me396;
mn397:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 242, 13);
me396:;
                        }
lc384:;
                        {
                            uint64_t t458 = gm_l30;
                            uint64_t r459; bool o460 = __builtin_add_overflow(t458, ((uint64_t)1ULL), &r459);
                            uint64_t t461 = r459;
                            gm_l30 = t461;
                        }
                    }
gm_ll388:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb384; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc384; }
                    }
                    goto gm_lad;
lb384:;
                }
            }
            {
                gm_b16_8 t462 = gm_l7;
                gm_f_fill_abmf5g5(t462, ((uint8_t)0ULL));
            }
            goto me3;
mn339:;
ma11:;
            uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t466 != ((uint8_t)16ULL)) goto pa465;
            goto po464;
pa465:;
            uint8_t t468 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t468 != ((uint8_t)17ULL)) goto pa467;
            goto po464;
pa467:;
            uint8_t t470 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t470 != ((uint8_t)18ULL)) goto pa469;
            goto po464;
pa469:;
            goto mn463;
po464:;
            {
                void * t473 = gm_l0;
                gm_t88rcucg1b7uk t474 = gm_l4;
                gm_t53at80pfdus3 t475;
                gm_f_kind_htv6093((void *)&t475, t473, GM_CONV(gm_csguankko4lmhd, t474));
                gm_t53at80pfdus3 t476 = t475;
                uint8_t t477 = GM_LD(uint8_t, (((uint8_t *)&t476) + 0));
                if (t477 != ((uint8_t)3ULL)) goto ie472;
                {
                    void * t479 = gm_l0;
                    gm_t88rcucg1b7uk t480 = gm_l4;
                    uint64_t t481 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t482 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t481 >= (uintptr_t)(t482)) gm_trap_index(t481, t482, gm_srcl8pn2pjtmj6k, 260, 38);
                    gm_b16_8 t483 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t481 * 16));
                    gm_b16_8 t484 = t483;
                    gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
                    double t486 = gm_f_get_float_grmhlm4(t479, GM_CONV(gm_csguankko4lmhd, t480), t485);
                    gm_l37 = t486;
                }
                {
                    void * t487 = gm_l0;
                    gm_t88rcucg1b7uk t488 = gm_l4;
                    uint64_t t489 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t490 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t489 >= (uintptr_t)(t490)) gm_trap_index(t489, t490, gm_srcl8pn2pjtmj6k, 261, 38);
                    gm_b16_8 t491 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t489 * 16));
                    gm_b16_8 t492 = t491;
                    gm_b16_8 t493 = GM_LD(gm_b16_8, ((uint8_t *)&t492));
                    double t494 = gm_f_get_float_grmhlm4(t487, GM_CONV(gm_csguankko4lmhd, t488), t493);
                    gm_l38 = t494;
                }
                {
                    double t495;
                    uint8_t t501 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t501) { case ((uint8_t)0ULL): goto ma499; case ((uint8_t)1ULL): goto ma499; case ((uint8_t)2ULL): goto ma499; case ((uint8_t)3ULL): goto ma499; case ((uint8_t)4ULL): goto ma499; case ((uint8_t)5ULL): goto ma499; case ((uint8_t)6ULL): goto ma499; case ((uint8_t)7ULL): goto ma499; case ((uint8_t)8ULL): goto ma499; case ((uint8_t)9ULL): goto ma499; case ((uint8_t)10ULL): goto ma499; case ((uint8_t)11ULL): goto ma499; case ((uint8_t)12ULL): goto ma499; case ((uint8_t)13ULL): goto ma499; case ((uint8_t)14ULL): goto ma499; case ((uint8_t)15ULL): goto ma499; case ((uint8_t)16ULL): goto ma497; case ((uint8_t)17ULL): goto ma498; case ((uint8_t)18ULL): goto ma499; case ((uint8_t)19ULL): goto ma499; case ((uint8_t)20ULL): goto ma499; case ((uint8_t)21ULL): goto ma499; case ((uint8_t)22ULL): goto ma499; case ((uint8_t)23ULL): goto ma499; case ((uint8_t)24ULL): goto ma499; case ((uint8_t)25ULL): goto ma499; case ((uint8_t)26ULL): goto ma499; case ((uint8_t)27ULL): goto ma499; case ((uint8_t)28ULL): goto ma499; case ((uint8_t)29ULL): goto ma499; case ((uint8_t)30ULL): goto ma499; case ((uint8_t)31ULL): goto ma499; case ((uint8_t)32ULL): goto ma499; case ((uint8_t)33ULL): goto ma499; case ((uint8_t)34ULL): goto ma499; case ((uint8_t)35ULL): goto ma499; case ((uint8_t)36ULL): goto ma499; case ((uint8_t)37ULL): goto ma499; case ((uint8_t)38ULL): goto ma499; case ((uint8_t)39ULL): goto ma499; case ((uint8_t)40ULL): goto ma499; case ((uint8_t)41ULL): goto ma499; case ((uint8_t)42ULL): goto ma499; case ((uint8_t)43ULL): goto ma499; case ((uint8_t)44ULL): goto ma499; case ((uint8_t)45ULL): goto ma499; case ((uint8_t)46ULL): goto ma499; case ((uint8_t)47ULL): goto ma499; case ((uint8_t)48ULL): goto ma499; case ((uint8_t)49ULL): goto ma499; case ((uint8_t)50ULL): goto ma499; case ((uint8_t)51ULL): goto ma499; case ((uint8_t)52ULL): goto ma499; case ((uint8_t)53ULL): goto ma499; case ((uint8_t)54ULL): goto ma499; case ((uint8_t)55ULL): goto ma499; case ((uint8_t)56ULL): goto ma499; case ((uint8_t)57ULL): goto ma499; case ((uint8_t)58ULL): goto ma499; case ((uint8_t)59ULL): goto ma499; case ((uint8_t)60ULL): goto ma499; case ((uint8_t)61ULL): goto ma499; case ((uint8_t)62ULL): goto ma499; case ((uint8_t)63ULL): goto ma499; case ((uint8_t)64ULL): goto ma499; case ((uint8_t)65ULL): goto ma499; case ((uint8_t)66ULL): goto ma499; case ((uint8_t)67ULL): goto ma499; case ((uint8_t)68ULL): goto ma499; case ((uint8_t)69ULL): goto ma499; case ((uint8_t)70ULL): goto ma499; case ((uint8_t)71ULL): goto ma499; case ((uint8_t)72ULL): goto ma499; case ((uint8_t)73ULL): goto ma499; case ((uint8_t)74ULL): goto ma499; case ((uint8_t)75ULL): goto ma499; case ((uint8_t)76ULL): goto ma499; case ((uint8_t)77ULL): goto ma499; case ((uint8_t)78ULL): goto ma499; case ((uint8_t)79ULL): goto ma499; case ((uint8_t)80ULL): goto ma499; case ((uint8_t)81ULL): goto ma499; default: goto ma499; }
ma497:;
                    uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t503 != ((uint8_t)16ULL)) goto mn502;
                    double t504 = gm_l37;
                    double t505 = gm_l38;
                    double t506 = gm_f_num_fmin_1k5uqp0(t504, t505);
                    t495 = t506;
                    goto me496;
mn502:;
ma498:;
                    uint8_t t508 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t508 != ((uint8_t)17ULL)) goto mn507;
                    double t509 = gm_l37;
                    double t510 = gm_l38;
                    double t511 = gm_f_num_fmax_0qau2jp(t509, t510);
                    t495 = t511;
                    goto me496;
mn507:;
ma499:;
                    double t513 = gm_l37;
                    double t514 = gm_l38;
                    double t515 = gm_f_num_fmax_0qau2jp(t513, t514);
                    void * t516 = gm_l0;
                    gm_t88rcucg1b7uk t517 = gm_l4;
                    uint64_t t518 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t519 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t518 >= (uintptr_t)(t519)) gm_trap_index(t518, t519, gm_srcl8pn2pjtmj6k, 265, 67);
                    gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t518 * 16));
                    gm_b16_8 t521 = t520;
                    gm_b16_8 t522 = GM_LD(gm_b16_8, ((uint8_t *)&t521));
                    double t523 = gm_f_get_float_grmhlm4(t516, GM_CONV(gm_csguankko4lmhd, t517), t522);
                    double t524 = gm_f_num_fmin_1k5uqp0(t515, t523);
                    t495 = t524;
                    goto me496;
mn512:;
ma500:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 262, 22);
me496:;
                    gm_l39 = t495;
                }
                {
                    void * t525 = gm_l0;
                    gm_t88rcucg1b7uk t526 = gm_l4;
                    double t527 = gm_l39;
                    gm_b16_8 t528 = gm_l7;
                    gm_f_put_float_fiad72d(t525, GM_CONV(gm_csguankko4lmhd, t526), t527, t528);
                }
                {
                    gm_b0_1 t529;
                    gm_tcfa6l7reoosn t530;
                    GM_ST(uint8_t, (uint8_t *)&t530 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t530 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t530 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t530 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t530 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t530 + 24, 0);
                    ((uint8_t *)&t530)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t530 + 8), t529);
                    gm_tcfa6l7reoosn t531 = t530;
                    uint8_t t532 = ((((uint8_t *)&t531))[0] != 0);
                    F.ret = t530;
                    goto gm_fret;
                }
                goto ix472;
ie472:;
ix472:;
            }
            {
                void * t533 = gm_l0;
                gm_t88rcucg1b7uk t534 = gm_l4;
                gm_tg5lql6itufk7 t535 = gm_f_int_info_ulcevdv(t533, GM_CONV(gm_csguankko4lmhd, t534));
                gm_tg5lql6itufk7 t536 = t535;
                gm_ttovhfee8mf28 t537;
                if (((((uint8_t *)&t536))[8] != 0)) {
                    gm_ttovhfee8mf28 t538 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t536) + 0));
                    t537 = t538;
                } else {
                    void * t539 = gm_l0;
                    gm_tl874t2atnlh3 t540 = gm_l1;
                    void * t541 = gm_l0;
                    void * t542 = GM_LD(void *, (((uint8_t *)t541) + 8));
                    gm_t88rcucg1b7uk t543 = gm_l4;
                    gm_b16_8 t544 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t542, t543);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t545;
                    GM_ST(gm_b16_8, ((uint8_t *)&t545 + 0), t544);
                    gm_tabb710d9mq2e t546;
                    gm_f_format_kj15nqp((void *)&t546, (void *)NULL, cx, NULL, t545);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t547 = t546;
                    if (((((uint8_t *)&t547))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t547) + 8, gm_srcl8pn2pjtmj6k, 270, 62);
                    gm_b16_8 t548 = GM_LD(gm_b16_8, (((uint8_t *)&t547) + 8));
                    gm_b16_8 t549 = t548;
                    gm_b16_8 t550 = GM_LD(gm_b16_8, ((uint8_t *)&t549));
                    gm_t76123gfet73g t551;
                    gm_f_trap_qh84l9d((void *)&t551, (void *)NULL, cx, NULL, t539, t540, t550);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t552;
                    GM_ST(uint8_t, (uint8_t *)&t552 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t552 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t552 + 4, 0);
                    ((uint8_t *)&t552)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t552 + 8), t551);
                    gm_tcfa6l7reoosn t553 = t552;
                    uint8_t t554 = ((((uint8_t *)&t553))[0] != 0);
                    F.ret = t552;
                    goto gm_fret;
                }
                gm_l40 = t537;
            }
            {
                void * t555 = gm_l0;
                gm_t88rcucg1b7uk t556 = gm_l4;
                uint64_t t557 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t558 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t557 >= (uintptr_t)(t558)) gm_trap_index(t557, t558, gm_srcl8pn2pjtmj6k, 271, 32);
                gm_b16_8 t559 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t557 * 16));
                gm_b16_8 t560 = t559;
                gm_b16_8 t561 = GM_LD(gm_b16_8, ((uint8_t *)&t560));
                unsigned __int128 t562 = gm_f_get_int_pg7p4gc(t555, GM_CONV(gm_csguankko4lmhd, t556), t561);
                gm_l41 = t562;
            }
            {
                void * t563 = gm_l0;
                gm_t88rcucg1b7uk t564 = gm_l4;
                uint64_t t565 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t566 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t565 >= (uintptr_t)(t566)) gm_trap_index(t565, t566, gm_srcl8pn2pjtmj6k, 272, 32);
                gm_b16_8 t567 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t565 * 16));
                gm_b16_8 t568 = t567;
                gm_b16_8 t569 = GM_LD(gm_b16_8, ((uint8_t *)&t568));
                unsigned __int128 t570 = gm_f_get_int_pg7p4gc(t563, GM_CONV(gm_csguankko4lmhd, t564), t569);
                gm_l42 = t570;
            }
            {
                gm_l43 = ((uint64_t)0ULL);
            }
            {
                uint8_t t576 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t576) { case ((uint8_t)0ULL): goto ma574; case ((uint8_t)1ULL): goto ma574; case ((uint8_t)2ULL): goto ma574; case ((uint8_t)3ULL): goto ma574; case ((uint8_t)4ULL): goto ma574; case ((uint8_t)5ULL): goto ma574; case ((uint8_t)6ULL): goto ma574; case ((uint8_t)7ULL): goto ma574; case ((uint8_t)8ULL): goto ma574; case ((uint8_t)9ULL): goto ma574; case ((uint8_t)10ULL): goto ma574; case ((uint8_t)11ULL): goto ma574; case ((uint8_t)12ULL): goto ma574; case ((uint8_t)13ULL): goto ma574; case ((uint8_t)14ULL): goto ma574; case ((uint8_t)15ULL): goto ma574; case ((uint8_t)16ULL): goto ma572; case ((uint8_t)17ULL): goto ma573; case ((uint8_t)18ULL): goto ma574; case ((uint8_t)19ULL): goto ma574; case ((uint8_t)20ULL): goto ma574; case ((uint8_t)21ULL): goto ma574; case ((uint8_t)22ULL): goto ma574; case ((uint8_t)23ULL): goto ma574; case ((uint8_t)24ULL): goto ma574; case ((uint8_t)25ULL): goto ma574; case ((uint8_t)26ULL): goto ma574; case ((uint8_t)27ULL): goto ma574; case ((uint8_t)28ULL): goto ma574; case ((uint8_t)29ULL): goto ma574; case ((uint8_t)30ULL): goto ma574; case ((uint8_t)31ULL): goto ma574; case ((uint8_t)32ULL): goto ma574; case ((uint8_t)33ULL): goto ma574; case ((uint8_t)34ULL): goto ma574; case ((uint8_t)35ULL): goto ma574; case ((uint8_t)36ULL): goto ma574; case ((uint8_t)37ULL): goto ma574; case ((uint8_t)38ULL): goto ma574; case ((uint8_t)39ULL): goto ma574; case ((uint8_t)40ULL): goto ma574; case ((uint8_t)41ULL): goto ma574; case ((uint8_t)42ULL): goto ma574; case ((uint8_t)43ULL): goto ma574; case ((uint8_t)44ULL): goto ma574; case ((uint8_t)45ULL): goto ma574; case ((uint8_t)46ULL): goto ma574; case ((uint8_t)47ULL): goto ma574; case ((uint8_t)48ULL): goto ma574; case ((uint8_t)49ULL): goto ma574; case ((uint8_t)50ULL): goto ma574; case ((uint8_t)51ULL): goto ma574; case ((uint8_t)52ULL): goto ma574; case ((uint8_t)53ULL): goto ma574; case ((uint8_t)54ULL): goto ma574; case ((uint8_t)55ULL): goto ma574; case ((uint8_t)56ULL): goto ma574; case ((uint8_t)57ULL): goto ma574; case ((uint8_t)58ULL): goto ma574; case ((uint8_t)59ULL): goto ma574; case ((uint8_t)60ULL): goto ma574; case ((uint8_t)61ULL): goto ma574; case ((uint8_t)62ULL): goto ma574; case ((uint8_t)63ULL): goto ma574; case ((uint8_t)64ULL): goto ma574; case ((uint8_t)65ULL): goto ma574; case ((uint8_t)66ULL): goto ma574; case ((uint8_t)67ULL): goto ma574; case ((uint8_t)68ULL): goto ma574; case ((uint8_t)69ULL): goto ma574; case ((uint8_t)70ULL): goto ma574; case ((uint8_t)71ULL): goto ma574; case ((uint8_t)72ULL): goto ma574; case ((uint8_t)73ULL): goto ma574; case ((uint8_t)74ULL): goto ma574; case ((uint8_t)75ULL): goto ma574; case ((uint8_t)76ULL): goto ma574; case ((uint8_t)77ULL): goto ma574; case ((uint8_t)78ULL): goto ma574; case ((uint8_t)79ULL): goto ma574; case ((uint8_t)80ULL): goto ma574; case ((uint8_t)81ULL): goto ma574; default: goto ma574; }
ma572:;
                uint8_t t578 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t578 != ((uint8_t)16ULL)) goto mn577;
                {
                    uint64_t t580;
                    uint8_t t582 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t583 = gm_l41;
                    unsigned __int128 t584 = gm_l42;
                    int32_t t585 = gm_f_int_cmp_kb3sm6u(t582, t583, t584);
                    uint8_t t586 = (t585 <= ((int32_t)0LL));
                    if (!t586) goto ie581;
                    uint64_t t588 = ((uint64_t)0ULL);
                    t580 = t588;
                    goto ix581;
ie581:;
                    uint64_t t590 = ((uint64_t)1ULL);
                    t580 = t590;
ix581:;
                    gm_l43 = t580;
                }
                goto me571;
mn577:;
ma573:;
                uint8_t t592 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t592 != ((uint8_t)17ULL)) goto mn591;
                {
                    uint64_t t594;
                    uint8_t t596 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t597 = gm_l41;
                    unsigned __int128 t598 = gm_l42;
                    int32_t t599 = gm_f_int_cmp_kb3sm6u(t596, t597, t598);
                    uint8_t t600 = (t599 >= ((int32_t)0LL));
                    if (!t600) goto ie595;
                    uint64_t t602 = ((uint64_t)0ULL);
                    t594 = t602;
                    goto ix595;
ie595:;
                    uint64_t t604 = ((uint64_t)1ULL);
                    t594 = t604;
ix595:;
                    gm_l43 = t594;
                }
                goto me571;
mn591:;
ma574:;
                {
                    void * t607 = gm_l0;
                    gm_t88rcucg1b7uk t608 = gm_l4;
                    uint64_t t609 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t610 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t609 >= (uintptr_t)(t610)) gm_trap_index(t609, t610, gm_srcl8pn2pjtmj6k, 278, 41);
                    gm_b16_8 t611 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t609 * 16));
                    gm_b16_8 t612 = t611;
                    gm_b16_8 t613 = GM_LD(gm_b16_8, ((uint8_t *)&t612));
                    unsigned __int128 t614 = gm_f_get_int_pg7p4gc(t607, GM_CONV(gm_csguankko4lmhd, t608), t613);
                    gm_l44 = t614;
                }
                {
                    uint8_t t616 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t617 = gm_l41;
                    unsigned __int128 t618 = gm_l42;
                    int32_t t619 = gm_f_int_cmp_kb3sm6u(t616, t617, t618);
                    uint8_t t620 = (t619 < ((int32_t)0LL));
                    if (!t620) goto ie615;
                    {
                        gm_l43 = ((uint64_t)1ULL);
                    }
                    goto ix615;
ie615:;
                    uint8_t t623 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t624 = gm_l41;
                    unsigned __int128 t625 = gm_l44;
                    int32_t t626 = gm_f_int_cmp_kb3sm6u(t623, t624, t625);
                    uint8_t t627 = (t626 > ((int32_t)0LL));
                    if (!t627) goto ie622;
                    {
                        gm_l43 = ((uint64_t)2ULL);
                    }
                    goto ix622;
ie622:;
ix622:;
ix615:;
                }
                goto me571;
mn605:;
ma575:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 274, 13);
me571:;
            }
            {
                gm_b16_8 t629 = gm_l7;
                uint64_t t630 = gm_l43;
                uint64_t t631 = (uintptr_t)t630;
                uint64_t t632 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t631 >= (uintptr_t)(t632)) gm_trap_index(t631, t632, gm_srcl8pn2pjtmj6k, 286, 27);
                gm_b16_8 t633 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t631 * 16));
                gm_b16_8 t634 = t633;
                gm_b16_8 t635 = GM_LD(gm_b16_8, ((uint8_t *)&t634));
                gm_f_copy_from_f7j6p05(t629, t635);
            }
            goto me3;
mn463:;
ma12:;
            uint8_t t637 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t637 != ((uint8_t)19ULL)) goto mn636;
            {
                void * t640 = gm_l0;
                gm_t88rcucg1b7uk t641 = gm_l4;
                gm_t53at80pfdus3 t642;
                gm_f_kind_htv6093((void *)&t642, t640, GM_CONV(gm_csguankko4lmhd, t641));
                gm_t53at80pfdus3 t643 = t642;
                uint8_t t644 = GM_LD(uint8_t, (((uint8_t *)&t643) + 0));
                if (t644 != ((uint8_t)3ULL)) goto ie639;
                {
                    void * t646 = gm_l0;
                    gm_t88rcucg1b7uk t647 = gm_l4;
                    void * t648 = gm_l0;
                    gm_t88rcucg1b7uk t649 = gm_l4;
                    uint64_t t650 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t651 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t650 >= (uintptr_t)(t651)) gm_trap_index(t650, t651, gm_srcl8pn2pjtmj6k, 290, 50);
                    gm_b16_8 t652 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t650 * 16));
                    gm_b16_8 t653 = t652;
                    gm_b16_8 t654 = GM_LD(gm_b16_8, ((uint8_t *)&t653));
                    double t655 = gm_f_get_float_grmhlm4(t648, GM_CONV(gm_csguankko4lmhd, t649), t654);
                    double t656 = t655;
                    uint64_t t657;
                    memset(&t657, 0, sizeof t657); memcpy(&t657, ((uint8_t *)&t656), sizeof t657 < 8 ? sizeof t657 : 8);
                    uint64_t t658 = (uint64_t)(t657 & ((uint64_t)9223372036854775807ULL));
                    uint64_t t659 = t658;
                    double t660;
                    memset(&t660, 0, sizeof t660); memcpy(&t660, ((uint8_t *)&t659), sizeof t660 < 8 ? sizeof t660 : 8);
                    gm_b16_8 t661 = gm_l7;
                    gm_f_put_float_fiad72d(t646, GM_CONV(gm_csguankko4lmhd, t647), t660, t661);
                }
                {
                    gm_b0_1 t662;
                    gm_tcfa6l7reoosn t663;
                    GM_ST(uint8_t, (uint8_t *)&t663 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t663 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t663 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t663 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t663 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t663 + 24, 0);
                    ((uint8_t *)&t663)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t663 + 8), t662);
                    gm_tcfa6l7reoosn t664 = t663;
                    uint8_t t665 = ((((uint8_t *)&t664))[0] != 0);
                    F.ret = t663;
                    goto gm_fret;
                }
                goto ix639;
ie639:;
ix639:;
            }
            {
                void * t666 = gm_l0;
                gm_t88rcucg1b7uk t667 = gm_l4;
                gm_tg5lql6itufk7 t668 = gm_f_int_info_ulcevdv(t666, GM_CONV(gm_csguankko4lmhd, t667));
                gm_tg5lql6itufk7 t669 = t668;
                gm_ttovhfee8mf28 t670;
                if (((((uint8_t *)&t669))[8] != 0)) {
                    gm_ttovhfee8mf28 t671 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t669) + 0));
                    t670 = t671;
                } else {
                    void * t672 = gm_l0;
                    gm_tl874t2atnlh3 t673 = gm_l1;
                    gm_b16_8 t674;
                    GM_ST(void *, (uint8_t *)&t674, (void *)(gm_sbel6qookv0gl));
                    GM_ST(uintptr_t, (uint8_t *)&t674 + 8, (uintptr_t)(53));
                    gm_t76123gfet73g t675;
                    gm_f_trap_qh84l9d((void *)&t675, (void *)NULL, cx, NULL, t672, t673, t674);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t676;
                    GM_ST(uint8_t, (uint8_t *)&t676 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t676 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t676 + 4, 0);
                    ((uint8_t *)&t676)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t676 + 8), t675);
                    gm_tcfa6l7reoosn t677 = t676;
                    uint8_t t678 = ((((uint8_t *)&t677))[0] != 0);
                    F.ret = t676;
                    goto gm_fret;
                }
                gm_l45 = t670;
            }
            {
                void * t679 = gm_l0;
                gm_t88rcucg1b7uk t680 = gm_l4;
                uint64_t t681 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t682 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t681 >= (uintptr_t)(t682)) gm_trap_index(t681, t682, gm_srcl8pn2pjtmj6k, 294, 32);
                gm_b16_8 t683 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t681 * 16));
                gm_b16_8 t684 = t683;
                gm_b16_8 t685 = GM_LD(gm_b16_8, ((uint8_t *)&t684));
                unsigned __int128 t686 = gm_f_get_int_pg7p4gc(t679, GM_CONV(gm_csguankko4lmhd, t680), t685);
                gm_l46 = t686;
            }
            {
                uint8_t t688 = 0;
                uint8_t t689 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                uint8_t t690 = (uint8_t)!t689;
                if (!t690) {
                    unsigned __int128 t691 = gm_l46;
                    unsigned __int128 n692 = (unsigned __int128)((uint64_t)127ULL);
                    if (n692 >= 128) gm_trap_shift((uint64_t)n692, "u128", gm_srcl8pn2pjtmj6k, 295, 36);
                    unsigned __int128 t693 = (unsigned __int128)(t691 >> (int)n692);
                    uint8_t t694 = ((t693 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                    t688 = t694;
                } else t688 = 1;
                if (!t688) goto ie687;
                {
                    void * t696 = gm_l0;
                    gm_b16_8 t697 = gm_l7;
                    unsigned __int128 t698 = gm_l46;
                    gm_f_put_uint_cddin99(t696, t697, t698);
                }
                {
                    gm_b0_1 t699;
                    gm_tcfa6l7reoosn t700;
                    GM_ST(uint8_t, (uint8_t *)&t700 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t700 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t700 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t700 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t700 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t700 + 24, 0);
                    ((uint8_t *)&t700)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t700 + 8), t699);
                    gm_tcfa6l7reoosn t701 = t700;
                    uint8_t t702 = ((((uint8_t *)&t701))[0] != 0);
                    F.ret = t700;
                    goto gm_fret;
                }
                goto ix687;
ie687:;
ix687:;
            }
            {
                unsigned __int128 t703 = gm_l46;
                unsigned __int128 r704; bool o705 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t703, &r704);
                unsigned __int128 t706 = r704;
                gm_l47 = t706;
            }
            {
                unsigned __int128 t708 = gm_l47;
                uint32_t t709 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 4));
                uint32_t r710; bool o711 = __builtin_sub_overflow(t709, ((uint32_t)1ULL), &r710);
                if (o711) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 300, 34);
                uint32_t t712 = r710;
                unsigned __int128 n713 = (unsigned __int128)t712;
                if (n713 >= 128) gm_trap_shift((uint64_t)n713, "u128", gm_srcl8pn2pjtmj6k, 300, 22);
                unsigned __int128 r714 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n713);
                bool o715 = (unsigned __int128)(r714 >> (int)n713) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                if (o715) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 300, 22);
                unsigned __int128 t716 = r714;
                uint8_t t717 = ((t708 == t716));
                if (!t717) goto ie707;
                {
                    void * t719 = gm_l0;
                    gm_tl874t2atnlh3 t720 = gm_l1;
                    gm_b16_8 t721;
                    GM_ST(void *, (uint8_t *)&t721, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t721 + 8, (uintptr_t)(3));
                    gm_tcfa6l7reoosn t722;
                    gm_f_checked_overflow_ma1bm1g((void *)&t722, (void *)NULL, cx, NULL, t719, t720, t721);
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
                }
                {
                    void * t727 = gm_l0;
                    gm_b16_8 t728 = gm_l7;
                    unsigned __int128 t729 = gm_l46;
                    gm_f_put_uint_cddin99(t727, t728, t729);
                }
                {
                    gm_b0_1 t730;
                    gm_tcfa6l7reoosn t731;
                    GM_ST(uint8_t, (uint8_t *)&t731 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t731 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t731 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t731 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t731 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t731 + 24, 0);
                    ((uint8_t *)&t731)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t731 + 8), t730);
                    gm_tcfa6l7reoosn t732 = t731;
                    uint8_t t733 = ((((uint8_t *)&t732))[0] != 0);
                    F.ret = t731;
                    goto gm_fret;
                }
                goto ix707;
ie707:;
ix707:;
            }
            {
                void * t734 = gm_l0;
                gm_b16_8 t735 = gm_l7;
                unsigned __int128 t736 = gm_l47;
                gm_f_put_uint_cddin99(t734, t735, t736);
            }
            goto me3;
mn636:;
ma13:;
            uint8_t t738 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t738 != ((uint8_t)20ULL)) goto mn737;
            {
                void * t740 = gm_l0;
                gm_t88rcucg1b7uk t741 = gm_l4;
                gm_tg5lql6itufk7 t742 = gm_f_int_info_ulcevdv(t740, GM_CONV(gm_csguankko4lmhd, t741));
                gm_tg5lql6itufk7 t743 = t742;
                gm_ttovhfee8mf28 t744;
                if (((((uint8_t *)&t743))[8] != 0)) {
                    gm_ttovhfee8mf28 t745 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t743) + 0));
                    t744 = t745;
                } else {
                    void * t746 = gm_l0;
                    gm_tl874t2atnlh3 t747 = gm_l1;
                    gm_b16_8 t748;
                    GM_ST(void *, (uint8_t *)&t748, (void *)(gm_s3gnqv0up712m));
                    GM_ST(uintptr_t, (uint8_t *)&t748 + 8, (uintptr_t)(60));
                    gm_t76123gfet73g t749;
                    gm_f_trap_qh84l9d((void *)&t749, (void *)NULL, cx, NULL, t746, t747, t748);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t750;
                    GM_ST(uint8_t, (uint8_t *)&t750 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t750 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t750 + 4, 0);
                    ((uint8_t *)&t750)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t750 + 8), t749);
                    gm_tcfa6l7reoosn t751 = t750;
                    uint8_t t752 = ((((uint8_t *)&t751))[0] != 0);
                    F.ret = t750;
                    goto gm_fret;
                }
                gm_l48 = t744;
            }
            {
                void * t753 = gm_l0;
                gm_t88rcucg1b7uk t754 = gm_l4;
                uint64_t t755 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t756 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t755 >= (uintptr_t)(t756)) gm_trap_index(t755, t756, gm_srcl8pn2pjtmj6k, 309, 32);
                gm_b16_8 t757 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t755 * 16));
                gm_b16_8 t758 = t757;
                gm_b16_8 t759 = GM_LD(gm_b16_8, ((uint8_t *)&t758));
                unsigned __int128 t760 = gm_f_get_int_pg7p4gc(t753, GM_CONV(gm_csguankko4lmhd, t754), t759);
                gm_l49 = t760;
            }
            {
                void * t761 = gm_l0;
                gm_t88rcucg1b7uk t762 = gm_l5;
                uint64_t t763 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t764 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t763 >= (uintptr_t)(t764)) gm_trap_index(t763, t764, gm_srcl8pn2pjtmj6k, 310, 32);
                gm_b16_8 t765 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t763 * 16));
                gm_b16_8 t766 = t765;
                gm_b16_8 t767 = GM_LD(gm_b16_8, ((uint8_t *)&t766));
                unsigned __int128 t768 = gm_f_get_int_pg7p4gc(t761, GM_CONV(gm_csguankko4lmhd, t762), t767);
                gm_l50 = t768;
            }
            {
                gm_l51 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t770 = GM_LD(uint8_t, (((uint8_t *)&gm_l48) + 0));
                unsigned __int128 t771 = gm_l49;
                unsigned __int128 t772 = gm_l50;
                int32_t t773 = gm_f_int_cmp_kb3sm6u(t770, t771, t772);
                uint8_t t774 = (t773 >= ((int32_t)0LL));
                if (!t774) goto ie769;
                {
                    unsigned __int128 t776 = gm_l49;
                    unsigned __int128 t777 = gm_l50;
                    unsigned __int128 r778; bool o779 = __builtin_sub_overflow(t776, t777, &r778);
                    unsigned __int128 t780 = r778;
                    gm_l51 = t780;
                }
                goto ix769;
ie769:;
                {
                    unsigned __int128 t782 = gm_l50;
                    unsigned __int128 t783 = gm_l49;
                    unsigned __int128 r784; bool o785 = __builtin_sub_overflow(t782, t783, &r784);
                    unsigned __int128 t786 = r784;
                    gm_l51 = t786;
                }
ix769:;
            }
            {
                void * t788 = gm_l0;
                gm_t88rcucg1b7uk t789 = gm_l3;
                gm_tg5lql6itufk7 t790 = gm_f_int_info_ulcevdv(t788, GM_CONV(gm_csguankko4lmhd, t789));
                gm_tg5lql6itufk7 t791 = t790;
                if (!((((uint8_t *)&t791))[8] != 0)) goto ie787;
                gm_ttovhfee8mf28 t792 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t791));
                gm_l52 = t792;
                {
                    uint8_t t794 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                    uint32_t t795 = GM_LD(uint32_t, (((uint8_t *)&gm_l52) + 4));
                    unsigned __int128 t796 = gm_l51;
                    unsigned __int128 t797 = gm_f_num_ext_348kmbu(t794, t795, t796);
                    gm_l51 = t797;
                }
                goto ix787;
ie787:;
ix787:;
            }
            {
                void * t798 = gm_l0;
                gm_b16_8 t799 = gm_l7;
                unsigned __int128 t800 = gm_l51;
                gm_f_put_uint_cddin99(t798, t799, t800);
            }
            goto me3;
mn737:;
ma14:;
            uint8_t t802 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t802 != ((uint8_t)21ULL)) goto mn801;
            gm_t9qkh0rsooc8d t803 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l53 = t803;
            void * t804 = gm_l0;
            gm_tl874t2atnlh3 t805 = gm_l1;
            gm_t9qkh0rsooc8d t806 = gm_l53;
            gm_t88rcucg1b7uk t807 = gm_l4;
            gm_t88rcucg1b7uk t808 = gm_l3;
            gm_t88rcucg1b7uk t809 = gm_l5;
            uint64_t t810 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t811 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t810 >= (uintptr_t)(t811)) gm_trap_index(t810, t811, gm_srcl8pn2pjtmj6k, 320, 60);
            gm_b16_8 t812 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t810 * 16));
            gm_b16_8 t813 = t812;
            gm_b16_8 t814 = GM_LD(gm_b16_8, ((uint8_t *)&t813));
            uint64_t t815 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t816 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t815 >= (uintptr_t)(t816)) gm_trap_index(t815, t816, gm_srcl8pn2pjtmj6k, 320, 66);
            gm_b16_8 t817 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t815 * 16));
            gm_b16_8 t818 = t817;
            gm_b16_8 t819 = GM_LD(gm_b16_8, ((uint8_t *)&t818));
            gm_b16_8 t820 = gm_l7;
            gm_f_pow_7je2uvi((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t804, t805, t806, t807, t808, t809, t814, t819, t820);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn801:;
ma15:;
            uint8_t t824 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t824 != ((uint8_t)22ULL)) goto pa823;
            goto po822;
pa823:;
            uint8_t t826 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t826 != ((uint8_t)23ULL)) goto pa825;
            goto po822;
pa825:;
            uint8_t t828 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t828 != ((uint8_t)24ULL)) goto pa827;
            goto po822;
pa827:;
            uint8_t t830 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t830 != ((uint8_t)25ULL)) goto pa829;
            goto po822;
pa829:;
            uint8_t t832 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t832 != ((uint8_t)26ULL)) goto pa831;
            goto po822;
pa831:;
            goto mn821;
po822:;
            {
                void * t835 = gm_l0;
                gm_t88rcucg1b7uk t836 = gm_l4;
                gm_t53at80pfdus3 t837;
                gm_f_kind_htv6093((void *)&t837, t835, GM_CONV(gm_csguankko4lmhd, t836));
                gm_t53at80pfdus3 t838 = t837;
                uint8_t t839;
                uint8_t t842 = GM_LD(uint8_t, (((uint8_t *)&t838) + 0));
                if (t842 != ((uint8_t)3ULL)) goto mn841;
                t839 = ((uint8_t)1);
                goto me840;
mn841:;
                t839 = ((uint8_t)0);
                goto me840;
mn843:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 322, 21);
me840:;
                uint8_t t844 = (uint8_t)!t839;
                if (!t844) goto ie834;
                {
                    void * t846 = gm_l0;
                    gm_tl874t2atnlh3 t847 = gm_l1;
                    gm_b16_8 t848;
                    GM_ST(void *, (uint8_t *)&t848, (void *)(gm_s9h8lp3gkedth));
                    GM_ST(uintptr_t, (uint8_t *)&t848 + 8, (uintptr_t)(28));
                    gm_t76123gfet73g t849;
                    gm_f_trap_qh84l9d((void *)&t849, (void *)NULL, cx, NULL, t846, t847, t848);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t850;
                    GM_ST(uint8_t, (uint8_t *)&t850 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t850 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t850 + 4, 0);
                    ((uint8_t *)&t850)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t850 + 8), t849);
                    gm_tcfa6l7reoosn t851 = t850;
                    uint8_t t852 = ((((uint8_t *)&t851))[0] != 0);
                    F.ret = t850;
                    goto gm_fret;
                }
                goto ix834;
ie834:;
ix834:;
            }
            {
                void * t853 = gm_l0;
                gm_t88rcucg1b7uk t854 = gm_l4;
                uint64_t t855 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t856 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t855 >= (uintptr_t)(t856)) gm_trap_index(t855, t856, gm_srcl8pn2pjtmj6k, 323, 34);
                gm_b16_8 t857 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t855 * 16));
                gm_b16_8 t858 = t857;
                gm_b16_8 t859 = GM_LD(gm_b16_8, ((uint8_t *)&t858));
                double t860 = gm_f_get_float_grmhlm4(t853, GM_CONV(gm_csguankko4lmhd, t854), t859);
                gm_l54 = t860;
            }
            {
                double t861 = gm_l54;
                double t862 = t861;
                uint64_t t863;
                memset(&t863, 0, sizeof t863); memcpy(&t863, ((uint8_t *)&t862), sizeof t863 < 8 ? sizeof t863 : 8);
                gm_l55 = t863;
            }
            {
                uint64_t t864 = gm_l55;
                unsigned __int128 n865 = (unsigned __int128)((uint64_t)52ULL);
                if (n865 >= 64) gm_trap_shift((uint64_t)n865, "u64", gm_srcl8pn2pjtmj6k, 325, 21);
                uint64_t t866 = (uint64_t)(t864 >> (int)n865);
                uint64_t t867 = (uint64_t)(t866 & ((uint64_t)2047ULL));
                gm_l56 = t867;
            }
            {
                uint8_t t875 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t875) { case ((uint8_t)0ULL): goto ma873; case ((uint8_t)1ULL): goto ma873; case ((uint8_t)2ULL): goto ma873; case ((uint8_t)3ULL): goto ma873; case ((uint8_t)4ULL): goto ma873; case ((uint8_t)5ULL): goto ma873; case ((uint8_t)6ULL): goto ma873; case ((uint8_t)7ULL): goto ma873; case ((uint8_t)8ULL): goto ma873; case ((uint8_t)9ULL): goto ma873; case ((uint8_t)10ULL): goto ma873; case ((uint8_t)11ULL): goto ma873; case ((uint8_t)12ULL): goto ma873; case ((uint8_t)13ULL): goto ma873; case ((uint8_t)14ULL): goto ma873; case ((uint8_t)15ULL): goto ma873; case ((uint8_t)16ULL): goto ma873; case ((uint8_t)17ULL): goto ma873; case ((uint8_t)18ULL): goto ma873; case ((uint8_t)19ULL): goto ma873; case ((uint8_t)20ULL): goto ma873; case ((uint8_t)21ULL): goto ma873; case ((uint8_t)22ULL): goto ma869; case ((uint8_t)23ULL): goto ma870; case ((uint8_t)24ULL): goto ma871; case ((uint8_t)25ULL): goto ma872; case ((uint8_t)26ULL): goto ma873; case ((uint8_t)27ULL): goto ma873; case ((uint8_t)28ULL): goto ma873; case ((uint8_t)29ULL): goto ma873; case ((uint8_t)30ULL): goto ma873; case ((uint8_t)31ULL): goto ma873; case ((uint8_t)32ULL): goto ma873; case ((uint8_t)33ULL): goto ma873; case ((uint8_t)34ULL): goto ma873; case ((uint8_t)35ULL): goto ma873; case ((uint8_t)36ULL): goto ma873; case ((uint8_t)37ULL): goto ma873; case ((uint8_t)38ULL): goto ma873; case ((uint8_t)39ULL): goto ma873; case ((uint8_t)40ULL): goto ma873; case ((uint8_t)41ULL): goto ma873; case ((uint8_t)42ULL): goto ma873; case ((uint8_t)43ULL): goto ma873; case ((uint8_t)44ULL): goto ma873; case ((uint8_t)45ULL): goto ma873; case ((uint8_t)46ULL): goto ma873; case ((uint8_t)47ULL): goto ma873; case ((uint8_t)48ULL): goto ma873; case ((uint8_t)49ULL): goto ma873; case ((uint8_t)50ULL): goto ma873; case ((uint8_t)51ULL): goto ma873; case ((uint8_t)52ULL): goto ma873; case ((uint8_t)53ULL): goto ma873; case ((uint8_t)54ULL): goto ma873; case ((uint8_t)55ULL): goto ma873; case ((uint8_t)56ULL): goto ma873; case ((uint8_t)57ULL): goto ma873; case ((uint8_t)58ULL): goto ma873; case ((uint8_t)59ULL): goto ma873; case ((uint8_t)60ULL): goto ma873; case ((uint8_t)61ULL): goto ma873; case ((uint8_t)62ULL): goto ma873; case ((uint8_t)63ULL): goto ma873; case ((uint8_t)64ULL): goto ma873; case ((uint8_t)65ULL): goto ma873; case ((uint8_t)66ULL): goto ma873; case ((uint8_t)67ULL): goto ma873; case ((uint8_t)68ULL): goto ma873; case ((uint8_t)69ULL): goto ma873; case ((uint8_t)70ULL): goto ma873; case ((uint8_t)71ULL): goto ma873; case ((uint8_t)72ULL): goto ma873; case ((uint8_t)73ULL): goto ma873; case ((uint8_t)74ULL): goto ma873; case ((uint8_t)75ULL): goto ma873; case ((uint8_t)76ULL): goto ma873; case ((uint8_t)77ULL): goto ma873; case ((uint8_t)78ULL): goto ma873; case ((uint8_t)79ULL): goto ma873; case ((uint8_t)80ULL): goto ma873; case ((uint8_t)81ULL): goto ma873; default: goto ma873; }
ma869:;
                uint8_t t877 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t877 != ((uint8_t)22ULL)) goto mn876;
                {
                    uint64_t t879 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t880 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t879 >= (uintptr_t)(t880)) gm_trap_index(t879, t880, gm_srcl8pn2pjtmj6k, 327, 29);
                    uint8_t *t881 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t879 * 1);
                    uint8_t t882;
                    uint64_t t884 = gm_l56;
                    uint8_t t885 = ((t884 == ((uint64_t)2047ULL)));
                    if (!t885) goto ie883;
                    uint64_t t886 = gm_l55;
                    uint64_t t887 = (uint64_t)(t886 & ((uint64_t)4503599627370495ULL));
                    uint8_t t888 = (!(t887 == ((uint64_t)0ULL)));
                    if (!t888) goto ie883;
                    uint8_t t890 = ((uint8_t)1ULL);
                    t882 = t890;
                    goto ix883;
ie883:;
                    uint8_t t892 = ((uint8_t)0ULL);
                    t882 = t892;
ix883:;
                    GM_ST(uint8_t, t881, t882);
                }
                goto me868;
mn876:;
ma870:;
                uint8_t t894 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t894 != ((uint8_t)23ULL)) goto mn893;
                {
                    uint64_t t896 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t897 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t896 >= (uintptr_t)(t897)) gm_trap_index(t896, t897, gm_srcl8pn2pjtmj6k, 328, 29);
                    uint8_t *t898 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t896 * 1);
                    uint8_t t899;
                    uint64_t t901 = gm_l56;
                    uint8_t t902 = ((t901 == ((uint64_t)2047ULL)));
                    if (!t902) goto ie900;
                    uint64_t t903 = gm_l55;
                    uint64_t t904 = (uint64_t)(t903 & ((uint64_t)4503599627370495ULL));
                    uint8_t t905 = ((t904 == ((uint64_t)0ULL)));
                    if (!t905) goto ie900;
                    uint8_t t907 = ((uint8_t)1ULL);
                    t899 = t907;
                    goto ix900;
ie900:;
                    uint8_t t909 = ((uint8_t)0ULL);
                    t899 = t909;
ix900:;
                    GM_ST(uint8_t, t898, t899);
                }
                goto me868;
mn893:;
ma871:;
                uint8_t t911 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t911 != ((uint8_t)24ULL)) goto mn910;
                {
                    uint64_t t913 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t914 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t913 >= (uintptr_t)(t914)) gm_trap_index(t913, t914, gm_srcl8pn2pjtmj6k, 329, 32);
                    uint8_t *t915 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t913 * 1);
                    uint8_t t916;
                    uint64_t t918 = gm_l56;
                    uint8_t t919 = ((t918 == ((uint64_t)2047ULL)));
                    if (!t919) goto ie917;
                    uint8_t t921 = ((uint8_t)0ULL);
                    t916 = t921;
                    goto ix917;
ie917:;
                    uint8_t t923 = ((uint8_t)1ULL);
                    t916 = t923;
ix917:;
                    GM_ST(uint8_t, t915, t916);
                }
                goto me868;
mn910:;
ma872:;
                uint8_t t925 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t925 != ((uint8_t)25ULL)) goto mn924;
                {
                    uint64_t t927 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t928 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t927 >= (uintptr_t)(t928)) gm_trap_index(t927, t928, gm_srcl8pn2pjtmj6k, 330, 31);
                    uint8_t *t929 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t927 * 1);
                    uint8_t t930;
                    uint64_t t932 = gm_l55;
                    unsigned __int128 n933 = (unsigned __int128)((uint64_t)63ULL);
                    if (n933 >= 64) gm_trap_shift((uint64_t)n933, "u64", gm_srcl8pn2pjtmj6k, 330, 44);
                    uint64_t t934 = (uint64_t)(t932 >> (int)n933);
                    uint8_t t935 = ((t934 == ((uint64_t)1ULL)));
                    if (!t935) goto ie931;
                    uint8_t t937 = ((uint8_t)1ULL);
                    t930 = t937;
                    goto ix931;
ie931:;
                    uint8_t t939 = ((uint8_t)0ULL);
                    t930 = t939;
ix931:;
                    GM_ST(uint8_t, t929, t930);
                }
                goto me868;
mn924:;
ma873:;
                void * t941 = gm_l0;
                gm_t88rcucg1b7uk t942 = gm_l4;
                double t943 = gm_l54;
                double t944 = (double)(__builtin_sqrt(t943));
                gm_b16_8 t945 = gm_l7;
                gm_f_put_float_fiad72d(t941, GM_CONV(gm_csguankko4lmhd, t942), t944, t945);
                goto me868;
mn940:;
ma874:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 326, 13);
me868:;
            }
            goto me3;
mn821:;
ma16:;
            uint8_t t947 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t947 != ((uint8_t)27ULL)) goto mn946;
            {
                void * t950 = gm_l0;
                gm_t88rcucg1b7uk t951 = gm_l4;
                gm_t53at80pfdus3 t952;
                gm_f_kind_htv6093((void *)&t952, t950, GM_CONV(gm_csguankko4lmhd, t951));
                gm_t53at80pfdus3 t953 = t952;
                uint8_t t954 = GM_LD(uint8_t, (((uint8_t *)&t953) + 0));
                if (t954 != ((uint8_t)3ULL)) goto ie949;
                uint16_t t955 = GM_LD(uint16_t, (((uint8_t *)&t953) + 8));
                gm_l57 = t955;
                {
                    void * t957 = gm_l0;
                    gm_t88rcucg1b7uk t958 = gm_l4;
                    uint64_t t959 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t960 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t959 >= (uintptr_t)(t960)) gm_trap_index(t959, t960, gm_srcl8pn2pjtmj6k, 336, 34);
                    gm_b16_8 t961 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t959 * 16));
                    gm_b16_8 t962 = t961;
                    gm_b16_8 t963 = GM_LD(gm_b16_8, ((uint8_t *)&t962));
                    double t964 = gm_f_get_float_grmhlm4(t957, GM_CONV(gm_csguankko4lmhd, t958), t963);
                    gm_l58 = t964;
                }
                {
                    void * t965 = gm_l0;
                    gm_t88rcucg1b7uk t966 = gm_l4;
                    uint64_t t967 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t968 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t967 >= (uintptr_t)(t968)) gm_trap_index(t967, t968, gm_srcl8pn2pjtmj6k, 337, 34);
                    gm_b16_8 t969 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t967 * 16));
                    gm_b16_8 t970 = t969;
                    gm_b16_8 t971 = GM_LD(gm_b16_8, ((uint8_t *)&t970));
                    double t972 = gm_f_get_float_grmhlm4(t965, GM_CONV(gm_csguankko4lmhd, t966), t971);
                    gm_l59 = t972;
                }
                {
                    void * t973 = gm_l0;
                    gm_t88rcucg1b7uk t974 = gm_l4;
                    uint64_t t975 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t976 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t975 >= (uintptr_t)(t976)) gm_trap_index(t975, t976, gm_srcl8pn2pjtmj6k, 338, 34);
                    gm_b16_8 t977 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t975 * 16));
                    gm_b16_8 t978 = t977;
                    gm_b16_8 t979 = GM_LD(gm_b16_8, ((uint8_t *)&t978));
                    double t980 = gm_f_get_float_grmhlm4(t973, GM_CONV(gm_csguankko4lmhd, t974), t979);
                    gm_l60 = t980;
                }
                {
                    uint16_t t982 = gm_l57;
                    uint8_t t983 = ((t982 == ((uint16_t)32ULL)));
                    if (!t983) goto ie981;
                    {
                        void * t985 = gm_l0;
                        gm_t88rcucg1b7uk t986 = gm_l4;
                        double t987 = gm_l58;
                        float t988 = (float)((double)t987);
                        double t989 = gm_l59;
                        float t990 = (float)((double)t989);
                        double t991 = gm_l60;
                        float t992 = (float)((double)t991);
                        float t993 = (float)(__builtin_fmaf(t988, t990, t992));
                        double t994 = (double)t993;
                        gm_b16_8 t995 = gm_l7;
                        gm_f_put_float_fiad72d(t985, GM_CONV(gm_csguankko4lmhd, t986), t994, t995);
                    }
                    goto ix981;
ie981:;
                    {
                        void * t997 = gm_l0;
                        gm_t88rcucg1b7uk t998 = gm_l4;
                        double t999 = gm_l58;
                        double t1000 = gm_l59;
                        double t1001 = gm_l60;
                        double t1002 = (double)(__builtin_fma(t999, t1000, t1001));
                        gm_b16_8 t1003 = gm_l7;
                        gm_f_put_float_fiad72d(t997, GM_CONV(gm_csguankko4lmhd, t998), t1002, t1003);
                    }
ix981:;
                }
                goto ix949;
ie949:;
                {
                    void * t1005 = gm_l0;
                    gm_tl874t2atnlh3 t1006 = gm_l1;
                    gm_b16_8 t1007;
                    GM_ST(void *, (uint8_t *)&t1007, (void *)(gm_sh01rb1u2jpfs));
                    GM_ST(uintptr_t, (uint8_t *)&t1007 + 8, (uintptr_t)(18));
                    gm_t76123gfet73g t1008;
                    gm_f_trap_qh84l9d((void *)&t1008, (void *)NULL, cx, NULL, t1005, t1006, t1007);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1009;
                    GM_ST(uint8_t, (uint8_t *)&t1009 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1009 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1009 + 4, 0);
                    ((uint8_t *)&t1009)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1009 + 8), t1008);
                    gm_tcfa6l7reoosn t1010 = t1009;
                    uint8_t t1011 = ((((uint8_t *)&t1010))[0] != 0);
                    F.ret = t1009;
                    goto gm_fret;
                }
ix949:;
            }
            goto me3;
mn946:;
ma17:;
            uint8_t t1015 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1015 != ((uint8_t)32ULL)) goto pa1014;
            goto po1013;
pa1014:;
            uint8_t t1017 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1017 != ((uint8_t)33ULL)) goto pa1016;
            goto po1013;
pa1016:;
            goto mn1012;
po1013:;
            {
                void * t1019 = gm_l0;
                gm_t88rcucg1b7uk t1020 = gm_l4;
                gm_tg5lql6itufk7 t1021 = gm_f_int_info_ulcevdv(t1019, GM_CONV(gm_csguankko4lmhd, t1020));
                gm_tg5lql6itufk7 t1022 = t1021;
                gm_ttovhfee8mf28 t1023;
                if (((((uint8_t *)&t1022))[8] != 0)) {
                    gm_ttovhfee8mf28 t1024 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1022) + 0));
                    t1023 = t1024;
                } else {
                    void * t1025 = gm_l0;
                    gm_tl874t2atnlh3 t1026 = gm_l1;
                    gm_b16_8 t1027;
                    GM_ST(void *, (uint8_t *)&t1027, (void *)(gm_s26c80h2pm6a7));
                    GM_ST(uintptr_t, (uint8_t *)&t1027 + 8, (uintptr_t)(25));
                    gm_t76123gfet73g t1028;
                    gm_f_trap_qh84l9d((void *)&t1028, (void *)NULL, cx, NULL, t1025, t1026, t1027);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1029;
                    GM_ST(uint8_t, (uint8_t *)&t1029 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1029 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1029 + 4, 0);
                    ((uint8_t *)&t1029)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1029 + 8), t1028);
                    gm_tcfa6l7reoosn t1030 = t1029;
                    uint8_t t1031 = ((((uint8_t *)&t1030))[0] != 0);
                    F.ret = t1029;
                    goto gm_fret;
                }
                gm_l61 = t1023;
            }
            {
                void * t1032 = gm_l0;
                gm_t88rcucg1b7uk t1033 = gm_l5;
                gm_tg5lql6itufk7 t1034 = gm_f_int_info_ulcevdv(t1032, GM_CONV(gm_csguankko4lmhd, t1033));
                gm_tg5lql6itufk7 t1035 = t1034;
                gm_ttovhfee8mf28 t1036;
                if (((((uint8_t *)&t1035))[8] != 0)) {
                    gm_ttovhfee8mf28 t1037 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1035) + 0));
                    t1036 = t1037;
                } else {
                    void * t1038 = gm_l0;
                    gm_tl874t2atnlh3 t1039 = gm_l1;
                    gm_b16_8 t1040;
                    GM_ST(void *, (uint8_t *)&t1040, (void *)(gm_sti4tkgslrkgo));
                    GM_ST(uintptr_t, (uint8_t *)&t1040 + 8, (uintptr_t)(25));
                    gm_t76123gfet73g t1041;
                    gm_f_trap_qh84l9d((void *)&t1041, (void *)NULL, cx, NULL, t1038, t1039, t1040);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1042;
                    GM_ST(uint8_t, (uint8_t *)&t1042 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1042 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1042 + 4, 0);
                    ((uint8_t *)&t1042)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1042 + 8), t1041);
                    gm_tcfa6l7reoosn t1043 = t1042;
                    uint8_t t1044 = ((((uint8_t *)&t1043))[0] != 0);
                    F.ret = t1042;
                    goto gm_fret;
                }
                gm_l62 = t1036;
            }
            {
                uint32_t t1045 = GM_LD(uint32_t, (((uint8_t *)&gm_l61) + 4));
                gm_l63 = t1045;
            }
            {
                void * t1046 = gm_l0;
                uint64_t t1047 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1048 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1047 >= (uintptr_t)(t1048)) gm_trap_index(t1047, t1048, gm_srcl8pn2pjtmj6k, 349, 29);
                gm_b16_8 t1049 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1047 * 16));
                gm_b16_8 t1050 = t1049;
                uint64_t t1051 = GM_LD(uintptr_t, ((uint8_t *)&t1050) + 8);
                void * t1052 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1050)));
                uint64_t t1053 = (uintptr_t)((uint64_t)0ULL);
                void * t1054 = gm_l0;
                gm_t88rcucg1b7uk t1055 = gm_l4;
                uint64_t t1056 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t1054, t1055);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t1057 = gm_f_us_cuv01fp(t1056);
                uint64_t t1058 = (uintptr_t)t1057;
                if (t1053 > t1058 || t1058 > (uintptr_t)t1051) gm_trap_slice(t1053, t1058, t1051, gm_srcl8pn2pjtmj6k, 349, 29);
                gm_b16_8 t1059;
                GM_ST(void *, (uint8_t *)&t1059, (void *)(((uint8_t *)t1052 + t1053 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t1059 + 8, (uintptr_t)(t1058 - t1053));
                gm_b16_8 t1060 = t1059;
                gm_b16_8 t1061 = GM_LD(gm_b16_8, ((uint8_t *)&t1060));
                unsigned __int128 t1062 = gm_f_get_uint_bq8c4b7(t1046, t1061);
                uint32_t t1063 = gm_l63;
                unsigned __int128 t1064 = gm_f_num_mask_do3naah(t1063);
                unsigned __int128 t1065 = (unsigned __int128)(t1062 & t1064);
                gm_l64 = t1065;
            }
            {
                void * t1066 = gm_l0;
                gm_t88rcucg1b7uk t1067 = gm_l5;
                uint64_t t1068 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1069 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1068 >= (uintptr_t)(t1069)) gm_trap_index(t1068, t1069, gm_srcl8pn2pjtmj6k, 350, 35);
                gm_b16_8 t1070 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1068 * 16));
                gm_b16_8 t1071 = t1070;
                gm_b16_8 t1072 = GM_LD(gm_b16_8, ((uint8_t *)&t1071));
                unsigned __int128 t1073 = gm_f_get_int_pg7p4gc(t1066, GM_CONV(gm_csguankko4lmhd, t1067), t1072);
                gm_l65 = t1073;
            }
            {
                gm_l66 = ((uint32_t)0ULL);
            }
            {
                uint8_t t1075 = 0;
                uint8_t t1076 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                if (t1076) {
                    unsigned __int128 t1077 = gm_l65;
                    unsigned __int128 n1078 = (unsigned __int128)((uint64_t)127ULL);
                    if (n1078 >= 128) gm_trap_shift((uint64_t)n1078, "u128", gm_srcl8pn2pjtmj6k, 352, 39);
                    unsigned __int128 t1079 = (unsigned __int128)(t1077 >> (int)n1078);
                    uint8_t t1080 = ((t1079 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                    t1075 = t1080;
                } else t1075 = 0;
                uint8_t t1081 = (uint8_t)!t1075;
                if (!t1081) goto ie1074;
                {
                    unsigned __int128 t1083 = gm_l65;
                    uint32_t t1084 = gm_l63;
                    unsigned __int128 t1085 = (unsigned __int128)t1084;
                    if (t1085 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 352, 65);
                    unsigned __int128 t1086 = (unsigned __int128)(t1083 % t1085);
                    uint32_t t1087 = (uint32_t)t1086;
                    gm_l66 = t1087;
                }
                goto ix1074;
ie1074:;
ix1074:;
            }
            {
                uint8_t t1089 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)33ULL)));
                if (!t1089) goto ie1088;
                {
                    uint32_t t1091 = gm_l63;
                    uint32_t t1092 = gm_l66;
                    uint32_t r1093; bool o1094 = __builtin_sub_overflow(t1091, t1092, &r1093);
                    if (o1094) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 353, 35);
                    uint32_t t1095 = r1093;
                    uint32_t t1096 = gm_l63;
                    if (t1096 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 353, 34);
                    uint32_t t1097 = (uint32_t)(t1095 % t1096);
                    gm_l66 = t1097;
                }
                goto ix1088;
ie1088:;
ix1088:;
            }
            {
                unsigned __int128 t1098;
                uint32_t t1100 = gm_l66;
                uint8_t t1101 = ((t1100 == ((uint32_t)0ULL)));
                if (!t1101) goto ie1099;
                unsigned __int128 t1103 = gm_l64;
                unsigned __int128 t1104 = t1103;
                t1098 = t1104;
                goto ix1099;
ie1099:;
                unsigned __int128 t1106 = gm_l64;
                uint32_t t1107 = gm_l66;
                unsigned __int128 t1108 = (unsigned __int128)t1107;
                unsigned __int128 n1109 = (unsigned __int128)t1108;
                if (n1109 >= 128) gm_trap_shift((uint64_t)n1109, "u128", gm_srcl8pn2pjtmj6k, 354, 43);
                unsigned __int128 r1110 = (unsigned __int128)((unsigned __int128)t1106 << (int)n1109);
                bool o1111 = (unsigned __int128)(r1110 >> (int)n1109) != t1106;
                unsigned __int128 t1112 = r1110;
                unsigned __int128 t1113 = gm_l64;
                uint32_t t1114 = gm_l63;
                uint32_t t1115 = gm_l66;
                uint32_t r1116; bool o1117 = __builtin_sub_overflow(t1114, t1115, &r1116);
                if (o1117) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 354, 71);
                uint32_t t1118 = r1116;
                unsigned __int128 t1119 = (unsigned __int128)t1118;
                unsigned __int128 n1120 = (unsigned __int128)t1119;
                if (n1120 >= 128) gm_trap_shift((uint64_t)n1120, "u128", gm_srcl8pn2pjtmj6k, 354, 61);
                unsigned __int128 t1121 = (unsigned __int128)(t1113 >> (int)n1120);
                unsigned __int128 t1122 = (unsigned __int128)(t1112 | t1121);
                uint32_t t1123 = gm_l63;
                unsigned __int128 t1124 = gm_f_num_mask_do3naah(t1123);
                unsigned __int128 t1125 = (unsigned __int128)(t1122 & t1124);
                unsigned __int128 t1126 = t1125;
                t1098 = t1126;
ix1099:;
                gm_l67 = t1098;
            }
            {
                void * t1127 = gm_l0;
                gm_b16_8 t1128 = gm_l7;
                uint8_t t1129 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                uint32_t t1130 = gm_l63;
                unsigned __int128 t1131 = gm_l67;
                unsigned __int128 t1132 = gm_f_num_ext_348kmbu(t1129, t1130, t1131);
                gm_f_put_uint_cddin99(t1127, t1128, t1132);
            }
            goto me3;
mn1012:;
ma18:;
            uint8_t t1136 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1136 != ((uint8_t)28ULL)) goto pa1135;
            goto po1134;
pa1135:;
            uint8_t t1138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1138 != ((uint8_t)29ULL)) goto pa1137;
            goto po1134;
pa1137:;
            uint8_t t1140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1140 != ((uint8_t)30ULL)) goto pa1139;
            goto po1134;
pa1139:;
            uint8_t t1142 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1142 != ((uint8_t)31ULL)) goto pa1141;
            goto po1134;
pa1141:;
            uint8_t t1144 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1144 != ((uint8_t)34ULL)) goto pa1143;
            goto po1134;
pa1143:;
            goto mn1133;
po1134:;
            {
                void * t1146 = gm_l0;
                gm_t88rcucg1b7uk t1147 = gm_l4;
                gm_tg5lql6itufk7 t1148 = gm_f_int_info_ulcevdv(t1146, GM_CONV(gm_csguankko4lmhd, t1147));
                gm_tg5lql6itufk7 t1149 = t1148;
                gm_ttovhfee8mf28 t1150;
                if (((((uint8_t *)&t1149))[8] != 0)) {
                    gm_ttovhfee8mf28 t1151 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1149) + 0));
                    t1150 = t1151;
                } else {
                    void * t1152 = gm_l0;
                    gm_tl874t2atnlh3 t1153 = gm_l1;
                    gm_b16_8 t1154;
                    GM_ST(void *, (uint8_t *)&t1154, (void *)(gm_spqf88ftq0gqk));
                    GM_ST(uintptr_t, (uint8_t *)&t1154 + 8, (uintptr_t)(28));
                    gm_t76123gfet73g t1155;
                    gm_f_trap_qh84l9d((void *)&t1155, (void *)NULL, cx, NULL, t1152, t1153, t1154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1156;
                    GM_ST(uint8_t, (uint8_t *)&t1156 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1156 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1156 + 4, 0);
                    ((uint8_t *)&t1156)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1156 + 8), t1155);
                    gm_tcfa6l7reoosn t1157 = t1156;
                    uint8_t t1158 = ((((uint8_t *)&t1157))[0] != 0);
                    F.ret = t1156;
                    goto gm_fret;
                }
                gm_l68 = t1150;
            }
            {
                uint32_t t1159 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 4));
                gm_l69 = t1159;
            }
            {
                void * t1160 = gm_l0;
                uint64_t t1161 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1161 >= (uintptr_t)(t1162)) gm_trap_index(t1161, t1162, gm_srcl8pn2pjtmj6k, 360, 29);
                gm_b16_8 t1163 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1161 * 16));
                gm_b16_8 t1164 = t1163;
                uint64_t t1165 = GM_LD(uintptr_t, ((uint8_t *)&t1164) + 8);
                void * t1166 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1164)));
                uint64_t t1167 = (uintptr_t)((uint64_t)0ULL);
                void * t1168 = gm_l0;
                gm_t88rcucg1b7uk t1169 = gm_l4;
                uint64_t t1170 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t1168, t1169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t1171 = gm_f_us_cuv01fp(t1170);
                uint64_t t1172 = (uintptr_t)t1171;
                if (t1167 > t1172 || t1172 > (uintptr_t)t1165) gm_trap_slice(t1167, t1172, t1165, gm_srcl8pn2pjtmj6k, 360, 29);
                gm_b16_8 t1173;
                GM_ST(void *, (uint8_t *)&t1173, (void *)(((uint8_t *)t1166 + t1167 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t1173 + 8, (uintptr_t)(t1172 - t1167));
                gm_b16_8 t1174 = t1173;
                gm_b16_8 t1175 = GM_LD(gm_b16_8, ((uint8_t *)&t1174));
                unsigned __int128 t1176 = gm_f_get_uint_bq8c4b7(t1160, t1175);
                uint32_t t1177 = gm_l69;
                unsigned __int128 t1178 = gm_f_num_mask_do3naah(t1177);
                unsigned __int128 t1179 = (unsigned __int128)(t1176 & t1178);
                gm_l70 = t1179;
            }
            {
                gm_l71 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t1187 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t1187) { case ((uint8_t)0ULL): goto ma1185; case ((uint8_t)1ULL): goto ma1185; case ((uint8_t)2ULL): goto ma1185; case ((uint8_t)3ULL): goto ma1185; case ((uint8_t)4ULL): goto ma1185; case ((uint8_t)5ULL): goto ma1185; case ((uint8_t)6ULL): goto ma1185; case ((uint8_t)7ULL): goto ma1185; case ((uint8_t)8ULL): goto ma1185; case ((uint8_t)9ULL): goto ma1185; case ((uint8_t)10ULL): goto ma1185; case ((uint8_t)11ULL): goto ma1185; case ((uint8_t)12ULL): goto ma1185; case ((uint8_t)13ULL): goto ma1185; case ((uint8_t)14ULL): goto ma1185; case ((uint8_t)15ULL): goto ma1185; case ((uint8_t)16ULL): goto ma1185; case ((uint8_t)17ULL): goto ma1185; case ((uint8_t)18ULL): goto ma1185; case ((uint8_t)19ULL): goto ma1185; case ((uint8_t)20ULL): goto ma1185; case ((uint8_t)21ULL): goto ma1185; case ((uint8_t)22ULL): goto ma1185; case ((uint8_t)23ULL): goto ma1185; case ((uint8_t)24ULL): goto ma1185; case ((uint8_t)25ULL): goto ma1185; case ((uint8_t)26ULL): goto ma1185; case ((uint8_t)27ULL): goto ma1185; case ((uint8_t)28ULL): goto ma1181; case ((uint8_t)29ULL): goto ma1182; case ((uint8_t)30ULL): goto ma1183; case ((uint8_t)31ULL): goto ma1184; case ((uint8_t)32ULL): goto ma1185; case ((uint8_t)33ULL): goto ma1185; case ((uint8_t)34ULL): goto ma1185; case ((uint8_t)35ULL): goto ma1185; case ((uint8_t)36ULL): goto ma1185; case ((uint8_t)37ULL): goto ma1185; case ((uint8_t)38ULL): goto ma1185; case ((uint8_t)39ULL): goto ma1185; case ((uint8_t)40ULL): goto ma1185; case ((uint8_t)41ULL): goto ma1185; case ((uint8_t)42ULL): goto ma1185; case ((uint8_t)43ULL): goto ma1185; case ((uint8_t)44ULL): goto ma1185; case ((uint8_t)45ULL): goto ma1185; case ((uint8_t)46ULL): goto ma1185; case ((uint8_t)47ULL): goto ma1185; case ((uint8_t)48ULL): goto ma1185; case ((uint8_t)49ULL): goto ma1185; case ((uint8_t)50ULL): goto ma1185; case ((uint8_t)51ULL): goto ma1185; case ((uint8_t)52ULL): goto ma1185; case ((uint8_t)53ULL): goto ma1185; case ((uint8_t)54ULL): goto ma1185; case ((uint8_t)55ULL): goto ma1185; case ((uint8_t)56ULL): goto ma1185; case ((uint8_t)57ULL): goto ma1185; case ((uint8_t)58ULL): goto ma1185; case ((uint8_t)59ULL): goto ma1185; case ((uint8_t)60ULL): goto ma1185; case ((uint8_t)61ULL): goto ma1185; case ((uint8_t)62ULL): goto ma1185; case ((uint8_t)63ULL): goto ma1185; case ((uint8_t)64ULL): goto ma1185; case ((uint8_t)65ULL): goto ma1185; case ((uint8_t)66ULL): goto ma1185; case ((uint8_t)67ULL): goto ma1185; case ((uint8_t)68ULL): goto ma1185; case ((uint8_t)69ULL): goto ma1185; case ((uint8_t)70ULL): goto ma1185; case ((uint8_t)71ULL): goto ma1185; case ((uint8_t)72ULL): goto ma1185; case ((uint8_t)73ULL): goto ma1185; case ((uint8_t)74ULL): goto ma1185; case ((uint8_t)75ULL): goto ma1185; case ((uint8_t)76ULL): goto ma1185; case ((uint8_t)77ULL): goto ma1185; case ((uint8_t)78ULL): goto ma1185; case ((uint8_t)79ULL): goto ma1185; case ((uint8_t)80ULL): goto ma1185; case ((uint8_t)81ULL): goto ma1185; default: goto ma1185; }
ma1181:;
                uint8_t t1189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1189 != ((uint8_t)28ULL)) goto mn1188;
                {
                    void * t1191 = gm_l0;
                    gm_b16_8 t1192 = gm_l7;
                    unsigned __int128 t1193 = gm_l70;
                    uint32_t t1194 = (uint32_t)gm_clz((unsigned __int128)(unsigned __int128)(t1193), 128);
                    uint32_t t1195 = gm_l69;
                    uint32_t r1196; bool o1197 = __builtin_sub_overflow(((uint32_t)128ULL), t1195, &r1196);
                    if (o1197) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 53);
                    uint32_t t1198 = r1196;
                    uint32_t r1199; bool o1200 = __builtin_sub_overflow(t1194, t1198, &r1199);
                    if (o1200) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 42);
                    uint32_t t1201 = r1199;
                    unsigned __int128 t1202 = (unsigned __int128)t1201;
                    gm_f_put_uint_cddin99(t1191, t1192, t1202);
                }
                {
                    gm_b0_1 t1203;
                    gm_tcfa6l7reoosn t1204;
                    GM_ST(uint8_t, (uint8_t *)&t1204 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1204 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1204 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1204 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1204 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1204 + 24, 0);
                    ((uint8_t *)&t1204)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1204 + 8), t1203);
                    gm_tcfa6l7reoosn t1205 = t1204;
                    uint8_t t1206 = ((((uint8_t *)&t1205))[0] != 0);
                    F.ret = t1204;
                    goto gm_fret;
                }
                goto me1180;
mn1188:;
ma1182:;
                uint8_t t1208 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1208 != ((uint8_t)29ULL)) goto mn1207;
                {
                    void * t1210 = gm_l0;
                    gm_b16_8 t1211 = gm_l7;
                    unsigned __int128 t1212;
                    unsigned __int128 t1214 = gm_l70;
                    uint8_t t1215 = ((t1214 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                    if (!t1215) goto ie1213;
                    uint32_t t1217 = gm_l69;
                    unsigned __int128 t1218 = (unsigned __int128)t1217;
                    unsigned __int128 t1219 = t1218;
                    t1212 = t1219;
                    goto ix1213;
ie1213:;
                    unsigned __int128 t1221 = gm_l70;
                    uint32_t t1222 = (uint32_t)gm_ctz((unsigned __int128)(unsigned __int128)(t1221), 128);
                    unsigned __int128 t1223 = (unsigned __int128)t1222;
                    unsigned __int128 t1224 = t1223;
                    t1212 = t1224;
ix1213:;
                    gm_f_put_uint_cddin99(t1210, t1211, t1212);
                }
                {
                    gm_b0_1 t1225;
                    gm_tcfa6l7reoosn t1226;
                    GM_ST(uint8_t, (uint8_t *)&t1226 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1226 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1226 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1226 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1226 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1226 + 24, 0);
                    ((uint8_t *)&t1226)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1226 + 8), t1225);
                    gm_tcfa6l7reoosn t1227 = t1226;
                    uint8_t t1228 = ((((uint8_t *)&t1227))[0] != 0);
                    F.ret = t1226;
                    goto gm_fret;
                }
                goto me1180;
mn1207:;
ma1183:;
                uint8_t t1230 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1230 != ((uint8_t)30ULL)) goto mn1229;
                {
                    void * t1232 = gm_l0;
                    gm_b16_8 t1233 = gm_l7;
                    unsigned __int128 t1234 = gm_l70;
                    uint32_t t1235 = (uint32_t)gm_popcount((unsigned __int128)(unsigned __int128)(t1234));
                    unsigned __int128 t1236 = (unsigned __int128)t1235;
                    gm_f_put_uint_cddin99(t1232, t1233, t1236);
                }
                {
                    gm_b0_1 t1237;
                    gm_tcfa6l7reoosn t1238;
                    GM_ST(uint8_t, (uint8_t *)&t1238 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1238 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1238 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1238 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1238 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1238 + 24, 0);
                    ((uint8_t *)&t1238)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1238 + 8), t1237);
                    gm_tcfa6l7reoosn t1239 = t1238;
                    uint8_t t1240 = ((((uint8_t *)&t1239))[0] != 0);
                    F.ret = t1238;
                    goto gm_fret;
                }
                goto me1180;
mn1229:;
ma1184:;
                uint8_t t1242 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1242 != ((uint8_t)31ULL)) goto mn1241;
                {
                    uint32_t t1245 = gm_l69;
                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 376, 24);
                    uint32_t t1246 = (uint32_t)(t1245 % ((uint32_t)8ULL));
                    uint8_t t1247 = (!(t1246 == ((uint32_t)0ULL)));
                    if (!t1247) goto ie1244;
                    {
                        void * t1249 = gm_l0;
                        gm_tl874t2atnlh3 t1250 = gm_l1;
                        uint32_t t1251 = gm_l69;
                        gm_t88rcucg1b7uk t1252;
                        GM_ST(uint32_t, ((uint8_t *)&t1252 + 0), t1251);
                        gm_tabb710d9mq2e t1253;
                        gm_f_format_ven7kt5((void *)&t1253, (void *)NULL, cx, NULL, t1252);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t1254 = t1253;
                        if (((((uint8_t *)&t1254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1254) + 8, gm_srcl8pn2pjtmj6k, 376, 62);
                        gm_b16_8 t1255 = GM_LD(gm_b16_8, (((uint8_t *)&t1254) + 8));
                        gm_b16_8 t1256 = t1255;
                        gm_b16_8 t1257 = GM_LD(gm_b16_8, ((uint8_t *)&t1256));
                        gm_t76123gfet73g t1258;
                        gm_f_trap_qh84l9d((void *)&t1258, (void *)NULL, cx, NULL, t1249, t1250, t1257);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t1259;
                        GM_ST(uint8_t, (uint8_t *)&t1259 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1259 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1259 + 4, 0);
                        ((uint8_t *)&t1259)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1259 + 8), t1258);
                        gm_tcfa6l7reoosn t1260 = t1259;
                        uint8_t t1261 = ((((uint8_t *)&t1260))[0] != 0);
                        F.ret = t1259;
                        goto gm_fret;
                    }
                    goto ix1244;
ie1244:;
ix1244:;
                }
                {
                    {
                        gm_l73 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1263 = gm_l69;
                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 33);
                        uint32_t t1264 = (uint32_t)(t1263 / ((uint32_t)8ULL));
                        gm_l72 = t1264;
                    }
                    {
                        for (;;) {
                            uint32_t t1266 = gm_l73;
                            uint32_t t1267 = gm_l72;
                            uint8_t t1268 = (t1266 < t1267);
                            if (!t1268) goto lb1265;
                            {
                                uint32_t t1271 = gm_l73;
                                uint32_t t1272 = t1271;
                                uint32_t t1275 = GM_LD(uint32_t, ((uint8_t *)&t1272));
                                gm_l74 = t1275;
                                {
                                    unsigned __int128 t1277 = gm_l71;
                                    unsigned __int128 t1278 = gm_l70;
                                    uint32_t t1279 = gm_l74;
                                    uint32_t r1280; bool o1281 = __builtin_mul_overflow(t1279, ((uint32_t)8ULL), &r1280);
                                    if (o1281) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 58);
                                    uint32_t t1282 = r1280;
                                    unsigned __int128 t1283 = (unsigned __int128)t1282;
                                    unsigned __int128 n1284 = (unsigned __int128)t1283;
                                    if (n1284 >= 128) gm_trap_shift((uint64_t)n1284, "u128", gm_srcl8pn2pjtmj6k, 377, 48);
                                    unsigned __int128 t1285 = (unsigned __int128)(t1278 >> (int)n1284);
                                    unsigned __int128 t1286 = (unsigned __int128)(t1285 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 255u)));
                                    uint32_t t1287 = gm_l69;
                                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1288 = (uint32_t)(t1287 / ((uint32_t)8ULL));
                                    uint32_t r1289; bool o1290 = __builtin_sub_overflow(t1288, ((uint32_t)1ULL), &r1289);
                                    if (o1290) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1291 = r1289;
                                    uint32_t t1292 = gm_l74;
                                    uint32_t r1293; bool o1294 = __builtin_sub_overflow(t1291, t1292, &r1293);
                                    if (o1294) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1295 = r1293;
                                    uint32_t r1296; bool o1297 = __builtin_mul_overflow(t1295, ((uint32_t)8ULL), &r1296);
                                    if (o1297) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 82);
                                    uint32_t t1298 = r1296;
                                    unsigned __int128 t1299 = (unsigned __int128)t1298;
                                    unsigned __int128 n1300 = (unsigned __int128)t1299;
                                    if (n1300 >= 128) gm_trap_shift((uint64_t)n1300, "u128", gm_srcl8pn2pjtmj6k, 377, 46);
                                    unsigned __int128 r1301 = (unsigned __int128)((unsigned __int128)t1286 << (int)n1300);
                                    bool o1302 = (unsigned __int128)(r1301 >> (int)n1300) != t1286;
                                    if (o1302) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 377, 46);
                                    unsigned __int128 t1303 = r1301;
                                    unsigned __int128 t1304 = (unsigned __int128)(t1277 | t1303);
                                    gm_l71 = t1304;
                                }
                                goto me1273;
mn1274:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 377, 21);
me1273:;
                            }
lc1265:;
                            {
                                uint32_t t1306 = gm_l73;
                                uint32_t r1307; bool o1308 = __builtin_add_overflow(t1306, ((uint32_t)1ULL), &r1307);
                                uint32_t t1309 = r1307;
                                gm_l73 = t1309;
                            }
                        }
lb1265:;
                    }
                }
                goto me1180;
mn1241:;
ma1185:;
                {
                    {
                        gm_l76 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1313 = gm_l69;
                        gm_l75 = t1313;
                    }
                    {
                        for (;;) {
                            uint32_t t1315 = gm_l76;
                            uint32_t t1316 = gm_l75;
                            uint8_t t1317 = (t1315 < t1316);
                            if (!t1317) goto lb1314;
                            {
                                uint32_t t1320 = gm_l76;
                                uint32_t t1321 = t1320;
                                uint32_t t1324 = GM_LD(uint32_t, ((uint8_t *)&t1321));
                                gm_l77 = t1324;
                                {
                                    unsigned __int128 t1327 = gm_l70;
                                    uint32_t t1328 = gm_l77;
                                    unsigned __int128 t1329 = (unsigned __int128)t1328;
                                    unsigned __int128 n1330 = (unsigned __int128)t1329;
                                    if (n1330 >= 128) gm_trap_shift((uint64_t)n1330, "u128", gm_srcl8pn2pjtmj6k, 381, 29);
                                    unsigned __int128 t1331 = (unsigned __int128)(t1327 >> (int)n1330);
                                    unsigned __int128 t1332 = (unsigned __int128)(t1331 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
                                    uint8_t t1333 = ((t1332 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                                    if (!t1333) goto ie1326;
                                    {
                                        unsigned __int128 t1335 = gm_l71;
                                        uint32_t t1336 = gm_l69;
                                        uint32_t r1337; bool o1338 = __builtin_sub_overflow(t1336, ((uint32_t)1ULL), &r1337);
                                        if (o1338) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                        uint32_t t1339 = r1337;
                                        uint32_t t1340 = gm_l77;
                                        uint32_t r1341; bool o1342 = __builtin_sub_overflow(t1339, t1340, &r1341);
                                        if (o1342) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                        uint32_t t1343 = r1341;
                                        unsigned __int128 t1344 = (unsigned __int128)t1343;
                                        unsigned __int128 n1345 = (unsigned __int128)t1344;
                                        if (n1345 >= 128) gm_trap_shift((uint64_t)n1345, "u128", gm_srcl8pn2pjtmj6k, 381, 59);
                                        unsigned __int128 r1346 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1345);
                                        bool o1347 = (unsigned __int128)(r1346 >> (int)n1345) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                        if (o1347) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 381, 59);
                                        unsigned __int128 t1348 = r1346;
                                        unsigned __int128 t1349 = (unsigned __int128)(t1335 | t1348);
                                        gm_l71 = t1349;
                                    }
                                    goto ix1326;
ie1326:;
ix1326:;
                                }
                                goto me1322;
mn1323:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 380, 21);
me1322:;
                            }
lc1314:;
                            {
                                uint32_t t1351 = gm_l76;
                                uint32_t r1352; bool o1353 = __builtin_add_overflow(t1351, ((uint32_t)1ULL), &r1352);
                                uint32_t t1354 = r1352;
                                gm_l76 = t1354;
                            }
                        }
lb1314:;
                    }
                }
                goto me1180;
mn1310:;
ma1186:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 362, 13);
me1180:;
            }
            {
                void * t1355 = gm_l0;
                gm_b16_8 t1356 = gm_l7;
                uint8_t t1357 = GM_LD(uint8_t, (((uint8_t *)&gm_l68) + 0));
                uint32_t t1358 = gm_l69;
                unsigned __int128 t1359 = gm_l71;
                unsigned __int128 t1360 = gm_f_num_ext_348kmbu(t1357, t1358, t1359);
                gm_f_put_uint_cddin99(t1355, t1356, t1360);
            }
            goto me3;
mn1133:;
ma19:;
            uint8_t t1362 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1362 != ((uint8_t)62ULL)) goto mn1361;
            {
                void * t1364 = gm_l0;
                gm_t88rcucg1b7uk t1365 = gm_l5;
                gm_tg5lql6itufk7 t1366 = gm_f_int_info_ulcevdv(t1364, GM_CONV(gm_csguankko4lmhd, t1365));
                gm_tg5lql6itufk7 t1367 = t1366;
                gm_ttovhfee8mf28 t1368;
                if (((((uint8_t *)&t1367))[8] != 0)) {
                    gm_ttovhfee8mf28 t1369 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1367) + 0));
                    t1368 = t1369;
                } else {
                    void * t1370 = gm_l0;
                    gm_tl874t2atnlh3 t1371 = gm_l1;
                    gm_b16_8 t1372;
                    GM_ST(void *, (uint8_t *)&t1372, (void *)(gm_sartclcojin4f));
                    GM_ST(uintptr_t, (uint8_t *)&t1372 + 8, (uintptr_t)(51));
                    gm_t76123gfet73g t1373;
                    gm_f_trap_qh84l9d((void *)&t1373, (void *)NULL, cx, NULL, t1370, t1371, t1372);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1374;
                    GM_ST(uint8_t, (uint8_t *)&t1374 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1374 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1374 + 4, 0);
                    ((uint8_t *)&t1374)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1374 + 8), t1373);
                    gm_tcfa6l7reoosn t1375 = t1374;
                    uint8_t t1376 = ((((uint8_t *)&t1375))[0] != 0);
                    F.ret = t1374;
                    goto gm_fret;
                }
                gm_l78 = t1368;
            }
            {
                uint8_t t1377 = GM_LD(uint8_t, (((uint8_t *)&gm_l78) + 0));
                void * t1378 = gm_l0;
                gm_t88rcucg1b7uk t1379 = gm_l5;
                uint64_t t1380 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1381 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1380 >= (uintptr_t)(t1381)) gm_trap_index(t1380, t1381, gm_srcl8pn2pjtmj6k, 389, 56);
                gm_b16_8 t1382 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1380 * 16));
                gm_b16_8 t1383 = t1382;
                gm_b16_8 t1384 = GM_LD(gm_b16_8, ((uint8_t *)&t1383));
                unsigned __int128 t1385 = gm_f_get_int_pg7p4gc(t1378, GM_CONV(gm_csguankko4lmhd, t1379), t1384);
                gm_f_num_val_i3upeur((void *)&gm_l79, t1377, t1385);
            }
            {
                unsigned __int128 t1386 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l79) + 16));
                uint64_t t1387 = (uint64_t)t1386;
                uint64_t t1388 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t1387);
                gm_l80 = t1388;
            }
            {
                void * t1389 = gm_l0;
                gm_tl874t2atnlh3 t1390 = gm_l1;
                gm_t88rcucg1b7uk t1391 = gm_l4;
                uint64_t t1392 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1393 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1392 >= (uintptr_t)(t1393)) gm_trap_index(t1392, t1393, gm_srcl8pn2pjtmj6k, 391, 38);
                gm_b16_8 t1394 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1392 * 16));
                void * t1395 = (void *)((uint8_t *)&gm_l80);
                gm_tcfa6l7reoosn t1396;
                gm_f_hash_val_nr2e99r((void *)&t1396, (void *)NULL, cx, NULL, t1389, t1390, t1391, t1394, t1395);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1397 = t1396;
                if (((((uint8_t *)&t1397))[0] != 0)) {
                    gm_t76123gfet73g t1398 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1397) + 8));
                    gm_tcfa6l7reoosn t1399;
                    GM_ST(uint8_t, (uint8_t *)&t1399 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1399 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1399 + 4, 0);
                    ((uint8_t *)&t1399)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1399 + 8), t1398);
                    F.ret = t1399;
                    goto gm_fret;
                }
                gm_b0_1 t1400 = GM_LD(gm_b0_1, (((uint8_t *)&t1397) + 8));
            }
            {
                void * t1401 = gm_l0;
                gm_b16_8 t1402 = gm_l7;
                uint64_t t1403 = gm_l80;
                unsigned __int128 t1404 = (unsigned __int128)t1403;
                gm_f_put_uint_cddin99(t1401, t1402, t1404);
            }
            goto me3;
mn1361:;
ma20:;
            uint8_t t1408 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1408 != ((uint8_t)40ULL)) goto pa1407;
            goto po1406;
pa1407:;
            uint8_t t1410 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1410 != ((uint8_t)41ULL)) goto pa1409;
            goto po1406;
pa1409:;
            uint8_t t1412 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1412 != ((uint8_t)42ULL)) goto pa1411;
            goto po1406;
pa1411:;
            uint8_t t1414 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1414 != ((uint8_t)43ULL)) goto pa1413;
            goto po1406;
pa1413:;
            uint8_t t1416 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1416 != ((uint8_t)44ULL)) goto pa1415;
            goto po1406;
pa1415:;
            uint8_t t1418 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1418 != ((uint8_t)45ULL)) goto pa1417;
            goto po1406;
pa1417:;
            uint8_t t1420 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1420 != ((uint8_t)46ULL)) goto pa1419;
            goto po1406;
pa1419:;
            uint8_t t1422 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1422 != ((uint8_t)47ULL)) goto pa1421;
            goto po1406;
pa1421:;
            uint8_t t1424 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1424 != ((uint8_t)48ULL)) goto pa1423;
            goto po1406;
pa1423:;
            uint8_t t1426 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1426 != ((uint8_t)49ULL)) goto pa1425;
            goto po1406;
pa1425:;
            uint8_t t1428 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1428 != ((uint8_t)50ULL)) goto pa1427;
            goto po1406;
pa1427:;
            uint8_t t1430 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1430 != ((uint8_t)51ULL)) goto pa1429;
            goto po1406;
pa1429:;
            uint8_t t1432 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1432 != ((uint8_t)52ULL)) goto pa1431;
            goto po1406;
pa1431:;
            uint8_t t1434 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1434 != ((uint8_t)53ULL)) goto pa1433;
            goto po1406;
pa1433:;
            uint8_t t1436 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1436 != ((uint8_t)54ULL)) goto pa1435;
            goto po1406;
pa1435:;
            uint8_t t1438 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1438 != ((uint8_t)55ULL)) goto pa1437;
            goto po1406;
pa1437:;
            uint8_t t1440 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1440 != ((uint8_t)56ULL)) goto pa1439;
            goto po1406;
pa1439:;
            uint8_t t1442 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1442 != ((uint8_t)57ULL)) goto pa1441;
            goto po1406;
pa1441:;
            goto mn1405;
po1406:;
            void * t1443 = gm_l0;
            gm_tl874t2atnlh3 t1444 = gm_l1;
            gm_tvdg94bk7aa22 t1445 = gm_l2;
            gm_t88rcucg1b7uk t1446 = gm_l3;
            gm_t88rcucg1b7uk t1447 = gm_l4;
            gm_t88rcucg1b7uk t1448 = gm_l5;
            gm_b16_8 t1449 = gm_l6;
            gm_b16_8 t1450 = gm_l7;
            gm_f_vector_builtin_3fn933h((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t1443, t1444, t1445, t1446, t1447, t1448, t1449, t1450);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn1405:;
ma21:;
            void * t1452 = gm_l0;
            gm_tl874t2atnlh3 t1453 = gm_l1;
            gm_tvdg94bk7aa22 t1454 = gm_l2;
            gm_th9nl3oso747e t1455;
            GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1455 + 0), t1454);
            gm_tabb710d9mq2e t1456;
            gm_f_format_985clkf((void *)&t1456, (void *)NULL, cx, NULL, t1455);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t1457 = t1456;
            if (((((uint8_t *)&t1457))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1457) + 8, gm_srcl8pn2pjtmj6k, 395, 42);
            gm_b16_8 t1458 = GM_LD(gm_b16_8, (((uint8_t *)&t1457) + 8));
            gm_b16_8 t1459 = t1458;
            gm_b16_8 t1460 = GM_LD(gm_b16_8, ((uint8_t *)&t1459));
            gm_t76123gfet73g t1461;
            gm_f_unsupported_70vvk1e((void *)&t1461, (void *)NULL, cx, NULL, t1452, t1453, t1460);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1462;
            GM_ST(uint8_t, (uint8_t *)&t1462 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1462 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1462 + 4, 0);
            ((uint8_t *)&t1462)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1462 + 8), t1461);
            gm_tcfa6l7reoosn t1463 = t1462;
            uint8_t t1464 = ((((uint8_t *)&t1463))[0] != 0);
            F.ret = t1462;
            goto gm_fret;
            goto me3;
mn1451:;
ma22:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 191, 5);
me3:;
        }
    }
    gm_b0_1 t1465;
    gm_tcfa6l7reoosn t1466;
    GM_ST(uint8_t, (uint8_t *)&t1466 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t1466 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t1466 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t1466 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t1466 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t1466 + 24, 0);
    ((uint8_t *)&t1466)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t1466 + 8), t1465);
    gm_tcfa6l7reoosn t1467 = t1466;
    F.ret = t1467;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_array_ptr_to_slice_7dg3m1v F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ptr_ty */
    gm_b16_8 gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    uint64_t gm_l4; /* len */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint64_t gm_l6; /* n */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)8ULL)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l5 = t8;
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l5;
        gm_t53at80pfdus3 t11;
        gm_f_kind_htv6093((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        gm_t53at80pfdus3 t12 = t11;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t13 != ((uint8_t)11ULL)) goto ie2;
        uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)&t12) + 8) + 0));
        gm_l6 = t14;
        {
            uint64_t t16 = gm_l6;
            gm_l4 = t16;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        gm_b16_8 t18 = gm_l3;
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_l2;
        uint64_t t21 = gm_f_ptr_of_b77b4c9(t19, t20);
        uint64_t t22 = gm_l4;
        gm_f_put_slice_ko14loi(t17, t18, t21, t22);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_coerce_bytes_9kf3u24 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* from */
    gm_t88rcucg1b7uk gm_l4; /* to */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_ttovhfee8mf28 gm_l7; /* info */
    uint32_t gm_l8; /* variant */
    gm_t88rcucg1b7uk gm_l9; /* derr */
    gm_b16_8 gm_l10; /* lifted */
    gm_t88rcucg1b7uk gm_l11; /* src_in */
    gm_t88rcucg1b7uk gm_l12; /* dst_in */
    gm_b16_8 gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    gm_b16_8 gm_l15; /* sl */
    GM_STACK_CHECK(&F, 2681u);
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
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma12; default: goto ma13; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)0ULL)) goto mn15;
            {
                void * t19 = gm_l0;
                gm_t88rcucg1b7uk t20 = gm_l4;
                gm_tg5lql6itufk7 t21 = gm_f_int_info_ulcevdv(t19, GM_CONV(gm_csguankko4lmhd, t20));
                gm_tg5lql6itufk7 t22 = t21;
                if (!((((uint8_t *)&t22))[8] != 0)) goto ie18;
                gm_ttovhfee8mf28 t23 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t22));
                gm_l7 = t23;
                {
                    void * t25 = gm_l0;
                    gm_b16_8 t26 = gm_l6;
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                    void * t29 = gm_l0;
                    gm_t88rcucg1b7uk t30 = gm_l3;
                    gm_b16_8 t31 = gm_l5;
                    gm_b16_8 t32 = t31;
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                    unsigned __int128 t34 = gm_f_get_int_pg7p4gc(t29, GM_CONV(gm_csguankko4lmhd, t30), t33);
                    unsigned __int128 t35 = gm_f_num_ext_348kmbu(t27, t28, t34);
                    gm_f_put_uint_cddin99(t25, t26, t35);
                }
                goto ix18;
ie18:;
                {
                    void * t37 = gm_l0;
                    gm_b16_8 t38 = gm_l6;
                    void * t39 = gm_l0;
                    gm_t88rcucg1b7uk t40 = gm_l3;
                    gm_b16_8 t41 = gm_l5;
                    gm_b16_8 t42 = t41;
                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                    unsigned __int128 t44 = gm_f_get_int_pg7p4gc(t39, GM_CONV(gm_csguankko4lmhd, t40), t43);
                    gm_f_put_uint_cddin99(t37, t38, t44);
                }
ix18:;
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t46 != ((uint8_t)1ULL)) goto mn45;
            {
                uint8_t t49 = 0;
                void * t50 = gm_l0;
                gm_t88rcucg1b7uk t51 = gm_l3;
                gm_t53at80pfdus3 t52;
                gm_f_kind_htv6093((void *)&t52, t50, GM_CONV(gm_csguankko4lmhd, t51));
                gm_t53at80pfdus3 t53 = t52;
                uint8_t t54;
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
                if (t57 != ((uint8_t)3ULL)) goto mn56;
                t54 = ((uint8_t)1);
                goto me55;
mn56:;
                t54 = ((uint8_t)0);
                goto me55;
mn58:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 21);
me55:;
                uint8_t t59 = (uint8_t)!t54;
                if (!t59) {
                    void * t60 = gm_l0;
                    gm_t88rcucg1b7uk t61 = gm_l4;
                    gm_t53at80pfdus3 t62;
                    gm_f_kind_htv6093((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
                    gm_t53at80pfdus3 t63 = t62;
                    uint8_t t64;
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
                    if (t67 != ((uint8_t)3ULL)) goto mn66;
                    t64 = ((uint8_t)1);
                    goto me65;
mn66:;
                    t64 = ((uint8_t)0);
                    goto me65;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 56);
me65:;
                    uint8_t t69 = (uint8_t)!t64;
                    t49 = t69;
                } else t49 = 1;
                if (!t49) goto ie48;
                {
                    void * t71 = gm_l0;
                    gm_tl874t2atnlh3 t72 = gm_l1;
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sd4ibvv64si9n));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(44));
                    gm_t76123gfet73g t74;
                    gm_f_trap_qh84l9d((void *)&t74, (void *)NULL, cx, NULL, t71, t72, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t75;
                    GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                    ((uint8_t *)&t75)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t75 + 8), t74);
                    gm_tcfa6l7reoosn t76 = t75;
                    uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
                    F.ret = t75;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
ix48:;
            }
            {
                void * t78 = gm_l0;
                gm_t88rcucg1b7uk t79 = gm_l4;
                void * t80 = gm_l0;
                gm_t88rcucg1b7uk t81 = gm_l3;
                gm_b16_8 t82 = gm_l5;
                gm_b16_8 t83 = t82;
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                double t85 = gm_f_get_float_grmhlm4(t80, GM_CONV(gm_csguankko4lmhd, t81), t84);
                gm_b16_8 t86 = gm_l6;
                gm_f_put_float_fiad72d(t78, GM_CONV(gm_csguankko4lmhd, t79), t85, t86);
            }
            goto me3;
mn45:;
ma6:;
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t88 != ((uint8_t)2ULL)) goto mn87;
            void * t89 = gm_l0;
            gm_t88rcucg1b7uk t90 = gm_l4;
            gm_b16_8 t91 = gm_l6;
            gm_b16_8 t92 = gm_l5;
            gm_b16_8 t93 = t92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_b0_1 t95 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t89, t90, t91, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn87:;
ma7:;
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t97 != ((uint8_t)3ULL)) goto mn96;
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l4;
            gm_b16_8 t100 = gm_l6;
            gm_b16_8 t101 = gm_l5;
            gm_b16_8 t102 = t101;
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
            gm_b0_1 t104 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t98, t99, t100, ((uint8_t)0), t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn96:;
ma8:;
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t106 != ((uint8_t)4ULL)) goto mn105;
            uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l8 = t107;
            {
                void * t110 = gm_l0;
                gm_t88rcucg1b7uk t111 = gm_l3;
                gm_b16_8 t112 = gm_l5;
                gm_b16_8 t113 = t112;
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                uint8_t t115 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t110, t111, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t115) goto ie109;
                {
                    void * t117 = gm_l0;
                    gm_t88rcucg1b7uk t118 = gm_l4;
                    gm_tgglga8ro3isc t119 = gm_f_res_types_pg7rvck(t117, GM_CONV(gm_csguankko4lmhd, t118));
                    gm_tgglga8ro3isc t120 = t119;
                    gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t120) + 0));
                    gm_l9 = t121;
                }
                {
                    void * t122 = gm_l0;
                    void * t123 = gm_l0;
                    gm_t88rcucg1b7uk t124 = gm_l9;
                    uint64_t t125 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t123, t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t126 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t122, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t126;
                }
                {
                    void * t127 = gm_l0;
                    gm_t88rcucg1b7uk t128 = gm_l9;
                    gm_b16_8 t129 = gm_l10;
                    uint32_t t130 = gm_l8;
                    void * t131 = gm_l0;
                    gm_t88rcucg1b7uk t132 = gm_l3;
                    gm_b16_8 t133 = gm_l5;
                    gm_b16_8 t134 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t131, t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t135 = t134;
                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                    gm_b0_1 t137 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t127, t128, t129, t130, t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t138 = gm_l0;
                    gm_t88rcucg1b7uk t139 = gm_l4;
                    gm_b16_8 t140 = gm_l6;
                    gm_b16_8 t141 = gm_l10;
                    gm_b16_8 t142 = t141;
                    gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                    gm_b0_1 t144 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t138, t139, t140, ((uint8_t)1), t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix109;
ie109:;
                {
                    void * t146 = gm_l0;
                    gm_t88rcucg1b7uk t147 = gm_l4;
                    gm_b16_8 t148 = gm_l6;
                    void * t149 = gm_l0;
                    gm_t88rcucg1b7uk t150 = gm_l3;
                    gm_b16_8 t151 = gm_l5;
                    gm_b16_8 t152 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t149, t150, t151);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t153 = t152;
                    gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                    gm_b0_1 t155 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t146, t147, t148, ((uint8_t)0), t154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix109:;
            }
            goto me3;
mn105:;
ma9:;
            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t157 != ((uint8_t)7ULL)) goto mn156;
            void * t158 = gm_l0;
            gm_t88rcucg1b7uk t159 = gm_l3;
            gm_b16_8 t160 = gm_l5;
            gm_b16_8 t161 = t160;
            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
            gm_b16_8 t163 = gm_l6;
            gm_b0_1 t164 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t158, t159, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn156:;
ma10:;
            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t166 != ((uint8_t)11ULL)) goto mn165;
            {
                void * t169 = gm_l0;
                gm_t88rcucg1b7uk t170 = gm_l3;
                gm_b16_8 t171 = gm_l5;
                gm_b16_8 t172 = t171;
                gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                uint8_t t174 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t169, t170, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t175 = (uint8_t)!t174;
                if (!t175) goto ie168;
                {
                    gm_b16_8 t177 = gm_l6;
                    gm_f_fill_abmf5g5(t177, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t178;
                    gm_tcfa6l7reoosn t179;
                    GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t179 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t179 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t179 + 24, 0);
                    ((uint8_t *)&t179)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t179 + 8), t178);
                    gm_tcfa6l7reoosn t180 = t179;
                    uint8_t t181 = ((((uint8_t *)&t180))[0] != 0);
                    F.ret = t179;
                    goto gm_fret;
                }
                goto ix168;
ie168:;
ix168:;
            }
            {
                void * t182 = gm_l0;
                gm_t88rcucg1b7uk t183 = gm_l3;
                gm_t88rcucg1b7uk t184 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t182, GM_CONV(gm_csguankko4lmhd, t183)));
                gm_l11 = t184;
            }
            {
                void * t185 = gm_l0;
                gm_t88rcucg1b7uk t186 = gm_l4;
                gm_t88rcucg1b7uk t187 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t185, GM_CONV(gm_csguankko4lmhd, t186)));
                gm_l12 = t187;
            }
            {
                void * t188 = gm_l0;
                gm_t88rcucg1b7uk t189 = gm_l3;
                gm_b16_8 t190 = gm_l5;
                gm_b16_8 t191 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t188, t189, t190);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t191;
            }
            {
                void * t193 = gm_l0;
                gm_t88rcucg1b7uk t194 = gm_l11;
                gm_t53at80pfdus3 t195;
                gm_f_kind_htv6093((void *)&t195, t193, GM_CONV(gm_csguankko4lmhd, t194));
                gm_t53at80pfdus3 t196 = t195;
                uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&t196) + 0));
                if (t197 != ((uint8_t)8ULL)) goto ie192;
                gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t196) + 8) + 4));
                gm_l14 = t198;
                void * t199 = gm_l0;
                gm_t88rcucg1b7uk t200 = gm_l12;
                gm_t53at80pfdus3 t201;
                gm_f_kind_htv6093((void *)&t201, t199, GM_CONV(gm_csguankko4lmhd, t200));
                gm_t53at80pfdus3 t202 = t201;
                uint8_t t203;
                uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&t202) + 0));
                if (t206 != ((uint8_t)10ULL)) goto mn205;
                t203 = ((uint8_t)1);
                goto me204;
mn205:;
                t203 = ((uint8_t)0);
                goto me204;
mn207:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 55);
me204:;
                if (!t203) goto ie192;
                void * t208 = gm_l0;
                gm_t88rcucg1b7uk t209 = gm_l14;
                gm_t53at80pfdus3 t210;
                gm_f_kind_htv6093((void *)&t210, t208, GM_CONV(gm_csguankko4lmhd, t209));
                gm_t53at80pfdus3 t211 = t210;
                uint8_t t212;
                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                if (t215 != ((uint8_t)11ULL)) goto mn214;
                t212 = ((uint8_t)1);
                goto me213;
mn214:;
                t212 = ((uint8_t)0);
                goto me213;
mn216:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 90);
me213:;
                if (!t212) goto ie192;
                {
                    void * t218 = gm_l0;
                    void * t219 = gm_l0;
                    gm_t88rcucg1b7uk t220 = gm_l12;
                    uint64_t t221 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t219, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t222 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t218, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t222;
                }
                {
                    void * t223 = gm_l0;
                    gm_t88rcucg1b7uk t224 = gm_l11;
                    gm_b16_8 t225 = gm_l13;
                    gm_b16_8 t226 = t225;
                    gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
                    gm_b16_8 t228 = gm_l15;
                    gm_b0_1 t229 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t223, t224, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t230 = gm_l0;
                    gm_t88rcucg1b7uk t231 = gm_l4;
                    gm_b16_8 t232 = gm_l6;
                    gm_b16_8 t233 = gm_l15;
                    gm_b16_8 t234 = t233;
                    gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                    gm_b0_1 t236 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t230, t231, t232, t235);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix192;
ie192:;
                {
                    void * t238 = gm_l0;
                    gm_t88rcucg1b7uk t239 = gm_l4;
                    gm_b16_8 t240 = gm_l6;
                    gm_b16_8 t241 = gm_l13;
                    gm_b16_8 t242 = t241;
                    gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
                    gm_b0_1 t244 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t238, t239, t240, t243);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix192:;
            }
            goto me3;
mn165:;
ma11:;
            uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t248 != ((uint8_t)6ULL)) goto pa247;
            goto po246;
pa247:;
            uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t250 != ((uint8_t)8ULL)) goto pa249;
            goto po246;
pa249:;
            uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t252 != ((uint8_t)10ULL)) goto pa251;
            goto po246;
pa251:;
            goto mn245;
po246:;
            gm_b16_8 t253 = gm_l6;
            gm_b16_8 t254 = gm_l5;
            gm_b16_8 t255 = t254;
            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
            gm_f_copy_from_f7j6p05(t253, t256);
            goto me3;
mn245:;
ma12:;
            uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t260 != ((uint8_t)5ULL)) goto pa259;
            goto po258;
pa259:;
            uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t262 != ((uint8_t)9ULL)) goto pa261;
            goto po258;
pa261:;
            uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t264 != ((uint8_t)12ULL)) goto pa263;
            goto po258;
pa263:;
            goto mn257;
po258:;
            void * t265 = gm_l0;
            gm_tl874t2atnlh3 t266 = gm_l1;
            gm_b16_8 t267;
            GM_ST(void *, (uint8_t *)&t267, (void *)(gm_sbjj41cb5lph3));
            GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(59));
            gm_t76123gfet73g t268;
            gm_f_trap_qh84l9d((void *)&t268, (void *)NULL, cx, NULL, t265, t266, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t269;
            GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
            ((uint8_t *)&t269)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t269 + 8), t268);
            gm_tcfa6l7reoosn t270 = t269;
            uint8_t t271 = ((((uint8_t *)&t270))[0] != 0);
            F.ret = t269;
            goto gm_fret;
            goto me3;
mn257:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 9, 5);
me3:;
        }
    }
    gm_b0_1 t272;
    gm_tcfa6l7reoosn t273;
    GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t273 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t273 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t273 + 24, 0);
    ((uint8_t *)&t273)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t273 + 8), t272);
    gm_tcfa6l7reoosn t274 = t273;
    F.ret = t274;
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
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_convert_is6an2k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src_ty */
    gm_t88rcucg1b7uk gm_l4; /* dst_ty */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_t88rcucg1b7uk gm_l7; /* target */
    uint8_t gm_l8; /* is_float */
    gm_t86h6hhm6d76n gm_l9; /* sv */
    double gm_l10; /* fv */
    gm_ttovhfee8mf28 gm_l11; /* info */
    gm_tcmkompbl09il gm_l12; /* u */
    gm_ttovhfee8mf28 gm_l13; /* info */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_ttovhfee8mf28 gm_l15; /* info */
    gm_ttovhfee8mf28 gm_l16; /* tinfo */
    uint8_t gm_l17; /* signed */
    uint32_t gm_l18; /* bits */
    gm_t51b2en4qp02s gm_l19; /* pb */
    uint64_t gm_l20; /* n */
    uint8_t gm_l21; /* finite */
    gm_t86h6hhm6d76n gm_l22; /* n */
    gm_t51b2en4qp02s gm_l23; /* pb */
    uint64_t gm_l24; /* sz */
    uint8_t gm_l25; /* neg */
    gm_t86h6hhm6d76n gm_l26; /* n */
    gm_t86h6hhm6d76n gm_l27; /* n */
    uint16_t gm_l28; /* w */
    double gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_ttovhfee8mf28 gm_l31; /* info */
    GM_STACK_CHECK(&F, 4757u);
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
        gm_t88rcucg1b7uk t2;
        uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
        if (!t4) goto ie3;
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t6, GM_CONV(gm_csguankko4lmhd, t7)));
        gm_t88rcucg1b7uk t9 = t8;
        t2 = t9;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t11 = gm_l4;
        gm_t88rcucg1b7uk t12 = t11;
        t2 = t12;
ix3:;
        gm_l7 = t2;
    }
    {
        gm_l8 = ((uint8_t)0);
    }
    {
        memset((uint8_t *)&gm_l9 + 1, 0, 15);
        GM_ST(uint8_t, (uint8_t *)&gm_l9 + 0, ((uint8_t)0));
        GM_ST(unsigned __int128, (uint8_t *)&gm_l9 + 16, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
    }
    {
        gm_l10 = ((double)0e0);
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l3;
        gm_t53at80pfdus3 t15;
        gm_f_kind_htv6093((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
        gm_t53at80pfdus3 t16 = t15;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma22; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma22; default: goto ma22; }
ma18:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t28 != ((uint8_t)0ULL)) goto pa27;
        goto po26;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t30 != ((uint8_t)1ULL)) goto pa29;
        goto po26;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t32 != ((uint8_t)2ULL)) goto pa31;
        goto po26;
pa31:;
        goto mn25;
po26:;
        {
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l3;
            gm_tg5lql6itufk7 t36 = gm_f_int_info_ulcevdv(t34, GM_CONV(gm_csguankko4lmhd, t35));
            gm_tg5lql6itufk7 t37 = t36;
            if (!((((uint8_t *)&t37))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 75, 21);
            gm_ttovhfee8mf28 t38 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t37));
            gm_l11 = t38;
        }
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            void * t40 = gm_l0;
            gm_t88rcucg1b7uk t41 = gm_l3;
            gm_b16_8 t42 = gm_l5;
            unsigned __int128 t43 = gm_f_get_int_pg7p4gc(t40, GM_CONV(gm_csguankko4lmhd, t41), t42);
            gm_f_num_val_i3upeur((void *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t39, t43);
        }
        goto me17;
mn25:;
ma19:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t45 != ((uint8_t)3ULL)) goto mn44;
        {
            gm_l8 = ((uint8_t)1);
        }
        {
            void * t47 = gm_l0;
            gm_t88rcucg1b7uk t48 = gm_l3;
            gm_b16_8 t49 = gm_l5;
            double t50 = gm_f_get_float_grmhlm4(t47, GM_CONV(gm_csguankko4lmhd, t48), t49);
            gm_l10 = t50;
        }
        goto me17;
mn44:;
ma20:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t52 != ((uint8_t)4ULL)) goto mn51;
        {
            unsigned __int128 t54;
            uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcl8pn2pjtmj6k, 82, 32);
            uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t56 * 1));
            uint8_t t59 = ((t58 == ((uint8_t)0ULL)));
            if (!t59) goto ie55;
            unsigned __int128 t61 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            t54 = t61;
            goto ix55;
ie55:;
            unsigned __int128 t63 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            t54 = t63;
ix55:;
            GM_ST(unsigned __int128, (((uint8_t *)&gm_l9) + 16), t54);
        }
        goto me17;
mn51:;
ma21:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t65 != ((uint8_t)17ULL)) goto mn64;
        {
            void * t68 = gm_l0;
            gm_t88rcucg1b7uk t69 = gm_l3;
            gm_t57euh612ligr t70;
            gm_f_union_def_h8mvf4t((void *)&t70, t68, GM_CONV(gm_csguankko4lmhd, t69));
            gm_t57euh612ligr t71 = t70;
            if (!((((uint8_t *)&t71))[24] != 0)) goto ie67;
            gm_tcmkompbl09il t72 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t71));
            gm_l12 = t72;
            {
                void * t74 = gm_l0;
                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                gm_tg5lql6itufk7 t76 = gm_f_int_info_ulcevdv(t74, GM_CONV(gm_csguankko4lmhd, t75));
                gm_tg5lql6itufk7 t77 = t76;
                if (!((((uint8_t *)&t77))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 85, 25);
                gm_ttovhfee8mf28 t78 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t77));
                gm_l13 = t78;
            }
            {
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                void * t80 = gm_l0;
                gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                gm_b16_8 t82 = gm_l5;
                gm_b16_8 t83 = t82;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&t83) + 8);
                void * t85 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t83)));
                uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
                void * t87 = gm_l0;
                gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                uint64_t t89 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t90 = gm_f_us_cuv01fp(t89);
                uint64_t t91 = (uintptr_t)t90;
                if (t86 > t91 || t91 > (uintptr_t)t84) gm_trap_slice(t86, t91, t84, gm_srcl8pn2pjtmj6k, 86, 60);
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(((uint8_t *)t85 + t86 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(t91 - t86));
                unsigned __int128 t93 = gm_f_get_int_pg7p4gc(t80, GM_CONV(gm_csguankko4lmhd, t81), t92);
                gm_f_num_val_i3upeur((void *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t79, t93);
            }
            goto ix67;
ie67:;
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l3;
            gm_tkn42u7na3t81 t97 = gm_f_bits_struct_ty_22ue20s(t95, GM_CONV(gm_csguankko4lmhd, t96));
            gm_tkn42u7na3t81 t98 = t97;
            if (!((((uint8_t *)&t98))[4] != 0)) goto ie94;
            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t98));
            gm_l14 = t99;
            {
                void * t101 = gm_l0;
                gm_t88rcucg1b7uk t102 = gm_l14;
                gm_tg5lql6itufk7 t103 = gm_f_int_info_ulcevdv(t101, GM_CONV(gm_csguankko4lmhd, t102));
                gm_tg5lql6itufk7 t104 = t103;
                if (!((((uint8_t *)&t104))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 88, 25);
                gm_ttovhfee8mf28 t105 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t104));
                gm_l15 = t105;
            }
            {
                memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 1, 0, 15);
                void * t106 = gm_l0;
                gm_b16_8 t107 = gm_l5;
                gm_b16_8 t108 = t107;
                uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&t108) + 8);
                void * t110 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t108)));
                uint64_t t111 = (uintptr_t)((uint64_t)0ULL);
                void * t112 = gm_l0;
                gm_t88rcucg1b7uk t113 = gm_l14;
                uint64_t t114 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t115 = gm_f_us_cuv01fp(t114);
                uint64_t t116 = (uintptr_t)t115;
                if (t111 > t116 || t116 > (uintptr_t)t109) gm_trap_slice(t111, t116, t109, gm_srcl8pn2pjtmj6k, 89, 57);
                gm_b16_8 t117;
                GM_ST(void *, (uint8_t *)&t117, (void *)(((uint8_t *)t110 + t111 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(t116 - t111));
                unsigned __int128 t118 = gm_f_get_uint_bq8c4b7(t106, t117);
                uint32_t t119 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 4));
                unsigned __int128 t120 = gm_f_num_mask_do3naah(t119);
                unsigned __int128 t121 = (unsigned __int128)(t118 & t120);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 0, ((uint8_t)0));
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 16, t121);
            }
            goto ix94;
ie94:;
            {
                void * t123 = gm_l0;
                gm_tl874t2atnlh3 t124 = gm_l1;
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 8));
                gm_t88rcucg1b7uk t127 = gm_l3;
                gm_b16_8 t128 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t129;
                GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                gm_tabb710d9mq2e t130;
                gm_f_format_pe0ujmq((void *)&t130, (void *)NULL, cx, NULL, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t131 = t130;
                if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 8, gm_srcl8pn2pjtmj6k, 91, 38);
                gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t131) + 8));
                gm_b16_8 t133 = t132;
                gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                gm_t76123gfet73g t135;
                gm_f_trap_qh84l9d((void *)&t135, (void *)NULL, cx, NULL, t123, t124, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t136;
                GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                ((uint8_t *)&t136)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t136 + 8), t135);
                gm_tcfa6l7reoosn t137 = t136;
                uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
                F.ret = t136;
                goto gm_fret;
            }
ix94:;
ix67:;
        }
        goto me17;
mn64:;
ma22:;
        void * t140 = gm_l0;
        gm_tl874t2atnlh3 t141 = gm_l1;
        void * t142 = gm_l0;
        void * t143 = GM_LD(void *, (((uint8_t *)t142) + 8));
        gm_t88rcucg1b7uk t144 = gm_l3;
        gm_b16_8 t145 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t143, t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t146;
        GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t145);
        gm_tabb710d9mq2e t147;
        gm_f_format_pe0ujmq((void *)&t147, (void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t148 = t147;
        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_srcl8pn2pjtmj6k, 94, 35);
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
        gm_b16_8 t150 = t149;
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        gm_t76123gfet73g t152;
        gm_f_trap_qh84l9d((void *)&t152, (void *)NULL, cx, NULL, t140, t141, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t153;
        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
        ((uint8_t *)&t153)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t153 + 8), t152);
        gm_tcfa6l7reoosn t154 = t153;
        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
        F.ret = t153;
        goto gm_fret;
        goto me17;
mn139:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 73, 5);
me17:;
    }
    {
        void * t157 = gm_l0;
        gm_t88rcucg1b7uk t158 = gm_l7;
        gm_tg5lql6itufk7 t159 = gm_f_int_info_ulcevdv(t157, GM_CONV(gm_csguankko4lmhd, t158));
        gm_tg5lql6itufk7 t160 = t159;
        if (!((((uint8_t *)&t160))[8] != 0)) goto ie156;
        gm_ttovhfee8mf28 t161 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t160));
        gm_l16 = t161;
        {
            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            gm_l17 = t163;
        }
        {
            uint32_t t164 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
            gm_l18 = t164;
        }
        {
            uint8_t t166 = gm_l8;
            uint8_t t167 = (uint8_t)!t166;
            if (!t167) goto ie165;
            {
                uint8_t t170 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
                if (!t170) goto ie169;
                {
                    uint8_t t173 = gm_l17;
                    uint32_t t174 = gm_l18;
                    uint8_t t175 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t173, t174);
                    if (!t175) goto ie172;
                    {
                    }
                    {
                        void * t177 = gm_l0;
                        gm_t88rcucg1b7uk t178 = gm_l7;
                        uint64_t t179 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t177, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t180 = gm_f_us_cuv01fp(t179);
                        gm_l20 = t180;
                    }
                    {
                        void * t181 = gm_l0;
                        void * t182 = ((uint8_t *)&gm_l19);
                        uint64_t t183 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t184 = gm_l20;
                        uint64_t t185 = (uintptr_t)t184;
                        if (t183 > t185 || t185 > (uintptr_t)16) gm_trap_slice(t183, t185, 16, gm_srcl8pn2pjtmj6k, 104, 32);
                        gm_b16_8 t186;
                        GM_ST(void *, (uint8_t *)&t186, (void *)(((uint8_t *)t182 + t183 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(t185 - t183));
                        uint8_t t187 = gm_l17;
                        uint32_t t188 = gm_l18;
                        unsigned __int128 t189 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                        unsigned __int128 t190 = gm_f_num_ext_348kmbu(t187, t188, t189);
                        gm_f_put_uint_cddin99(t181, t186, t190);
                    }
                    {
                        void * t191 = gm_l0;
                        gm_t88rcucg1b7uk t192 = gm_l4;
                        gm_b16_8 t193 = gm_l6;
                        void * t194 = ((uint8_t *)&gm_l19);
                        uint64_t t195 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t196 = gm_l20;
                        uint64_t t197 = (uintptr_t)t196;
                        if (t195 > t197 || t197 > (uintptr_t)16) gm_trap_slice(t195, t197, 16, gm_srcl8pn2pjtmj6k, 105, 46);
                        gm_b16_8 t198;
                        GM_ST(void *, (uint8_t *)&t198, (void *)(((uint8_t *)t194 + t195 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(t197 - t195));
                        gm_b16_8 t199 = t198;
                        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                        gm_b0_1 t201 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t191, t192, t193, t200);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix172;
ie172:;
                    {
                        gm_b16_8 t203 = gm_l6;
                        gm_f_fill_abmf5g5(t203, ((uint8_t)0ULL));
                    }
ix172:;
                }
                goto ix169;
ie169:;
                uint8_t t205 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
                if (!t205) goto ie204;
                {
                    void * t207 = gm_l0;
                    gm_b16_8 t208 = gm_l6;
                    uint8_t t209 = gm_l17;
                    uint32_t t210 = gm_l18;
                    uint8_t t211 = gm_l17;
                    uint32_t t212 = gm_l18;
                    gm_t86h6hhm6d76n t213;
                    gm_f_num_sat_66dqcpl((void *)&t213, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t211, t212);
                    unsigned __int128 t214 = gm_f_num_bits_onl28vp(&t213);
                    unsigned __int128 t215 = gm_f_num_ext_348kmbu(t209, t210, t214);
                    gm_f_put_uint_cddin99(t207, t208, t215);
                }
                goto ix204;
ie204:;
                {
                    void * t217 = gm_l0;
                    gm_b16_8 t218 = gm_l6;
                    uint8_t t219 = gm_l17;
                    uint32_t t220 = gm_l18;
                    unsigned __int128 t221 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                    unsigned __int128 t222 = gm_f_num_ext_348kmbu(t219, t220, t221);
                    gm_f_put_uint_cddin99(t217, t218, t222);
                }
ix204:;
ix169:;
            }
            {
                gm_b0_1 t223;
                gm_tcfa6l7reoosn t224;
                GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t224 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t224 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 24, 0);
                ((uint8_t *)&t224)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t224 + 8), t223);
                gm_tcfa6l7reoosn t225 = t224;
                uint8_t t226 = ((((uint8_t *)&t225))[0] != 0);
                F.ret = t224;
                goto gm_fret;
            }
            goto ix165;
ie165:;
ix165:;
        }
        {
            double t227 = gm_l10;
            double t228 = gm_l10;
            double t229 = (t227 - t228);
            uint8_t t230 = ((t229 == ((double)0e0)));
            gm_l21 = t230;
        }
        {
            uint8_t t232 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
            if (!t232) goto ie231;
            {
                gm_b16_8 t234 = gm_l6;
                gm_f_fill_abmf5g5(t234, ((uint8_t)0ULL));
            }
            {
                uint8_t t236 = 0;
                uint8_t t237 = gm_l21;
                uint8_t t238 = (uint8_t)!t237;
                if (!t238) {
                    double t239 = gm_l10;
                    uint8_t t240 = gm_f_num_integral_qbts4a6(t239);
                    uint8_t t241 = (uint8_t)!t240;
                    t236 = t241;
                } else t236 = 1;
                if (!t236) goto ie235;
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
                goto ix235;
ie235:;
ix235:;
            }
            {
                double t247 = gm_l10;
                gm_tvdg94bk7aa22 t248;
                GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t248 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t248 + 0), ((uint8_t)1ULL));
                gm_tsp9ul70rnkg6 t249;
                gm_f_num_float_int_ba5kc1t((void *)&t249, t247, t248);
                gm_tsp9ul70rnkg6 t250 = t249;
                gm_t86h6hhm6d76n t251;
                if (((((uint8_t *)&t250))[32] != 0)) {
                    gm_t86h6hhm6d76n t252 = GM_LD(gm_t86h6hhm6d76n, (((uint8_t *)&t250) + 0));
                    t251 = t252;
                } else {
                    gm_b0_1 t253;
                    gm_tcfa6l7reoosn t254;
                    GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t254 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t254 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t254 + 24, 0);
                    ((uint8_t *)&t254)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t254 + 8), t253);
                    gm_tcfa6l7reoosn t255 = t254;
                    uint8_t t256 = ((((uint8_t *)&t255))[0] != 0);
                    F.ret = t254;
                    goto gm_fret;
                }
                gm_l22 = t251;
            }
            {
                uint8_t t258 = gm_l17;
                uint32_t t259 = gm_l18;
                uint8_t t260 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)), t258, t259);
                uint8_t t261 = (uint8_t)!t260;
                if (!t261) goto ie257;
                {
                    gm_b0_1 t263;
                    gm_tcfa6l7reoosn t264;
                    GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t264 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t264 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t264 + 24, 0);
                    ((uint8_t *)&t264)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t264 + 8), t263);
                    gm_tcfa6l7reoosn t265 = t264;
                    uint8_t t266 = ((((uint8_t *)&t265))[0] != 0);
                    F.ret = t264;
                    goto gm_fret;
                }
                goto ix257;
ie257:;
ix257:;
            }
            {
            }
            {
                void * t267 = gm_l0;
                gm_t88rcucg1b7uk t268 = gm_l7;
                uint64_t t269 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t267, t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t270 = gm_f_us_cuv01fp(t269);
                gm_l24 = t270;
            }
            {
                void * t271 = gm_l0;
                void * t272 = ((uint8_t *)&gm_l23);
                uint64_t t273 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t274 = gm_l24;
                uint64_t t275 = (uintptr_t)t274;
                if (t273 > t275 || t275 > (uintptr_t)16) gm_trap_slice(t273, t275, 16, gm_srcl8pn2pjtmj6k, 124, 24);
                gm_b16_8 t276;
                GM_ST(void *, (uint8_t *)&t276, (void *)(((uint8_t *)t272 + t273 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(t275 - t273));
                uint8_t t277 = gm_l17;
                uint32_t t278 = gm_l18;
                unsigned __int128 t279 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)));
                unsigned __int128 t280 = gm_f_num_ext_348kmbu(t277, t278, t279);
                gm_f_put_uint_cddin99(t271, t276, t280);
            }
            {
                void * t281 = gm_l0;
                gm_t88rcucg1b7uk t282 = gm_l4;
                gm_b16_8 t283 = gm_l6;
                void * t284 = ((uint8_t *)&gm_l23);
                uint64_t t285 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t286 = gm_l24;
                uint64_t t287 = (uintptr_t)t286;
                if (t285 > t287 || t287 > (uintptr_t)16) gm_trap_slice(t285, t287, 16, gm_srcl8pn2pjtmj6k, 125, 38);
                gm_b16_8 t288;
                GM_ST(void *, (uint8_t *)&t288, (void *)(((uint8_t *)t284 + t285 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(t287 - t285));
                gm_b16_8 t289 = t288;
                gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
                gm_b0_1 t291 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t281, t282, t283, t290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t292;
                gm_tcfa6l7reoosn t293;
                GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 24, 0);
                ((uint8_t *)&t293)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t293 + 8), t292);
                gm_tcfa6l7reoosn t294 = t293;
                uint8_t t295 = ((((uint8_t *)&t294))[0] != 0);
                F.ret = t293;
                goto gm_fret;
            }
            goto ix231;
ie231:;
ix231:;
        }
        {
            uint8_t t297 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
            if (!t297) goto ie296;
            {
                double t300 = gm_l10;
                uint8_t t301 = gm_f_num_is_nan_b0ma7mo(t300);
                if (!t301) goto ie299;
                {
                    gm_b16_8 t303 = gm_l6;
                    gm_f_fill_abmf5g5(t303, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t304;
                    gm_tcfa6l7reoosn t305;
                    GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t305 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t305 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t305 + 24, 0);
                    ((uint8_t *)&t305)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t305 + 8), t304);
                    gm_tcfa6l7reoosn t306 = t305;
                    uint8_t t307 = ((((uint8_t *)&t306))[0] != 0);
                    F.ret = t305;
                    goto gm_fret;
                }
                goto ix299;
ie299:;
ix299:;
            }
            {
                double t308 = gm_l10;
                double t309 = t308;
                uint64_t t310;
                memset(&t310, 0, sizeof t310); memcpy(&t310, ((uint8_t *)&t309), sizeof t310 < 8 ? sizeof t310 : 8);
                unsigned __int128 n311 = (unsigned __int128)((uint64_t)63ULL);
                if (n311 >= 64) gm_trap_shift((uint64_t)n311, "u64", gm_srcl8pn2pjtmj6k, 133, 21);
                uint64_t t312 = (uint64_t)(t310 >> (int)n311);
                uint8_t t313 = ((t312 == ((uint64_t)1ULL)));
                gm_l25 = t313;
            }
            {
                memset((uint8_t *)&gm_l26 + 1, 0, 15);
                uint8_t t314 = gm_l25;
                GM_ST(uint8_t, (uint8_t *)&gm_l26 + 0, t314);
                unsigned __int128 t315 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                GM_ST(unsigned __int128, (uint8_t *)&gm_l26 + 16, t315);
            }
            {
                uint8_t t317 = gm_l21;
                if (!t317) goto ie316;
                {
                    double t319 = gm_l10;
                    gm_tvdg94bk7aa22 t320;
                    GM_ST(uint8_t, (uint8_t *)&t320 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t320 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t320 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t320 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t320 + 0), ((uint8_t)1ULL));
                    gm_tsp9ul70rnkg6 t321;
                    gm_f_num_float_int_ba5kc1t((void *)&t321, t319, t320);
                    gm_tsp9ul70rnkg6 t322 = t321;
                    gm_t86h6hhm6d76n t323;
                    if (((((uint8_t *)&t322))[32] != 0)) {
                        gm_t86h6hhm6d76n t324 = GM_LD(gm_t86h6hhm6d76n, (((uint8_t *)&t322) + 0));
                        t323 = t324;
                    } else {
                        gm_t86h6hhm6d76n t325 = gm_l26;
                        t323 = t325;
                    }
                    gm_l26 = t323;
                }
                goto ix316;
ie316:;
ix316:;
            }
            {
                void * t326 = gm_l0;
                gm_b16_8 t327 = gm_l6;
                uint8_t t328 = gm_l17;
                uint32_t t329 = gm_l18;
                uint8_t t330 = gm_l17;
                uint32_t t331 = gm_l18;
                gm_t86h6hhm6d76n t332;
                gm_f_num_sat_66dqcpl((void *)&t332, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l26)), t330, t331);
                unsigned __int128 t333 = gm_f_num_bits_onl28vp(&t332);
                unsigned __int128 t334 = gm_f_num_ext_348kmbu(t328, t329, t333);
                gm_f_put_uint_cddin99(t326, t327, t334);
            }
            {
                gm_b0_1 t335;
                gm_tcfa6l7reoosn t336;
                GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t336 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t336 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t336 + 24, 0);
                ((uint8_t *)&t336)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t336 + 8), t335);
                gm_tcfa6l7reoosn t337 = t336;
                uint8_t t338 = ((((uint8_t *)&t337))[0] != 0);
                F.ret = t336;
                goto gm_fret;
            }
            goto ix296;
ie296:;
ix296:;
        }
        {
            uint8_t t340 = gm_l21;
            if (!t340) goto ie339;
            {
                double t343 = gm_l10;
                gm_tvdg94bk7aa22 t344 = gm_l2;
                gm_tsp9ul70rnkg6 t345;
                gm_f_num_float_int_ba5kc1t((void *)&t345, t343, t344);
                gm_tsp9ul70rnkg6 t346 = t345;
                if (!((((uint8_t *)&t346))[32] != 0)) goto ie342;
                gm_t86h6hhm6d76n t347 = GM_LD(gm_t86h6hhm6d76n, ((uint8_t *)&t346));
                gm_l27 = t347;
                {
                    uint8_t t350 = gm_l17;
                    uint32_t t351 = gm_l18;
                    uint8_t t352 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)), t350, t351);
                    if (!t352) goto ie349;
                    {
                        void * t354 = gm_l0;
                        gm_b16_8 t355 = gm_l6;
                        uint8_t t356 = gm_l17;
                        uint32_t t357 = gm_l18;
                        unsigned __int128 t358 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)));
                        unsigned __int128 t359 = gm_f_num_ext_348kmbu(t356, t357, t358);
                        gm_f_put_uint_cddin99(t354, t355, t359);
                    }
                    {
                        gm_b0_1 t360;
                        gm_tcfa6l7reoosn t361;
                        GM_ST(uint8_t, (uint8_t *)&t361 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t361 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t361 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t361 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t361 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t361 + 24, 0);
                        ((uint8_t *)&t361)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t361 + 8), t360);
                        gm_tcfa6l7reoosn t362 = t361;
                        uint8_t t363 = ((((uint8_t *)&t362))[0] != 0);
                        F.ret = t361;
                        goto gm_fret;
                    }
                    goto ix349;
ie349:;
ix349:;
                }
                goto ix342;
ie342:;
ix342:;
            }
            goto ix339;
ie339:;
ix339:;
        }
        {
            void * t365 = gm_l0;
            uint8_t t366 = gm_f_fast_23em7sv(t365);
            if (!t366) goto ie364;
            {
                gm_b16_8 t368 = gm_l6;
                gm_f_fill_abmf5g5(t368, ((uint8_t)0ULL));
            }
            {
                gm_b0_1 t369;
                gm_tcfa6l7reoosn t370;
                GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t370 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t370 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t370 + 24, 0);
                ((uint8_t *)&t370)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t370 + 8), t369);
                gm_tcfa6l7reoosn t371 = t370;
                uint8_t t372 = ((((uint8_t *)&t371))[0] != 0);
                F.ret = t370;
                goto gm_fret;
            }
            goto ix364;
ie364:;
ix364:;
        }
        {
            void * t373 = gm_l0;
            gm_tl874t2atnlh3 t374 = gm_l1;
            gm_b16_8 t375;
            GM_ST(void *, (uint8_t *)&t375, (void *)(gm_s9o2c36nosv54));
            GM_ST(uintptr_t, (uint8_t *)&t375 + 8, (uintptr_t)(40));
            gm_t76123gfet73g t376;
            gm_f_trap_qh84l9d((void *)&t376, (void *)NULL, cx, NULL, t373, t374, t375);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t377;
            GM_ST(uint8_t, (uint8_t *)&t377 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t377 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t377 + 4, 0);
            ((uint8_t *)&t377)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t377 + 8), t376);
            gm_tcfa6l7reoosn t378 = t377;
            uint8_t t379 = ((((uint8_t *)&t378))[0] != 0);
            F.ret = t377;
            goto gm_fret;
        }
        goto ix156;
ie156:;
ix156:;
    }
    {
        void * t381 = gm_l0;
        gm_t88rcucg1b7uk t382 = gm_l7;
        gm_t53at80pfdus3 t383;
        gm_f_kind_htv6093((void *)&t383, t381, GM_CONV(gm_csguankko4lmhd, t382));
        gm_t53at80pfdus3 t384 = t383;
        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&t384) + 0));
        if (t385 != ((uint8_t)3ULL)) goto ie380;
        uint16_t t386 = GM_LD(uint16_t, (((uint8_t *)&t384) + 8));
        gm_l28 = t386;
        {
            double t388;
            uint8_t t390 = gm_l8;
            if (!t390) goto ie389;
            double t392 = gm_l10;
            uint16_t t393 = gm_l28;
            double t394 = gm_f_num_round_2pna7m3(t392, t393);
            double t395 = t394;
            t388 = t395;
            goto ix389;
ie389:;
            uint16_t t397 = gm_l28;
            double t398 = gm_f_num_int_to_float_4hrvis6(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t397);
            double t399 = t398;
            t388 = t399;
ix389:;
            gm_l29 = t388;
        }
        {
            void * t400 = gm_l0;
            gm_t88rcucg1b7uk t401 = gm_l7;
            double t402 = gm_l29;
            gm_b16_8 t403 = gm_l6;
            gm_f_put_float_fiad72d(t400, GM_CONV(gm_csguankko4lmhd, t401), t402, t403);
        }
        {
            gm_b0_1 t404;
            gm_tcfa6l7reoosn t405;
            GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 24, 0);
            ((uint8_t *)&t405)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t405 + 8), t404);
            gm_tcfa6l7reoosn t406 = t405;
            uint8_t t407 = ((((uint8_t *)&t406))[0] != 0);
            F.ret = t405;
            goto gm_fret;
        }
        goto ix380;
ie380:;
ix380:;
    }
    {
        void * t409 = gm_l0;
        gm_t88rcucg1b7uk t410 = gm_l7;
        gm_tkn42u7na3t81 t411 = gm_f_bits_struct_ty_22ue20s(t409, GM_CONV(gm_csguankko4lmhd, t410));
        gm_tkn42u7na3t81 t412 = t411;
        if (!((((uint8_t *)&t412))[4] != 0)) goto ie408;
        gm_t88rcucg1b7uk t413 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t412));
        gm_l30 = t413;
        {
            uint8_t t416 = gm_l8;
            uint8_t t417 = (uint8_t)!t416;
            if (!t417) goto ie415;
            {
                void * t419 = gm_l0;
                gm_t88rcucg1b7uk t420 = gm_l30;
                gm_tg5lql6itufk7 t421 = gm_f_int_info_ulcevdv(t419, GM_CONV(gm_csguankko4lmhd, t420));
                gm_tg5lql6itufk7 t422 = t421;
                if (!((((uint8_t *)&t422))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 160, 21);
                gm_ttovhfee8mf28 t423 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t422));
                gm_l31 = t423;
            }
            {
                void * t424 = gm_l0;
                gm_b16_8 t425 = gm_l6;
                uint32_t t426 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                unsigned __int128 t427 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                unsigned __int128 t428 = gm_f_num_ext_348kmbu(((uint8_t)0), t426, t427);
                gm_f_put_uint_cddin99(t424, t425, t428);
            }
            {
                gm_b0_1 t429;
                gm_tcfa6l7reoosn t430;
                GM_ST(uint8_t, (uint8_t *)&t430 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t430 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t430 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t430 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t430 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t430 + 24, 0);
                ((uint8_t *)&t430)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t430 + 8), t429);
                gm_tcfa6l7reoosn t431 = t430;
                uint8_t t432 = ((((uint8_t *)&t431))[0] != 0);
                F.ret = t430;
                goto gm_fret;
            }
            goto ix415;
ie415:;
ix415:;
        }
        goto ix408;
ie408:;
ix408:;
    }
    {
        void * t433 = gm_l0;
        gm_tl874t2atnlh3 t434 = gm_l1;
        void * t435 = gm_l0;
        void * t436 = GM_LD(void *, (((uint8_t *)t435) + 8));
        gm_t88rcucg1b7uk t437 = gm_l7;
        gm_b16_8 t438 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t436, t437);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t439;
        GM_ST(gm_b16_8, ((uint8_t *)&t439 + 0), t438);
        gm_tabb710d9mq2e t440;
        gm_f_format_fv5tfj6((void *)&t440, (void *)NULL, cx, NULL, t439);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t441 = t440;
        if (((((uint8_t *)&t441))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t441) + 8, gm_srcl8pn2pjtmj6k, 165, 26);
        gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&t441) + 8));
        gm_b16_8 t443 = t442;
        gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
        gm_t76123gfet73g t445;
        gm_f_trap_qh84l9d((void *)&t445, (void *)NULL, cx, NULL, t433, t434, t444);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t446;
        GM_ST(uint8_t, (uint8_t *)&t446 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t446 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t446 + 4, 0);
        ((uint8_t *)&t446)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t446 + 8), t445);
        gm_tcfa6l7reoosn t447 = t446;
        uint8_t t448 = ((((uint8_t *)&t447))[0] != 0);
        F.ret = t446;
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
inline __attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_bytes_2qnf0cp F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* w */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t t10 = gm_l2;
                    uint64_t r11; bool o12 = __builtin_sub_overflow(t9, t10, &r11);
                    if (o12) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 416, 22);
                    uint64_t t13 = r11;
                    uint64_t t14 = (t13 < ((uint64_t)8ULL) ? t13 : ((uint64_t)8ULL));
                    gm_l4 = t14;
                }
                {
                    for (;;) {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = gm_l4;
                        uint8_t t18 = (t16 < t17);
                        if (!t18) goto lb15;
                        {
                            uint64_t t21 = gm_l5;
                            uint64_t t22 = t21;
                            uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)&t22));
                            gm_l6 = t25;
                            {
                                uint64_t t27 = gm_l3;
                                uint64_t t28 = gm_l2;
                                uint64_t t29 = gm_l6;
                                uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
                                if (o31) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 416, 61);
                                uint64_t t32 = r30;
                                uint64_t t33 = (uintptr_t)t32;
                                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcl8pn2pjtmj6k, 416, 55);
                                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t33 * 1));
                                uint64_t t36 = (uint64_t)t35;
                                uint64_t t37 = gm_l6;
                                uint64_t r38; bool o39 = __builtin_mul_overflow(t37, ((uint64_t)8ULL), &r38);
                                if (o39) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 416, 76);
                                uint64_t t40 = r38;
                                uint64_t t41 = (uint64_t)t40;
                                unsigned __int128 n42 = (unsigned __int128)t41;
                                if (n42 >= 64) gm_trap_shift((uint64_t)n42, "u64", gm_srcl8pn2pjtmj6k, 416, 51);
                                uint64_t r43 = (uint64_t)((uint64_t)t36 << (int)n42);
                                bool o44 = (uint64_t)(r43 >> (int)n42) != t36;
                                if (o44) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 416, 51);
                                uint64_t t45 = r43;
                                uint64_t t46 = (uint64_t)(t27 | t45);
                                gm_l3 = t46;
                            }
                            goto me23;
mn24:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 416, 9);
me23:;
                        }
lc15:;
                        {
                            uint64_t t48 = gm_l5;
                            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                            uint64_t t51 = r49;
                            gm_l5 = t51;
                        }
                    }
lb15:;
                }
            }
            {
                void * t52 = gm_l0;
                uint64_t t53 = gm_l3;
                gm_f_hash_u64_32bripn(t52, t53);
            }
            {
                uint64_t t54 = gm_l2;
                uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)8ULL), &r55);
                if (o56) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 418, 9);
                uint64_t t57 = r55;
                gm_l2 = t57;
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
extern __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2) { gm_f_hash_int_26mf5j0(a0, &a1, a2); }
extern __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_hash_val_nr2e99r F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* h */
    gm_ttovhfee8mf28 gm_l5; /* info */
    double gm_l6; /* x */
    gm_b16_8 gm_l7; /* elems */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint64_t gm_l13; /* esz */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* inner */
    gm_t88rcucg1b7uk gm_l22; /* err */
    gm_t88rcucg1b7uk gm_l23; /* ok */
    uint8_t gm_l24; /* is_err */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* u */
    uint32_t gm_l27; /* vi */
    gm_t88rcucg1b7uk gm_l28; /* pt */
    uint64_t gm_l29; /* off */
    void * gm_l30; /* a */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* i */
    gm_tfbnciphf3261 gm_l35; /* fl */
    uint64_t gm_l36; /* off */
    GM_STACK_CHECK(&F, 5854u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = gm_l0;
            gm_t88rcucg1b7uk t4 = gm_l2;
            gm_t53at80pfdus3 t5;
            gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
            gm_t53at80pfdus3 t6 = t5;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            switch (t21) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma19; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma19; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma19; default: goto ma19; }
ma8:;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t25 != ((uint8_t)0ULL)) goto pa24;
            goto po23;
pa24:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t27 != ((uint8_t)1ULL)) goto pa26;
            goto po23;
pa26:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t29 != ((uint8_t)2ULL)) goto pa28;
            goto po23;
pa28:;
            goto mn22;
po23:;
            {
                void * t31 = gm_l0;
                gm_t88rcucg1b7uk t32 = gm_l2;
                gm_tg5lql6itufk7 t33 = gm_f_int_info_ulcevdv(t31, GM_CONV(gm_csguankko4lmhd, t32));
                gm_tg5lql6itufk7 t34 = t33;
                if (!((((uint8_t *)&t34))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 434, 21);
                gm_ttovhfee8mf28 t35 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t34));
                gm_l5 = t35;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                void * t38 = gm_l0;
                gm_t88rcucg1b7uk t39 = gm_l2;
                gm_b16_8 t40 = gm_l3;
                gm_b16_8 t41 = t40;
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                unsigned __int128 t43 = gm_f_get_int_pg7p4gc(t38, GM_CONV(gm_csguankko4lmhd, t39), t42);
                gm_t86h6hhm6d76n t44;
                gm_f_num_val_i3upeur((void *)&t44, t37, t43);
                uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                gm_f_hash_int_26mf5j0(t36, &t44, t45);
            }
            goto me7;
mn22:;
ma9:;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t47 != ((uint8_t)3ULL)) goto mn46;
            {
                void * t49 = gm_l0;
                gm_t88rcucg1b7uk t50 = gm_l2;
                gm_b16_8 t51 = gm_l3;
                gm_b16_8 t52 = t51;
                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                double t54 = gm_f_get_float_grmhlm4(t49, GM_CONV(gm_csguankko4lmhd, t50), t53);
                gm_l6 = t54;
            }
            {
                double t56 = gm_l6;
                uint8_t t57 = ((t56 == ((double)0e0)));
                if (!t57) goto ie55;
                {
                    gm_l6 = ((double)0e0);
                }
                goto ix55;
ie55:;
ix55:;
            }
            {
                void * t59 = gm_l4;
                double t60 = gm_l6;
                double t61 = t60;
                uint64_t t62;
                memset(&t62, 0, sizeof t62); memcpy(&t62, ((uint8_t *)&t61), sizeof t62 < 8 ? sizeof t62 : 8);
                gm_f_hash_u64_32bripn(t59, t62);
            }
            goto me7;
mn46:;
ma10:;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t64 != ((uint8_t)4ULL)) goto mn63;
            void * t65 = gm_l4;
            uint64_t t66;
            uint64_t t68 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcl8pn2pjtmj6k, 442, 33);
            uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t68 * 1));
            uint8_t t71 = ((t70 == ((uint8_t)0ULL)));
            if (!t71) goto ie67;
            uint64_t t73 = ((uint64_t)0ULL);
            t66 = t73;
            goto ix67;
ie67:;
            uint64_t t75 = ((uint64_t)1ULL);
            t66 = t75;
ix67:;
            gm_f_hash_u64_32bripn(t65, t66);
            goto me7;
mn63:;
ma11:;
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t79 != ((uint8_t)8ULL)) goto pa78;
            goto po77;
pa78:;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t81 != ((uint8_t)9ULL)) goto pa80;
            goto po77;
pa80:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t83 != ((uint8_t)7ULL)) goto pa82;
            goto po77;
pa82:;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t85 != ((uint8_t)16ULL)) goto pa84;
            goto po77;
pa84:;
            goto mn76;
po77:;
            void * t86 = gm_l4;
            void * t87 = gm_l0;
            gm_b16_8 t88 = gm_l3;
            gm_b16_8 t89 = t88;
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
            uint64_t t91 = gm_f_ptr_of_b77b4c9(t87, t90);
            gm_f_hash_u64_32bripn(t86, t91);
            goto me7;
mn76:;
ma12:;
            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t93 != ((uint8_t)5ULL)) goto mn92;
            goto me7;
mn92:;
ma13:;
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t96 != ((uint8_t)12ULL)) goto mn95;
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
            gm_l7 = t97;
            {
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l8 = t100;
                }
                {
                    for (;;) {
                        uint64_t t102 = gm_l9;
                        uint64_t t103 = gm_l8;
                        uint8_t t104 = (t102 < t103);
                        if (!t104) goto lb101;
                        {
                            uint64_t t107 = gm_l9;
                            uint64_t t108 = t107;
                            uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)&t108));
                            gm_l10 = t111;
                            {
                                void * t113 = gm_l0;
                                gm_tl874t2atnlh3 t114 = gm_l1;
                                uint64_t t115 = gm_l10;
                                uint64_t t116 = (uintptr_t)t115;
                                uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_srcl8pn2pjtmj6k, 446, 58);
                                gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t116 * 4));
                                void * t119 = gm_l0;
                                gm_t88rcucg1b7uk t120 = gm_l2;
                                gm_b16_8 t121 = gm_l3;
                                uint64_t t122 = gm_l10;
                                uint32_t t123 = (uint32_t)t122;
                                gm_b16_8 t124 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t119, t120, t121, t123);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                void * t125 = gm_l4;
                                gm_tcfa6l7reoosn t126;
                                gm_f_hash_val_nr2e99r((void *)&t126, (void *)NULL, cx, NULL, t113, t114, t118, t124, t125);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                gm_tcfa6l7reoosn t127 = t126;
                                if (((((uint8_t *)&t127))[0] != 0)) {
                                    gm_t76123gfet73g t128 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t127) + 8));
                                    gm_tcfa6l7reoosn t129;
                                    GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                                    ((uint8_t *)&t129)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t129 + 8), t128);
                                    F.ret = t129;
                                    goto gm_fret;
                                }
                                gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 8));
                            }
                            goto me109;
mn110:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 446, 13);
me109:;
                        }
lc101:;
                        {
                            uint64_t t132 = gm_l9;
                            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                            uint64_t t135 = r133;
                            gm_l9 = t135;
                        }
                    }
gm_ll105:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb101; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc101; }
                    }
                    goto gm_lad;
lb101:;
                }
            }
            goto me7;
mn95:;
ma14:;
            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t137 != ((uint8_t)11ULL)) goto mn136;
            uint64_t t138 = GM_LD(uint64_t, ((((uint8_t *)&t6) + 8) + 0));
            gm_l11 = t138;
            gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
            gm_l12 = t139;
            {
                void * t141 = gm_l0;
                gm_t88rcucg1b7uk t142 = gm_l12;
                uint64_t t143 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t141, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t144 = gm_f_us_cuv01fp(t143);
                gm_l13 = t144;
            }
            {
                void * t146 = gm_l0;
                gm_t88rcucg1b7uk t147 = gm_l12;
                gm_t53at80pfdus3 t148;
                gm_f_kind_htv6093((void *)&t148, t146, GM_CONV(gm_csguankko4lmhd, t147));
                gm_t53at80pfdus3 t149 = t148;
                gm_tjcj4j2gbh6c8 t150;
                GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)0));
                GM_ST(uint16_t, ((uint8_t *)&t150 + 2), ((uint16_t)8ULL));
                gm_tjcj4j2gbh6c8 t151 = t150;
                uint8_t t152 = (((GM_LD(uint8_t, ((uint8_t *)&t149) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t149) + 8), ((uint8_t *)&t151))));
                if (!t152) goto ie145;
                {
                    void * t154 = gm_l4;
                    gm_b16_8 t155 = gm_l3;
                    gm_b16_8 t156 = t155;
                    uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&t156) + 8);
                    void * t158 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t156)));
                    uint64_t t159 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t160 = gm_l11;
                    uint64_t t161 = gm_f_us_cuv01fp(t160);
                    uint64_t t162 = (uintptr_t)t161;
                    if (t159 > t162 || t162 > (uintptr_t)t157) gm_trap_slice(t159, t162, t157, gm_srcl8pn2pjtmj6k, 451, 31);
                    gm_b16_8 t163;
                    GM_ST(void *, (uint8_t *)&t163, (void *)(((uint8_t *)t158 + t159 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(t162 - t159));
                    gm_b16_8 t164 = t163;
                    gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                    gm_f_hash_bytes_2qnf0cp(t154, t165);
                }
                {
                    gm_b0_1 t166;
                    gm_tcfa6l7reoosn t167;
                    GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t167 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t167 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t167 + 24, 0);
                    ((uint8_t *)&t167)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t167 + 8), t166);
                    gm_tcfa6l7reoosn t168 = t167;
                    uint8_t t169 = ((((uint8_t *)&t168))[0] != 0);
                    F.ret = t167;
                    goto gm_fret;
                }
                goto ix145;
ie145:;
ix145:;
            }
            {
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t171 = gm_l11;
                    uint64_t t172 = gm_f_us_cuv01fp(t171);
                    gm_l14 = t172;
                }
                {
                    for (;;) {
                        uint64_t t174 = gm_l15;
                        uint64_t t175 = gm_l14;
                        uint8_t t176 = (t174 < t175);
                        if (!t176) goto lb173;
                        {
                            uint64_t t179 = gm_l15;
                            uint64_t t180 = t179;
                            uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)&t180));
                            gm_l16 = t183;
                            {
                                void * t185 = gm_l0;
                                gm_tl874t2atnlh3 t186 = gm_l1;
                                gm_t88rcucg1b7uk t187 = gm_l12;
                                uint64_t t188 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t189 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t190 = gm_l16;
                                uint64_t t191 = gm_l13;
                                uint64_t r192; bool o193 = __builtin_mul_overflow(t190, t191, &r192);
                                if (o193) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 62);
                                uint64_t t194 = r192;
                                uint64_t t195 = (uintptr_t)t194;
                                uint64_t t196 = gm_l16;
                                uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
                                if (o198) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 454, 72);
                                uint64_t t199 = r197;
                                uint64_t t200 = gm_l13;
                                uint64_t r201; bool o202 = __builtin_mul_overflow(t199, t200, &r201);
                                if (o202) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 71);
                                uint64_t t203 = r201;
                                uint64_t t204 = (uintptr_t)t203;
                                if (t195 > t204 || t204 > (uintptr_t)t188) gm_trap_slice(t195, t204, t188, gm_srcl8pn2pjtmj6k, 454, 60);
                                gm_b16_8 t205;
                                GM_ST(void *, (uint8_t *)&t205, (void *)(((uint8_t *)t189 + t195 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(t204 - t195));
                                void * t206 = gm_l4;
                                gm_tcfa6l7reoosn t207;
                                gm_f_hash_val_nr2e99r((void *)&t207, (void *)NULL, cx, NULL, t185, t186, t187, t205, t206);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                gm_tcfa6l7reoosn t208 = t207;
                                if (((((uint8_t *)&t208))[0] != 0)) {
                                    gm_t76123gfet73g t209 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t208) + 8));
                                    gm_tcfa6l7reoosn t210;
                                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                                    ((uint8_t *)&t210)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t210 + 8), t209);
                                    F.ret = t210;
                                    goto gm_fret;
                                }
                                gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 8));
                            }
                            goto me181;
mn182:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 454, 13);
me181:;
                        }
lc173:;
                        {
                            uint64_t t213 = gm_l15;
                            uint64_t r214; bool o215 = __builtin_add_overflow(t213, ((uint64_t)1ULL), &r214);
                            uint64_t t216 = r214;
                            gm_l15 = t216;
                        }
                    }
gm_ll177:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb173; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc173; }
                    }
                    goto gm_lad;
lb173:;
                }
            }
            goto me7;
mn136:;
ma15:;
            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t218 != ((uint8_t)10ULL)) goto mn217;
            {
                void * t220 = gm_l0;
                gm_tl874t2atnlh3 t221 = gm_l1;
                gm_t88rcucg1b7uk t222 = gm_l2;
                gm_b16_8 t223 = gm_l3;
                gm_tmc91gv7n9c0i t224;
                gm_f_seq_of_ifahtjs((void *)&t224, (void *)NULL, cx, NULL, t220, t221, t222, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tmc91gv7n9c0i t225 = t224;
                if (((((uint8_t *)&t225))[0] != 0)) {
                    gm_t76123gfet73g t226 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t225) + 8));
                    gm_tcfa6l7reoosn t227;
                    GM_ST(uint8_t, (uint8_t *)&t227 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t227 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                    ((uint8_t *)&t227)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t227 + 8), t226);
                    F.ret = t227;
                    goto gm_fret;
                }
                gm_t2jeg3bg7sfqa t228 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t225) + 8));
                F.l17 = t228;
            }
            {
                void * t229 = gm_l4;
                uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                gm_f_hash_u64_32bripn(t229, t230);
            }
            {
                void * t232 = gm_l0;
                gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 0));
                gm_t53at80pfdus3 t234;
                gm_f_kind_htv6093((void *)&t234, t232, GM_CONV(gm_csguankko4lmhd, t233));
                gm_t53at80pfdus3 t235 = t234;
                gm_tjcj4j2gbh6c8 t236;
                GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)0));
                GM_ST(uint16_t, ((uint8_t *)&t236 + 2), ((uint16_t)8ULL));
                gm_tjcj4j2gbh6c8 t237 = t236;
                uint8_t t238 = (((GM_LD(uint8_t, ((uint8_t *)&t235) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t235) + 8), ((uint8_t *)&t237))));
                if (!t238) goto ie231;
                {
                    void * t240 = gm_l4;
                    gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 24));
                    gm_b16_8 t242 = t241;
                    uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&t242) + 8);
                    void * t244 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t242)));
                    uint64_t t245 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t247 = gm_f_us_cuv01fp(t246);
                    uint64_t t248 = (uintptr_t)t247;
                    if (t245 > t248 || t248 > (uintptr_t)t243) gm_trap_slice(t245, t248, t243, gm_srcl8pn2pjtmj6k, 460, 31);
                    gm_b16_8 t249;
                    GM_ST(void *, (uint8_t *)&t249, (void *)(((uint8_t *)t244 + t245 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(t248 - t245));
                    gm_b16_8 t250 = t249;
                    gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
                    gm_f_hash_bytes_2qnf0cp(t240, t251);
                }
                {
                    gm_b0_1 t252;
                    gm_tcfa6l7reoosn t253;
                    GM_ST(uint8_t, (uint8_t *)&t253 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t253 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t253 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t253 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t253 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t253 + 24, 0);
                    ((uint8_t *)&t253)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t253 + 8), t252);
                    gm_tcfa6l7reoosn t254 = t253;
                    uint8_t t255 = ((((uint8_t *)&t254))[0] != 0);
                    F.ret = t253;
                    goto gm_fret;
                }
                goto ix231;
ie231:;
ix231:;
            }
            {
                {
                    gm_l19 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t258 = gm_f_us_cuv01fp(t257);
                    gm_l18 = t258;
                }
                {
                    for (;;) {
                        uint64_t t260 = gm_l19;
                        uint64_t t261 = gm_l18;
                        uint8_t t262 = (t260 < t261);
                        if (!t262) goto lb259;
                        {
                            uint64_t t265 = gm_l19;
                            uint64_t t266 = t265;
                            uint64_t t269 = GM_LD(uint64_t, ((uint8_t *)&t266));
                            gm_l20 = t269;
                            {
                                void * t271 = gm_l0;
                                gm_tl874t2atnlh3 t272 = gm_l1;
                                gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 0));
                                uint64_t t274 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                void * t275 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 24)));
                                uint64_t t276 = gm_l20;
                                uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                uint64_t r278; bool o279 = __builtin_mul_overflow(t276, t277, &r278);
                                if (o279) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 71);
                                uint64_t t280 = r278;
                                uint64_t t281 = (uintptr_t)t280;
                                uint64_t t282 = gm_l20;
                                uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                                if (o284) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 463, 83);
                                uint64_t t285 = r283;
                                uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                uint64_t r287; bool o288 = __builtin_mul_overflow(t285, t286, &r287);
                                if (o288) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 82);
                                uint64_t t289 = r287;
                                uint64_t t290 = (uintptr_t)t289;
                                if (t281 > t290 || t290 > (uintptr_t)t274) gm_trap_slice(t281, t290, t274, gm_srcl8pn2pjtmj6k, 463, 64);
                                gm_b16_8 t291;
                                GM_ST(void *, (uint8_t *)&t291, (void *)(((uint8_t *)t275 + t281 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t291 + 8, (uintptr_t)(t290 - t281));
                                void * t292 = gm_l4;
                                gm_tcfa6l7reoosn t293;
                                gm_f_hash_val_nr2e99r((void *)&t293, (void *)NULL, cx, NULL, t271, t272, t273, t291, t292);
                                if (gm_unw.kind) {
                                    goto gm_ll263;
                                }
                                gm_tcfa6l7reoosn t294 = t293;
                                if (((((uint8_t *)&t294))[0] != 0)) {
                                    gm_t76123gfet73g t295 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t294) + 8));
                                    gm_tcfa6l7reoosn t296;
                                    GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                                    ((uint8_t *)&t296)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t296 + 8), t295);
                                    F.ret = t296;
                                    goto gm_fret;
                                }
                                gm_b0_1 t297 = GM_LD(gm_b0_1, (((uint8_t *)&t294) + 8));
                            }
                            goto me267;
mn268:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 463, 13);
me267:;
                        }
lc259:;
                        {
                            uint64_t t299 = gm_l19;
                            uint64_t r300; bool o301 = __builtin_add_overflow(t299, ((uint64_t)1ULL), &r300);
                            uint64_t t302 = r300;
                            gm_l19 = t302;
                        }
                    }
gm_ll263:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb259; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc259; }
                    }
                    goto gm_lad;
lb259:;
                }
            }
            goto me7;
mn217:;
ma16:;
            uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t304 != ((uint8_t)13ULL)) goto mn303;
            gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
            gm_l21 = t305;
            {
                void * t308 = gm_l0;
                void * t309 = GM_LD(void *, (((uint8_t *)t308) + 8));
                gm_t88rcucg1b7uk t310 = gm_l21;
                uint8_t t311 = gm_f_nullable_pointer_7qfmn6o(t309, GM_CONV(gm_csguankko4lmhd, t310));
                if (!t311) goto ie307;
                {
                    void * t313 = gm_l4;
                    void * t314 = gm_l0;
                    gm_b16_8 t315 = gm_l3;
                    gm_b16_8 t316 = t315;
                    gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
                    uint64_t t318 = gm_f_ptr_of_b77b4c9(t314, t317);
                    gm_f_hash_u64_32bripn(t313, t318);
                }
                goto ix307;
ie307:;
                void * t320 = gm_l0;
                gm_t88rcucg1b7uk t321 = gm_l2;
                gm_b16_8 t322 = gm_l3;
                gm_b16_8 t323 = t322;
                gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                uint8_t t325 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t320, t321, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t325) goto ie319;
                {
                    void * t327 = gm_l4;
                    gm_f_hash_u64_32bripn(t327, ((uint64_t)1ULL));
                }
                {
                    void * t328 = gm_l0;
                    gm_tl874t2atnlh3 t329 = gm_l1;
                    gm_t88rcucg1b7uk t330 = gm_l21;
                    void * t331 = gm_l0;
                    gm_t88rcucg1b7uk t332 = gm_l2;
                    gm_b16_8 t333 = gm_l3;
                    gm_b16_8 t334 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t331, t332, t333);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t335 = gm_l4;
                    gm_tcfa6l7reoosn t336;
                    gm_f_hash_val_nr2e99r((void *)&t336, (void *)NULL, cx, NULL, t328, t329, t330, t334, t335);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t337 = t336;
                    if (((((uint8_t *)&t337))[0] != 0)) {
                        gm_t76123gfet73g t338 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t337) + 8));
                        gm_tcfa6l7reoosn t339;
                        GM_ST(uint8_t, (uint8_t *)&t339 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t339 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t339 + 4, 0);
                        ((uint8_t *)&t339)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t339 + 8), t338);
                        F.ret = t339;
                        goto gm_fret;
                    }
                    gm_b0_1 t340 = GM_LD(gm_b0_1, (((uint8_t *)&t337) + 8));
                }
                goto ix319;
ie319:;
                {
                    void * t342 = gm_l4;
                    gm_f_hash_u64_32bripn(t342, ((uint64_t)0ULL));
                }
ix319:;
ix307:;
            }
            goto me7;
mn303:;
ma17:;
            uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t344 != ((uint8_t)14ULL)) goto mn343;
            gm_t88rcucg1b7uk t345 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
            gm_l22 = t345;
            gm_t88rcucg1b7uk t346 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
            gm_l23 = t346;
            {
                void * t348 = gm_l0;
                gm_t88rcucg1b7uk t349 = gm_l2;
                gm_b16_8 t350 = gm_l3;
                gm_b16_8 t351 = t350;
                gm_b16_8 t352 = GM_LD(gm_b16_8, ((uint8_t *)&t351));
                uint8_t t353 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t348, t349, t352);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t353;
            }
            {
                void * t354 = gm_l4;
                uint64_t t355;
                uint8_t t357 = gm_l24;
                if (!t357) goto ie356;
                uint64_t t359 = ((uint64_t)1ULL);
                t355 = t359;
                goto ix356;
ie356:;
                uint64_t t361 = ((uint64_t)0ULL);
                t355 = t361;
ix356:;
                gm_f_hash_u64_32bripn(t354, t355);
            }
            {
                void * t362 = gm_l0;
                gm_tl874t2atnlh3 t363 = gm_l1;
                gm_t88rcucg1b7uk t364;
                uint8_t t366 = gm_l24;
                if (!t366) goto ie365;
                gm_t88rcucg1b7uk t368 = gm_l22;
                gm_t88rcucg1b7uk t369 = t368;
                t364 = t369;
                goto ix365;
ie365:;
                gm_t88rcucg1b7uk t371 = gm_l23;
                gm_t88rcucg1b7uk t372 = t371;
                t364 = t372;
ix365:;
                void * t373 = gm_l0;
                gm_t88rcucg1b7uk t374 = gm_l2;
                gm_b16_8 t375 = gm_l3;
                gm_b16_8 t376 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t373, t374, t375);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t377 = gm_l4;
                gm_tcfa6l7reoosn t378;
                gm_f_hash_val_nr2e99r((void *)&t378, (void *)NULL, cx, NULL, t362, t363, t364, t376, t377);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t379 = t378;
                if (((((uint8_t *)&t379))[0] != 0)) {
                    gm_t76123gfet73g t380 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t379) + 8));
                    gm_tcfa6l7reoosn t381;
                    GM_ST(uint8_t, (uint8_t *)&t381 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t381 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t381 + 4, 0);
                    ((uint8_t *)&t381)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t381 + 8), t380);
                    F.ret = t381;
                    goto gm_fret;
                }
                gm_b0_1 t382 = GM_LD(gm_b0_1, (((uint8_t *)&t379) + 8));
            }
            goto me7;
mn343:;
ma18:;
            uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t384 != ((uint8_t)17ULL)) goto mn383;
            {
                void * t387 = gm_l0;
                gm_t88rcucg1b7uk t388 = gm_l2;
                gm_tkn42u7na3t81 t389 = gm_f_bits_struct_ty_22ue20s(t387, GM_CONV(gm_csguankko4lmhd, t388));
                gm_tkn42u7na3t81 t390 = t389;
                if (!((((uint8_t *)&t390))[4] != 0)) goto ie386;
                gm_t88rcucg1b7uk t391 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t390));
                gm_l25 = t391;
                {
                    void * t393 = gm_l0;
                    gm_tl874t2atnlh3 t394 = gm_l1;
                    gm_t88rcucg1b7uk t395 = gm_l25;
                    gm_b16_8 t396 = gm_l3;
                    void * t397 = gm_l4;
                    gm_tcfa6l7reoosn t398;
                    gm_f_hash_val_nr2e99r((void *)&t398, (void *)NULL, cx, NULL, t393, t394, t395, t396, t397);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t399 = t398;
                    if (((((uint8_t *)&t399))[0] != 0)) {
                        gm_t76123gfet73g t400 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t399) + 8));
                        gm_tcfa6l7reoosn t401;
                        GM_ST(uint8_t, (uint8_t *)&t401 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t401 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t401 + 4, 0);
                        ((uint8_t *)&t401)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t401 + 8), t400);
                        F.ret = t401;
                        goto gm_fret;
                    }
                    gm_b0_1 t402 = GM_LD(gm_b0_1, (((uint8_t *)&t399) + 8));
                }
                {
                    gm_b0_1 t403;
                    gm_tcfa6l7reoosn t404;
                    GM_ST(uint8_t, (uint8_t *)&t404 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t404 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t404 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t404 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t404 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t404 + 24, 0);
                    ((uint8_t *)&t404)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t404 + 8), t403);
                    gm_tcfa6l7reoosn t405 = t404;
                    uint8_t t406 = ((((uint8_t *)&t405))[0] != 0);
                    F.ret = t404;
                    goto gm_fret;
                }
                goto ix386;
ie386:;
ix386:;
            }
            {
                void * t408 = gm_l0;
                gm_t88rcucg1b7uk t409 = gm_l2;
                gm_t57euh612ligr t410;
                gm_f_union_def_h8mvf4t((void *)&t410, t408, GM_CONV(gm_csguankko4lmhd, t409));
                gm_t57euh612ligr t411 = t410;
                if (!((((uint8_t *)&t411))[24] != 0)) goto ie407;
                gm_tcmkompbl09il t412 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t411));
                gm_l26 = t412;
                {
                    void * t414 = gm_l0;
                    gm_t88rcucg1b7uk t415 = gm_l2;
                    gm_b16_8 t416 = gm_l3;
                    gm_b16_8 t417 = t416;
                    gm_b16_8 t418 = GM_LD(gm_b16_8, ((uint8_t *)&t417));
                    gm_t9805qtonbsag t419 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t414, t415, t418);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9805qtonbsag t420 = t419;
                    uint32_t t421;
                    if (((((uint8_t *)&t420))[4] != 0)) {
                        uint32_t t422 = GM_LD(uint32_t, (((uint8_t *)&t420) + 0));
                        t421 = t422;
                    } else {
                        void * t423 = gm_l0;
                        gm_tl874t2atnlh3 t424 = gm_l1;
                        gm_t88rcucg1b7uk t425 = gm_l2;
                        gm_b16_8 t426 = gm_l3;
                        gm_b16_8 t427 = t426;
                        gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                        gm_t76123gfet73g t429;
                        gm_f_invalid_tag_1gif354((void *)&t429, (void *)NULL, cx, NULL, t423, t424, t425, t428);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t430;
                        GM_ST(uint8_t, (uint8_t *)&t430 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t430 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t430 + 4, 0);
                        ((uint8_t *)&t430)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t430 + 8), t429);
                        gm_tcfa6l7reoosn t431 = t430;
                        uint8_t t432 = ((((uint8_t *)&t431))[0] != 0);
                        F.ret = t430;
                        goto gm_fret;
                    }
                    gm_l27 = t421;
                }
                {
                    void * t433 = gm_l4;
                    uint32_t t434 = gm_l27;
                    uint64_t t435 = (uint64_t)t434;
                    gm_f_hash_u64_32bripn(t433, t435);
                }
                {
                    gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t438 = t437;
                    uint32_t t439 = gm_l27;
                    uint64_t t440 = (uint64_t)t439;
                    uint64_t t441 = gm_f_us_cuv01fp(t440);
                    uint64_t t442 = (uintptr_t)t441;
                    gm_b16_8 t443 = t438;
                    uint64_t t444 = GM_LD(uintptr_t, ((uint8_t *)&t443) + 8);
                    if (t442 >= (uintptr_t)(t444)) gm_trap_index(t442, t444, gm_srcl8pn2pjtmj6k, 488, 26);
                    if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t443)) + t442 * 72) + 16))[4] != 0)) goto ie436;
                    gm_t88rcucg1b7uk t445 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t443)) + t442 * 72) + 16));
                    gm_l28 = t445;
                    {
                        void * t447 = gm_l0;
                        gm_t88rcucg1b7uk t448 = gm_l2;
                        uint64_t t449 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t447, t448);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t450 = gm_f_us_cuv01fp(t449);
                        gm_l29 = t450;
                    }
                    {
                        void * t451 = gm_l0;
                        gm_tl874t2atnlh3 t452 = gm_l1;
                        gm_t88rcucg1b7uk t453 = gm_l28;
                        gm_b16_8 t454 = gm_l3;
                        gm_b16_8 t455 = t454;
                        uint64_t t456 = GM_LD(uintptr_t, ((uint8_t *)&t455) + 8);
                        void * t457 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t455)));
                        uint64_t t458 = gm_l29;
                        uint64_t t459 = (uintptr_t)t458;
                        uint64_t t460 = gm_l29;
                        void * t461 = gm_l0;
                        gm_t88rcucg1b7uk t462 = gm_l28;
                        uint64_t t463 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t461, t462);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t464 = gm_f_us_cuv01fp(t463);
                        uint64_t r465; bool o466 = __builtin_add_overflow(t460, t464, &r465);
                        if (o466) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 490, 53);
                        uint64_t t467 = r465;
                        uint64_t t468 = (uintptr_t)t467;
                        if (t459 > t468 || t468 > (uintptr_t)t456) gm_trap_slice(t459, t468, t456, gm_srcl8pn2pjtmj6k, 490, 46);
                        gm_b16_8 t469;
                        GM_ST(void *, (uint8_t *)&t469, (void *)(((uint8_t *)t457 + t459 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t469 + 8, (uintptr_t)(t468 - t459));
                        void * t470 = gm_l4;
                        gm_tcfa6l7reoosn t471;
                        gm_f_hash_val_nr2e99r((void *)&t471, (void *)NULL, cx, NULL, t451, t452, t453, t469, t470);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t472 = t471;
                        if (((((uint8_t *)&t472))[0] != 0)) {
                            gm_t76123gfet73g t473 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t472) + 8));
                            gm_tcfa6l7reoosn t474;
                            GM_ST(uint8_t, (uint8_t *)&t474 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t474 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t474 + 4, 0);
                            ((uint8_t *)&t474)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t474 + 8), t473);
                            F.ret = t474;
                            goto gm_fret;
                        }
                        gm_b0_1 t475 = GM_LD(gm_b0_1, (((uint8_t *)&t472) + 8));
                    }
                    goto ix436;
ie436:;
ix436:;
                }
                {
                    gm_b0_1 t476;
                    gm_tcfa6l7reoosn t477;
                    GM_ST(uint8_t, (uint8_t *)&t477 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t477 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t477 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t477 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t477 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t477 + 24, 0);
                    ((uint8_t *)&t477)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t477 + 8), t476);
                    gm_tcfa6l7reoosn t478 = t477;
                    uint8_t t479 = ((((uint8_t *)&t478))[0] != 0);
                    F.ret = t477;
                    goto gm_fret;
                }
                goto ix407;
ie407:;
ix407:;
            }
            {
                void * t480 = gm_l0;
                gm_t88rcucg1b7uk t481 = gm_l2;
                void * t482 = gm_f_adt_of_8k5vqud(t480, GM_CONV(gm_csguankko4lmhd, t481));
                void * t483 = t482;
                void * t484;
                if ((GM_LD(void *, ((uint8_t *)&t483)) != 0)) {
                    void * t485 = GM_LD(void *, (((uint8_t *)&t483) + 0));
                    t484 = t485;
                } else {
                    void * t486 = gm_l0;
                    gm_tl874t2atnlh3 t487 = gm_l1;
                    void * t488 = gm_l0;
                    void * t489 = GM_LD(void *, (((uint8_t *)t488) + 8));
                    gm_t88rcucg1b7uk t490 = gm_l2;
                    gm_b16_8 t491 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t489, t490);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t492;
                    GM_ST(gm_b16_8, ((uint8_t *)&t492 + 0), t491);
                    gm_tabb710d9mq2e t493;
                    gm_f_format_h8e7ps1((void *)&t493, (void *)NULL, cx, NULL, t492);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t494 = t493;
                    if (((((uint8_t *)&t494))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t494) + 8, gm_srcl8pn2pjtmj6k, 494, 56);
                    gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&t494) + 8));
                    gm_b16_8 t496 = t495;
                    gm_b16_8 t497 = GM_LD(gm_b16_8, ((uint8_t *)&t496));
                    gm_t76123gfet73g t498;
                    gm_f_trap_qh84l9d((void *)&t498, (void *)NULL, cx, NULL, t486, t487, t497);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t499;
                    GM_ST(uint8_t, (uint8_t *)&t499 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t499 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t499 + 4, 0);
                    ((uint8_t *)&t499)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t499 + 8), t498);
                    gm_tcfa6l7reoosn t500 = t499;
                    uint8_t t501 = ((((uint8_t *)&t500))[0] != 0);
                    F.ret = t499;
                    goto gm_fret;
                }
                gm_l30 = t484;
            }
            {
                void * t503 = gm_l30;
                uint8_t t504 = GM_LD(uint8_t, ((((uint8_t *)t503) + 24) + 0));
                if (t504 != ((uint8_t)0ULL)) goto ie502;
                gm_tpautr4br7h3l t505 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t503) + 24) + 8));
                F.l31 = t505;
                {
                    {
                        gm_l33 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t508 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                        gm_l32 = t508;
                    }
                    {
                        for (;;) {
                            uint64_t t510 = gm_l33;
                            uint64_t t511 = gm_l32;
                            uint8_t t512 = (t510 < t511);
                            if (!t512) goto lb509;
                            {
                                uint64_t t515 = gm_l33;
                                uint64_t t516 = t515;
                                uint64_t t519 = GM_LD(uint64_t, ((uint8_t *)&t516));
                                gm_l34 = t519;
                                {
                                    void * t521 = gm_l0;
                                    gm_t88rcucg1b7uk t522 = gm_l2;
                                    uint64_t t523 = gm_l34;
                                    uint32_t t524 = (uint32_t)t523;
                                    gm_tfbnciphf3261 t525 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t521, t522, t524);
                                    if (gm_unw.kind) {
                                        goto gm_ll513;
                                    }
                                    gm_l35 = t525;
                                }
                                {
                                    uint64_t t526 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                    uint64_t t527 = gm_f_us_cuv01fp(t526);
                                    gm_l36 = t527;
                                }
                                {
                                    void * t528 = gm_l0;
                                    gm_tl874t2atnlh3 t529 = gm_l1;
                                    gm_t88rcucg1b7uk t530 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 0));
                                    gm_b16_8 t531 = gm_l3;
                                    gm_b16_8 t532 = t531;
                                    uint64_t t533 = GM_LD(uintptr_t, ((uint8_t *)&t532) + 8);
                                    void * t534 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t532)));
                                    uint64_t t535 = gm_l36;
                                    uint64_t t536 = (uintptr_t)t535;
                                    uint64_t t537 = gm_l36;
                                    void * t538 = gm_l0;
                                    gm_t88rcucg1b7uk t539 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 0));
                                    uint64_t t540 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t538, t539);
                                    if (gm_unw.kind) {
                                        goto gm_ll513;
                                    }
                                    uint64_t t541 = gm_f_us_cuv01fp(t540);
                                    uint64_t r542; bool o543 = __builtin_add_overflow(t537, t541, &r542);
                                    if (o543) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 499, 52);
                                    uint64_t t544 = r542;
                                    uint64_t t545 = (uintptr_t)t544;
                                    if (t536 > t545 || t545 > (uintptr_t)t533) gm_trap_slice(t536, t545, t533, gm_srcl8pn2pjtmj6k, 499, 45);
                                    gm_b16_8 t546;
                                    GM_ST(void *, (uint8_t *)&t546, (void *)(((uint8_t *)t534 + t536 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t546 + 8, (uintptr_t)(t545 - t536));
                                    void * t547 = gm_l4;
                                    gm_tcfa6l7reoosn t548;
                                    gm_f_hash_val_nr2e99r((void *)&t548, (void *)NULL, cx, NULL, t528, t529, t530, t546, t547);
                                    if (gm_unw.kind) {
                                        goto gm_ll513;
                                    }
                                    gm_tcfa6l7reoosn t549 = t548;
                                    if (((((uint8_t *)&t549))[0] != 0)) {
                                        gm_t76123gfet73g t550 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t549) + 8));
                                        gm_tcfa6l7reoosn t551;
                                        GM_ST(uint8_t, (uint8_t *)&t551 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t551 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t551 + 4, 0);
                                        ((uint8_t *)&t551)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t551 + 8), t550);
                                        F.ret = t551;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t552 = GM_LD(gm_b0_1, (((uint8_t *)&t549) + 8));
                                }
                                goto me517;
mn518:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 496, 13);
me517:;
                            }
lc509:;
                            {
                                uint64_t t554 = gm_l33;
                                uint64_t r555; bool o556 = __builtin_add_overflow(t554, ((uint64_t)1ULL), &r555);
                                uint64_t t557 = r555;
                                gm_l33 = t557;
                            }
                        }
gm_ll513:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb509; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc509; }
                        }
                        goto gm_lad;
lb509:;
                    }
                }
                goto ix502;
ie502:;
                {
                    void * t559 = gm_l0;
                    gm_tl874t2atnlh3 t560 = gm_l1;
                    void * t561 = gm_l0;
                    void * t562 = GM_LD(void *, (((uint8_t *)t561) + 8));
                    gm_t88rcucg1b7uk t563 = gm_l2;
                    gm_b16_8 t564 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t562, t563);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t565;
                    GM_ST(gm_b16_8, ((uint8_t *)&t565 + 0), t564);
                    gm_tabb710d9mq2e t566;
                    gm_f_format_8k5eh95((void *)&t566, (void *)NULL, cx, NULL, t565);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t567 = t566;
                    if (((((uint8_t *)&t567))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t567) + 8, gm_srcl8pn2pjtmj6k, 495, 62);
                    gm_b16_8 t568 = GM_LD(gm_b16_8, (((uint8_t *)&t567) + 8));
                    gm_b16_8 t569 = t568;
                    gm_b16_8 t570 = GM_LD(gm_b16_8, ((uint8_t *)&t569));
                    gm_t76123gfet73g t571;
                    gm_f_trap_qh84l9d((void *)&t571, (void *)NULL, cx, NULL, t559, t560, t570);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t572;
                    GM_ST(uint8_t, (uint8_t *)&t572 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t572 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t572 + 4, 0);
                    ((uint8_t *)&t572)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t572 + 8), t571);
                    gm_tcfa6l7reoosn t573 = t572;
                    uint8_t t574 = ((((uint8_t *)&t573))[0] != 0);
                    F.ret = t572;
                    goto gm_fret;
                }
ix502:;
            }
            goto me7;
mn383:;
ma19:;
            void * t576 = gm_l0;
            gm_tl874t2atnlh3 t577 = gm_l1;
            void * t578 = gm_l0;
            void * t579 = GM_LD(void *, (((uint8_t *)t578) + 8));
            gm_t88rcucg1b7uk t580 = gm_l2;
            gm_b16_8 t581 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t579, t580);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t582;
            GM_ST(gm_b16_8, ((uint8_t *)&t582 + 0), t581);
            gm_tabb710d9mq2e t583;
            gm_f_format_8k5eh95((void *)&t583, (void *)NULL, cx, NULL, t582);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t584 = t583;
            if (((((uint8_t *)&t584))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t584) + 8, gm_srcl8pn2pjtmj6k, 502, 35);
            gm_b16_8 t585 = GM_LD(gm_b16_8, (((uint8_t *)&t584) + 8));
            gm_b16_8 t586 = t585;
            gm_b16_8 t587 = GM_LD(gm_b16_8, ((uint8_t *)&t586));
            gm_t76123gfet73g t588;
            gm_f_trap_qh84l9d((void *)&t588, (void *)NULL, cx, NULL, t576, t577, t587);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t589;
            GM_ST(uint8_t, (uint8_t *)&t589 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t589 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t589 + 4, 0);
            ((uint8_t *)&t589)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t589 + 8), t588);
            gm_tcfa6l7reoosn t590 = t589;
            uint8_t t591 = ((((uint8_t *)&t590))[0] != 0);
            F.ret = t589;
            goto gm_fret;
            goto me7;
mn575:;
ma20:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 432, 5);
me7:;
        }
    }
    gm_b0_1 t592;
    gm_tcfa6l7reoosn t593;
    GM_ST(uint8_t, (uint8_t *)&t593 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t593 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t593 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t593 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t593 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t593 + 24, 0);
    ((uint8_t *)&t593)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t593 + 8), t592);
    gm_tcfa6l7reoosn t594 = t593;
    F.ret = t594;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_int_text_vrjnuu2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    gm_ttovhfee8mf28 gm_l4; /* info */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_tg5lql6itufk7 t8 = gm_f_int_info_ulcevdv(t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_tg5lql6itufk7 t9 = t8;
        gm_ttovhfee8mf28 t10;
        if (((((uint8_t *)&t9))[8] != 0)) {
            gm_ttovhfee8mf28 t11 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l4 = t10;
    }
    {
        gm_b16_8 t14;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (!t16) goto ie15;
        unsigned __int128 t18 = gm_l3;
        unsigned __int128 t19 = t18;
        __int128 t20;
        memset(&t20, 0, sizeof t20); memcpy(&t20, ((uint8_t *)&t19), sizeof t20 < 16 ? sizeof t20 : 16);
        gm_tpl3q6e8s9qo1 t21;
        GM_ST(__int128, ((uint8_t *)&t21 + 0), t20);
        gm_tabb710d9mq2e t22;
        gm_f_format_9qvncot((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcl8pn2pjtmj6k, 185, 29);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_b16_8 t25 = t24;
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        t14 = t27;
        goto ix15;
ie15:;
        unsigned __int128 t29 = gm_l3;
        gm_tsn2omssg6cpf t30;
        GM_ST(unsigned __int128, ((uint8_t *)&t30 + 0), t29);
        gm_tabb710d9mq2e t31;
        gm_f_format_ra7pont((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcl8pn2pjtmj6k, 185, 70);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        t14 = t36;
ix15:;
        uint8_t t37 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_align_lavuopj F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK(&F, 268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)8ULL)) goto pa14;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l2 = t16;
        goto po13;
pa14:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t18 != ((uint8_t)9ULL)) goto pa17;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l2 = t19;
        goto po13;
pa17:;
        goto mn12;
po13:;
        uint64_t t20;
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l2;
        uint64_t t24 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
        if (!t25) goto ie21;
        uint64_t t27 = ((uint64_t)1ULL);
        t20 = t27;
        goto ix21;
ie21:;
        void * t29 = gm_l0;
        gm_t88rcucg1b7uk t30 = gm_l2;
        uint64_t t31 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t32 = t31;
        t20 = t32;
ix21:;
        uint8_t t33 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me6;
mn12:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t35 != ((uint8_t)13ULL)) goto mn34;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l4 = t36;
        void * t37 = gm_l0;
        gm_t88rcucg1b7uk t38 = gm_l4;
        uint64_t t39 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t40 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me6;
mn34:;
ma9:;
        uint8_t t42 = 0;
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
        goto me6;
mn41:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 174, 5);
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
