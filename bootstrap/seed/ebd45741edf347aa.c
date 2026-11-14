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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
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
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
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
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
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
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
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
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
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
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_t1s75i08qtptd f0; } gm_t4vsf0g4hsv81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_t4vsf0g4hsv81 f0; uint8_t f1; uint8_t pe[7]; } gm_t13p2dc04q01f;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t4vsf0g4hsv81) == 40 && _Alignof(gm_t4vsf0g4hsv81) == 8 && offsetof(gm_t4vsf0g4hsv81, f0) == 0, "layout of `(name: ?Ident)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t13p2dc04q01f) == 48 && _Alignof(gm_t13p2dc04q01f) == 8 && offsetof(gm_t13p2dc04q01f, f0) == 0 && offsetof(gm_t13p2dc04q01f, f1) == 40, "layout of `?(name: ?Ident)`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
typedef struct { uint64_t f0; gm_t7te2tdpebbt4 f1; } gm_tl1vi0nd1uql9;
_Static_assert(sizeof(gm_tl1vi0nd1uql9) == 152 && _Alignof(gm_tl1vi0nd1uql9) == 8 && offsetof(gm_tl1vi0nd1uql9, f0) == 0 && offsetof(gm_tl1vi0nd1uql9, f1) == 8, "layout of `(i: usize, x: Arg)`");
struct gm_fr_array_element_t6q5jtp {
    gm_frame h;
    void * ret;
};
struct gm_fr_elements_tuple_gdm5n6q {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* stmts */
    gm_trqfvmjtt4sij l8; /* elems */
    gm_trqfvmjtt4sij l9; /* ts */
    void * ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8ckfo47 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8lf24cm {
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
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
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
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fvvkq82 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j5ps4qc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
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
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_pack_rest_args_cknfrfl {
    gm_frame h;
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_trqfvmjtt4sij l14; /* out */
    gm_trqfvmjtt4sij l15; /* rest */
    gm_t7te2tdpebbt4 l20; /* a */
    gm_t7te2tdpebbt4 l22; /* packed */
    gm_tc3oht4ksth9d l23; /* e */
    gm_trqfvmjtt4sij l25; /* elems */
    gm_tc3oht4ksth9d l29; /* e */
    gm_tck5nas0nf5v8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
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
struct gm_fr_spread_array_v7s332a {
    gm_frame h;
    gm_tb7u19loo3j87 ret;
};
struct gm_fr_spread_generic_args_8qtof2f {
    gm_frame h;
    gm_t7te2tdpebbt4 l8; /* a */
    gm_trqfvmjtt4sij l9; /* written */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l14; /* x */
    gm_tc3oht4ksth9d l15; /* x */
    gm_tc3oht4ksth9d l16; /* x */
    gm_tc3oht4ksth9d l17; /* e */
    gm_t13p2dc04q01f l18; /* named */
    gm_tc3oht4ksth9d l24; /* base */
    gm_t4vsf0g4hsv81 l25; /* nm */
    gm_trqfvmjtt4sij l27; /* parts */
    gm_ta42iqot1auae l37; /* lit */
    gm_tc3oht4ksth9d l39; /* elem */
    gm_t0mglksrkfa5e l44; /* x */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_spread_parts_e62q2l2 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* parts */
    gm_trqfvmjtt4sij l18; /* ts */
    gm_trqfvmjtt4sij l19; /* ns */
    gm_t0mglksrkfa5e l22; /* x */
    gm_trqfvmjtt4sij l24; /* parts */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_synthetic_hdhcgpb {
    gm_frame h;
    gm_t2gtbh39ptekd l0; /* kind */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_array_element_t6q5jtp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8lf24cm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_format_7f8ekea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l53b7vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p5cs5qn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q62qvnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uubl3s3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_3qmicjr(void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j5ps4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_q1fs5ps(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bsig5q3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_spread_array_v7s332a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_spread_generic_args_8qtof2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_synthetic_hdhcgpb(void *gm_out, const void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_synthetic_hdhcgpb_c(gm_t2gtbh39ptekd a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k70n01rs29g[40] __attribute__((aligned(8)));
extern uint8_t gm_ka2mtmecqk3[40] __attribute__((aligned(8)));
extern uint8_t gm_kbjtngq55qv[40] __attribute__((aligned(8)));
extern uint8_t gm_ke30i57o09o[40] __attribute__((aligned(8)));
extern uint8_t gm_kjn96jdce98[40] __attribute__((aligned(8)));
extern uint8_t gm_kqn03v7t2qt[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1olfuo31ml9r[64];
extern const uint8_t gm_s2of8ntot1on0[81];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcdjp1iiokf034[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8ckfo47 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8lf24cm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8lf24cm F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fvvkq82 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8ckfo47();
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
inline __attribute__((aligned(4))) void gm_f_list_j5ps4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j5ps4qc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8lf24cm();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) void gm_f_synthetic_hdhcgpb(void *gm_out, const void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_synthetic_hdhcgpb F;
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, ((uint32_t)4294967295ULL));
    memcpy((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, ((uint8_t *)a0), 64);
    gm_tl874t2atnlh3 t1 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void * gm_f_array_element_t6q5jtp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_array_element_t6q5jtp F;
    void * gm_l0; /* s */
    void * gm_l1; /* base */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* i */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* index */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t5 = gm_l3;
        gm_tbci5ospk5nnn t6;
        gm_f_from_u64_ud13q5v((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t7 + 8), t6);
        gm_tl874t2atnlh3 t8 = gm_l4;
        void * t9 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t9;
    }
    {
        gm_t88rcucg1b7uk t10 = gm_l2;
        gm_tlcl4u558l6p5 t11;
        memset((uint8_t *)&t11 + 17, 0, 7);
        void * t12 = gm_l1;
        GM_ST(void *, (uint8_t *)&t11 + 0, t12);
        void * t13 = gm_l5;
        GM_ST(void *, (uint8_t *)&t11 + 8, t13);
        GM_ST(uint8_t, (uint8_t *)&t11 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t14 + 8), t11);
        gm_tl874t2atnlh3 t15 = gm_l4;
        void * t16 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = 0;
        F.ret = t16;
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
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_elements_tuple_gdm5n6q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* vt */
    gm_tlcm4lvu3g1a8 gm_l5; /* a */
    void * gm_l7; /* base */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ty */
    void * gm_l14; /* agg */
    GM_STACK_CHECK(&F, 979u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l3;
        gm_t88rcucg1b7uk t13 = gm_l4;
        gm_t53at80pfdus3 t14;
        gm_f_kind_jcnt4hm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        gm_t53at80pfdus3 t15 = t14;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma19; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma19; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma18; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma19; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma17; default: goto ma19; }
ma17:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t23 != ((uint8_t)26ULL)) goto mn22;
        uint8_t t24 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me16;
mn22:;
ma18:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t28 != ((uint8_t)11ULL)) goto pa27;
        goto po26;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t30 != ((uint8_t)20ULL)) goto pa29;
        goto po26;
pa29:;
        goto mn25;
po26:;
        {
            void * t32 = gm_l0;
            gm_t88rcucg1b7uk t33 = gm_l4;
            gm_tl874t2atnlh3 t34 = gm_l2;
            gm_tb7u19loo3j87 t35;
            gm_f_spread_array_v7s332a((void *)&t35, (void *)NULL, cx, NULL, t32, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb7u19loo3j87 t36 = t35;
            gm_tlcm4lvu3g1a8 t37;
            if (((((uint8_t *)&t36))[16] != 0)) {
                gm_tlcm4lvu3g1a8 t38 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t36) + 0));
                t37 = t38;
            } else {
                uint8_t t39 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l5 = t37;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t40 = gm_l0;
            void * t41 = gm_l1;
            void * t42 = (void *)((uint8_t *)&F.l6);
            void * t43 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t43;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                gm_l10 = t45;
            }
            {
                for (;;) {
                    uint64_t t47 = gm_l11;
                    uint64_t t48 = gm_l10;
                    uint8_t t49 = (t47 < t48);
                    if (!t49) goto lb46;
                    {
                        uint64_t t52 = gm_l11;
                        uint64_t t53 = t52;
                        uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&t53));
                        gm_l12 = t56;
                        {
                            void * t58 = (void *)((uint8_t *)&F.l8);
                            void * t59 = gm_l0;
                            void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
                            void * t61 = gm_l7;
                            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                            uint64_t t63 = gm_l12;
                            gm_tl874t2atnlh3 t64 = gm_l2;
                            void * t65 = gm_f_array_element_t6q5jtp((void *)NULL, cx, NULL, t60, t61, t62, t63, t64);
                            if (gm_unw.kind) {
                                goto gm_ll50;
                            }
                            gm_tftb1odc739t5 t66 = gm_f_push_d943vgp_2(t58, &(*(gm_t69m56meqefs8 *)((uint8_t *)t65)));
                            gm_tftb1odc739t5 t67 = t66;
                            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcdjp1iiokf034, 182, 17);
                            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                        }
                        {
                            void * t69 = (void *)((uint8_t *)&F.l9);
                            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                            gm_tftb1odc739t5 t71 = gm_f_push_k3l9nic(t69, GM_CONV(gm_csguankko4lmhd, t70));
                            gm_tftb1odc739t5 t72 = t71;
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcdjp1iiokf034, 183, 17);
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                        }
                        goto me54;
mn55:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 181, 13);
me54:;
                    }
lc46:;
                    {
                        uint64_t t75 = gm_l11;
                        uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                        uint64_t t78 = r76;
                        gm_l11 = t78;
                    }
                }
gm_ll50:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
                }
                goto gm_lad;
lb46:;
            }
        }
        {
            void * t79 = gm_l3;
            gm_t6j6smj7qoctu t80;
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t82 = t81;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            GM_ST(gm_b16_8, (uint8_t *)&t80 + 0, t83);
            gm_b16_8 t84 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t85 = t84;
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
            GM_ST(gm_b16_8, (uint8_t *)&t80 + 16, t86);
            gm_t53at80pfdus3 t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t87 + 8), t80);
            gm_t88rcucg1b7uk t88 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t79, &t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t88;
        }
        {
            gm_t88rcucg1b7uk t89 = gm_l13;
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_teti411eujvf4 t91;
            memset(&t91, 0, sizeof t91);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 8), t90);
            gm_tl874t2atnlh3 t92 = gm_l2;
            void * t93 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t93;
        }
        {
            uint64_t t95 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            uint8_t t96 = ((t95 == ((uint64_t)0ULL)));
            if (!t96) goto ie94;
            {
                void * t98 = gm_l14;
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            gm_t88rcucg1b7uk t100 = gm_l13;
            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            void * t102 = gm_l14;
            gm_tl874t2atnlh3 t103 = gm_l2;
            void * t104 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t100, t101, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t105 = 0;
            F.ret = t104;
            goto gm_fret;
        }
        goto me16;
mn25:;
ma19:;
        void * t107 = gm_l1;
        uint8_t t108 = 0;
        F.ret = t107;
        goto gm_fret;
        goto me16;
mn106:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 173, 5);
me16:;
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
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_pack_rest_args_cknfrfl F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* slot */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tf5srh4mp4b3i gm_l4; /* r */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l9; /* ri */
    uint64_t gm_l10; /* before */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    uint64_t gm_l16; /* seen */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_t08olq97rscn3 gm_l21; /* n */
    void * gm_l24; /* inner */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* k */
    gm_tl874t2atnlh3 gm_l30; /* sp */
    gm_tl874t2atnlh3 gm_l31; /* first */
    gm_tl874t2atnlh3 gm_l32; /* last */
    uint32_t gm_l33; /* at */
    uint64_t gm_l34; /* pos */
    uint64_t gm_l35; /* first */
    uint64_t gm_l36; /* n */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    GM_STACK_CHECK(&F, 4298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l5 = t4;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l6;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    gm_tgsvmddj3e85e t11;
                    uint64_t t12 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l6;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t14 * 56), 56);
                    gm_tgsvmddj3e85e t16 = t11;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l7 = t19;
                    gm_t5v0c8ia6a5mo t20 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t16) + 8));
                    F.l8 = t20;
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 48));
                        if (!t23) goto ie22;
                        {
                            uint64_t t25 = gm_l7;
                            gm_tf5srh4mp4b3i t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[8] = 1;
                            gm_l4 = t26;
                        }
                        {
                            goto lb5;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 14, 5);
