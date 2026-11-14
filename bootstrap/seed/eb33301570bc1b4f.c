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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
typedef struct { gm_tbccvikg6b96e f0; uint8_t f1; uint8_t pe[7]; } gm_ttfi3b3ccbpes;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_ttfi3b3ccbpes) == 40 && _Alignof(gm_ttfi3b3ccbpes) == 8 && offsetof(gm_ttfi3b3ccbpes, f0) == 0 && offsetof(gm_ttfi3b3ccbpes, f1) == 32, "layout of `?AllocTag`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_memory_builtin_9s47dk8 {
    gm_frame h;
    gm_tpekhfs32op0f l36; /* e */
    gm_tpekhfs32op0f l39; /* e */
    gm_tpekhfs32op0f l42; /* e */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d613p2h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e7344u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_p0ej7hu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u3ioj90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_get_hd4d6j0(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_64ej4om(void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_remove_gjirbtf(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_at_ktabiji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckh5e429gka[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1gjt02j7um1i[37];
extern const uint8_t gm_s2qp8mqi0c2le[48];
extern const uint8_t gm_s8pst1ovjhtbk[71];
extern const uint8_t gm_sp0ag7curvl4o[42];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srccp2mbkjs1vn4[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const gm_type gm_tyo86ks9qkvt;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
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
    if (t6 > t10 || t10 > (uintptr_t)t4) gm_trap_slice(t6, t10, t4, gm_srcnqtpfma6qe4a, 106, 67);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + t6 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t6));
    uint64_t t12 = gm_l2;
    gm_f_put_u64_irp77r2(t1, t11, t12);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)8ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            uint64_t t8 = gm_l2;
            gm_f_wr_word_npte9fs(t6, t7, t8);
        }
        goto ix2;
ie2:;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l1;
            uint64_t t12 = gm_l2;
            unsigned __int128 t13 = (unsigned __int128)t12;
            gm_f_put_uint_cddin99(t10, t11, t13);
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)0));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)0));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
    GM_STACK_CHECK_SHALLOW(&F, 569u);
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
        void * t28 = t27;
        void * t29;
        if ((GM_LD(void *, ((uint8_t *)&t28)) != 0)) {
            void * t30 = GM_LD(void *, (((uint8_t *)&t28) + 0));
            t29 = t30;
        } else {
            uint8_t t31 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t29;
    }
    {
        uint64_t t32 = gm_l1;
        void * t33 = gm_l5;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
        uint64_t r35; bool o36 = __builtin_sub_overflow(t32, t34, &r35);
        if (o36) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t37 = r35;
        gm_l7 = t37;
    }
    {
        uint64_t t38 = gm_l7;
        void * t39 = gm_l5;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
        unsigned __int128 n41 = (unsigned __int128)t40;
        if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t42 = (uint64_t)(t38 >> (int)n41);
        gm_l8 = t42;
    }
    {
        void * t43 = gm_l6;
        void * t44 = t43;
        uint64_t t45 = gm_l8;
        uint64_t t46 = gm_f_us_cuv01fp(t45);
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = (void *)((uint8_t *)t44 + t47 * 24);
        gm_l9 = t48;
    }
    {
        uint64_t t49 = gm_l7;
        uint64_t t50 = gm_l8;
        void * t51 = gm_l5;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
        unsigned __int128 n53 = (unsigned __int128)t52;
        if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r54 = (uint64_t)((uint64_t)t50 << (int)n53);
        bool o55 = (uint64_t)(r54 >> (int)n53) != t50;
        if (o55) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t56 = r54;
        uint64_t r57; bool o58 = __builtin_sub_overflow(t49, t56, &r57);
        if (o58) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t59 = r57;
        gm_l10 = t59;
    }
    {
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        uint8_t t63 = 0;
        void * t64 = gm_l9;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t64) + 17));
        uint8_t t66 = (uint8_t)!t65;
        if (!t66) {
            uint64_t t67 = gm_l2;
            void * t68 = gm_l9;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
            uint8_t t70 = (t67 > t69);
            t63 = t70;
        } else t63 = 1;
        if (!t63) {
            uint64_t t71 = gm_l10;
            void * t72 = gm_l9;
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 8));
            uint64_t t74 = gm_l2;
            uint64_t r75; bool o76 = __builtin_sub_overflow(t73, t74, &r75);
            if (o76) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t77 = r75;
            uint8_t t78 = (t71 > t77);
            t62 = t78;
        } else t62 = 1;
        if (!t62) {
            uint8_t t79 = 0;
            uint8_t t80 = gm_l3;
            if (t80) {
                void * t81 = gm_l9;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t81) + 18));
                uint8_t t83 = (uint8_t)!t82;
                t79 = t83;
            } else t79 = 0;
            t61 = t79;
        } else t61 = 1;
        if (!t61) goto ie60;
        {
            uint8_t t85 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        void * t86 = gm_l9;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = t87;
        uint64_t t89 = gm_l10;
        uint64_t t90 = gm_f_us_cuv01fp(t89);
        uint64_t t91 = (uintptr_t)t90;
        void * t92 = (void *)((uint8_t *)t88 + t91 * 1);
        void * t93 = t92;
        uint8_t t94 = 0;
        F.ret = t93;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
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
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 329, 13);
                uint64_t t12 = gm_l2;
                uint8_t t13 = (uint8_t)t12;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1), t13);
            }
            {
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 330, 13);
                uint64_t t16 = gm_l2;
                unsigned __int128 n17 = (unsigned __int128)((uint64_t)8ULL);
                if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcnqtpfma6qe4a, 330, 21);
                uint64_t t18 = (uint64_t)(t16 >> (int)n17);
                uint8_t t19 = (uint8_t)t18;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1), t19);
            }
            {
                uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcnqtpfma6qe4a, 331, 13);
                uint64_t t22 = gm_l2;
                unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
                if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 331, 21);
                uint64_t t24 = (uint64_t)(t22 >> (int)n23);
                uint8_t t25 = (uint8_t)t24;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 1), t25);
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcnqtpfma6qe4a, 332, 13);
                uint64_t t28 = gm_l2;
                unsigned __int128 n29 = (unsigned __int128)((uint64_t)24ULL);
                if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcnqtpfma6qe4a, 332, 21);
                uint64_t t30 = (uint64_t)(t28 >> (int)n29);
                uint8_t t31 = (uint8_t)t30;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1), t31);
            }
            {
                uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 333, 13);
                uint64_t t34 = gm_l2;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)32ULL);
                if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_srcnqtpfma6qe4a, 333, 21);
                uint64_t t36 = (uint64_t)(t34 >> (int)n35);
                uint8_t t37 = (uint8_t)t36;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1), t37);
            }
            {
                uint64_t t38 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcnqtpfma6qe4a, 334, 13);
                uint64_t t40 = gm_l2;
                unsigned __int128 n41 = (unsigned __int128)((uint64_t)40ULL);
                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 334, 21);
                uint64_t t42 = (uint64_t)(t40 >> (int)n41);
                uint8_t t43 = (uint8_t)t42;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 1), t43);
            }
            {
                uint64_t t44 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 335, 13);
                uint64_t t46 = gm_l2;
                unsigned __int128 n47 = (unsigned __int128)((uint64_t)48ULL);
                if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcnqtpfma6qe4a, 335, 21);
                uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t44 * 1), t49);
            }
            {
                uint64_t t50 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 336, 13);
                uint64_t t52 = gm_l2;
                unsigned __int128 n53 = (unsigned __int128)((uint64_t)56ULL);
                if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcnqtpfma6qe4a, 336, 21);
                uint64_t t54 = (uint64_t)(t52 >> (int)n53);
                uint8_t t55 = (uint8_t)t54;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1), t55);
            }
            {
                gm_b0_1 t56;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 340, 13);
                uint64_t t64 = gm_l2;
                uint8_t t65 = (uint8_t)t64;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t62 * 1), t65);
            }
            {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t70 = ((t69 == ((uint64_t)4ULL)));
            if (!t70) goto ie68;
            {
                uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcnqtpfma6qe4a, 344, 13);
                uint64_t t74 = gm_l2;
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t72 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcnqtpfma6qe4a, 345, 13);
                uint64_t t78 = gm_l2;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)8ULL);
                if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 345, 21);
                uint64_t t80 = (uint64_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 346, 13);
                uint64_t t84 = gm_l2;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)16ULL);
                if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcnqtpfma6qe4a, 346, 21);
                uint64_t t86 = (uint64_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcnqtpfma6qe4a, 347, 13);
                uint64_t t90 = gm_l2;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)24ULL);
                if (n91 >= 64) gm_trap_shift((uint64_t)n91, "u64", gm_srcnqtpfma6qe4a, 347, 21);
                uint64_t t92 = (uint64_t)(t90 >> (int)n91);
                uint8_t t93 = (uint8_t)t92;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t88 * 1), t93);
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t96 = gm_l0;
        gm_b16_8 t97 = gm_l1;
        uint64_t t98 = gm_l2;
        unsigned __int128 t99 = (unsigned __int128)t98;
        gm_f_put_uint_cddin99(t96, t97, t99);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)1));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)1));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6) {
    struct gm_fr_memory_builtin_9s47dk8 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* dst */
    uint64_t gm_l7; /* size */
    uint64_t gm_l8; /* al */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* d */
    gm_t2doi72kmmlil gm_l11; /* pb */
    gm_b16_8 gm_l12; /* p */
    uint64_t gm_l13; /* p */
    uint64_t gm_l14; /* size */
    gm_tilak255t5m34 gm_l15; /* d */
    uint64_t gm_l16; /* p */
    uint64_t gm_l17; /* owner */
    uint64_t gm_l18; /* vt */
    gm_b16_8 gm_l19; /* file */
    gm_b16_8 gm_l20; /* site */
    gm_tbccvikg6b96e gm_l21; /* tag */
    uint64_t gm_l22; /* addr */
    gm_t88rcucg1b7uk gm_l23; /* ty */
    uint64_t gm_l24; /* al */
    gm_b16_8 gm_l25; /* old */
    gm_t9qkh0rsooc8d gm_l26; /* rmw */
    unsigned __int128 gm_l27; /* x */
    unsigned __int128 gm_l28; /* y */
    uint8_t gm_l29; /* signed */
    unsigned __int128 gm_l30; /* r */
    gm_b16_8 gm_l31; /* nb */
    gm_t88rcucg1b7uk gm_l32; /* elem */
    gm_t2lqu8jsj511d gm_l33; /* d */
    gm_t2lqu8jsj511d gm_l34; /* s */
    uint64_t gm_l35; /* n */
    gm_b16_8 gm_l37; /* src */
    gm_b16_8 gm_l38; /* tmp */
    uint64_t gm_l40; /* p */
    uint64_t gm_l41; /* n */
    gm_b16_8 gm_l43; /* b */
    gm_t88rcucg1b7uk gm_l44; /* vt */
    gm_t2lqu8jsj511d gm_l45; /* sp */
    uint64_t gm_l46; /* n */
    gm_t88rcucg1b7uk gm_l47; /* lane */
    gm_b16_8 gm_l48; /* what */
    uint64_t gm_l49; /* al */
    GM_STACK_CHECK(&F, 10929u);
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
            switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma12; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma12; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma11; case ((uint8_t)59ULL): goto ma11; case ((uint8_t)60ULL): goto ma10; case ((uint8_t)61ULL): goto ma12; case ((uint8_t)62ULL): goto ma12; case ((uint8_t)63ULL): goto ma12; case ((uint8_t)64ULL): goto ma7; case ((uint8_t)65ULL): goto ma7; case ((uint8_t)66ULL): goto ma7; case ((uint8_t)67ULL): goto ma7; case ((uint8_t)68ULL): goto ma7; case ((uint8_t)69ULL): goto ma8; case ((uint8_t)70ULL): goto ma7; case ((uint8_t)71ULL): goto ma7; case ((uint8_t)72ULL): goto ma12; case ((uint8_t)73ULL): goto ma12; case ((uint8_t)74ULL): goto ma4; case ((uint8_t)75ULL): goto ma5; case ((uint8_t)76ULL): goto ma6; case ((uint8_t)77ULL): goto ma6; case ((uint8_t)78ULL): goto ma9; case ((uint8_t)79ULL): goto ma12; case ((uint8_t)80ULL): goto ma12; case ((uint8_t)81ULL): goto ma12; default: goto ma12; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)74ULL)) goto mn15;
            {
                void * t18 = gm_l0;
                uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srccp2mbkjs1vn4, 12, 31);
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 16));
                gm_b16_8 t22 = t21;
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                uint64_t t24 = gm_f_get_u64_tgpb2vh(t18, t23);
                gm_l7 = t24;
            }
            {
                void * t25 = gm_l0;
                uint64_t t26 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srccp2mbkjs1vn4, 13, 29);
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t26 * 16));
                gm_b16_8 t29 = t28;
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                uint64_t t31 = gm_f_get_u64_tgpb2vh(t25, t30);
                uint64_t t32 = (t31 > ((uint64_t)1ULL) ? t31 : ((uint64_t)1ULL));
                gm_l8 = t32;
            }
            {
                uint64_t t34 = gm_l8;
                uint64_t t35 = gm_l8;
                uint64_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint64_t)1ULL), &r36);
                if (o37) gm_trap("integer overflow in `-`", gm_srccp2mbkjs1vn4, 14, 22);
                uint64_t t38 = r36;
                uint64_t t39 = (uint64_t)(t34 & t38);
                uint8_t t40 = (!(t39 == ((uint64_t)0ULL)));
                if (!t40) goto ie33;
                {
                    void * t42 = gm_l0;
                    gm_tl874t2atnlh3 t43 = gm_l1;
                    uint64_t t44 = gm_l8;
                    gm_t4ce0jd3tdro4 t45;
                    GM_ST(uint64_t, ((uint8_t *)&t45 + 0), t44);
                    gm_tabb710d9mq2e t46;
                    gm_f_format_jc3ftg7((void *)&t46, (void *)NULL, cx, NULL, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t47 = t46;
                    if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srccp2mbkjs1vn4, 14, 62);
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
                    gm_b16_8 t49 = t48;
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                    gm_t76123gfet73g t51;
                    gm_f_trap_qh84l9d((void *)&t51, (void *)NULL, cx, NULL, t42, t43, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t52;
                    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                    ((uint8_t *)&t52)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t52 + 8), t51);
                    gm_tcfa6l7reoosn t53 = t52;
                    uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                uint64_t t56 = gm_l8;
                uint64_t t57 = GM_LD(uint64_t, gm_ckh5e429gka);
                uint8_t t58 = (t56 > t57);
                if (!t58) goto ie55;
                {
                    gm_b16_8 t60 = gm_l6;
                    gm_f_fill_abmf5g5(t60, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t61;
                    gm_tcfa6l7reoosn t62;
                    GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t62 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t62 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t62 + 24, 0);
                    ((uint8_t *)&t62)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t62 + 8), t61);
                    gm_tcfa6l7reoosn t63 = t62;
                    uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
                    F.ret = t62;
                    goto gm_fret;
                }
                goto ix55;