me17:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l6;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l6 = t31;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t32 = gm_l4;
        gm_tf5srh4mp4b3i t33 = t32;
        uint64_t t34;
        if (((((uint8_t *)&t33))[8] != 0)) {
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&t33) + 0));
            t34 = t35;
        } else {
            gm_tck5nas0nf5v8 t36;
            memset(&t36, 0, sizeof t36);
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        gm_l9 = t34;
    }
    {
        gm_l10 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            uint64_t t39 = gm_l9;
            gm_l11 = t39;
        }
        {
            for (;;) {
                uint64_t t41 = gm_l12;
                uint64_t t42 = gm_l11;
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    uint64_t t46 = gm_l12;
                    uint64_t t47 = t46;
                    uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&t47));
                    gm_l13 = t50;
                    {
                        uint64_t t53 = gm_l13;
                        uint64_t t54 = (uintptr_t)t53;
                        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcdjp1iiokf034, 23, 12);
                        uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t54 * 1));
                        if (!t56) goto ie52;
                        {
                            uint64_t t58 = gm_l10;
                            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                            if (o60) gm_trap("integer overflow in `+`", gm_srcdjp1iiokf034, 23, 22);
                            uint64_t t61 = r59;
                            gm_l10 = t61;
                        }
                        goto ix52;
ie52:;
ix52:;
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 22, 5);
me48:;
                }
lc40:;
                {
                    uint64_t t63 = gm_l12;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    uint64_t t66 = r64;
                    gm_l12 = t66;
                }
            }
lb40:;
        }
    }
    {
        gm_f_list_6oos54v((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l16 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t68 = gm_l2;
            gm_l17 = t68;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t70 = gm_l18;
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t72 = (t70 < t71);
                if (!t72) goto lb69;
                {
                    gm_tl1vi0nd1uql9 t75;
                    uint64_t t76 = gm_l18;
                    GM_ST(uint64_t, (uint8_t *)&t75 + 0, t76);
                    uint64_t t77 = gm_l18;
                    uint64_t t78 = (uintptr_t)t77;
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    memcpy((uint8_t *)&t75 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t78 * 144), 144);
                    gm_tl1vi0nd1uql9 t80 = t75;
                    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&t80) + 0));
                    gm_l19 = t83;
                    gm_t7te2tdpebbt4 t84 = GM_LD(gm_t7te2tdpebbt4, (((uint8_t *)&t80) + 8));
                    F.l20 = t84;
                    {
                        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        switch (t91) { case ((uint8_t)0ULL): goto ma88; case ((uint8_t)1ULL): goto ma87; case ((uint8_t)2ULL): goto ma89; default: goto ma90; }
ma87:;
                        uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        if (t93 != ((uint8_t)1ULL)) goto mn92;
                        gm_t08olq97rscn3 t94 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l20) + 0) + 8) + 0));
                        gm_l21 = t94;
                        {
                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                            uint64_t t98 = gm_l9;
                            uint64_t t99 = (uintptr_t)t98;
                            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_srcdjp1iiokf034, 31, 30);
                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t99 * 56) + 0));
                            gm_b16_8 t102 = t97;
                            gm_b16_8 t103 = t101;
                            uint8_t t104 = (gm_eq0iqiubfm7e(((uint8_t *)&t102), ((uint8_t *)&t103)));
                            if (!t104) goto ie96;
                            {
                                gm_tbgd14esf0jvi t106;
                                memset((uint8_t *)&t106 + 12, 0, 4);
                                gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t106 + 0, t107);
                                gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                gm_tnpifv20irnph t109;
                                GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                                gm_tabb710d9mq2e t110;
                                gm_f_format_l53b7vj((void *)&t110, (void *)NULL, cx, NULL, t109);
                                if (gm_unw.kind) {
                                    goto gm_ll73;
                                }
                                gm_tabb710d9mq2e t111 = t110;
                                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcdjp1iiokf034, 32, 58);
                                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                                gm_b16_8 t113 = t112;
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                                GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t114);
                                gm_ts5t31j0bugq1 t115;
                                GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t115 + 8), t106);
                                gm_tck5nas0nf5v8 t116;
                                GM_ST(uint8_t, (uint8_t *)&t116 + 41, 0);
                                GM_ST(uint16_t, (uint8_t *)&t116 + 42, 0);
                                GM_ST(uint32_t, (uint8_t *)&t116 + 44, 0);
                                GM_ST(gm_ts5t31j0bugq1, ((uint8_t *)&t116), t115);
                                ((uint8_t *)&t116)[40] = 1;
                                uint8_t t117 = 0;
                                F.ret = t116;
                                goto gm_fret;
                            }
                            goto ix96;
ie96:;
ix96:;
                        }
                        {
                            void * t118 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t119 = gm_f_push_jis5lhu(t118, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l20)));
                            gm_tftb1odc739t5 t120 = t119;
                            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srcdjp1iiokf034, 34, 17);
                            gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
                        }
                        goto me86;
mn92:;
ma88:;
                        uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        if (t123 != ((uint8_t)0ULL)) goto mn122;
                        uint64_t t125 = gm_l16;
                        uint64_t t126 = gm_l10;
                        uint8_t t127 = (t125 < t126);
                        if (!t127) goto ie124;
                        {
                            uint64_t t129 = gm_l16;
                            uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                            if (o131) gm_trap("integer overflow in `+`", gm_srcdjp1iiokf034, 37, 17);
                            uint64_t t132 = r130;
                            gm_l16 = t132;
                        }
                        {
                            void * t133 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t134 = gm_f_push_jis5lhu(t133, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l20)));
                            gm_tftb1odc739t5 t135 = t134;
                            if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcdjp1iiokf034, 38, 17);
                            gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                        }
                        goto ix124;
ie124:;
                        {
                            void * t138 = (void *)((uint8_t *)&F.l15);
                            uint64_t t139 = gm_l19;
                            gm_tftb1odc739t5 t140 = gm_f_push_ucgmlo2(t138, t139);
                            gm_tftb1odc739t5 t141 = t140;
                            if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srcdjp1iiokf034, 40, 17);
                            gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
                        }
ix124:;
                        goto me86;
mn122:;
ma89:;
                        uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        if (t144 != ((uint8_t)2ULL)) goto mn143;
                        {
                            uint64_t t147 = gm_l16;
                            uint64_t t148 = gm_l10;
                            uint8_t t149 = (t147 < t148);
                            if (!t149) goto ie146;
                            {
                                gm_tbgd14esf0jvi t151;
                                memset((uint8_t *)&t151 + 12, 0, 4);
                                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t151 + 0, t152);
                                gm_b16_8 t153;
                                GM_ST(void *, (uint8_t *)&t153, (void *)(gm_s2of8ntot1on0));
                                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(80));
                                GM_ST(gm_b16_8, (uint8_t *)&t151 + 16, t153);
                                gm_ts5t31j0bugq1 t154;
                                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t154 + 8), t151);
                                gm_tck5nas0nf5v8 t155;
                                GM_ST(uint8_t, (uint8_t *)&t155 + 41, 0);
                                GM_ST(uint16_t, (uint8_t *)&t155 + 42, 0);
                                GM_ST(uint32_t, (uint8_t *)&t155 + 44, 0);
                                GM_ST(gm_ts5t31j0bugq1, ((uint8_t *)&t155), t154);
                                ((uint8_t *)&t155)[40] = 1;
                                uint8_t t156 = 0;
                                F.ret = t155;
                                goto gm_fret;
                            }
                            goto ix146;
ie146:;
ix146:;
                        }
                        {
                            void * t157 = (void *)((uint8_t *)&F.l15);
                            uint64_t t158 = gm_l19;
                            gm_tftb1odc739t5 t159 = gm_f_push_ucgmlo2(t157, t158);
                            gm_tftb1odc739t5 t160 = t159;
                            if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 1, gm_srcdjp1iiokf034, 44, 17);
                            gm_b0_1 t161 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 1));
                        }
                        goto me86;
mn143:;
ma90:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 29, 9);
me86:;
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 28, 5);
me81:;
                }
lc69:;
                {
                    uint64_t t163 = gm_l18;
                    uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                    uint64_t t166 = r164;
                    gm_l18 = t166;
                }
            }
gm_ll73:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb69; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc69; }
            }
            goto gm_lad;
lb69:;
        }
    }
    {
        memset((uint8_t *)&F.l22 + 140, 0, 4);
        gm_t2gtbh39ptekd t167;
        memset(&t167, 0, sizeof t167);
        GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)7ULL));
        gm_tl874t2atnlh3 t168 = gm_l3;
        gm_tc3oht4ksth9d t169;
        gm_f_synthetic_hdhcgpb((void *)&t169, &t167, GM_CONV(gm_cseahvg8v7p0vs, t168));
        gm_t22mtv3sd9prf t170;
        memset(&t170, 0, sizeof t170);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t170 + 8), t169);
        GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&F.l22 + 0, t170);
        gm_tl874t2atnlh3 t171 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l22 + 128, t171);
    }
    {
        uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
        uint8_t t174 = ((t173 == ((uint64_t)1ULL)));
        if (!t174) goto ie172;
        uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t176 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
        if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srcdjp1iiokf034, 49, 51);
        uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t175 * 8));
        uint64_t t178 = (uintptr_t)t177;
        uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t178 >= (uintptr_t)(t179)) gm_trap_index(t178, t179, gm_srcdjp1iiokf034, 49, 46);
        uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t178 * 144) + 0) + 0));
        if (t180 != ((uint8_t)2ULL)) goto ie172;
        gm_tc3oht4ksth9d t181 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t178 * 144) + 0) + 8));
        F.l23 = t181;
        {
            gm_tq7s98ihonkju t183 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l23)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k70n01rs29g)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t184 = t183;
            if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 8, gm_srcdjp1iiokf034, 50, 18);
            void * t185 = GM_LD(void *, (((uint8_t *)&t184) + 8));
            gm_l24 = t185;
        }
        {
            memset((uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 140, 0, 4);
            void * t186 = gm_l24;
            gm_t2gtbh39ptekd t187;
            memset(&t187, 0, sizeof t187);
            GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)11ULL));
            GM_ST(void *, ((uint8_t *)&t187 + 8), t186);
            gm_tl874t2atnlh3 t188 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l23) + 72));
            gm_tc3oht4ksth9d t189;
            gm_f_synthetic_hdhcgpb((void *)&t189, &t187, GM_CONV(gm_cseahvg8v7p0vs, t188));
            gm_t22mtv3sd9prf t190;
            memset(&t190, 0, sizeof t190);
            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t190 + 8), t189);
            uint64_t t191 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t192 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            if (t191 >= (uintptr_t)(t192)) gm_trap_index(t191, t192, gm_srcdjp1iiokf034, 51, 93);
            uint64_t t193 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t191 * 8));
            uint64_t t194 = (uintptr_t)t193;
            uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srcdjp1iiokf034, 51, 88);
            gm_tl874t2atnlh3 t196 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t194 * 144) + 128));
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 0, t190);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 128, t196);
        }
        goto ix172;
ie172:;
        {
            gm_f_list_fvvkq82((void *)&F.l25, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                gm_l26 = t199;
            }
            {
                gm_l27 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t201 = gm_l27;
                    uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    uint8_t t203 = (t201 < t202);
                    if (!t203) goto lb200;
                    {
                        uint64_t t206 = gm_l27;
                        uint64_t t207 = (uintptr_t)t206;
                        uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint64_t t209 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t207 * 8));
                        uint64_t t210 = t209;
                        uint64_t t213 = GM_LD(uint64_t, ((uint8_t *)&t210));
                        gm_l28 = t213;
                        {
                            uint64_t t215 = gm_l28;
                            uint64_t t216 = (uintptr_t)t215;
                            uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t216 >= (uintptr_t)(t217)) gm_trap_index(t216, t217, gm_srcdjp1iiokf034, 55, 19);
                            uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t216 * 144) + 0) + 0));
                            if (t220 != ((uint8_t)0ULL)) goto mn219;
                            gm_tc3oht4ksth9d t221 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t216 * 144) + 0) + 8));
                            F.l29 = t221;
                            void * t222 = (void *)((uint8_t *)&F.l25);
                            gm_tv1o1j44ii4db t223;
                            memset((uint8_t *)&t223 + 129, 0, 7);
                            gm_t1s75i08qtptd t224;
                            memset(&t224, 0, sizeof t224);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t223 + 0, t224);
                            memcpy((uint8_t *)&t223 + 40, ((uint8_t *)&F.l29), 88);
                            GM_ST(uint8_t, (uint8_t *)&t223 + 128, ((uint8_t)0));
                            gm_tftb1odc739t5 t225 = gm_f_push_l8mrumu(t222, &t223);
                            gm_tftb1odc739t5 t226 = t225;
                            if (((((uint8_t *)&t226))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t226) + 1, gm_srcdjp1iiokf034, 56, 35);
                            gm_b0_1 t227 = GM_LD(gm_b0_1, (((uint8_t *)&t226) + 1));
                            goto me218;
mn219:;
                            gm_tbgd14esf0jvi t229;
                            memset((uint8_t *)&t229 + 12, 0, 4);
                            uint64_t t230 = gm_l28;
                            uint64_t t231 = (uintptr_t)t230;
                            uint64_t t232 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t231 >= (uintptr_t)(t232)) gm_trap_index(t231, t232, gm_srcdjp1iiokf034, 57, 42);
                            gm_tl874t2atnlh3 t233 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t231 * 144) + 128));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t229 + 0, t233);
                            gm_b16_8 t234;
                            GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s1olfuo31ml9r));
                            GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(63));
                            GM_ST(gm_b16_8, (uint8_t *)&t229 + 16, t234);
                            gm_ts5t31j0bugq1 t235;
                            GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t235 + 8), t229);
                            gm_tck5nas0nf5v8 t236;
                            GM_ST(uint8_t, (uint8_t *)&t236 + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&t236 + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&t236 + 44, 0);
                            GM_ST(gm_ts5t31j0bugq1, ((uint8_t *)&t236), t235);
                            ((uint8_t *)&t236)[40] = 1;
                            uint8_t t237 = 0;
                            F.ret = t236;
                            goto gm_fret;
                            goto me218;
mn228:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 55, 13);
me218:;
                        }
                        goto me211;
mn212:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 54, 9);
me211:;
                    }
lc200:;
                    {
                        uint64_t t239 = gm_l27;
                        uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                        uint64_t t242 = r240;
                        gm_l27 = t242;
                    }
                }
lb200:;
            }
        }
        {
            gm_tl874t2atnlh3 t243 = gm_l3;
            gm_l30 = t243;
        }
        {
            uint64_t t245 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            uint8_t t246 = (t245 > ((uint64_t)0ULL));
            if (!t246) goto ie244;
            {
                uint64_t t248 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t249 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                if (t248 >= (uintptr_t)(t249)) gm_trap_index(t248, t249, gm_srcdjp1iiokf034, 62, 27);
                uint64_t t250 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t248 * 8));
                uint64_t t251 = (uintptr_t)t250;
                uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t251 >= (uintptr_t)(t252)) gm_trap_index(t251, t252, gm_srcdjp1iiokf034, 62, 22);
                gm_tl874t2atnlh3 t253 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t251 * 144) + 128));
                gm_l31 = t253;
            }
            {
                uint64_t t254 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                uint64_t r255; bool o256 = __builtin_sub_overflow(t254, ((uint64_t)1ULL), &r255);
                if (o256) gm_trap("integer overflow in `-`", gm_srcdjp1iiokf034, 63, 37);
                uint64_t t257 = r255;
                uint64_t t258 = (uintptr_t)t257;
                uint64_t t259 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                if (t258 >= (uintptr_t)(t259)) gm_trap_index(t258, t259, gm_srcdjp1iiokf034, 63, 26);
                uint64_t t260 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t258 * 8));
                uint64_t t261 = (uintptr_t)t260;
                uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t261 >= (uintptr_t)(t262)) gm_trap_index(t261, t262, gm_srcdjp1iiokf034, 63, 21);
                gm_tl874t2atnlh3 t263 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t261 * 144) + 128));
                gm_l32 = t263;
            }
            {
                uint32_t t264 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 0));
                uint32_t t265 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                uint32_t t266 = GM_LD(uint32_t, (((uint8_t *)&gm_l32) + 8));
                gm_tl874t2atnlh3 t267;
                GM_ST(uint32_t, ((uint8_t *)&t267 + 0), t264);
                GM_ST(uint32_t, ((uint8_t *)&t267 + 4), t265);
                GM_ST(uint32_t, ((uint8_t *)&t267 + 8), t266);
                gm_l30 = t267;
            }
            goto ix244;
ie244:;
            {
                uint32_t t269 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
                uint32_t t270 = (t269 > ((uint32_t)1ULL) ? t269 : ((uint32_t)1ULL));
                uint32_t r271; bool o272 = __builtin_sub_overflow(t270, ((uint32_t)1ULL), &r271);
                if (o272) gm_trap("integer overflow in `-`", gm_srcdjp1iiokf034, 66, 19);
                uint32_t t273 = r271;
                gm_l33 = t273;
            }
            {
                uint32_t t274 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                uint32_t t275 = gm_l33;
                uint32_t t276 = gm_l33;
                gm_tl874t2atnlh3 t277;
                GM_ST(uint32_t, ((uint8_t *)&t277 + 0), t274);
                GM_ST(uint32_t, ((uint8_t *)&t277 + 4), t275);
                GM_ST(uint32_t, ((uint8_t *)&t277 + 8), t276);
                gm_l30 = t277;
            }
ix244:;
        }
        {
            memset((uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 140, 0, 4);
            gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
            gm_t2gtbh39ptekd t279;
            memset(&t279, 0, sizeof t279);
            GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)10ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t279 + 8), t278);
            gm_tl874t2atnlh3 t280 = gm_l30;
            gm_tc3oht4ksth9d t281;
            gm_f_synthetic_hdhcgpb((void *)&t281, &t279, GM_CONV(gm_cseahvg8v7p0vs, t280));
            gm_t22mtv3sd9prf t282;
            memset(&t282, 0, sizeof t282);
            GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t282 + 8), t281);
            gm_tl874t2atnlh3 t283 = gm_l30;
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 0, t282);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 128, t283);
        }
ix172:;
    }
    {
        uint64_t t284 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        gm_l34 = t284;
    }
    {
        uint64_t t286 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
        uint8_t t287 = (t286 > ((uint64_t)0ULL));
        if (!t287) goto ie285;
        {
            uint64_t t289 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t290 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            if (t289 >= (uintptr_t)(t290)) gm_trap_index(t289, t290, gm_srcdjp1iiokf034, 75, 18);
            uint64_t t291 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t289 * 8));
            gm_l35 = t291;
        }
        {
            gm_l36 = ((uint64_t)0ULL);
        }
        {
            {
                gm_l38 = ((uint64_t)0ULL);
            }
            {
                uint64_t t293 = gm_l35;
                gm_l37 = t293;
            }
            {
                for (;;) {
                    uint64_t t295 = gm_l38;
                    uint64_t t296 = gm_l37;
                    uint8_t t297 = (t295 < t296);
                    if (!t297) goto lb294;
                    {
                        uint64_t t300 = gm_l38;
                        uint64_t t301 = t300;
                        uint64_t t304 = GM_LD(uint64_t, ((uint8_t *)&t301));
                        gm_l39 = t304;
                        {
                            uint64_t t307 = gm_l39;
                            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                            gm_b16_8 t309 = t308;
                            uint8_t t310 = 0;
                            uint64_t t311 = t307;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t309) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t309))) + i * 8, ((uint8_t *)&t311))) { t310 = 1; break; }
                            uint8_t t312 = (uint8_t)(!t310);
                            if (!t312) goto ie306;
                            {
                                uint64_t t314 = gm_l36;
                                uint64_t r315; bool o316 = __builtin_add_overflow(t314, ((uint64_t)1ULL), &r315);
                                if (o316) gm_trap("integer overflow in `+`", gm_srcdjp1iiokf034, 78, 38);
                                uint64_t t317 = r315;
                                gm_l36 = t317;
                            }
                            goto ix306;
ie306:;
ix306:;
                        }
                        goto me302;
mn303:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 77, 9);
me302:;
                    }
lc294:;
                    {
                        uint64_t t319 = gm_l38;
                        uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                        uint64_t t322 = r320;
                        gm_l38 = t322;
                    }
                }
lb294:;
            }
        }
        {
            uint64_t t323 = gm_l36;
            gm_l34 = t323;
        }
        goto ix285;