ie55:;
ix55:;
            }
            {
                void * t65 = gm_l0;
                void * t66 = (void *)(((uint8_t *)t65) + 184);
                uint64_t t67 = gm_l7;
                uint64_t t68 = gm_l8;
                gm_t9qkh0rsooc8d t69;
                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)2ULL));
                gm_t3j6egd6cfnk5 t70;
                gm_f_alloc_bfo8187((void *)&t70, (void *)NULL, cx, NULL, t66, t67, t68, t69, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3j6egd6cfnk5 t71 = t70;
                uint64_t t72;
                if (!((((uint8_t *)&t71))[0] != 0)) {
                    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&t71) + 8));
                    t72 = t73;
                } else {
                    {
                        gm_b16_8 t75 = gm_l6;
                        gm_f_fill_abmf5g5(t75, ((uint8_t)0ULL));
                    }
                    gm_b0_1 t76;
                    gm_tcfa6l7reoosn t77;
                    GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t77 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t77 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t77 + 24, 0);
                    ((uint8_t *)&t77)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t77 + 8), t76);
                    gm_tcfa6l7reoosn t78 = t77;
                    uint8_t t79 = ((((uint8_t *)&t78))[0] != 0);
                    F.ret = t77;
                    goto gm_fret;
                }
                gm_l9 = t72;
            }
            {
                void * t81 = gm_l0;
                uint8_t t82 = ((GM_LD(uint8_t, (((uint8_t *)t81) + 88) + 0) == ((uint8_t)0ULL)));
                if (!t82) goto ie80;
                uint64_t t83 = gm_l7;
                uint8_t t84 = (t83 > ((uint64_t)0ULL));
                if (!t84) goto ie80;
                {
                    void * t86 = gm_l0;
                    void * t87 = (void *)(((uint8_t *)t86) + 184);
                    uint64_t t88 = gm_l9;
                    uint64_t t89 = gm_l7;
                    gm_t934k1j7u70ct t90;
                    gm_f_write_view_pebit6h((void *)&t90, t87, t88, t89, ((uint64_t)1ULL));
                    gm_t934k1j7u70ct t91 = t90;
                    gm_b16_8 t92;
                    if (!((((uint8_t *)&t91))[0] != 0)) {
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                        t92 = t93;
                    } else {
                        gm_b0_1 t94;
                        gm_tcfa6l7reoosn t95;
                        GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t95 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t95 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t95 + 24, 0);
                        ((uint8_t *)&t95)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t95 + 8), t94);
                        gm_tcfa6l7reoosn t96 = t95;
                        uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
                        F.ret = t95;
                        goto gm_fret;
                    }
                    gm_l10 = t92;
                }
                {
                    gm_b16_8 t98 = gm_l10;
                    gm_f_fill_abmf5g5(t98, ((uint8_t)170ULL));
                }
                goto ix80;
ie80:;
ix80:;
            }
            {
                gm_t2doi72kmmlil t99;
                GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 1), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 2), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 3), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 4), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 5), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 6), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 7), ((uint8_t)0ULL));
                gm_l11 = t99;
            }
            {
                void * t100 = ((uint8_t *)&gm_l11);
                uint64_t t101 = (uintptr_t)((uint64_t)0ULL);
                void * t102 = gm_l0;
                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 176));
                uint64_t t104 = gm_f_us_cuv01fp(t103);
                uint64_t t105 = (uintptr_t)t104;
                if (t101 > t105 || t105 > (uintptr_t)8) gm_trap_slice(t101, t105, 8, gm_srccp2mbkjs1vn4, 32, 18);
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(((uint8_t *)t100 + t101 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(t105 - t101));
                gm_l12 = t106;
            }
            {
                void * t107 = gm_l0;
                gm_b16_8 t108 = gm_l12;
                uint64_t t109 = gm_l9;
                gm_f_put_ptr_188p3mm(t107, t108, t109);
            }
            {
                void * t110 = gm_l0;
                gm_t88rcucg1b7uk t111 = gm_l5;
                gm_b16_8 t112 = gm_l6;
                gm_b16_8 t113 = gm_l12;
                gm_b16_8 t114 = t113;
                gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                gm_b0_1 t116 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t110, t111, t112, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t118 != ((uint8_t)75ULL)) goto mn117;
            {
                void * t120 = gm_l0;
                uint64_t t121 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_srccp2mbkjs1vn4, 37, 27);
                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t121 * 16));
                gm_b16_8 t124 = t123;
                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                uint64_t t126 = gm_f_ptr_of_b77b4c9(t120, t125);
                gm_l13 = t126;
            }
            {
                void * t127 = gm_l0;
                uint64_t t128 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srccp2mbkjs1vn4, 38, 31);
                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t128 * 16));
                gm_b16_8 t131 = t130;
                gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                uint64_t t133 = gm_f_get_u64_tgpb2vh(t127, t132);
                gm_l14 = t133;
            }
            {
                void * t134 = gm_l0;
                void * t135 = (void *)(((uint8_t *)t134) + 184);
                uint64_t t136 = gm_l13;
                gm_tqvpud5umqqp1 t137;
                gm_f_describe_t62jhnt((void *)&t137, t135, t136);
                gm_tqvpud5umqqp1 t138 = t137;
                gm_tilak255t5m34 t139;
                if (((((uint8_t *)&t138))[24] != 0)) {
                    gm_tilak255t5m34 t140 = GM_LD(gm_tilak255t5m34, (((uint8_t *)&t138) + 0));
                    t139 = t140;
                } else {
                    void * t141 = gm_l0;
                    gm_tl874t2atnlh3 t142 = gm_l1;
                    uint64_t t143 = gm_l13;
                    uint64_t t144 = gm_l14;
                    gm_t2lqu8jsj511d t145;
                    GM_ST(uint64_t, ((uint8_t *)&t145 + 0), t143);
                    GM_ST(uint64_t, ((uint8_t *)&t145 + 8), t144);
                    gm_tabb710d9mq2e t146;
                    gm_f_format_e7344u2((void *)&t146, (void *)NULL, cx, NULL, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t147 = t146;
                    if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 8, gm_srccp2mbkjs1vn4, 39, 62);
                    gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&t147) + 8));
                    gm_b16_8 t149 = t148;
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                    gm_t76123gfet73g t151;
                    gm_f_trap_qh84l9d((void *)&t151, (void *)NULL, cx, NULL, t141, t142, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t152;
                    GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                    ((uint8_t *)&t152)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t152 + 8), t151);
                    gm_tcfa6l7reoosn t153 = t152;
                    uint8_t t154 = ((((uint8_t *)&t153))[0] != 0);
                    F.ret = t152;
                    goto gm_fret;
                }
                gm_l15 = t139;
            }
            {
                uint8_t t156 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t156) goto ie155;
                uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                if (!t157) goto ie155;
                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                uint64_t t159 = gm_l13;
                uint8_t t160 = ((t158 == t159));
                if (!t160) goto ie155;
                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint64_t t162 = gm_l14;
                uint8_t t163 = ((t161 == t162));
                if (!t163) goto ie155;
                {
                    {
                        void * t166 = gm_l0;
                        void * t167 = (void *)(((uint8_t *)t166) + 184);
                        uint64_t t168 = gm_l13;
                        uint8_t t169 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t167, t168);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                {
                    gm_b0_1 t170;
                    gm_tcfa6l7reoosn t171;
                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t171 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t171 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t171 + 24, 0);
                    ((uint8_t *)&t171)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t171 + 8), t170);
                    gm_tcfa6l7reoosn t172 = t171;
                    uint8_t t173 = ((((uint8_t *)&t172))[0] != 0);
                    F.ret = t171;
                    goto gm_fret;
                }
                goto ix155;