ie285:;
ix285:;
    }
    {
        void * t324 = (void *)((uint8_t *)&F.l14);
        uint64_t t325 = gm_l34;
        uint64_t t326 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        uint64_t t327 = (t325 < t326 ? t325 : t326);
        gm_tftb1odc739t5 t328 = gm_f_insert_3qmicjr(t324, t327, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)));
        gm_tftb1odc739t5 t329 = t328;
        if (((((uint8_t *)&t329))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t329) + 1, gm_srcdjp1iiokf034, 82, 5);
        gm_b0_1 t330 = GM_LD(gm_b0_1, (((uint8_t *)&t329) + 1));
    }
    {
        gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_ts5t31j0bugq1 t332;
        GM_ST(uint8_t, (uint8_t *)&t332 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t332 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t332 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t332 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t332 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t332 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t332 + 8), t331);
        gm_tck5nas0nf5v8 t333;
        GM_ST(uint8_t, (uint8_t *)&t333 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t333 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t333 + 44, 0);
        GM_ST(gm_ts5t31j0bugq1, ((uint8_t *)&t333), t332);
        ((uint8_t *)&t333)[40] = 1;
        uint8_t t334 = 0;
        F.ret = t333;
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
inline __attribute__((aligned(4))) void gm_f_spread_array_v7s332a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_spread_array_v7s332a F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    GM_STACK_CHECK(&F, 615u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma9:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t15 != ((uint8_t)11ULL)) goto mn14;
        uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)&t7) + 8) + 12) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn14;
        {
            void * t18 = gm_l0;
            gm_tl874t2atnlh3 t19 = gm_l2;
            void * t20 = gm_l0;
            gm_t88rcucg1b7uk t21 = gm_l1;
            gm_b16_8 t22 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
            gm_tabb710d9mq2e t24;
            gm_f_format_q62qvnl((void *)&t24, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcdjp1iiokf034, 150, 28);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_b0_1 t29 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t19, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tb7u19loo3j87 t30;
            memset(&t30, 0, sizeof t30);
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto me8;
mn14:;
ma10:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t33 != ((uint8_t)11ULL)) goto mn32;
        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l3 = t34;
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 8));
        gm_l4 = t35;
        uint64_t t36 = gm_l3;
        gm_t88rcucg1b7uk t37 = gm_l4;
        gm_tlcm4lvu3g1a8 t38;
        GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t36);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38 + 8), t37);
        gm_tb7u19loo3j87 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 20, 0);
        GM_ST(gm_tlcm4lvu3g1a8, ((uint8_t *)&t39), t38);
        ((uint8_t *)&t39)[16] = 1;
        uint8_t t40 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me8;
mn32:;
ma11:;
        {
            void * t43 = gm_l0;
            gm_tl874t2atnlh3 t44 = gm_l2;
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l1;
            gm_b16_8 t47 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t48;
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
            gm_tabb710d9mq2e t49;
            gm_f_format_p5cs5qn((void *)&t49, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t50 = t49;
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcdjp1iiokf034, 155, 28);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
            gm_b16_8 t52 = t51;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_b0_1 t54 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t43, t44, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tb7u19loo3j87 t55;
            memset(&t55, 0, sizeof t55);
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto me8;
mn41:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 148, 5);
me8:;
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
__attribute__((aligned(4))) void gm_f_spread_generic_args_8qtof2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_spread_generic_args_8qtof2f F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    void * gm_l2; /* stmts */
    void * gm_l3; /* tys */
    uint64_t gm_l4; /* last_spread */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    gm_t08olq97rscn3 gm_l19; /* n */
    void * gm_l20; /* v */
    gm_t88rcucg1b7uk gm_l21; /* vt */
    gm_b16_8 gm_l22; /* name */
    uint32_t gm_l23; /* local */
    gm_t08olq97rscn3 gm_l26; /* n */
    gm_b16_8 gm_l28; /* es */
    gm_b16_8 gm_l29; /* ns */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    gm_tlcm4lvu3g1a8 gm_l33; /* arr */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    void * gm_l38; /* index */
    gm_th2lqbo77pvsc gm_l40; /* fs */
    gm_b16_8 gm_l41; /* f */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* i */
    gm_th2lqbo77pvsc gm_l48; /* p */
    gm_b16_8 gm_l49; /* n */
    gm_t08olq97rscn3 gm_l50; /* id */
    GM_STACK_CHECK(&F, 7502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t6 = gm_l1;
            gm_l5 = t6;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l6;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_tl1vi0nd1uql9 t13;
                    uint64_t t14 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l6;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t16 * 144), 144);
                    gm_tl1vi0nd1uql9 t18 = t13;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l7 = t21;
                    gm_t7te2tdpebbt4 t22 = GM_LD(gm_t7te2tdpebbt4, (((uint8_t *)&t18) + 8));
                    F.l8 = t22;
                    {
                        uint8_t t25;
                        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 0) + 0));
                        if (t28 != ((uint8_t)2ULL)) goto mn27;
                        t25 = ((uint8_t)1);
                        goto me26;
mn27:;
                        t25 = ((uint8_t)0);
                        goto me26;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 204, 12);
me26:;
                        if (!t25) goto ie24;
                        {
                            uint64_t t31 = gm_l7;
                            gm_l4 = t31;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 203, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t33 = gm_l6;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l6 = t36;
                }
            }
lb7:;
        }
    }
    {
        gm_f_list_6oos54v((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t38 = gm_l1;
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
                    gm_tl1vi0nd1uql9 t45;
                    uint64_t t46 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t45 + 0, t46);
                    uint64_t t47 = gm_l11;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t45 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t48 * 144), 144);
                    gm_tl1vi0nd1uql9 t50 = t45;
                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t50) + 0));
                    gm_l12 = t53;
                    gm_t7te2tdpebbt4 t54 = GM_LD(gm_t7te2tdpebbt4, (((uint8_t *)&t50) + 8));
                    F.l13 = t54;
                    {
                        gm_tc3oht4ksth9d t56;
                        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t61 != ((uint8_t)2ULL)) goto pa60;
                        gm_tc3oht4ksth9d t62 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l13) + 0) + 8));
                        F.l14 = t62;
                        goto po59;
pa60:;
                        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t64 != ((uint8_t)0ULL)) goto pa63;
                        gm_tc3oht4ksth9d t65 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l13) + 0) + 8));
                        F.l14 = t65;
                        goto po59;
pa63:;
                        goto mn58;
po59:;
                        gm_tc3oht4ksth9d t66 = F.l14;
                        t56 = t66;
                        goto me57;
mn58:;
                        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t68 != ((uint8_t)1ULL)) goto mn67;
                        gm_tc3oht4ksth9d t69 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l13) + 0) + 8) + 32));
                        F.l16 = t69;
                        gm_tc3oht4ksth9d t70 = F.l16;
                        t56 = t70;
                        goto me57;
mn67:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 208, 14);
me57:;
                        F.l17 = t56;
                    }
                    {
                        gm_t13p2dc04q01f t71;
                        memset(&t71, 0, sizeof t71);
                        F.l18 = t71;
                    }
                    {
                        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        switch (t77) { case ((uint8_t)0ULL): goto ma74; case ((uint8_t)1ULL): goto ma75; case ((uint8_t)2ULL): goto ma73; default: goto ma76; }
ma73:;
                        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t79 != ((uint8_t)2ULL)) goto mn78;
                        goto me72;
mn78:;
ma74:;
                        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t82 != ((uint8_t)0ULL)) goto mn81;
                        {
                            uint64_t t85 = gm_l12;
                            uint64_t t86 = gm_l4;
                            uint8_t t87 = (t85 >= t86);
                            if (!t87) goto ie84;
                            {
                                void * t89 = (void *)((uint8_t *)&F.l9);
                                gm_tftb1odc739t5 t90 = gm_f_push_jis5lhu(t89, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l13)));
                                gm_tftb1odc739t5 t91 = t90;
                                if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcdjp1iiokf034, 218, 21);
                                gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
                            }
                            {
                                goto lc39;
                            }
                            goto ix84;
ie84:;
ix84:;
                        }
                        {
                            gm_t4vsf0g4hsv81 t93;
                            gm_t1s75i08qtptd t94;
                            memset(&t94, 0, sizeof t94);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t93 + 0, t94);
                            gm_t13p2dc04q01f t95;
                            GM_ST(uint8_t, (uint8_t *)&t95 + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&t95 + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&t95 + 44, 0);
                            GM_ST(gm_t4vsf0g4hsv81, ((uint8_t *)&t95), t93);
                            ((uint8_t *)&t95)[40] = 1;
                            F.l18 = t95;
                        }
                        goto me72;
mn81:;
ma75:;
                        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t97 != ((uint8_t)1ULL)) goto mn96;
                        gm_t08olq97rscn3 t98 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l13) + 0) + 8) + 0));
                        gm_l19 = t98;
                        {
                            uint64_t t101 = gm_l12;
                            uint64_t t102 = gm_l4;
                            uint8_t t103 = (t101 >= t102);
                            if (!t103) goto ie100;
                            {
                                void * t105 = (void *)((uint8_t *)&F.l9);
                                gm_tftb1odc739t5 t106 = gm_f_push_jis5lhu(t105, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l13)));
                                gm_tftb1odc739t5 t107 = t106;
                                if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcdjp1iiokf034, 225, 21);
                                gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                            }
                            {
                                goto lc39;
                            }
                            goto ix100;
ie100:;
ix100:;
                        }
                        {
                            gm_t4vsf0g4hsv81 t109;
                            gm_t08olq97rscn3 t110 = gm_l19;
                            gm_t1s75i08qtptd t111;
                            GM_ST(uint8_t, (uint8_t *)&t111 + 33, 0);
                            GM_ST(uint16_t, (uint8_t *)&t111 + 34, 0);
                            GM_ST(uint32_t, (uint8_t *)&t111 + 36, 0);
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t111), t110);
                            ((uint8_t *)&t111)[32] = 1;
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t109 + 0, t111);
                            gm_t13p2dc04q01f t112;
                            GM_ST(uint8_t, (uint8_t *)&t112 + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&t112 + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&t112 + 44, 0);
                            GM_ST(gm_t4vsf0g4hsv81, ((uint8_t *)&t112), t109);
                            ((uint8_t *)&t112)[40] = 1;
                            F.l18 = t112;
                        }
                        goto me72;
mn96:;
ma76:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 214, 9);
me72:;
                    }
                    {
                        uint8_t t114;
                        if (((((uint8_t *)&F.l18))[40] != 0)) goto mn116;
                        t114 = ((uint8_t)0);
                        goto me115;
mn116:;
                        t114 = ((uint8_t)1);
                        goto me115;
mn117:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 232, 12);
me115:;
                        if (!t114) goto ie113;
                        uint8_t t118;
                        uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        switch (t125) { case ((uint8_t)0ULL): goto ma121; case ((uint8_t)1ULL): goto ma123; case ((uint8_t)2ULL): goto ma122; case ((uint8_t)3ULL): goto ma123; case ((uint8_t)4ULL): goto ma123; case ((uint8_t)5ULL): goto ma123; case ((uint8_t)6ULL): goto ma120; case ((uint8_t)7ULL): goto ma123; case ((uint8_t)8ULL): goto ma123; case ((uint8_t)9ULL): goto ma123; case ((uint8_t)10ULL): goto ma123; case ((uint8_t)11ULL): goto ma123; case ((uint8_t)12ULL): goto ma123; case ((uint8_t)13ULL): goto ma123; case ((uint8_t)14ULL): goto ma123; case ((uint8_t)15ULL): goto ma123; case ((uint8_t)16ULL): goto ma123; case ((uint8_t)17ULL): goto ma123; case ((uint8_t)18ULL): goto ma123; case ((uint8_t)19ULL): goto ma123; case ((uint8_t)20ULL): goto ma123; case ((uint8_t)21ULL): goto ma123; case ((uint8_t)22ULL): goto ma123; case ((uint8_t)23ULL): goto ma123; case ((uint8_t)24ULL): goto ma123; case ((uint8_t)25ULL): goto ma123; case ((uint8_t)26ULL): goto ma123; case ((uint8_t)27ULL): goto ma123; case ((uint8_t)28ULL): goto ma123; case ((uint8_t)29ULL): goto ma123; case ((uint8_t)30ULL): goto ma123; case ((uint8_t)31ULL): goto ma123; case ((uint8_t)32ULL): goto ma123; case ((uint8_t)33ULL): goto ma123; case ((uint8_t)34ULL): goto ma123; case ((uint8_t)35ULL): goto ma123; case ((uint8_t)36ULL): goto ma123; case ((uint8_t)37ULL): goto ma123; case ((uint8_t)38ULL): goto ma123; case ((uint8_t)39ULL): goto ma123; case ((uint8_t)40ULL): goto ma123; case ((uint8_t)41ULL): goto ma123; case ((uint8_t)42ULL): goto ma123; case ((uint8_t)43ULL): goto ma123; case ((uint8_t)44ULL): goto ma123; case ((uint8_t)45ULL): goto ma123; case ((uint8_t)46ULL): goto ma123; case ((uint8_t)47ULL): goto ma123; case ((uint8_t)48ULL): goto ma123; case ((uint8_t)49ULL): goto ma123; case ((uint8_t)50ULL): goto ma123; default: goto ma123; }
ma120:;
                        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        if (t127 != ((uint8_t)6ULL)) goto mn126;
                        t118 = ((uint8_t)1);
                        goto me119;
mn126:;
ma121:;
                        uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        if (t129 != ((uint8_t)0ULL)) goto mn128;
                        t118 = ((uint8_t)1);
                        goto me119;
mn128:;
ma122:;
                        uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        if (t131 != ((uint8_t)2ULL)) goto mn130;
                        t118 = ((uint8_t)1);
                        goto me119;
mn130:;
ma123:;
                        t118 = ((uint8_t)0);
                        goto me119;
mn132:;
ma124:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 232, 34);
me119:;
                        if (!t118) goto ie113;
                        {
                            void * t134 = (void *)((uint8_t *)&F.l9);
                            gm_tftb1odc739t5 t135 = gm_f_push_jis5lhu(t134, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l13)));
                            gm_tftb1odc739t5 t136 = t135;
                            if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_srcdjp1iiokf034, 233, 13);
                            gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
                        }
                        {
                            goto lc39;
                        }
                        goto ix113;
ie113:;
ix113:;
                    }
                    {
                        void * t138 = gm_l0;
                        void * t139 = (void *)((uint8_t *)&F.l17);
                        gm_tkn42u7na3t81 t140;
                        memset(&t140, 0, sizeof t140);
                        void * t141 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t138, t139, t140);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_l20 = t141;
                    }
                    {
                        void * t142 = gm_l0;
                        void * t143 = (void *)(((uint8_t *)t142) + 96);
                        void * t144 = gm_l0;
                        void * t145 = GM_LD(void *, (((uint8_t *)t144) + 0));
                        void * t146 = gm_l20;
                        gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t146) + 0));
                        gm_t88rcucg1b7uk t148 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t143, t145, t147);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_l21 = t148;
                    }
                    {
                        void * t149 = gm_l0;
                        uint64_t t150 = GM_LD(uintptr_t, ((((uint8_t *)t149) + 16) + 0) + 8);
                        gm_t4ce0jd3tdro4 t151;
                        GM_ST(uint64_t, ((uint8_t *)&t151 + 0), t150);
                        gm_tabb710d9mq2e t152;
                        gm_f_format_uubl3s3((void *)&t152, (void *)NULL, cx, NULL, t151);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_tabb710d9mq2e t153 = t152;
                        if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 8, gm_srcdjp1iiokf034, 238, 17);
                        gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&t153) + 8));
                        gm_l22 = t154;
                    }
                    {
                        void * t155 = gm_l0;
                        gm_b16_8 t156 = gm_l22;
                        gm_b16_8 t157 = t156;
                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                        gm_t88rcucg1b7uk t159 = gm_l21;
                        gm_tl874t2atnlh3 t160 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        uint32_t t161 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t155, t158, t159, ((uint8_t)0), t160);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_l23 = t161;
                    }
                    {
                        void * t162 = gm_l2;
                        uint32_t t163 = gm_l23;
                        void * t164 = gm_l20;
                        gm_ttggq7ng4kp3p t165;
                        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t165 + 0), t163);
                        GM_ST(void *, ((uint8_t *)&t165 + 8), t164);
                        gm_trcmmd12cefb1 t166;
                        GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t166 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t166 + 8), t165);
                        gm_tftb1odc739t5 t167 = gm_f_push_d6c76ud_2(t162, &t166);
                        gm_tftb1odc739t5 t168 = t167;
                        if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_srcdjp1iiokf034, 240, 9);
                        gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
                    }
                    {
                        void * t170 = gm_l0;
                        gm_b16_8 t171 = gm_l22;
                        gm_b16_8 t172 = t171;
                        gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                        uint32_t t174 = gm_l23;
                        gm_t930q5l38hdap t175;
                        memset(&t175, 0, sizeof t175);
                        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t175 + 8), t174);
                        gm_tl874t2atnlh3 t176 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        gm_b0_1 t177 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t170, t173, &t175, t176);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                    }
                    {
                        gm_t08olq97rscn3 t178;
                        memset((uint8_t *)&t178 + 28, 0, 4);
                        gm_b16_8 t179 = gm_l22;
                        gm_b16_8 t180 = t179;
                        gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                        GM_ST(gm_b16_8, (uint8_t *)&t178 + 0, t181);
                        gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t178 + 16, t182);
                        gm_t2gtbh39ptekd t183;
                        GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t183 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t183 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t183 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t183 + 8), t178);
                        gm_tl874t2atnlh3 t184 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        gm_f_synthetic_hdhcgpb((void *)&F.l24, &t183, GM_CONV(gm_cseahvg8v7p0vs, t184));
                    }
                    {
                        if (!((((uint8_t *)&F.l18))[40] != 0)) goto ie185;
                        gm_t4vsf0g4hsv81 t186 = GM_LD(gm_t4vsf0g4hsv81, ((uint8_t *)&F.l18));
                        F.l25 = t186;
                        {
                            if (!(((((uint8_t *)&F.l25) + 0))[32] != 0)) goto ie188;
                            gm_t08olq97rscn3 t189 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l25) + 0));
                            gm_l26 = t189;
                            {
                                void * t191 = (void *)((uint8_t *)&F.l9);
                                gm_t7te2tdpebbt4 t192;
                                memset((uint8_t *)&t192 + 140, 0, 4);
                                gm_t56mj5hnm2uc6 t193;
                                memcpy((uint8_t *)&t193 + 0, ((uint8_t *)&gm_l26), 32);
                                memcpy((uint8_t *)&t193 + 32, ((uint8_t *)&F.l24), 88);
                                gm_t22mtv3sd9prf t194;
                                GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t194 + 8), t193);
                                GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t192 + 0, t194);
                                gm_tl874t2atnlh3 t195 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t192 + 128, t195);
                                gm_tftb1odc739t5 t196 = gm_f_push_jis5lhu(t191, &t192);
                                gm_tftb1odc739t5 t197 = t196;
                                if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srcdjp1iiokf034, 245, 17);
                                gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                            }
                            goto ix188;
ie188:;
                            {
                                void * t200 = (void *)((uint8_t *)&F.l9);
                                gm_t7te2tdpebbt4 t201;
                                memset((uint8_t *)&t201 + 140, 0, 4);
                                gm_tc3oht4ksth9d t202 = F.l24;
                                gm_t22mtv3sd9prf t203;
                                memset(&t203, 0, sizeof t203);
                                GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t203 + 8), t202);
                                GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t201 + 0, t203);
                                gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t201 + 128, t204);
                                gm_tftb1odc739t5 t205 = gm_f_push_jis5lhu(t200, &t201);
                                gm_tftb1odc739t5 t206 = t205;
                                if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 1, gm_srcdjp1iiokf034, 247, 17);
                                gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t206) + 1));
                            }
ix188:;
                        }
                        {
                            goto lc39;
                        }
                        goto ix185;
ie185:;
ix185:;
                    }
                    {
                        gm_f_list_0c2v515((void *)&F.l27, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                    }
                    {
                        void * t208 = gm_l3;
                        gm_t88rcucg1b7uk t209 = gm_l21;
                        gm_t53at80pfdus3 t210;
                        gm_f_kind_jcnt4hm((void *)&t210, t208, GM_CONV(gm_csguankko4lmhd, t209));
                        gm_t53at80pfdus3 t211 = t210;
                        uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                        switch (t218) { case ((uint8_t)0ULL): goto ma216; case ((uint8_t)1ULL): goto ma216; case ((uint8_t)2ULL): goto ma216; case ((uint8_t)3ULL): goto ma216; case ((uint8_t)4ULL): goto ma216; case ((uint8_t)5ULL): goto ma216; case ((uint8_t)6ULL): goto ma216; case ((uint8_t)7ULL): goto ma216; case ((uint8_t)8ULL): goto ma216; case ((uint8_t)9ULL): goto ma216; case ((uint8_t)10ULL): goto ma216; case ((uint8_t)11ULL): goto ma215; case ((uint8_t)12ULL): goto ma213; case ((uint8_t)13ULL): goto ma216; case ((uint8_t)14ULL): goto ma216; case ((uint8_t)15ULL): goto ma216; case ((uint8_t)16ULL): goto ma216; case ((uint8_t)17ULL): goto ma216; case ((uint8_t)18ULL): goto ma216; case ((uint8_t)19ULL): goto ma216; case ((uint8_t)20ULL): goto ma215; case ((uint8_t)21ULL): goto ma216; case ((uint8_t)22ULL): goto ma216; case ((uint8_t)23ULL): goto ma216; case ((uint8_t)24ULL): goto ma216; case ((uint8_t)25ULL): goto ma216; case ((uint8_t)26ULL): goto ma214; default: goto ma216; }
ma213:;
                        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                        if (t220 != ((uint8_t)12ULL)) goto mn219;
                        gm_b16_8 t221 = GM_LD(gm_b16_8, ((((uint8_t *)&t211) + 8) + 0));
                        gm_l28 = t221;
                        gm_b16_8 t222 = GM_LD(gm_b16_8, ((((uint8_t *)&t211) + 8) + 16));
                        gm_l29 = t222;
                        {
                            gm_l31 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            gm_l30 = t224;
                        }
                        {
                            for (;;) {
                                uint64_t t226 = gm_l31;
                                uint64_t t227 = gm_l30;
                                uint8_t t228 = (t226 < t227);
                                if (!t228) goto lb225;
                                {
                                    uint64_t t231 = gm_l31;
                                    uint64_t t232 = t231;
                                    uint64_t t235 = GM_LD(uint64_t, ((uint8_t *)&t232));
                                    gm_l32 = t235;
                                    {
                                        void * t237 = (void *)((uint8_t *)&F.l27);
                                        gm_th2lqbo77pvsc t238;
                                        uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                        uint8_t t241 = (t240 > ((uint64_t)0ULL));
                                        if (!t241) goto ie239;
                                        uint64_t t243 = gm_l32;
                                        uint64_t t244 = (uintptr_t)t243;
                                        uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                        if (t244 >= (uintptr_t)(t245)) gm_trap_index(t244, t245, gm_srcdjp1iiokf034, 253, 79);
                                        gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t244 * 16));
                                        gm_b16_8 t247 = t246;
                                        gm_th2lqbo77pvsc t248;
                                        GM_ST(uint8_t, (uint8_t *)&t248 + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t248 + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t248 + 20, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t248), t247);
                                        ((uint8_t *)&t248)[16] = 1;
                                        t238 = t248;
                                        goto ix239;
ie239:;
                                        gm_th2lqbo77pvsc t250;
                                        memset(&t250, 0, sizeof t250);
                                        gm_th2lqbo77pvsc t251 = t250;
                                        t238 = t251;
ix239:;
                                        gm_tftb1odc739t5 t252 = gm_f_push_d0271rf(t237, &t238);
                                        gm_tftb1odc739t5 t253 = t252;
                                        if (((((uint8_t *)&t253))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t253) + 1, gm_srcdjp1iiokf034, 253, 52);
                                        gm_b0_1 t254 = GM_LD(gm_b0_1, (((uint8_t *)&t253) + 1));
                                    }
                                    goto me233;
mn234:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 253, 31);
me233:;
                                }