ie155:;
ix155:;
            }
            {
                uint8_t t175 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t175) goto ie174;
                uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                uint8_t t177 = (uint8_t)!t176;
                if (!t177) goto ie174;
                {
                    void * t179 = gm_l0;
                    gm_tl874t2atnlh3 t180 = gm_l1;
                    uint64_t t181 = gm_l13;
                    gm_t4ce0jd3tdro4 t182;
                    GM_ST(uint64_t, ((uint8_t *)&t182 + 0), t181);
                    gm_tabb710d9mq2e t183;
                    gm_f_format_u3ioj90((void *)&t183, (void *)NULL, cx, NULL, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t184 = t183;
                    if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 8, gm_srccp2mbkjs1vn4, 44, 70);
                    gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&t184) + 8));
                    gm_b16_8 t186 = t185;
                    gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                    gm_t76123gfet73g t188;
                    gm_f_trap_qh84l9d((void *)&t188, (void *)NULL, cx, NULL, t179, t180, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t189;
                    GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                    ((uint8_t *)&t189)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t189 + 8), t188);
                    gm_tcfa6l7reoosn t190 = t189;
                    uint8_t t191 = ((((uint8_t *)&t190))[0] != 0);
                    F.ret = t189;
                    goto gm_fret;
                }
                goto ix174;
ie174:;
ix174:;
            }
            {
                uint8_t t193 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t193) goto ie192;
                uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                uint64_t t195 = gm_l13;
                uint8_t t196 = ((t194 == t195));
                if (!t196) goto ie192;
                {
                    void * t198 = gm_l0;
                    gm_tl874t2atnlh3 t199 = gm_l1;
                    gm_tkpb4kplftgaa t200;
                    uint64_t t201 = gm_l13;
                    GM_ST(uint64_t, (uint8_t *)&t200 + 0, t201);
                    uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    GM_ST(uint64_t, (uint8_t *)&t200 + 8, t202);
                    uint64_t t203 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t200 + 16, t203);
                    gm_tabb710d9mq2e t204;
                    gm_f_format_p0ej7hu((void *)&t204, (void *)NULL, cx, NULL, &t200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t205 = t204;
                    if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_srccp2mbkjs1vn4, 46, 71);
                    gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
                    gm_b16_8 t207 = t206;
                    gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
                    gm_t76123gfet73g t209;
                    gm_f_trap_qh84l9d((void *)&t209, (void *)NULL, cx, NULL, t198, t199, t208);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                    ((uint8_t *)&t210)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t210 + 8), t209);
                    gm_tcfa6l7reoosn t211 = t210;
                    uint8_t t212 = ((((uint8_t *)&t211))[0] != 0);
                    F.ret = t210;
                    goto gm_fret;
                }
                goto ix192;
ie192:;
ix192:;
            }
            {
                void * t213 = gm_l0;
                gm_tl874t2atnlh3 t214 = gm_l1;
                uint64_t t215 = gm_l13;
                uint64_t t216 = gm_l14;
                gm_t2lqu8jsj511d t217;
                GM_ST(uint64_t, ((uint8_t *)&t217 + 0), t215);
                GM_ST(uint64_t, ((uint8_t *)&t217 + 8), t216);
                gm_tabb710d9mq2e t218;
                gm_f_format_e7344u2((void *)&t218, (void *)NULL, cx, NULL, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t219 = t218;
                if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_srccp2mbkjs1vn4, 47, 34);
                gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
                gm_b16_8 t221 = t220;
                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                gm_t76123gfet73g t223;
                gm_f_trap_qh84l9d((void *)&t223, (void *)NULL, cx, NULL, t213, t214, t222);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t224;
                GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t224 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t224 + 4, 0);
                ((uint8_t *)&t224)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t224 + 8), t223);
                gm_tcfa6l7reoosn t225 = t224;
                uint8_t t226 = ((((uint8_t *)&t225))[0] != 0);
                F.ret = t224;
                goto gm_fret;
            }
            goto me3;
mn117:;
ma6:;
            uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t230 != ((uint8_t)76ULL)) goto pa229;
            goto po228;
pa229:;
            uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t232 != ((uint8_t)77ULL)) goto pa231;
            goto po228;
pa231:;
            goto mn227;
po228:;
            {
                void * t235 = gm_l0;
                uint8_t t236 = (!(GM_LD(uint8_t, (((uint8_t *)t235) + 88) + 0) == ((uint8_t)0ULL)));
                if (!t236) goto ie234;
                {
                    gm_b0_1 t238;
                    gm_tcfa6l7reoosn t239;
                    GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t239 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t239 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t239 + 24, 0);
                    ((uint8_t *)&t239)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t239 + 8), t238);
                    gm_tcfa6l7reoosn t240 = t239;
                    uint8_t t241 = ((((uint8_t *)&t240))[0] != 0);
                    F.ret = t239;
                    goto gm_fret;
                }
                goto ix234;
ie234:;
ix234:;
            }
            {
                void * t242 = gm_l0;
                uint64_t t243 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t243 >= (uintptr_t)(t244)) gm_trap_index(t243, t244, gm_srccp2mbkjs1vn4, 51, 27);
                gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t243 * 16));
                gm_b16_8 t246 = t245;
                gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
                uint64_t t248 = gm_f_ptr_of_b77b4c9(t242, t247);
                gm_l16 = t248;
            }
            {
                void * t249 = gm_l0;
                uint64_t t250 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t251 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t250 >= (uintptr_t)(t251)) gm_trap_index(t250, t251, gm_srccp2mbkjs1vn4, 52, 31);
                gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t250 * 16));
                gm_b16_8 t253 = t252;
                gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
                uint64_t t255 = gm_f_ptr_of_b77b4c9(t249, t254);
                gm_l17 = t255;
            }
            {
                void * t256 = gm_l0;
                uint64_t t257 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t258 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t257 >= (uintptr_t)(t258)) gm_trap_index(t257, t258, gm_srccp2mbkjs1vn4, 53, 28);
                gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t257 * 16));
                gm_b16_8 t260 = t259;
                gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
                uint64_t t262 = gm_f_ptr_of_b77b4c9(t256, t261);
                gm_l18 = t262;
            }
            {
                void * t263 = gm_l0;
                gm_tl874t2atnlh3 t264 = gm_l1;
                uint64_t t265 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t265 >= (uintptr_t)(t266)) gm_trap_index(t265, t266, gm_srccp2mbkjs1vn4, 54, 45);
                gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t265 * 16));
                gm_b16_8 t268 = t267;
                gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
                gm_td0ept12e7q8b t270;
                gm_f_read_string_ttuk4gr((void *)&t270, (void *)NULL, cx, NULL, t263, t264, t269);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_td0ept12e7q8b t271 = t270;
                if (((((uint8_t *)&t271))[0] != 0)) {
                    gm_t76123gfet73g t272 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t271) + 8));
                    gm_tcfa6l7reoosn t273;
                    GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
                    ((uint8_t *)&t273)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t273 + 8), t272);
                    F.ret = t273;
                    goto gm_fret;
                }
                gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)&t271) + 8));
                gm_l19 = t274;
            }
            {
                gm_tgbgti82mf879 t275;
                gm_b16_8 t276 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t275 + 0, t276);
                void * t277 = gm_l0;
                uint64_t t278 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t278 >= (uintptr_t)(t279)) gm_trap_index(t278, t279, gm_srccp2mbkjs1vn4, 55, 60);
                gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t278 * 16));
                gm_b16_8 t281 = t280;
                gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                uint64_t t283 = gm_f_get_u64_tgpb2vh(t277, t282);
                GM_ST(uint64_t, (uint8_t *)&t275 + 16, t283);
                void * t284 = gm_l0;
                uint64_t t285 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t285 >= (uintptr_t)(t286)) gm_trap_index(t285, t286, gm_srccp2mbkjs1vn4, 55, 77);
                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t285 * 16));
                gm_b16_8 t288 = t287;
                gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
                uint64_t t290 = gm_f_get_u64_tgpb2vh(t284, t289);
                GM_ST(uint64_t, (uint8_t *)&t275 + 24, t290);
                gm_tabb710d9mq2e t291;
                gm_f_format_bv13gdl((void *)&t291, (void *)NULL, cx, NULL, &t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t292 = t291;
                if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 8, gm_srccp2mbkjs1vn4, 55, 21);
                gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&t292) + 8));
                gm_l20 = t293;
            }
            {
                uint8_t t295 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)76ULL)));
                if (!t295) goto ie294;
                {
                    void * t297 = gm_l0;
                    void * t298 = (void *)(((uint8_t *)t297) + 1296);
                    uint64_t t299 = gm_l16;
                    gm_tbccvikg6b96e t300;
                    uint64_t t301 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t300 + 0, t301);
                    uint64_t t302 = gm_l18;
                    GM_ST(uint64_t, (uint8_t *)&t300 + 8, t302);
                    gm_b16_8 t303 = gm_l20;
                    gm_b16_8 t304 = t303;
                    gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                    GM_ST(gm_b16_8, (uint8_t *)&t300 + 16, t305);
                    gm_tftb1odc739t5 t306 = gm_f_put_64ej4om(t298, t299, &t300);
                    gm_tftb1odc739t5 t307 = t306;
                    if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 1, gm_srccp2mbkjs1vn4, 57, 17);
                    gm_b0_1 t308 = GM_LD(gm_b0_1, (((uint8_t *)&t307) + 1));
                }
                {
                    gm_b0_1 t309;
                    gm_tcfa6l7reoosn t310;
                    GM_ST(uint8_t, (uint8_t *)&t310 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t310 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t310 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t310 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t310 + 24, 0);
                    ((uint8_t *)&t310)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t310 + 8), t309);
                    gm_tcfa6l7reoosn t311 = t310;
                    uint8_t t312 = ((((uint8_t *)&t311))[0] != 0);
                    F.ret = t310;
                    goto gm_fret;
                }
                goto ix294;