lc225:;
                                {
                                    uint64_t t256 = gm_l31;
                                    uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                                    uint64_t t259 = r257;
                                    gm_l31 = t259;
                                }
                            }
lb225:;
                        }
                        goto me212;
mn219:;
ma214:;
                        uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                        if (t261 != ((uint8_t)26ULL)) goto mn260;
                        gm_th2lqbo77pvsc t262;
                        memset(&t262, 0, sizeof t262);
                        uint8_t t263 = 0;
                        F.ret = t262;
                        goto gm_fret;
                        goto me212;
mn260:;
ma215:;
                        uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                        if (t267 != ((uint8_t)11ULL)) goto pa266;
                        goto po265;
pa266:;
                        uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                        if (t269 != ((uint8_t)20ULL)) goto pa268;
                        goto po265;
pa268:;
                        goto mn264;
po265:;
                        {
                            void * t271 = gm_l0;
                            gm_t88rcucg1b7uk t272 = gm_l21;
                            gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                            gm_tb7u19loo3j87 t274;
                            gm_f_spread_array_v7s332a((void *)&t274, (void *)NULL, cx, NULL, t271, t272, t273);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_tb7u19loo3j87 t275 = t274;
                            gm_tlcm4lvu3g1a8 t276;
                            if (((((uint8_t *)&t275))[16] != 0)) {
                                gm_tlcm4lvu3g1a8 t277 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t275) + 0));
                                t276 = t277;
                            } else {
                                gm_th2lqbo77pvsc t278;
                                memset(&t278, 0, sizeof t278);
                                uint8_t t279 = 0;
                                F.ret = t278;
                                goto gm_fret;
                            }
                            gm_l33 = t276;
                        }
                        {
                            {
                                gm_l35 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 0));
                                gm_l34 = t281;
                            }
                            {
                                for (;;) {
                                    uint64_t t283 = gm_l35;
                                    uint64_t t284 = gm_l34;
                                    uint8_t t285 = (t283 < t284);
                                    if (!t285) goto lb282;
                                    {
                                        uint64_t t288 = gm_l35;
                                        uint64_t t289 = t288;
                                        uint64_t t292 = GM_LD(uint64_t, ((uint8_t *)&t289));
                                        gm_l36 = t292;
                                        {
                                            memset((uint8_t *)&F.l37 + 61, 0, 3);
                                            uint64_t t294 = gm_l36;
                                            gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l37 + 0)), (void *)NULL, cx, NULL, t294);
                                            if (gm_unw.kind) {
                                                goto gm_ll286;
                                            }
                                            gm_t46lmulm8gaj3 t295;
                                            memset(&t295, 0, sizeof t295);
                                            GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&F.l37 + 48, t295);
                                            GM_ST(uint8_t, (uint8_t *)&F.l37 + 60, ((uint8_t)0));
                                        }
                                        {
                                            gm_tq7s98ihonkju t296 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &(*(gm_ta42iqot1auae *)((uint8_t *)&F.l37)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjn96jdce98)));
                                            if (gm_unw.kind) {
                                                goto gm_ll286;
                                            }
                                            gm_tq7s98ihonkju t297 = t296;
                                            if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 8, gm_srcdjp1iiokf034, 259, 49);
                                            void * t298 = GM_LD(void *, (((uint8_t *)&t297) + 8));
                                            gm_t2gtbh39ptekd t299;
                                            memset(&t299, 0, sizeof t299);
                                            GM_ST(uint8_t, ((uint8_t *)&t299 + 0), ((uint8_t)0ULL));
                                            GM_ST(void *, ((uint8_t *)&t299 + 8), t298);
                                            gm_tl874t2atnlh3 t300 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_tc3oht4ksth9d t301;
                                            gm_f_synthetic_hdhcgpb((void *)&t301, &t299, GM_CONV(gm_cseahvg8v7p0vs, t300));
                                            gm_tq7s98ihonkju t302 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t301, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kqn03v7t2qt)));
                                            if (gm_unw.kind) {
                                                goto gm_ll286;
                                            }
                                            gm_tq7s98ihonkju t303 = t302;
                                            if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 8, gm_srcdjp1iiokf034, 259, 30);
                                            void * t304 = GM_LD(void *, (((uint8_t *)&t303) + 8));
                                            gm_l38 = t304;
                                        }
                                        {
                                            gm_tq7s98ihonkju t305 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l24)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbjtngq55qv)));
                                            if (gm_unw.kind) {
                                                goto gm_ll286;
                                            }
                                            gm_tq7s98ihonkju t306 = t305;
                                            if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 8, gm_srcdjp1iiokf034, 260, 46);
                                            void * t307 = GM_LD(void *, (((uint8_t *)&t306) + 8));
                                            void * t308 = gm_l38;
                                            gm_t3mf58gv9kf3n t309;
                                            GM_ST(void *, ((uint8_t *)&t309 + 0), t307);
                                            GM_ST(void *, ((uint8_t *)&t309 + 8), t308);
                                            gm_t2gtbh39ptekd t310;
                                            memset(&t310, 0, sizeof t310);
                                            GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)20ULL));
                                            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t310 + 8), t309);
                                            gm_tl874t2atnlh3 t311 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_f_synthetic_hdhcgpb((void *)&F.l39, &t310, GM_CONV(gm_cseahvg8v7p0vs, t311));
                                        }
                                        {
                                            void * t312 = (void *)((uint8_t *)&F.l9);
                                            gm_t7te2tdpebbt4 t313;
                                            memset((uint8_t *)&t313 + 140, 0, 4);
                                            gm_tc3oht4ksth9d t314 = F.l39;
                                            gm_t22mtv3sd9prf t315;
                                            memset(&t315, 0, sizeof t315);
                                            GM_ST(uint8_t, ((uint8_t *)&t315 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t315 + 8), t314);
                                            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t313 + 0, t315);
                                            gm_tl874t2atnlh3 t316 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t313 + 128, t316);
                                            gm_tftb1odc739t5 t317 = gm_f_push_jis5lhu(t312, &t313);
                                            gm_tftb1odc739t5 t318 = t317;
                                            if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 1, gm_srcdjp1iiokf034, 261, 21);
                                            gm_b0_1 t319 = GM_LD(gm_b0_1, (((uint8_t *)&t318) + 1));
                                        }
                                        goto me290;
mn291:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 257, 17);
me290:;
                                    }
lc282:;
                                    {
                                        uint64_t t321 = gm_l35;
                                        uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                                        uint64_t t324 = r322;
                                        gm_l35 = t324;
                                    }
                                }
gm_ll286:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb282; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc282; }
                                }
                                goto gm_ll43;
lb282:;
                            }
                        }
                        {
                            goto lc39;
                        }
                        goto me212;
mn264:;
ma216:;
                        {
                            void * t327 = gm_l0;
                            void * t328 = GM_LD(void *, (((uint8_t *)t327) + 0));
                            gm_t88rcucg1b7uk t329 = gm_l21;
                            gm_f_struct_fields_chqu3it((void *)&gm_l40, t328, GM_CONV(gm_csguankko4lmhd, t329));
                        }
                        {
                            if (!((((uint8_t *)&gm_l40))[16] != 0)) goto ie330;
                            gm_b16_8 t331 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l40));
                            gm_l41 = t331;
                            void * t332 = gm_l0;
                            gm_t88rcucg1b7uk t333 = gm_l21;
                            gm_ttu09lij36f8c t334;
                            gm_f_hidden_field_u7uao2s((void *)&t334, t332, GM_CONV(gm_csguankko4lmhd, t333));
                            gm_ttu09lij36f8c t335 = t334;
                            uint8_t t336;
                            if (((((uint8_t *)&t335))[32] != 0)) goto mn338;
                            t336 = ((uint8_t)1);
                            goto me337;
mn338:;
                            t336 = ((uint8_t)0);
                            goto me337;
mn339:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 267, 32);
me337:;
                            if (!t336) goto ie330;
                            {
                                {
                                    gm_b16_8 t342 = gm_l41;
                                    gm_l42 = t342;
                                }
                                {
                                    gm_l43 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t344 = gm_l43;
                                        uint64_t t345 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        uint8_t t346 = (t344 < t345);
                                        if (!t346) goto lb343;
                                        {
                                            uint64_t t349 = gm_l43;
                                            uint64_t t350 = (uintptr_t)t349;
                                            uint64_t t351 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                            gm_t0mglksrkfa5e t354 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t350 * 40));
                                            F.l44 = t354;
                                            {
                                                void * t356 = (void *)((uint8_t *)&F.l27);
                                                gm_b16_8 t357 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 0));
                                                gm_th2lqbo77pvsc t358;
                                                GM_ST(uint8_t, (uint8_t *)&t358 + 17, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t358 + 18, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t358 + 20, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t358), t357);
                                                ((uint8_t *)&t358)[16] = 1;
                                                gm_tftb1odc739t5 t359 = gm_f_push_d0271rf(t356, &t358);
                                                gm_tftb1odc739t5 t360 = t359;
                                                if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 1, gm_srcdjp1iiokf034, 268, 34);
                                                gm_b0_1 t361 = GM_LD(gm_b0_1, (((uint8_t *)&t360) + 1));
                                            }
                                            goto me352;
mn353:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 268, 21);
me352:;
                                        }
lc343:;
                                        {
                                            uint64_t t363 = gm_l43;
                                            uint64_t r364; bool o365 = __builtin_add_overflow(t363, ((uint64_t)1ULL), &r364);
                                            uint64_t t366 = r364;
                                            gm_l43 = t366;
                                        }
                                    }