ie294:;
ix294:;
            }
            {
                void * t313 = gm_l0;
                void * t314 = (void *)(((uint8_t *)t313) + 1296);
                uint64_t t315 = gm_l16;
                gm_ttfi3b3ccbpes t316;
                gm_f_get_hd4d6j0((void *)&t316, t314, t315);
                gm_ttfi3b3ccbpes t317 = t316;
                gm_tbccvikg6b96e t318;
                if (((((uint8_t *)&t317))[32] != 0)) {
                    gm_tbccvikg6b96e t319 = GM_LD(gm_tbccvikg6b96e, (((uint8_t *)&t317) + 0));
                    t318 = t319;
                } else {
                    gm_b0_1 t320;
                    gm_tcfa6l7reoosn t321;
                    GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t321 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t321 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t321 + 24, 0);
                    ((uint8_t *)&t321)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t321 + 8), t320);
                    gm_tcfa6l7reoosn t322 = t321;
                    uint8_t t323 = ((((uint8_t *)&t322))[0] != 0);
                    F.ret = t321;
                    goto gm_fret;
                }
                gm_l21 = t318;
            }
            {
                {
                    void * t325 = gm_l0;
                    void * t326 = (void *)(((uint8_t *)t325) + 1296);
                    uint64_t t327 = gm_l16;
                    gm_ttfi3b3ccbpes t328;
                    gm_f_remove_gjirbtf((void *)&t328, t326, t327);
                }
            }
            {
                uint8_t t330 = 0;
                uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 0));
                uint64_t t332 = gm_l17;
                uint8_t t333 = (!(t331 == t332));
                if (!t333) {
                    uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t335 = gm_l18;
                    uint8_t t336 = (!(t334 == t335));
                    t330 = t336;
                } else t330 = 1;
                if (!t330) goto ie329;
                {
                    void * t338 = gm_l0;
                    gm_tl874t2atnlh3 t339 = gm_l1;
                    gm_t6j6smj7qoctu t340;
                    gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t340 + 0, t341);
                    gm_b16_8 t342 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t340 + 16, t342);
                    gm_tabb710d9mq2e t343;
                    gm_f_format_d613p2h((void *)&t343, (void *)NULL, cx, NULL, &t340);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t344 = t343;
                    if (((((uint8_t *)&t344))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t344) + 8, gm_srccp2mbkjs1vn4, 62, 82);
                    gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&t344) + 8));
                    gm_b16_8 t346 = t345;
                    gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                    gm_t76123gfet73g t348;
                    gm_f_trap_qh84l9d((void *)&t348, (void *)NULL, cx, NULL, t338, t339, t347);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t349;
                    GM_ST(uint8_t, (uint8_t *)&t349 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t349 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t349 + 4, 0);
                    ((uint8_t *)&t349)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t349 + 8), t348);
                    gm_tcfa6l7reoosn t350 = t349;
                    uint8_t t351 = ((((uint8_t *)&t350))[0] != 0);
                    F.ret = t349;
                    goto gm_fret;
                }
                goto ix329;
ie329:;
ix329:;
            }
            goto me3;
mn227:;
ma7:;
            uint8_t t355 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t355 != ((uint8_t)64ULL)) goto pa354;
            goto po353;
pa354:;
            uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t357 != ((uint8_t)65ULL)) goto pa356;
            goto po353;
pa356:;
            uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t359 != ((uint8_t)66ULL)) goto pa358;
            goto po353;
pa358:;
            uint8_t t361 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t361 != ((uint8_t)67ULL)) goto pa360;
            goto po353;
pa360:;
            uint8_t t363 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t363 != ((uint8_t)68ULL)) goto pa362;
            goto po353;
pa362:;
            uint8_t t365 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t365 != ((uint8_t)70ULL)) goto pa364;
            goto po353;
pa364:;
            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t367 != ((uint8_t)71ULL)) goto pa366;
            goto po353;
pa366:;
            goto mn352;
po353:;
            {
                void * t369 = gm_l0;
                uint64_t t370 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t370 >= (uintptr_t)(t371)) gm_trap_index(t370, t371, gm_srccp2mbkjs1vn4, 65, 30);
                gm_b16_8 t372 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t370 * 16));
                gm_b16_8 t373 = t372;
                gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&t373));
                uint64_t t375 = gm_f_ptr_of_b77b4c9(t369, t374);
                gm_l22 = t375;
            }
            {
                void * t377 = gm_l0;
                uint64_t t378 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t378 >= (uintptr_t)(t379)) gm_trap_index(t378, t379, gm_srccp2mbkjs1vn4, 66, 35);
                void * t380 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t378 * 8));
                gm_t88rcucg1b7uk t381 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t380) + 96));
                gm_t53at80pfdus3 t382;
                gm_f_kind_htv6093((void *)&t382, t377, GM_CONV(gm_csguankko4lmhd, t381));
                gm_t53at80pfdus3 t383 = t382;
                uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&t383) + 0));
                if (t384 != ((uint8_t)8ULL)) goto ie376;
                gm_t88rcucg1b7uk t385 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t383) + 8) + 4));
                gm_l23 = t385;
                {
                    void * t387 = gm_l0;
                    gm_t88rcucg1b7uk t388 = gm_l23;
                    uint64_t t389 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t387, t388);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t389;
                }
                {
                    void * t390 = gm_l0;
                    void * t391 = gm_l0;
                    gm_t88rcucg1b7uk t392 = gm_l23;
                    uint64_t t393 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t391, t392);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t394 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t390, t393);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t394;
                }
                {
                    void * t395 = gm_l0;
                    gm_tl874t2atnlh3 t396 = gm_l1;
                    uint64_t t397 = gm_l22;
                    uint64_t t398 = gm_l24;
                    gm_b16_8 t399 = gm_l25;
                    gm_tcfa6l7reoosn t400;
                    gm_f_load_at_6u81jcc((void *)&t400, (void *)NULL, cx, NULL, t395, t396, t397, t398, t399);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t401 = t400;
                    if (((((uint8_t *)&t401))[0] != 0)) {
                        gm_t76123gfet73g t402 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t401) + 8));
                        gm_tcfa6l7reoosn t403;
                        GM_ST(uint8_t, (uint8_t *)&t403 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t403 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t403 + 4, 0);
                        ((uint8_t *)&t403)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t403 + 8), t402);
                        F.ret = t403;
                        goto gm_fret;
                    }
                    gm_b0_1 t404 = GM_LD(gm_b0_1, (((uint8_t *)&t401) + 8));
                }
                {
                    uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t413) { case ((uint8_t)0ULL): goto ma411; case ((uint8_t)1ULL): goto ma411; case ((uint8_t)2ULL): goto ma411; case ((uint8_t)3ULL): goto ma411; case ((uint8_t)4ULL): goto ma411; case ((uint8_t)5ULL): goto ma411; case ((uint8_t)6ULL): goto ma411; case ((uint8_t)7ULL): goto ma411; case ((uint8_t)8ULL): goto ma411; case ((uint8_t)9ULL): goto ma411; case ((uint8_t)10ULL): goto ma411; case ((uint8_t)11ULL): goto ma411; case ((uint8_t)12ULL): goto ma411; case ((uint8_t)13ULL): goto ma411; case ((uint8_t)14ULL): goto ma411; case ((uint8_t)15ULL): goto ma411; case ((uint8_t)16ULL): goto ma411; case ((uint8_t)17ULL): goto ma411; case ((uint8_t)18ULL): goto ma411; case ((uint8_t)19ULL): goto ma411; case ((uint8_t)20ULL): goto ma411; case ((uint8_t)21ULL): goto ma411; case ((uint8_t)22ULL): goto ma411; case ((uint8_t)23ULL): goto ma411; case ((uint8_t)24ULL): goto ma411; case ((uint8_t)25ULL): goto ma411; case ((uint8_t)26ULL): goto ma411; case ((uint8_t)27ULL): goto ma411; case ((uint8_t)28ULL): goto ma411; case ((uint8_t)29ULL): goto ma411; case ((uint8_t)30ULL): goto ma411; case ((uint8_t)31ULL): goto ma411; case ((uint8_t)32ULL): goto ma411; case ((uint8_t)33ULL): goto ma411; case ((uint8_t)34ULL): goto ma411; case ((uint8_t)35ULL): goto ma411; case ((uint8_t)36ULL): goto ma411; case ((uint8_t)37ULL): goto ma411; case ((uint8_t)38ULL): goto ma411; case ((uint8_t)39ULL): goto ma411; case ((uint8_t)40ULL): goto ma411; case ((uint8_t)41ULL): goto ma411; case ((uint8_t)42ULL): goto ma411; case ((uint8_t)43ULL): goto ma411; case ((uint8_t)44ULL): goto ma411; case ((uint8_t)45ULL): goto ma411; case ((uint8_t)46ULL): goto ma411; case ((uint8_t)47ULL): goto ma411; case ((uint8_t)48ULL): goto ma411; case ((uint8_t)49ULL): goto ma411; case ((uint8_t)50ULL): goto ma411; case ((uint8_t)51ULL): goto ma411; case ((uint8_t)52ULL): goto ma411; case ((uint8_t)53ULL): goto ma411; case ((uint8_t)54ULL): goto ma411; case ((uint8_t)55ULL): goto ma411; case ((uint8_t)56ULL): goto ma411; case ((uint8_t)57ULL): goto ma411; case ((uint8_t)58ULL): goto ma411; case ((uint8_t)59ULL): goto ma411; case ((uint8_t)60ULL): goto ma411; case ((uint8_t)61ULL): goto ma411; case ((uint8_t)62ULL): goto ma411; case ((uint8_t)63ULL): goto ma411; case ((uint8_t)64ULL): goto ma406; case ((uint8_t)65ULL): goto ma407; case ((uint8_t)66ULL): goto ma408; case ((uint8_t)67ULL): goto ma409; case ((uint8_t)68ULL): goto ma410; case ((uint8_t)69ULL): goto ma411; case ((uint8_t)70ULL): goto ma406; case ((uint8_t)71ULL): goto ma407; case ((uint8_t)72ULL): goto ma411; case ((uint8_t)73ULL): goto ma411; case ((uint8_t)74ULL): goto ma411; case ((uint8_t)75ULL): goto ma411; case ((uint8_t)76ULL): goto ma411; case ((uint8_t)77ULL): goto ma411; case ((uint8_t)78ULL): goto ma411; case ((uint8_t)79ULL): goto ma411; case ((uint8_t)80ULL): goto ma411; case ((uint8_t)81ULL): goto ma411; default: goto ma411; }