lb343:;
                                }
                            }
                            goto ix330;
ie330:;
                            {
                                void * t368 = gm_l0;
                                gm_tl874t2atnlh3 t369 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                void * t370 = gm_l0;
                                gm_t88rcucg1b7uk t371 = gm_l21;
                                gm_b16_8 t372 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t370, t371);
                                if (gm_unw.kind) {
                                    goto gm_ll43;
                                }
                                gm_tnpifv20irnph t373;
                                GM_ST(gm_b16_8, ((uint8_t *)&t373 + 0), t372);
                                gm_tabb710d9mq2e t374;
                                gm_f_format_7f8ekea((void *)&t374, (void *)NULL, cx, NULL, t373);
                                if (gm_unw.kind) {
                                    goto gm_ll43;
                                }
                                gm_tabb710d9mq2e t375 = t374;
                                if (((((uint8_t *)&t375))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t375) + 8, gm_srcdjp1iiokf034, 270, 38);
                                gm_b16_8 t376 = GM_LD(gm_b16_8, (((uint8_t *)&t375) + 8));
                                gm_b16_8 t377 = t376;
                                gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&t377));
                                gm_b0_1 t379 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t368, t369, t378);
                                if (gm_unw.kind) {
                                    goto gm_ll43;
                                }
                            }
                            {
                                gm_th2lqbo77pvsc t380;
                                memset(&t380, 0, sizeof t380);
                                uint8_t t381 = 0;
                                F.ret = t380;
                                goto gm_fret;
                            }
ix330:;
                        }
                        goto me212;
mn325:;
ma217:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 252, 9);
me212:;
                    }
                    {
                        {
                            gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                            gm_b16_8 t384 = t383;
                            gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&t384));
                            gm_l45 = t385;
                        }
                        {
                            gm_l46 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t387 = gm_l46;
                                uint64_t t388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                                uint8_t t389 = (t387 < t388);
                                if (!t389) goto lb386;
                                {
                                    gm_t13urt52695sh t392;
                                    uint64_t t393 = gm_l46;
                                    GM_ST(uint64_t, (uint8_t *)&t392 + 0, t393);
                                    uint64_t t394 = gm_l46;
                                    uint64_t t395 = (uintptr_t)t394;
                                    uint64_t t396 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                                    memcpy((uint8_t *)&t392 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t395 * 24), 24);
                                    gm_t13urt52695sh t397 = t392;
                                    uint64_t t400 = GM_LD(uint64_t, (((uint8_t *)&t397) + 0));
                                    gm_l47 = t400;
                                    gm_th2lqbo77pvsc t401 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t397) + 8));
                                    gm_l48 = t401;
                                    {
                                        if (!((((uint8_t *)&gm_l48))[16] != 0)) goto ie403;
                                        gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l48));
                                        gm_l49 = t404;
                                        {
                                            memset((uint8_t *)&gm_l50 + 28, 0, 4);
                                            gm_b16_8 t406 = gm_l49;
                                            GM_ST(gm_b16_8, (uint8_t *)&gm_l50 + 0, t406);
                                            gm_tl874t2atnlh3 t407 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&gm_l50 + 16, t407);
                                        }
                                        {
                                            void * t408 = (void *)((uint8_t *)&F.l9);
                                            gm_t7te2tdpebbt4 t409;
                                            memset((uint8_t *)&t409 + 140, 0, 4);
                                            gm_t56mj5hnm2uc6 t410;
                                            memcpy((uint8_t *)&t410 + 0, ((uint8_t *)&gm_l50), 32);
                                            gm_tioie2ju3q4ul t411;
                                            gm_tq7s98ihonkju t412 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l24)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ka2mtmecqk3)));
                                            if (gm_unw.kind) {
                                                goto gm_ll390;
                                            }
                                            gm_tq7s98ihonkju t413 = t412;
                                            if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 8, gm_srcdjp1iiokf034, 278, 72);
                                            void * t414 = GM_LD(void *, (((uint8_t *)&t413) + 8));
                                            GM_ST(void *, (uint8_t *)&t411 + 0, t414);
                                            memcpy((uint8_t *)&t411 + 8, ((uint8_t *)&gm_l50), 32);
                                            gm_t2gtbh39ptekd t415;
                                            GM_ST(uint8_t, (uint8_t *)&t415 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t415 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t415 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t415 + 48, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t415 + 56, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t415 + 0), ((uint8_t)22ULL));
                                            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t415 + 8), t411);
                                            gm_tl874t2atnlh3 t416 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_f_synthetic_hdhcgpb((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&t410 + 32)), &t415, GM_CONV(gm_cseahvg8v7p0vs, t416));
                                            gm_t22mtv3sd9prf t417;
                                            GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t417 + 8), t410);
                                            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t409 + 0, t417);
                                            gm_tl874t2atnlh3 t418 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t409 + 128, t418);
                                            gm_tftb1odc739t5 t419 = gm_f_push_jis5lhu(t408, &t409);
                                            gm_tftb1odc739t5 t420 = t419;
                                            if (((((uint8_t *)&t420))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t420) + 1, gm_srcdjp1iiokf034, 278, 17);
                                            gm_b0_1 t421 = GM_LD(gm_b0_1, (((uint8_t *)&t420) + 1));
                                        }
                                        goto ix403;
ie403:;
                                        {
                                            void * t423 = (void *)((uint8_t *)&F.l9);
                                            gm_t7te2tdpebbt4 t424;
                                            memset((uint8_t *)&t424 + 140, 0, 4);
                                            gm_tupk62cr6lbdu t425;
                                            gm_tq7s98ihonkju t426 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l24)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ke30i57o09o)));
                                            if (gm_unw.kind) {
                                                goto gm_ll390;
                                            }
                                            gm_tq7s98ihonkju t427 = t426;
                                            if (((((uint8_t *)&t427))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t427) + 8, gm_srcdjp1iiokf034, 280, 78);
                                            void * t428 = GM_LD(void *, (((uint8_t *)&t427) + 8));
                                            GM_ST(void *, (uint8_t *)&t425 + 0, t428);
                                            uint64_t t429 = gm_l47;
                                            uint32_t t430 = (uint32_t)t429;
                                            GM_ST(uint32_t, (uint8_t *)&t425 + 8, t430);
                                            gm_tl874t2atnlh3 t431 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t425 + 12, t431);
                                            gm_t2gtbh39ptekd t432;
                                            memset(&t432, 0, sizeof t432);
                                            GM_ST(uint8_t, ((uint8_t *)&t432 + 0), ((uint8_t)23ULL));
                                            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t432 + 8), t425);
                                            gm_tl874t2atnlh3 t433 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_tc3oht4ksth9d t434;
                                            gm_f_synthetic_hdhcgpb((void *)&t434, &t432, GM_CONV(gm_cseahvg8v7p0vs, t433));
                                            gm_t22mtv3sd9prf t435;
                                            memset(&t435, 0, sizeof t435);
                                            GM_ST(uint8_t, ((uint8_t *)&t435 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t435 + 8), t434);
                                            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t424 + 0, t435);
                                            gm_tl874t2atnlh3 t436 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t424 + 128, t436);
                                            gm_tftb1odc739t5 t437 = gm_f_push_jis5lhu(t423, &t424);
                                            gm_tftb1odc739t5 t438 = t437;
                                            if (((((uint8_t *)&t438))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t438) + 1, gm_srcdjp1iiokf034, 280, 17);
                                            gm_b0_1 t439 = GM_LD(gm_b0_1, (((uint8_t *)&t438) + 1));
                                        }
ix403:;
                                    }
                                    goto me398;
mn399:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 275, 9);
me398:;
                                }
lc386:;
                                {
                                    uint64_t t441 = gm_l46;
                                    uint64_t r442; bool o443 = __builtin_add_overflow(t441, ((uint64_t)1ULL), &r442);
                                    uint64_t t444 = r442;
                                    gm_l46 = t444;
                                }
                            }
gm_ll390:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb386; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc386; }
                            }
                            goto gm_ll43;
lb386:;
                        }
                    }
                    goto me51;
mn52:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 207, 5);
me51:;
                }
lc39:;
                {
                    uint64_t t446 = gm_l11;
                    uint64_t r447; bool o448 = __builtin_add_overflow(t446, ((uint64_t)1ULL), &r447);
                    uint64_t t449 = r447;
                    gm_l11 = t449;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        gm_b16_8 t450 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_th2lqbo77pvsc t451;
        GM_ST(uint8_t, (uint8_t *)&t451 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t451 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t451 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t451), t450);
        ((uint8_t *)&t451)[16] = 1;
        uint8_t t452 = 0;
        F.ret = t451;
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
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_spread_parts_e62q2l2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* stmts */
    void * gm_l3; /* tys */
    void * gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_b16_8 gm_l6; /* elems */
    gm_b16_8 gm_l7; /* names */
    gm_b16_8 gm_l8; /* es */
    gm_b16_8 gm_l9; /* ns */
    gm_tlcm4lvu3g1a8 gm_l10; /* a */
    void * gm_l11; /* base */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_th2lqbo77pvsc gm_l16; /* fs */
    gm_b16_8 gm_l17; /* f */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    void * gm_l23; /* base */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    gm_t88rcucg1b7uk gm_l28; /* t */
    void * gm_l29; /* proj */
    gm_th2lqbo77pvsc gm_l30; /* name */
    GM_STACK_CHECK(&F, 2243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        gm_tkn42u7na3t81 t7;
        memset(&t7, 0, sizeof t7);
        void * t8 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 96);
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_t88rcucg1b7uk t15 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t10, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t15;
    }
    {
        gm_b16_8 t16 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t17 = t16;
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_l6 = t18;
    }
    {
        gm_b16_8 t19 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_l7 = t21;
    }
    {
        void * t22 = gm_l3;
        gm_t88rcucg1b7uk t23 = gm_l5;
        gm_t53at80pfdus3 t24;
        gm_f_kind_jcnt4hm((void *)&t24, t22, GM_CONV(gm_csguankko4lmhd, t23));
        gm_t53at80pfdus3 t25 = t24;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma29; case ((uint8_t)12ULL): goto ma27; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma30; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma29; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma30; case ((uint8_t)24ULL): goto ma30; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma28; default: goto ma30; }
ma27:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t34 != ((uint8_t)12ULL)) goto mn33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((((uint8_t *)&t25) + 8) + 0));
        gm_l8 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((((uint8_t *)&t25) + 8) + 16));
        gm_l9 = t36;
        {
            gm_b16_8 t38 = gm_l8;
            gm_l6 = t38;
        }
        {
            gm_b16_8 t39 = gm_l9;
            gm_l7 = t39;
        }
        goto me26;
mn33:;
ma28:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t41 != ((uint8_t)26ULL)) goto mn40;
        gm_th2lqbo77pvsc t42;
        memset(&t42, 0, sizeof t42);
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
        goto me26;
mn40:;
ma29:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t47 != ((uint8_t)11ULL)) goto pa46;
        goto po45;
pa46:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t49 != ((uint8_t)20ULL)) goto pa48;
        goto po45;
pa48:;
        goto mn44;