ma406:;
                    uint8_t t417 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t417 != ((uint8_t)64ULL)) goto pa416;
                    goto po415;
pa416:;
                    uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t419 != ((uint8_t)70ULL)) goto pa418;
                    goto po415;
pa418:;
                    goto mn414;
po415:;
                    gm_b16_8 t420 = gm_l6;
                    gm_b16_8 t421 = gm_l25;
                    gm_b16_8 t422 = t421;
                    gm_b16_8 t423 = GM_LD(gm_b16_8, ((uint8_t *)&t422));
                    gm_f_copy_from_f7j6p05(t420, t423);
                    goto me405;
mn414:;
ma407:;
                    uint8_t t427 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t427 != ((uint8_t)65ULL)) goto pa426;
                    goto po425;
pa426:;
                    uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t429 != ((uint8_t)71ULL)) goto pa428;
                    goto po425;
pa428:;
                    goto mn424;
po425:;
                    void * t430 = gm_l0;
                    gm_tl874t2atnlh3 t431 = gm_l1;
                    uint64_t t432 = gm_l22;
                    uint64_t t433 = gm_l24;
                    uint64_t t434 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t434 >= (uintptr_t)(t435)) gm_trap_index(t434, t435, gm_srccp2mbkjs1vn4, 72, 80);
                    gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t434 * 16));
                    gm_b16_8 t437 = t436;
                    gm_b16_8 t438 = GM_LD(gm_b16_8, ((uint8_t *)&t437));
                    gm_tcfa6l7reoosn t439;
                    gm_f_store_at_ktabiji((void *)&t439, (void *)NULL, cx, NULL, t430, t431, t432, t433, t438);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t440 = t439;
                    if (((((uint8_t *)&t440))[0] != 0)) {
                        gm_t76123gfet73g t441 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t440) + 8));
                        gm_tcfa6l7reoosn t442;
                        GM_ST(uint8_t, (uint8_t *)&t442 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t442 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t442 + 4, 0);
                        ((uint8_t *)&t442)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t442 + 8), t441);
                        F.ret = t442;
                        goto gm_fret;
                    }
                    gm_b0_1 t443 = GM_LD(gm_b0_1, (((uint8_t *)&t440) + 8));
                    goto me405;
mn424:;
ma408:;
                    uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t445 != ((uint8_t)66ULL)) goto mn444;
                    {
                        void * t447 = gm_l0;
                        gm_tl874t2atnlh3 t448 = gm_l1;
                        uint64_t t449 = gm_l22;
                        uint64_t t450 = gm_l24;
                        uint64_t t451 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t452 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t451 >= (uintptr_t)(t452)) gm_trap_index(t451, t452, gm_srccp2mbkjs1vn4, 74, 52);
                        gm_b16_8 t453 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t451 * 16));
                        gm_b16_8 t454 = t453;
                        gm_b16_8 t455 = GM_LD(gm_b16_8, ((uint8_t *)&t454));
                        gm_tcfa6l7reoosn t456;
                        gm_f_store_at_ktabiji((void *)&t456, (void *)NULL, cx, NULL, t447, t448, t449, t450, t455);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t457 = t456;
                        if (((((uint8_t *)&t457))[0] != 0)) {
                            gm_t76123gfet73g t458 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t457) + 8));
                            gm_tcfa6l7reoosn t459;
                            GM_ST(uint8_t, (uint8_t *)&t459 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t459 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t459 + 4, 0);
                            ((uint8_t *)&t459)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t459 + 8), t458);
                            F.ret = t459;
                            goto gm_fret;
                        }
                        gm_b0_1 t460 = GM_LD(gm_b0_1, (((uint8_t *)&t457) + 8));
                    }
                    {
                        gm_b16_8 t461 = gm_l6;
                        gm_b16_8 t462 = gm_l25;
                        gm_b16_8 t463 = t462;
                        gm_b16_8 t464 = GM_LD(gm_b16_8, ((uint8_t *)&t463));
                        gm_f_copy_from_f7j6p05(t461, t464);
                    }
                    goto me405;
mn444:;
ma409:;
                    uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t466 != ((uint8_t)67ULL)) goto mn465;
                    {
                        void * t469 = gm_l0;
                        gm_tl874t2atnlh3 t470 = gm_l1;
                        gm_t88rcucg1b7uk t471 = gm_l23;
                        gm_b16_8 t472 = gm_l25;
                        uint64_t t473 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t474 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t473 >= (uintptr_t)(t474)) gm_trap_index(t473, t474, gm_srccp2mbkjs1vn4, 78, 51);
                        gm_b16_8 t475 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t473 * 16));
                        gm_t1ec8br9ddkav t476;
                        gm_f_equal_e5vf82h((void *)&t476, (void *)NULL, cx, NULL, t469, t470, t471, t472, t475);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t1ec8br9ddkav t477 = t476;
                        if (((((uint8_t *)&t477))[0] != 0)) {
                            gm_t76123gfet73g t478 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t477) + 8));
                            gm_tcfa6l7reoosn t479;
                            GM_ST(uint8_t, (uint8_t *)&t479 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t479 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t479 + 4, 0);
                            ((uint8_t *)&t479)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t479 + 8), t478);
                            F.ret = t479;
                            goto gm_fret;
                        }
                        uint8_t t480 = GM_LD(uint8_t, (((uint8_t *)&t477) + 8));
                        if (!t480) goto ie468;
                        {
                            void * t482 = gm_l0;
                            gm_tl874t2atnlh3 t483 = gm_l1;
                            uint64_t t484 = gm_l22;
                            uint64_t t485 = gm_l24;
                            uint64_t t486 = (uintptr_t)((uint64_t)2ULL);
                            uint64_t t487 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t486 >= (uintptr_t)(t487)) gm_trap_index(t486, t487, gm_srccp2mbkjs1vn4, 79, 56);
                            gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t486 * 16));
                            gm_b16_8 t489 = t488;
                            gm_b16_8 t490 = GM_LD(gm_b16_8, ((uint8_t *)&t489));
                            gm_tcfa6l7reoosn t491;
                            gm_f_store_at_ktabiji((void *)&t491, (void *)NULL, cx, NULL, t482, t483, t484, t485, t490);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tcfa6l7reoosn t492 = t491;
                            if (((((uint8_t *)&t492))[0] != 0)) {
                                gm_t76123gfet73g t493 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t492) + 8));
                                gm_tcfa6l7reoosn t494;
                                GM_ST(uint8_t, (uint8_t *)&t494 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t494 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t494 + 4, 0);
                                ((uint8_t *)&t494)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t494 + 8), t493);
                                F.ret = t494;
                                goto gm_fret;
                            }
                            gm_b0_1 t495 = GM_LD(gm_b0_1, (((uint8_t *)&t492) + 8));
                        }
                        {
                            gm_b16_8 t496 = gm_l6;
                            gm_f_fill_abmf5g5(t496, ((uint8_t)0ULL));
                        }
                        goto ix468;
ie468:;
                        {
                            void * t498 = gm_l0;
                            gm_t88rcucg1b7uk t499 = gm_l5;
                            gm_b16_8 t500 = gm_l6;
                            gm_b16_8 t501 = gm_l25;
                            gm_b16_8 t502 = t501;
                            gm_b16_8 t503 = GM_LD(gm_b16_8, ((uint8_t *)&t502));
                            gm_b0_1 t504 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t498, t499, t500, t503);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
ix468:;
                    }
                    goto me405;
mn465:;
ma410:;
                    uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t506 != ((uint8_t)68ULL)) goto mn505;
                    gm_t9qkh0rsooc8d t507 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                    gm_l26 = t507;
                    {
                        void * t509 = gm_l0;
                        gm_t88rcucg1b7uk t510 = gm_l23;
                        gm_b16_8 t511 = gm_l25;
                        gm_b16_8 t512 = t511;
                        gm_b16_8 t513 = GM_LD(gm_b16_8, ((uint8_t *)&t512));
                        unsigned __int128 t514 = gm_f_get_int_pg7p4gc(t509, GM_CONV(gm_csguankko4lmhd, t510), t513);
                        gm_l27 = t514;
                    }
                    {
                        void * t515 = gm_l0;
                        gm_t88rcucg1b7uk t516 = gm_l23;
                        uint64_t t517 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t518 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t517 >= (uintptr_t)(t518)) gm_trap_index(t517, t518, gm_srccp2mbkjs1vn4, 87, 40);
                        gm_b16_8 t519 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t517 * 16));
                        gm_b16_8 t520 = t519;
                        gm_b16_8 t521 = GM_LD(gm_b16_8, ((uint8_t *)&t520));
                        unsigned __int128 t522 = gm_f_get_int_pg7p4gc(t515, GM_CONV(gm_csguankko4lmhd, t516), t521);
                        gm_l28 = t522;
                    }
                    {
                        void * t523 = gm_l0;
                        gm_t88rcucg1b7uk t524 = gm_l23;
                        gm_tg5lql6itufk7 t525 = gm_f_int_info_ulcevdv(t523, GM_CONV(gm_csguankko4lmhd, t524));
                        gm_tg5lql6itufk7 t526 = t525;
                        gm_ttovhfee8mf28 t527;
                        if (((((uint8_t *)&t526))[8] != 0)) {
                            gm_ttovhfee8mf28 t528 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t526) + 0));
                            t527 = t528;
                        } else {
                            gm_ttovhfee8mf28 t529;
                            GM_ST(uint8_t, (uint8_t *)&t529 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t529 + 2, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t529 + 0), ((uint8_t)0));
                            GM_ST(uint32_t, ((uint8_t *)&t529 + 4), ((uint32_t)0ULL));
                            t527 = t529;
                        }
                        gm_ttovhfee8mf28 t530 = t527;
                        uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)&t530) + 0));
                        gm_l29 = t531;
                    }
                    {
                        unsigned __int128 t532;
                        uint8_t t542 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        switch (t542) { case ((uint8_t)0ULL): goto ma534; case ((uint8_t)1ULL): goto ma535; case ((uint8_t)2ULL): goto ma536; case ((uint8_t)3ULL): goto ma537; case ((uint8_t)4ULL): goto ma538; case ((uint8_t)5ULL): goto ma539; case ((uint8_t)6ULL): goto ma540; default: goto ma541; }
ma534:;
                        uint8_t t544 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t544 != ((uint8_t)0ULL)) goto mn543;
                        unsigned __int128 t545 = gm_l27;
                        unsigned __int128 t546 = gm_l28;
                        unsigned __int128 r547; bool o548 = __builtin_add_overflow(t545, t546, &r547);
                        unsigned __int128 t549 = r547;
                        t532 = t549;
                        goto me533;
mn543:;
ma535:;
                        uint8_t t551 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t551 != ((uint8_t)1ULL)) goto mn550;
                        unsigned __int128 t552 = gm_l27;
                        unsigned __int128 t553 = gm_l28;
                        unsigned __int128 r554; bool o555 = __builtin_sub_overflow(t552, t553, &r554);
                        unsigned __int128 t556 = r554;
                        t532 = t556;
                        goto me533;
mn550:;
ma536:;
                        uint8_t t558 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t558 != ((uint8_t)2ULL)) goto mn557;
                        unsigned __int128 t559 = gm_l27;
                        unsigned __int128 t560 = gm_l28;
                        unsigned __int128 t561 = (unsigned __int128)(t559 & t560);
                        t532 = t561;
                        goto me533;
mn557:;
ma537:;
                        uint8_t t563 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t563 != ((uint8_t)3ULL)) goto mn562;
                        unsigned __int128 t564 = gm_l27;
                        unsigned __int128 t565 = gm_l28;
                        unsigned __int128 t566 = (unsigned __int128)(t564 | t565);
                        t532 = t566;
                        goto me533;
mn562:;
ma538:;
                        uint8_t t568 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t568 != ((uint8_t)4ULL)) goto mn567;
                        unsigned __int128 t569 = gm_l27;
                        unsigned __int128 t570 = gm_l28;
                        unsigned __int128 t571 = (unsigned __int128)(t569 ^ t570);
                        t532 = t571;
                        goto me533;
mn567:;
ma539:;
                        uint8_t t573 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t573 != ((uint8_t)5ULL)) goto mn572;
                        unsigned __int128 t574;
                        uint8_t t576 = 0;
                        uint8_t t577 = 0;
                        uint8_t t578 = gm_l29;
                        if (t578) {
                            unsigned __int128 t579 = gm_l28;
                            unsigned __int128 t580 = t579;
                            __int128 t581;
                            memset(&t581, 0, sizeof t581); memcpy(&t581, ((uint8_t *)&t580), sizeof t581 < 16 ? sizeof t581 : 16);
                            unsigned __int128 t582 = gm_l27;
                            unsigned __int128 t583 = t582;
                            __int128 t584;
                            memset(&t584, 0, sizeof t584); memcpy(&t584, ((uint8_t *)&t583), sizeof t584 < 16 ? sizeof t584 : 16);
                            uint8_t t585 = (t581 > t584);
                            t577 = t585;
                        } else t577 = 0;
                        if (!t577) {
                            uint8_t t586 = 0;
                            uint8_t t587 = gm_l29;
                            uint8_t t588 = (uint8_t)!t587;
                            if (t588) {
                                unsigned __int128 t589 = gm_l28;
                                unsigned __int128 t590 = gm_l27;
                                uint8_t t591 = (t589 > t590);
                                t586 = t591;
                            } else t586 = 0;
                            t576 = t586;
                        } else t576 = 1;
                        if (!t576) goto ie575;
                        unsigned __int128 t593 = gm_l28;
                        unsigned __int128 t594 = t593;
                        t574 = t594;
                        goto ix575;
ie575:;
                        unsigned __int128 t596 = gm_l27;
                        unsigned __int128 t597 = t596;
                        t574 = t597;
ix575:;
                        t532 = t574;
                        goto me533;
mn572:;
ma540:;
                        uint8_t t599 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t599 != ((uint8_t)6ULL)) goto mn598;
                        unsigned __int128 t600;
                        uint8_t t602 = 0;
                        uint8_t t603 = 0;
                        uint8_t t604 = gm_l29;
                        if (t604) {
                            unsigned __int128 t605 = gm_l28;
                            unsigned __int128 t606 = t605;
                            __int128 t607;
                            memset(&t607, 0, sizeof t607); memcpy(&t607, ((uint8_t *)&t606), sizeof t607 < 16 ? sizeof t607 : 16);
                            unsigned __int128 t608 = gm_l27;
                            unsigned __int128 t609 = t608;
                            __int128 t610;
                            memset(&t610, 0, sizeof t610); memcpy(&t610, ((uint8_t *)&t609), sizeof t610 < 16 ? sizeof t610 : 16);
                            uint8_t t611 = (t607 < t610);
                            t603 = t611;
                        } else t603 = 0;
                        if (!t603) {
                            uint8_t t612 = 0;
                            uint8_t t613 = gm_l29;
                            uint8_t t614 = (uint8_t)!t613;
                            if (t614) {
                                unsigned __int128 t615 = gm_l28;
                                unsigned __int128 t616 = gm_l27;
                                uint8_t t617 = (t615 < t616);
                                t612 = t617;
                            } else t612 = 0;
                            t602 = t612;
                        } else t602 = 1;
                        if (!t602) goto ie601;
                        unsigned __int128 t619 = gm_l28;
                        unsigned __int128 t620 = t619;
                        t600 = t620;
                        goto ix601;
ie601:;
                        unsigned __int128 t622 = gm_l27;
                        unsigned __int128 t623 = t622;
                        t600 = t623;
ix601:;
                        t532 = t600;
                        goto me533;
mn598:;
ma541:;
                        gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 89, 31);
me533:;
                        gm_l30 = t532;
                    }
                    {
                        void * t624 = gm_l0;
                        void * t625 = gm_l0;
                        gm_t88rcucg1b7uk t626 = gm_l23;
                        uint64_t t627 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t625, t626);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t628 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t624, t627);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l31 = t628;
                    }
                    {
                        void * t629 = gm_l0;
                        gm_b16_8 t630 = gm_l31;
                        unsigned __int128 t631 = gm_l30;
                        gm_f_put_uint_cddin99(t629, t630, t631);
                    }
                    {
                        void * t632 = gm_l0;
                        gm_tl874t2atnlh3 t633 = gm_l1;
                        uint64_t t634 = gm_l22;
                        uint64_t t635 = gm_l24;
                        gm_b16_8 t636 = gm_l31;
                        gm_b16_8 t637 = t636;
                        gm_b16_8 t638 = GM_LD(gm_b16_8, ((uint8_t *)&t637));
                        gm_tcfa6l7reoosn t639;
                        gm_f_store_at_ktabiji((void *)&t639, (void *)NULL, cx, NULL, t632, t633, t634, t635, t638);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t640 = t639;
                        if (((((uint8_t *)&t640))[0] != 0)) {
                            gm_t76123gfet73g t641 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t640) + 8));
                            gm_tcfa6l7reoosn t642;
                            GM_ST(uint8_t, (uint8_t *)&t642 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t642 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t642 + 4, 0);
                            ((uint8_t *)&t642)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t642 + 8), t641);
                            F.ret = t642;
                            goto gm_fret;
                        }
                        gm_b0_1 t643 = GM_LD(gm_b0_1, (((uint8_t *)&t640) + 8));
                    }
                    {
                        gm_b16_8 t644 = gm_l6;
                        gm_b16_8 t645 = gm_l25;
                        gm_b16_8 t646 = t645;
                        gm_b16_8 t647 = GM_LD(gm_b16_8, ((uint8_t *)&t646));
                        gm_f_copy_from_f7j6p05(t644, t647);
                    }
                    goto me405;
mn505:;
ma411:;
                    goto me405;
mn648:;
ma412:;
                    gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 70, 13);
me405:;
                }
                goto ix376;
ie376:;
                {
                    void * t651 = gm_l0;
                    gm_tl874t2atnlh3 t652 = gm_l1;
                    gm_b16_8 t653;
                    GM_ST(void *, (uint8_t *)&t653, (void *)(gm_s8pst1ovjhtbk));
                    GM_ST(uintptr_t, (uint8_t *)&t653 + 8, (uintptr_t)(70));
                    gm_t76123gfet73g t654;
                    gm_f_trap_qh84l9d((void *)&t654, (void *)NULL, cx, NULL, t651, t652, t653);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t655;
                    GM_ST(uint8_t, (uint8_t *)&t655 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t655 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t655 + 4, 0);
                    ((uint8_t *)&t655)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t655 + 8), t654);
                    gm_tcfa6l7reoosn t656 = t655;
                    uint8_t t657 = ((((uint8_t *)&t656))[0] != 0);
                    F.ret = t655;
                    goto gm_fret;
                }
ix376:;
            }
            goto me3;
mn352:;
ma8:;
            uint8_t t659 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t659 != ((uint8_t)69ULL)) goto mn658;
            goto me3;