po45:;
        {
            void * t51 = gm_l0;
            gm_t88rcucg1b7uk t52 = gm_l5;
            void * t53 = gm_l1;
            gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
            gm_tb7u19loo3j87 t55;
            gm_f_spread_array_v7s332a((void *)&t55, (void *)NULL, cx, NULL, t51, t52, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb7u19loo3j87 t56 = t55;
            gm_tlcm4lvu3g1a8 t57;
            if (((((uint8_t *)&t56))[16] != 0)) {
                gm_tlcm4lvu3g1a8 t58 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t56) + 0));
                t57 = t58;
            } else {
                gm_th2lqbo77pvsc t59;
                memset(&t59, 0, sizeof t59);
                uint8_t t60 = 0;
                F.ret = t59;
                goto gm_fret;
            }
            gm_l10 = t57;
        }
        {
            void * t61 = gm_l0;
            void * t62 = gm_l4;
            void * t63 = gm_l2;
            void * t64 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t61, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t64;
        }
        {
            gm_f_list_j5ps4qc((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                gm_l13 = t66;
            }
            {
                for (;;) {
                    uint64_t t68 = gm_l14;
                    uint64_t t69 = gm_l13;
                    uint8_t t70 = (t68 < t69);
                    if (!t70) goto lb67;
                    {
                        uint64_t t73 = gm_l14;
                        uint64_t t74 = t73;
                        uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)&t74));
                        gm_l15 = t77;
                        {
                            void * t79 = (void *)((uint8_t *)&F.l12);
                            gm_tpouv9r7lrri2 t80;
                            gm_th2lqbo77pvsc t81;
                            memset(&t81, 0, sizeof t81);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t80 + 0, t81);
                            void * t82 = gm_l0;
                            void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
                            void * t84 = gm_l11;
                            gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t86 = gm_l15;
                            void * t87 = gm_l1;
                            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
                            void * t89 = gm_f_array_element_t6q5jtp((void *)NULL, cx, NULL, t83, t84, t85, t86, t88);
                            if (gm_unw.kind) {
                                goto gm_ll71;
                            }
                            GM_ST(void *, (uint8_t *)&t80 + 24, t89);
                            gm_tftb1odc739t5 t90 = gm_f_push_bsig5q3(t79, &t80);
                            gm_tftb1odc739t5 t91 = t90;
                            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcdjp1iiokf034, 114, 33);
                            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
                        }
                        goto me75;
mn76:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 114, 13);
me75:;
                    }
lc67:;
                    {
                        uint64_t t94 = gm_l14;
                        uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                        uint64_t t97 = r95;
                        gm_l14 = t97;
                    }
                }
gm_ll71:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb67; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc67; }
                }
                goto gm_lad;
lb67:;
            }
        }
        {
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_th2lqbo77pvsc t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t99), t98);
            ((uint8_t *)&t99)[16] = 1;
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
        }
        goto me26;
mn44:;
ma30:;
        {
            void * t103 = gm_l0;
            void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
            gm_t88rcucg1b7uk t105 = gm_l5;
            gm_f_struct_fields_chqu3it((void *)&gm_l16, t104, GM_CONV(gm_csguankko4lmhd, t105));
        }
        {
            if (!((((uint8_t *)&gm_l16))[16] != 0)) goto ie106;
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l17 = t107;
            void * t108 = gm_l0;
            gm_t88rcucg1b7uk t109 = gm_l5;
            gm_ttu09lij36f8c t110;
            gm_f_hidden_field_u7uao2s((void *)&t110, t108, GM_CONV(gm_csguankko4lmhd, t109));
            gm_ttu09lij36f8c t111 = t110;
            uint8_t t112;
            if (((((uint8_t *)&t111))[32] != 0)) goto mn114;
            t112 = ((uint8_t)1);
            goto me113;
mn114:;
            t112 = ((uint8_t)0);
            goto me113;
mn115:;
            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 119, 28);
me113:;
            if (!t112) goto ie106;
            {
                gm_f_list_nof7vfj((void *)&F.l18, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_epvducg((void *)&F.l19, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t118 = gm_l17;
                    gm_l20 = t118;
                }
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t120 = gm_l21;
                        uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        uint8_t t122 = (t120 < t121);
                        if (!t122) goto lb119;
                        {
                            uint64_t t125 = gm_l21;
                            uint64_t t126 = (uintptr_t)t125;
                            uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                            gm_t0mglksrkfa5e t130 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t126 * 40));
                            F.l22 = t130;
                            {
                                void * t132 = (void *)((uint8_t *)&F.l18);
                                gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l22) + 16));
                                gm_tftb1odc739t5 t134 = gm_f_push_k3l9nic(t132, GM_CONV(gm_csguankko4lmhd, t133));
                                gm_tftb1odc739t5 t135 = t134;
                                if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcdjp1iiokf034, 123, 21);
                                gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                            }
                            {
                                void * t137 = (void *)((uint8_t *)&F.l19);
                                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                                gm_tftb1odc739t5 t139 = gm_f_push_3ef746b(t137, t138);
                                gm_tftb1odc739t5 t140 = t139;
                                if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_srcdjp1iiokf034, 124, 21);
                                gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
                            }
                            goto me128;
mn129:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 122, 17);
me128:;
                        }
lc119:;
                        {
                            uint64_t t143 = gm_l21;
                            uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                            uint64_t t146 = r144;
                            gm_l21 = t146;
                        }
                    }
lb119:;
                }
            }
            {
                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_b16_8 t148 = t147;
                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                gm_l6 = t149;
            }
            {
                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                gm_b16_8 t151 = t150;
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                gm_l7 = t152;
            }
            goto ix106;
ie106:;
            {
                void * t154 = gm_l0;
                void * t155 = gm_l1;
                gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t155) + 72));
                void * t157 = gm_l0;
                gm_t88rcucg1b7uk t158 = gm_l5;
                gm_b16_8 t159 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t157, t158);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t160;
                GM_ST(gm_b16_8, ((uint8_t *)&t160 + 0), t159);
                gm_tabb710d9mq2e t161;
                gm_f_format_7f8ekea((void *)&t161, (void *)NULL, cx, NULL, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t162 = t161;
                if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 8, gm_srcdjp1iiokf034, 129, 34);
                gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
                gm_b16_8 t164 = t163;
                gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                gm_b0_1 t166 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t154, t156, t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_th2lqbo77pvsc t167;
                memset(&t167, 0, sizeof t167);
                uint8_t t168 = 0;
                F.ret = t167;
                goto gm_fret;
            }
ix106:;
        }
        goto me26;
mn101:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 104, 5);
me26:;
    }
    {
        void * t169 = gm_l0;
        void * t170 = gm_l4;
        void * t171 = gm_l2;
        void * t172 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t169, t170, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t172;
    }
    {
        gm_f_list_j5ps4qc((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t174 = gm_l6;
            gm_l25 = t174;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t176 = gm_l26;
                uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t178 = (t176 < t177);
                if (!t178) goto lb175;
                {
                    uint64_t t181 = gm_l26;
                    uint64_t t182 = gm_l26;
                    uint64_t t183 = (uintptr_t)t182;
                    uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t183 * 4));
                    gm_tlcm4lvu3g1a8 t186;
                    GM_ST(uint32_t, (uint8_t *)&t186 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t186 + 0), t181);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t186 + 8), t185);
                    gm_tlcm4lvu3g1a8 t187 = t186;
                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&t187) + 0));
                    gm_l27 = t190;
                    gm_t88rcucg1b7uk t191 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t187) + 8));
                    gm_l28 = t191;
                    {
                        gm_t88rcucg1b7uk t193 = gm_l28;
                        void * t194 = gm_l23;
                        uint64_t t195 = gm_l27;
                        uint32_t t196 = (uint32_t)t195;
                        gm_tbl8il92l97ut t197;
                        GM_ST(uint32_t, (uint8_t *)&t197 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t197 + 0), t194);
                        GM_ST(uint32_t, ((uint8_t *)&t197 + 8), t196);
                        gm_teti411eujvf4 t198;
                        memset(&t198, 0, sizeof t198);
                        GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t198 + 8), t197);
                        void * t199 = gm_l1;
                        gm_tl874t2atnlh3 t200 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t199) + 72));
                        void * t201 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t193, &t198, t200);
                        if (gm_unw.kind) {
                            goto gm_ll179;
                        }
                        gm_l29 = t201;
                    }
                    {
                        gm_th2lqbo77pvsc t202;
                        uint64_t t204 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t205 = (t204 > ((uint64_t)0ULL));
                        if (!t205) goto ie203;
                        uint64_t t207 = gm_l27;
                        uint64_t t208 = (uintptr_t)t207;
                        uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        if (t208 >= (uintptr_t)(t209)) gm_trap_index(t208, t209, gm_srcdjp1iiokf034, 138, 44);
                        gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t208 * 16));
                        gm_b16_8 t211 = t210;
                        gm_th2lqbo77pvsc t212;
                        GM_ST(uint8_t, (uint8_t *)&t212 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t212 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t212 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t212), t211);
                        ((uint8_t *)&t212)[16] = 1;
                        t202 = t212;
                        goto ix203;
ie203:;
                        gm_th2lqbo77pvsc t214;
                        memset(&t214, 0, sizeof t214);
                        gm_th2lqbo77pvsc t215 = t214;
                        t202 = t215;
ix203:;
                        gm_l30 = t202;
                    }
                    {
                        void * t216 = (void *)((uint8_t *)&F.l24);
                        gm_tpouv9r7lrri2 t217;
                        memcpy((uint8_t *)&t217 + 0, ((uint8_t *)&gm_l30), 24);
                        void * t218 = gm_l29;
                        GM_ST(void *, (uint8_t *)&t217 + 24, t218);
                        gm_tftb1odc739t5 t219 = gm_f_push_bsig5q3(t216, &t217);
                        gm_tftb1odc739t5 t220 = t219;
                        if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 1, gm_srcdjp1iiokf034, 139, 9);
                        gm_b0_1 t221 = GM_LD(gm_b0_1, (((uint8_t *)&t220) + 1));
                    }
                    goto me188;
mn189:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 136, 5);
me188:;
                }
lc175:;
                {
                    uint64_t t223 = gm_l26;
                    uint64_t r224; bool o225 = __builtin_add_overflow(t223, ((uint64_t)1ULL), &r224);
                    uint64_t t226 = r224;
                    gm_l26 = t226;
                }
            }
gm_ll179:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb175; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc175; }
            }
            goto gm_lad;
lb175:;
        }
    }
    {
        gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        gm_th2lqbo77pvsc t228;
        GM_ST(uint8_t, (uint8_t *)&t228 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t228 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t228 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t228), t227);
        ((uint8_t *)&t228)[16] = 1;
        uint8_t t229 = 0;
        F.ret = t228;
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
extern __attribute__((aligned(4))) void gm_f_synthetic_hdhcgpb(void *gm_out, const void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_synthetic_hdhcgpb_c(gm_t2gtbh39ptekd a0, gm_cseahvg8v7p0vs a1) { gm_tc3oht4ksth9d gm_r; gm_f_synthetic_hdhcgpb(&gm_r, &a0, a1); return gm_r; }