mn658:;
ma9:;
            uint8_t t662 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t662 != ((uint8_t)78ULL)) goto mn661;
            {
                void * t665 = gm_l0;
                uint64_t t666 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t667 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t666 >= (uintptr_t)(t667)) gm_trap_index(t666, t667, gm_srccp2mbkjs1vn4, 108, 39);
                void * t668 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t666 * 8));
                gm_t88rcucg1b7uk t669 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t668) + 96));
                gm_t53at80pfdus3 t670;
                gm_f_kind_htv6093((void *)&t670, t665, GM_CONV(gm_csguankko4lmhd, t669));
                gm_t53at80pfdus3 t671 = t670;
                uint8_t t672 = GM_LD(uint8_t, (((uint8_t *)&t671) + 0));
                if (t672 != ((uint8_t)10ULL)) goto ie664;
                gm_t88rcucg1b7uk t673 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t671) + 8) + 4));
                gm_l32 = t673;
                {
                    void * t675 = gm_l0;
                    uint64_t t676 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t677 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t676 >= (uintptr_t)(t677)) gm_trap_index(t676, t677, gm_srccp2mbkjs1vn4, 109, 32);
                    gm_b16_8 t678 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t676 * 16));
                    gm_b16_8 t679 = t678;
                    gm_b16_8 t680 = GM_LD(gm_b16_8, ((uint8_t *)&t679));
                    gm_t2lqu8jsj511d t681 = gm_f_slice_parts_1se5hu4(t675, t680);
                    gm_l33 = t681;
                }
                {
                    void * t682 = gm_l0;
                    uint64_t t683 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t684 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t683 >= (uintptr_t)(t684)) gm_trap_index(t683, t684, gm_srccp2mbkjs1vn4, 110, 32);
                    gm_b16_8 t685 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t683 * 16));
                    gm_b16_8 t686 = t685;
                    gm_b16_8 t687 = GM_LD(gm_b16_8, ((uint8_t *)&t686));
                    gm_t2lqu8jsj511d t688 = gm_f_slice_parts_1se5hu4(t682, t687);
                    gm_l34 = t688;
                }
                {
                    uint64_t t689 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    void * t690 = gm_l0;
                    gm_t88rcucg1b7uk t691 = gm_l32;
                    uint64_t t692 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t690, t691);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t r693; bool o694 = __builtin_mul_overflow(t689, t692, &r693);
                    if (o694) gm_trap("integer overflow in `*`", gm_srccp2mbkjs1vn4, 111, 18);
                    uint64_t t695 = r693;
                    gm_l35 = t695;
                }
                {
                    uint64_t t697 = gm_l35;
                    uint8_t t698 = ((t697 == ((uint64_t)0ULL)));
                    if (!t698) goto ie696;
                    {
                        gm_b0_1 t700;
                        gm_tcfa6l7reoosn t701;
                        GM_ST(uint8_t, (uint8_t *)&t701 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t701 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t701 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t701 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t701 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t701 + 24, 0);
                        ((uint8_t *)&t701)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t701 + 8), t700);
                        gm_tcfa6l7reoosn t702 = t701;
                        uint8_t t703 = ((((uint8_t *)&t702))[0] != 0);
                        F.ret = t701;
                        goto gm_fret;
                    }
                    goto ix696;
ie696:;
ix696:;
                }
                {
                    void * t704 = gm_l0;
                    void * t705 = (void *)(((uint8_t *)t704) + 184);
                    uint64_t t706 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 0));
                    uint64_t t707 = gm_l35;
                    gm_t934k1j7u70ct t708;
                    gm_f_read_u6r4vns((void *)&t708, t705, t706, t707, ((uint64_t)1ULL));
                    gm_t934k1j7u70ct t709 = t708;
                    gm_b16_8 t710;
                    if (!((((uint8_t *)&t709))[0] != 0)) {
                        gm_b16_8 t711 = GM_LD(gm_b16_8, (((uint8_t *)&t709) + 8));
                        t710 = t711;
                    } else {
                        gm_tpekhfs32op0f t712 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t709) + 8));
                        F.l36 = t712;
                        void * t713 = gm_l0;
                        gm_tl874t2atnlh3 t714 = gm_l1;
                        gm_t76123gfet73g t715;
                        gm_f_mem_trap_ecogko0((void *)&t715, (void *)NULL, cx, NULL, t713, t714, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l36)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t716;
                        GM_ST(uint8_t, (uint8_t *)&t716 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t716 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t716 + 4, 0);
                        ((uint8_t *)&t716)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t716 + 8), t715);
                        gm_tcfa6l7reoosn t717 = t716;
                        uint8_t t718 = ((((uint8_t *)&t717))[0] != 0);
                        F.ret = t716;
                        goto gm_fret;
                    }
                    gm_l37 = t710;
                }
                {
                    void * t719 = gm_l0;
                    uint64_t t720 = gm_l35;
                    gm_b16_8 t721 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t719, t720);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l38 = t721;
                }
                {
                    gm_b16_8 t722 = gm_l38;
                    gm_b16_8 t723 = gm_l37;
                    gm_b16_8 t724 = t723;
                    gm_b16_8 t725 = GM_LD(gm_b16_8, ((uint8_t *)&t724));
                    gm_f_copy_from_f7j6p05(t722, t725);
                }
                {
                    void * t726 = gm_l0;
                    void * t727 = (void *)(((uint8_t *)t726) + 184);
                    uint64_t t728 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 0));
                    gm_b16_8 t729 = gm_l38;
                    gm_b16_8 t730 = t729;
                    gm_b16_8 t731 = GM_LD(gm_b16_8, ((uint8_t *)&t730));
                    gm_tgfdnufetdnfc t732;
                    gm_f_write_5gub0ul((void *)&t732, t727, t728, t731, ((uint64_t)1ULL));
                    gm_tgfdnufetdnfc t733 = t732;
                    gm_b0_1 t734;
                    if (!((((uint8_t *)&t733))[0] != 0)) {
                        gm_b0_1 t735 = GM_LD(gm_b0_1, (((uint8_t *)&t733) + 8));
                        t734 = t735;
                    } else {
                        gm_tpekhfs32op0f t736 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t733) + 8));
                        F.l39 = t736;
                        void * t737 = gm_l0;
                        gm_tl874t2atnlh3 t738 = gm_l1;
                        gm_t76123gfet73g t739;
                        gm_f_mem_trap_ecogko0((void *)&t739, (void *)NULL, cx, NULL, t737, t738, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l39)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t740;
                        GM_ST(uint8_t, (uint8_t *)&t740 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t740 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t740 + 4, 0);
                        ((uint8_t *)&t740)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t740 + 8), t739);
                        gm_tcfa6l7reoosn t741 = t740;
                        uint8_t t742 = ((((uint8_t *)&t741))[0] != 0);
                        F.ret = t740;
                        goto gm_fret;
                    }
                }
                goto ix664;
ie664:;
                {
                    void * t744 = gm_l0;
                    gm_tl874t2atnlh3 t745 = gm_l1;
                    gm_b16_8 t746;
                    GM_ST(void *, (uint8_t *)&t746, (void *)(gm_sp0ag7curvl4o));
                    GM_ST(uintptr_t, (uint8_t *)&t746 + 8, (uintptr_t)(41));
                    gm_t76123gfet73g t747;
                    gm_f_trap_qh84l9d((void *)&t747, (void *)NULL, cx, NULL, t744, t745, t746);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t748;
                    GM_ST(uint8_t, (uint8_t *)&t748 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t748 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t748 + 4, 0);
                    ((uint8_t *)&t748)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t748 + 8), t747);
                    gm_tcfa6l7reoosn t749 = t748;
                    uint8_t t750 = ((((uint8_t *)&t749))[0] != 0);
                    F.ret = t748;
                    goto gm_fret;
                }
ix664:;
            }
            goto me3;
mn661:;
ma10:;
            uint8_t t752 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t752 != ((uint8_t)60ULL)) goto mn751;
            {
                void * t754 = gm_l0;
                uint64_t t755 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t756 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t755 >= (uintptr_t)(t756)) gm_trap_index(t755, t756, gm_srccp2mbkjs1vn4, 120, 27);
                gm_b16_8 t757 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t755 * 16));
                gm_b16_8 t758 = t757;
                gm_b16_8 t759 = GM_LD(gm_b16_8, ((uint8_t *)&t758));
                uint64_t t760 = gm_f_ptr_of_b77b4c9(t754, t759);
                gm_l40 = t760;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    if (!((uint8_t)1)) goto lb761;
                    {
                        void * t764 = gm_l0;
                        void * t765 = (void *)(((uint8_t *)t764) + 184);
                        uint64_t t766 = gm_l40;
                        uint64_t t767 = gm_l41;
                        uint64_t r768; bool o769 = __builtin_add_overflow(t766, t767, &r768);
                        if (o769) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 123, 33);
                        uint64_t t770 = r768;
                        gm_t934k1j7u70ct t771;
                        gm_f_read_u6r4vns((void *)&t771, t765, t770, ((uint64_t)1ULL), ((uint64_t)1ULL));
                        gm_t934k1j7u70ct t772 = t771;
                        gm_b16_8 t773;
                        if (!((((uint8_t *)&t772))[0] != 0)) {
                            gm_b16_8 t774 = GM_LD(gm_b16_8, (((uint8_t *)&t772) + 8));
                            t773 = t774;
                        } else {
                            gm_tpekhfs32op0f t775 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t772) + 8));
                            F.l42 = t775;
                            void * t776 = gm_l0;
                            gm_tl874t2atnlh3 t777 = gm_l1;
                            gm_t76123gfet73g t778;
                            gm_f_mem_trap_ecogko0((void *)&t778, (void *)NULL, cx, NULL, t776, t777, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l42)));
                            if (gm_unw.kind) {
                                goto gm_ll762;
                            }
                            gm_tcfa6l7reoosn t779;
                            GM_ST(uint8_t, (uint8_t *)&t779 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t779 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t779 + 4, 0);
                            ((uint8_t *)&t779)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t779 + 8), t778);
                            gm_tcfa6l7reoosn t780 = t779;
                            uint8_t t781 = ((((uint8_t *)&t780))[0] != 0);
                            F.ret = t779;
                            goto gm_fret;
                        }
                        gm_l43 = t773;
                    }
                    {
                        uint64_t t783 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t784 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        if (t783 >= (uintptr_t)(t784)) gm_trap_index(t783, t784, gm_srccp2mbkjs1vn4, 124, 20);
                        uint8_t t785 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t783 * 1));
                        uint8_t t786 = ((t785 == ((uint8_t)0ULL)));
                        if (!t786) goto ie782;
                        {
                            goto lb761;
                        }
                        goto ix782;
ie782:;
ix782:;
                    }
                    {
                        uint64_t t788 = gm_l41;
                        uint64_t r789; bool o790 = __builtin_add_overflow(t788, ((uint64_t)1ULL), &r789);
                        if (o790) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 125, 17);
                        uint64_t t791 = r789;
                        gm_l41 = t791;
                    }
lc761:;
                }
gm_ll762:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb761; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc761; }
                }
                goto gm_lad;
lb761:;
            }
            {
                void * t792 = gm_l0;
                gm_b16_8 t793 = gm_l6;
                uint64_t t794 = gm_l41;
                gm_f_put_u64_irp77r2(t792, t793, t794);
            }
            goto me3;
mn751:;
ma11:;
            uint8_t t798 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t798 != ((uint8_t)58ULL)) goto pa797;
            goto po796;
pa797:;
            uint8_t t800 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t800 != ((uint8_t)59ULL)) goto pa799;
            goto po796;
pa799:;
            goto mn795;
po796:;
            {
                gm_t88rcucg1b7uk t802;
                uint8_t t804 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                if (!t804) goto ie803;
                gm_t88rcucg1b7uk t806 = gm_l5;
                gm_t88rcucg1b7uk t807 = t806;
                t802 = t807;
                goto ix803;
ie803:;
                uint64_t t809 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t810 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t809 >= (uintptr_t)(t810)) gm_trap_index(t809, t810, gm_srccp2mbkjs1vn4, 130, 55);
                void * t811 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t809 * 8));
                gm_t88rcucg1b7uk t812 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t811) + 96));
                gm_t88rcucg1b7uk t813 = t812;
                t802 = t813;
ix803:;
                gm_l44 = t802;
            }
            {
                void * t814 = gm_l0;
                gm_b16_8 t815;
                uint8_t t817 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                if (!t817) goto ie816;
                uint64_t t819 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t820 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t819 >= (uintptr_t)(t820)) gm_trap_index(t819, t820, gm_srccp2mbkjs1vn4, 131, 53);
                gm_b16_8 t821 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t819 * 16));
                gm_b16_8 t822 = t821;
                gm_b16_8 t823 = t822;
                gm_b16_8 t824 = GM_LD(gm_b16_8, ((uint8_t *)&t823));
                t815 = t824;
                goto ix816;
ie816:;
                uint64_t t826 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t827 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t826 >= (uintptr_t)(t827)) gm_trap_index(t826, t827, gm_srccp2mbkjs1vn4, 131, 67);
                gm_b16_8 t828 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t826 * 16));
                gm_b16_8 t829 = t828;
                gm_b16_8 t830 = t829;
                gm_b16_8 t831 = GM_LD(gm_b16_8, ((uint8_t *)&t830));
                t815 = t831;
ix816:;
                gm_t2lqu8jsj511d t832 = gm_f_slice_parts_1se5hu4(t814, t815);
                gm_l45 = t832;
            }
            {
                void * t834 = gm_l0;
                gm_t88rcucg1b7uk t835 = gm_l44;
                gm_t53at80pfdus3 t836;
                gm_f_kind_htv6093((void *)&t836, t834, GM_CONV(gm_csguankko4lmhd, t835));
                gm_t53at80pfdus3 t837 = t836;
                uint8_t t838 = GM_LD(uint8_t, (((uint8_t *)&t837) + 0));
                if (t838 != ((uint8_t)11ULL)) goto ie833;
                uint64_t t839 = GM_LD(uint64_t, ((((uint8_t *)&t837) + 8) + 0));
                gm_l46 = t839;
                gm_t88rcucg1b7uk t840 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t837) + 8) + 8));
                gm_l47 = t840;
                {
                    uint64_t t843 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                    uint64_t t844 = gm_l46;
                    uint8_t t845 = (t843 < t844);
                    if (!t845) goto ie842;
                    void * t846 = gm_l0;
                    uint8_t t847 = gm_f_fast_23em7sv(t846);
                    uint8_t t848 = (uint8_t)!t847;
                    if (!t848) goto ie842;
                    {
                        gm_b16_8 t850;
                        uint8_t t852 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                        if (!t852) goto ie851;
                        gm_b16_8 t854;
                        GM_ST(void *, (uint8_t *)&t854, (void *)(gm_sqfrv3g5v9m2n));
                        GM_ST(uintptr_t, (uint8_t *)&t854 + 8, (uintptr_t)(4));
                        gm_b16_8 t855 = t854;
                        t850 = t855;
                        goto ix851;
ie851:;
                        gm_b16_8 t857;
                        GM_ST(void *, (uint8_t *)&t857, (void *)(gm_s0ugop4l95o1j));
                        GM_ST(uintptr_t, (uint8_t *)&t857 + 8, (uintptr_t)(5));
                        gm_b16_8 t858 = t857;
                        t850 = t858;
ix851:;
                        gm_l48 = t850;
                    }
                    {
                        void * t859 = gm_l0;
                        gm_tl874t2atnlh3 t860 = gm_l1;
                        gm_tgbgti82mf879 t861;
                        gm_b16_8 t862 = gm_l48;
                        GM_ST(gm_b16_8, (uint8_t *)&t861 + 0, t862);
                        uint64_t t863 = gm_l46;
                        GM_ST(uint64_t, (uint8_t *)&t861 + 16, t863);
                        uint64_t t864 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                        GM_ST(uint64_t, (uint8_t *)&t861 + 24, t864);
                        gm_tabb710d9mq2e t865;
                        gm_f_format_37g0gks((void *)&t865, (void *)NULL, cx, NULL, &t861);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t866 = t865;
                        if (((((uint8_t *)&t866))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t866) + 8, gm_srccp2mbkjs1vn4, 135, 38);
                        gm_b16_8 t867 = GM_LD(gm_b16_8, (((uint8_t *)&t866) + 8));
                        gm_b16_8 t868 = t867;
                        gm_b16_8 t869 = GM_LD(gm_b16_8, ((uint8_t *)&t868));
                        gm_t76123gfet73g t870;
                        gm_f_trap_qh84l9d((void *)&t870, (void *)NULL, cx, NULL, t859, t860, t869);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t871;
                        GM_ST(uint8_t, (uint8_t *)&t871 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t871 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t871 + 4, 0);
                        ((uint8_t *)&t871)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t871 + 8), t870);
                        gm_tcfa6l7reoosn t872 = t871;
                        uint8_t t873 = ((((uint8_t *)&t872))[0] != 0);
                        F.ret = t871;
                        goto gm_fret;
                    }
                    goto ix842;
ie842:;
ix842:;
                }
                {
                    void * t874 = gm_l0;
                    gm_t88rcucg1b7uk t875 = gm_l47;
                    uint64_t t876 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t874, t875);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t876;
                }
                {
                    uint8_t t878 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                    if (!t878) goto ie877;
                    {
                        void * t880 = gm_l0;
                        gm_tl874t2atnlh3 t881 = gm_l1;
                        uint64_t t882 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 0));
                        uint64_t t883 = gm_l49;
                        gm_b16_8 t884 = gm_l6;
                        gm_tcfa6l7reoosn t885;
                        gm_f_load_at_6u81jcc((void *)&t885, (void *)NULL, cx, NULL, t880, t881, t882, t883, t884);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t886 = t885;
                        if (((((uint8_t *)&t886))[0] != 0)) {
                            gm_t76123gfet73g t887 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t886) + 8));
                            gm_tcfa6l7reoosn t888;
                            GM_ST(uint8_t, (uint8_t *)&t888 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t888 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t888 + 4, 0);
                            ((uint8_t *)&t888)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t888 + 8), t887);
                            F.ret = t888;
                            goto gm_fret;
                        }
                        gm_b0_1 t889 = GM_LD(gm_b0_1, (((uint8_t *)&t886) + 8));
                    }
                    goto ix877;
ie877:;
                    {
                        void * t891 = gm_l0;
                        gm_tl874t2atnlh3 t892 = gm_l1;
                        uint64_t t893 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 0));
                        uint64_t t894 = gm_l49;
                        uint64_t t895 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t896 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t895 >= (uintptr_t)(t896)) gm_trap_index(t895, t896, gm_srccp2mbkjs1vn4, 138, 112);
                        gm_b16_8 t897 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t895 * 16));
                        gm_b16_8 t898 = t897;
                        gm_b16_8 t899 = GM_LD(gm_b16_8, ((uint8_t *)&t898));
                        gm_tcfa6l7reoosn t900;
                        gm_f_store_at_ktabiji((void *)&t900, (void *)NULL, cx, NULL, t891, t892, t893, t894, t899);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t901 = t900;
                        if (((((uint8_t *)&t901))[0] != 0)) {
                            gm_t76123gfet73g t902 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t901) + 8));
                            gm_tcfa6l7reoosn t903;
                            GM_ST(uint8_t, (uint8_t *)&t903 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t903 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t903 + 4, 0);
                            ((uint8_t *)&t903)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t903 + 8), t902);
                            F.ret = t903;
                            goto gm_fret;
                        }
                        gm_b0_1 t904 = GM_LD(gm_b0_1, (((uint8_t *)&t901) + 8));
                    }
ix877:;
                }
                goto ix833;
ie833:;
                {
                    void * t906 = gm_l0;
                    gm_tl874t2atnlh3 t907 = gm_l1;
                    gm_b16_8 t908;
                    GM_ST(void *, (uint8_t *)&t908, (void *)(gm_s2qp8mqi0c2le));
                    GM_ST(uintptr_t, (uint8_t *)&t908 + 8, (uintptr_t)(47));
                    gm_t76123gfet73g t909;
                    gm_f_trap_qh84l9d((void *)&t909, (void *)NULL, cx, NULL, t906, t907, t908);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t910;
                    GM_ST(uint8_t, (uint8_t *)&t910 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t910 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t910 + 4, 0);
                    ((uint8_t *)&t910)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t910 + 8), t909);
                    gm_tcfa6l7reoosn t911 = t910;
                    uint8_t t912 = ((((uint8_t *)&t911))[0] != 0);
                    F.ret = t910;
                    goto gm_fret;
                }
ix833:;
            }
            goto me3;
mn795:;
ma12:;
            void * t914 = gm_l0;
            gm_tl874t2atnlh3 t915 = gm_l1;
            gm_b16_8 t916;
            GM_ST(void *, (uint8_t *)&t916, (void *)(gm_s1gjt02j7um1i));
            GM_ST(uintptr_t, (uint8_t *)&t916 + 8, (uintptr_t)(36));
            gm_t76123gfet73g t917;
            gm_f_trap_qh84l9d((void *)&t917, (void *)NULL, cx, NULL, t914, t915, t916);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t918;
            GM_ST(uint8_t, (uint8_t *)&t918 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t918 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t918 + 4, 0);
            ((uint8_t *)&t918)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t918 + 8), t917);
            gm_tcfa6l7reoosn t919 = t918;
            uint8_t t920 = ((((uint8_t *)&t919))[0] != 0);
            F.ret = t918;
            goto gm_fret;
            goto me3;
mn913:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 10, 5);
me3:;
        }
    }
    gm_b0_1 t921;
    gm_tcfa6l7reoosn t922;
    GM_ST(uint8_t, (uint8_t *)&t922 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t922 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t922 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t922 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t922 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t922 + 24, 0);
    ((uint8_t *)&t922)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t922 + 8), t921);
    gm_tcfa6l7reoosn t923 = t922;
    F.ret = t923;
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
