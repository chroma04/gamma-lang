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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; void * f1; gm_t88rcucg1b7uk f2; uint8_t pe[4]; } gm_tkmj998anon0r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkmj998anon0r) == 24 && _Alignof(gm_tkmj998anon0r) == 8 && offsetof(gm_tkmj998anon0r, f0) == 0 && offsetof(gm_tkmj998anon0r, f1) == 8 && offsetof(gm_tkmj998anon0r, f2) == 16, "layout of `PendingConst`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_check_default_4bf68q8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_const_array_kn5cgp9 {
    gm_frame h;
    gm_tc3oht4ksth9d l11; /* x */
    gm_t7te2tdpebbt4 l14; /* arg */
    gm_tc3oht4ksth9d l15; /* x */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_check_d48mnu7 {
    gm_frame h;
    gm_tckdobunaulee l14; /* v */
    gm_tckdobunaulee l28; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_construction_61mg03r {
    gm_frame h;
    gm_tpautr4br7h3l l9; /* def */
    gm_trqfvmjtt4sij l10; /* given */
    gm_t7te2tdpebbt4 l15; /* arg */
    gm_tc3oht4ksth9d l17; /* x */
    gm_t0mglksrkfa5e l22; /* f */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_type_7rijs95 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_construction_type_pk33veh {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_eq_ceufbo4 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_expect_bool_edv2577 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_expect_type_uld3786 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_field_visible_7kr8oue {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_float_finite_ak3diof {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* q */
    uint8_t ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_suffix_type_icfvn23 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_untyped_fits_tp6gphv {
    gm_frame h;
    gm_t5vbkdq81qfs7 l6; /* r */
    gm_t0ialelq3ql1k l7; /* v */
    gm_tbci5ospk5nnn l8; /* i */
    gm_tngtuiuskn938 l10; /* q */
    gm_tngtuiuskn938 l11; /* q */
    uint8_t ret;
};
struct gm_fr_untyped_if_plain_lbrp24p {
    gm_frame h;
    gm_tkv1vap90vht2 l5; /* r */
    gm_tkv1vap90vht2 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq09el5ous7u(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0uoi35f1be(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2dij7vp475(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq4eilu97soo(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqeggohi50ng(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjhti3ghd62(const uint8_t *a, const uint8_t *b);
inline bool gm_eqlolcr6ikvg(const uint8_t *a, const uint8_t *b);
inline bool gm_eqm4u0k078hs(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq98vpo7v5c(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrm9iu7q40e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt4v14i9rtv(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_array_kn5cgp9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_construction_61mg03r(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3, gm_tkn42u7na3t81 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_type_pk33veh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_bool_edv2577(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_type_uld3786(void * a0, gm_csguankko4lmhd a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_field_visible_7kr8oue(void * a0, uint64_t a1, uint32_t a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_float_finite_ak3diof(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_f64_1parp9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7jrb8if(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_suffix_type_icfvn23(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tj2m9dbh1ekv6 a1);
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_untyped_fits_tp6gphv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern const uint8_t gm_s14frn0dagfs0[25];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2eo2k3vc53po[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq09el5ous7u(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqeggohi50ng(a + 0, b + 0)) return false;
    if (!gm_eqeggohi50ng(a + 48, b + 48)) return false;
    return true;
}
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0uoi35f1be(const uint8_t *a, const uint8_t *b) {
    if (a[208] != b[208]) return false;
    return !a[208] || gm_eqm4u0k078hs(a, b);
}
inline bool gm_eq2dij7vp475(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq4eilu97soo(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqlolcr6ikvg(a + 0, b + 0)) return false;
    if (!gm_eq2dij7vp475(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqeggohi50ng(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eqq98vpo7v5c(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqjhti3ghd62(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq09el5ous7u(a + 0, b + 0)) return false;
    if (!gm_eq09el5ous7u(a + 96, b + 96)) return false;
    return true;
}
inline bool gm_eqlolcr6ikvg(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqm4u0k078hs(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqt4v14i9rtv(a + 8, b + 8);
    return true;
}
inline bool gm_eqq98vpo7v5c(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqrm9iu7q40e(a + 0, b + 0)) return false;
    if (!gm_eq6groq37ram(a + 16, b + 16)) return false;
    if (!gm_eq4eilu97soo(a + 24, b + 24)) return false;
    return true;
}
inline bool gm_eqrm9iu7q40e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 4) == 0;
}
inline bool gm_eqt4v14i9rtv(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqeggohi50ng(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eq09el5ous7u(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eq929lennfd3(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqjhti3ghd62(a + 8, b + 8);
    return true;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_ceufbo4 F;
    GM_STACK_CHECK_SHALLOW(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (t2) {
        uint8_t t3 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
        t1 = t3;
    } else t1 = 0;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_field_visible_7kr8oue(void * a0, uint64_t a1, uint32_t a2, void * a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_visible_7kr8oue F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* id */
    void * gm_l3; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t1 = gm_l0;
    void * t2 = gm_l3;
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t2) + 20));
    uint32_t t4 = gm_l2;
    uint64_t t5 = (uint64_t)t4;
    uint64_t t6 = (uintptr_t)t5;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1088) + 0) + 8);
    if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_src2eo2k3vc53po, 216, 109);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 1088) + 0)) + t6 * 184) + 16));
    uint64_t t10 = gm_l1;
    uint8_t t11 = gm_f_visible_ll07dr9(t1, t3, t9, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_check_default_4bf68q8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* hook */
    GM_STACK_CHECK(&F, 309u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l3;
        uint8_t t7 = gm_f_is_error_8k27gd1(t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!t7) {
            void * t8 = gm_l0;
            uint64_t t9 = gm_l1;
            void * t10 = gm_l2;
            gm_t88rcucg1b7uk t11 = gm_l3;
            gm_tkn42u7na3t81 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[4] = 1;
            gm_tkn42u7na3t81 t13 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t8, t9, t10, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14;
            if (((((uint8_t *)&t13))[4] != 0)) goto mn16;
            t14 = ((uint8_t)0);
            goto me15;
mn16:;
            t14 = ((uint8_t)1);
            goto me15;
mn17:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 30, 30);
me15:;
            t3 = t14;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 2168));
        if (!(GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto ie21;
        void * t24 = GM_LD(void *, ((uint8_t *)&t23));
        gm_l4 = t24;
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        gm_t88rcucg1b7uk t27 = gm_l3;
        uint8_t t28 = gm_f_has_param_u9t0efs(t26, GM_CONV(gm_csguankko4lmhd, t27));
        uint8_t t29 = (uint8_t)!t28;
        if (!t29) goto ie21;
        {
            {
                void * t32 = gm_l4;
                void * t33 = gm_l0;
                uint64_t t34 = gm_l1;
                void * t35 = gm_l2;
                gm_t88rcucg1b7uk t36 = gm_l3;
                gm_tkn42u7na3t81 t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37), t36);
                ((uint8_t *)&t37)[4] = 1;
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sna4vk4o059d0));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(9));
                void * t39 = gm_l2;
                gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
                void * t41 = t32;
                gm_t9805qtonbsag t42;
                if (GM_PTR_TAGGED(t41)) {
                    t42 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t41))(t33, t34, t35, t37, t38, GM_CONV(gm_cseahvg8v7p0vs, t40));
                } else {
                    t42 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t41)(NULL, cx, NULL, t33, t34, t35, t37, t38, t40);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_b0_1 t43;
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 464);
        gm_tkmj998anon0r t47;
        memset((uint8_t *)&t47 + 20, 0, 4);
        uint64_t t48 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t47 + 0, t48);
        void * t49 = gm_l2;
        GM_ST(void *, (uint8_t *)&t47 + 8, t49);
        gm_t88rcucg1b7uk t50 = gm_l3;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t47 + 16, t50);
        gm_tftb1odc739t5 t51 = gm_f_push_7jrb8if(t46, &t47);
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_src2eo2k3vc53po, 37, 5);
        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_array_kn5cgp9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_const_array_kn5cgp9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    void * gm_l4; /* call */
    gm_b16_8 gm_l5; /* args */
    gm_t88rcucg1b7uk gm_l6; /* t */
    uint64_t gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_tnnjeerolos0a gm_l9; /* kind */
    gm_t08olq97rscn3 gm_l10; /* n */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 1063u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        void * t3 = gm_f_construction_call_ktqtrpo(t2);
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tkn42u7na3t81 t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l4 = t4;
    }
    {
        void * t9 = gm_l4;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t10 != ((uint8_t)19ULL)) goto ie8;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l5 = t11;
        {
            void * t13 = gm_l0;
            uint64_t t14 = gm_l1;
            void * t15 = gm_l2;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            gm_tkn42u7na3t81 t17 = gm_f_construction_type_pk33veh((void *)NULL, cx, NULL, t13, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t18;
            if (((((uint8_t *)&t17))[4] != 0)) {
                gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t17) + 0));
                t18 = t19;
            } else {
                gm_tkn42u7na3t81 t20;
                memset(&t20, 0, sizeof t20);
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            gm_l6 = t18;
        }
        {
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l6;
            gm_t53at80pfdus3 t26;
            gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
            if (t27 != ((uint8_t)11ULL)) goto ie22;
            uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)&t26) + 8) + 0));
            gm_l7 = t28;
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t26) + 8) + 8));
            gm_l8 = t29;
            gm_tnnjeerolos0a t30 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t26) + 8) + 12));
            gm_l9 = t30;
            {
                uint8_t t33 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l9) + 0) == ((uint8_t)0ULL)));
                if (!t33) goto ie32;
                {
                    gm_tkn42u7na3t81 t35;
                    memset(&t35, 0, sizeof t35);
                    uint8_t t36 = 0;
                    F.ret = t35;
                    goto gm_fret;
                }
                goto ix32;
ie32:;
ix32:;
            }
            {
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t39 = ((t38 == ((uint64_t)1ULL)));
                if (!t39) goto ie37;
                {
                    uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src2eo2k3vc53po, 139, 28);
                    uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t42 * 144) + 0) + 0));
                    if (t44 != ((uint8_t)1ULL)) goto ie41;
                    gm_t08olq97rscn3 t45 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t42 * 144) + 0) + 8) + 0));
                    gm_l10 = t45;
                    gm_tc3oht4ksth9d t46 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t42 * 144) + 0) + 8) + 32));
                    F.l11 = t46;
                    {
                        uint8_t t49 = 0;
                        uint8_t t50 = (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l10) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l10) + 0)), gm_spevct0q3cjgb, 4) == 0));
                        if (!t50) {
                            void * t51 = gm_l0;
                            uint64_t t52 = gm_l1;
                            void * t53 = (void *)((uint8_t *)&F.l11);
                            gm_t88rcucg1b7uk t54 = gm_l8;
                            gm_tkn42u7na3t81 t55;
                            GM_ST(uint8_t, (uint8_t *)&t55 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t55 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t55), t54);
                            ((uint8_t *)&t55)[4] = 1;
                            gm_tkn42u7na3t81 t56 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t51, t52, t53, t55);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t57;
                            if (((((uint8_t *)&t56))[4] != 0)) goto mn59;
                            t57 = ((uint8_t)1);
                            goto me58;
mn59:;
                            t57 = ((uint8_t)0);
                            goto me58;
mn60:;
                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 140, 40);
me58:;
                            t49 = t57;
                        } else t49 = 1;
                        if (!t49) goto ie48;
                        {
                            gm_tkn42u7na3t81 t62;
                            memset(&t62, 0, sizeof t62);
                            uint8_t t63 = 0;
                            F.ret = t62;
                            goto gm_fret;
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    {
                        void * t64 = gm_l0;
                        gm_t88rcucg1b7uk t65 = gm_l6;
                        gm_tkn42u7na3t81 t66 = gm_l3;
                        gm_tkn42u7na3t81 t67 = gm_f_expect_type_uld3786(t64, GM_CONV(gm_csguankko4lmhd, t65), t66);
                        uint8_t t68 = 0;
                        F.ret = t67;
                        goto gm_fret;
                    }
                    goto ix41;
ie41:;
ix41:;
                }
                goto ix37;
ie37:;
ix37:;
            }
            {
                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t71 = (uint64_t)t70;
                uint64_t t72 = gm_l7;
                uint8_t t73 = (!(t71 == t72));
                if (!t73) goto ie69;
                {
                    gm_tkn42u7na3t81 t75;
                    memset(&t75, 0, sizeof t75);
                    uint8_t t76 = 0;
                    F.ret = t75;
                    goto gm_fret;
                }
                goto ix69;
ie69:;
ix69:;
            }
            {
                {
                    gm_b16_8 t78 = gm_l5;
                    gm_l12 = t78;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t80 = gm_l13;
                        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t82 = (t80 < t81);
                        if (!t82) goto lb79;
                        {
                            uint64_t t85 = gm_l13;
                            uint64_t t86 = (uintptr_t)t85;
                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_t7te2tdpebbt4 t90 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t86 * 144));
                            F.l14 = t90;
                            {
                                uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                                if (t93 != ((uint8_t)0ULL)) goto ie92;
                                gm_tc3oht4ksth9d t94 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l14) + 0) + 8));
                                F.l15 = t94;
                                {
                                    void * t97 = gm_l0;
                                    uint64_t t98 = gm_l1;
                                    void * t99 = (void *)((uint8_t *)&F.l15);
                                    gm_t88rcucg1b7uk t100 = gm_l8;
                                    gm_tkn42u7na3t81 t101;
                                    GM_ST(uint8_t, (uint8_t *)&t101 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t101 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t101), t100);
                                    ((uint8_t *)&t101)[4] = 1;
                                    gm_tkn42u7na3t81 t102 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t97, t98, t99, t101);
                                    if (gm_unw.kind) {
                                        goto gm_ll83;
                                    }
                                    uint8_t t103;
                                    if (((((uint8_t *)&t102))[4] != 0)) goto mn105;
                                    t103 = ((uint8_t)1);
                                    goto me104;
mn105:;
                                    t103 = ((uint8_t)0);
                                    goto me104;
mn106:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 147, 12);
me104:;
                                    if (!t103) goto ie96;
                                    {
                                        gm_tkn42u7na3t81 t108;
                                        memset(&t108, 0, sizeof t108);
                                        uint8_t t109 = 0;
                                        F.ret = t108;
                                        goto gm_fret;
                                    }
                                    goto ix96;
ie96:;
ix96:;
                                }
                                goto ix92;
ie92:;
                                {
                                    gm_tkn42u7na3t81 t111;
                                    memset(&t111, 0, sizeof t111);
                                    uint8_t t112 = 0;
                                    F.ret = t111;
                                    goto gm_fret;
                                }
ix92:;
                            }
                            goto me88;
mn89:;
                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 145, 5);
me88:;
                        }
lc79:;
                        {
                            uint64_t t114 = gm_l13;
                            uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                            uint64_t t117 = r115;
                            gm_l13 = t117;
                        }
                    }
gm_ll83:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
                    }
                    goto gm_lad;
lb79:;
                }
            }
            {
                void * t118 = gm_l0;
                gm_t88rcucg1b7uk t119 = gm_l6;
                gm_tkn42u7na3t81 t120 = gm_l3;
                gm_tkn42u7na3t81 t121 = gm_f_expect_type_uld3786(t118, GM_CONV(gm_csguankko4lmhd, t119), t120);
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix22;
ie22:;
            {
                gm_tkn42u7na3t81 t124;
                memset(&t124, 0, sizeof t124);
                uint8_t t125 = 0;
                F.ret = t124;
                goto gm_fret;
            }
ix22:;
        }
        goto ix8;
ie8:;
        {
            gm_tkn42u7na3t81 t127;
            memset(&t127, 0, sizeof t127);
            uint8_t t128 = 0;
            F.ret = t127;
            goto gm_fret;
        }
ix8:;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_const_check_d48mnu7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    void * gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tibupt4dmcqai gm_l6; /* lit */
    gm_t08olq97rscn3 gm_l7; /* id */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* target_ */
    gm_t88rcucg1b7uk gm_l10; /* inner */
    gm_tep2drsl9408b gm_l11; /* u */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l15; /* lit */
    gm_tj2m9dbh1ekv6 gm_l16; /* suffix */
    gm_t88rcucg1b7uk gm_l17; /* t */
    void * gm_l18; /* lit */
    gm_t9qkh0rsooc8d gm_l19; /* suffix */
    gm_t88rcucg1b7uk gm_l20; /* t */
    void * gm_l21; /* base */
    gm_t08olq97rscn3 gm_l22; /* name */
    uint64_t gm_l23; /* errors */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_tep2drsl9408b gm_l25; /* u */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t88rcucg1b7uk gm_l29; /* x */
    void * gm_l30; /* callee */
    gm_b16_8 gm_l31; /* args */
    gm_t88rcucg1b7uk gm_l32; /* t */
    GM_STACK_CHECK(&F, 2358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; case ((uint8_t)27ULL): goto ma14; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma14; case ((uint8_t)31ULL): goto ma14; case ((uint8_t)32ULL): goto ma14; case ((uint8_t)33ULL): goto ma14; case ((uint8_t)34ULL): goto ma14; case ((uint8_t)35ULL): goto ma14; case ((uint8_t)36ULL): goto ma14; case ((uint8_t)37ULL): goto ma14; case ((uint8_t)38ULL): goto ma14; case ((uint8_t)39ULL): goto ma14; case ((uint8_t)40ULL): goto ma14; case ((uint8_t)41ULL): goto ma14; case ((uint8_t)42ULL): goto ma14; case ((uint8_t)43ULL): goto ma14; case ((uint8_t)44ULL): goto ma14; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma14; case ((uint8_t)47ULL): goto ma14; case ((uint8_t)48ULL): goto ma14; case ((uint8_t)49ULL): goto ma14; case ((uint8_t)50ULL): goto ma14; default: goto ma14; }
ma4:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)9ULL)) goto mn17;
        void * t19 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t19;
        void * t20 = gm_l0;
        uint64_t t21 = gm_l1;
        void * t22 = gm_l4;
        gm_tkn42u7na3t81 t23 = gm_l3;
        gm_tkn42u7na3t81 t24 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t20, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
        goto me3;
mn17:;
ma5:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)4ULL)) goto mn26;
        {
            gm_tkn42u7na3t81 t29 = gm_l3;
            gm_t88rcucg1b7uk t30;
            if (((((uint8_t *)&t29))[4] != 0)) {
                gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t29) + 0));
                t30 = t31;
            } else {
                gm_tkn42u7na3t81 t32;
                memset(&t32, 0, sizeof t32);
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            gm_l5 = t30;
        }
        {
            gm_tkn42u7na3t81 t34;
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 8);
            gm_t88rcucg1b7uk t38 = gm_l5;
            gm_t53at80pfdus3 t39;
            gm_f_kind_jcnt4hm((void *)&t39, t37, GM_CONV(gm_csguankko4lmhd, t38));
            uint8_t t40;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t43 != ((uint8_t)13ULL)) goto mn42;
            t40 = ((uint8_t)1);
            goto me41;
mn42:;
            t40 = ((uint8_t)0);
            goto me41;
mn44:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 47, 23);
me41:;
            if (!t40) goto ie35;
            gm_t88rcucg1b7uk t46 = gm_l5;
            gm_t88rcucg1b7uk t47 = t46;
            gm_tkn42u7na3t81 t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48), t47);
            ((uint8_t *)&t48)[4] = 1;
            t34 = t48;
            goto ix35;
ie35:;
            gm_tkn42u7na3t81 t50;
            memset(&t50, 0, sizeof t50);
            gm_tkn42u7na3t81 t51 = t50;
            t34 = t51;
ix35:;
            uint8_t t52 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto me3;
mn26:;
ma6:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t54 != ((uint8_t)3ULL)) goto mn53;
        void * t55 = gm_l0;
        gm_tkn42u7na3t81 t56 = gm_l3;
        gm_tkn42u7na3t81 t57 = gm_f_expect_bool_edv2577((void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t58 = 0;
        F.ret = t57;
        goto gm_fret;
        goto me3;
mn53:;
ma7:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)2ULL)) goto mn59;
        gm_tibupt4dmcqai t61 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t61;
        {
            uint8_t t64 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 16) + 0) == ((uint8_t)0ULL)));
            if (!t64) goto ie63;
            {
                gm_tkn42u7na3t81 t66;
                memset(&t66, 0, sizeof t66);
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix63;
ie63:;
ix63:;
        }
        {
            void * t68 = gm_l0;
            void * t69 = gm_l0;
            void * t70 = (void *)(((uint8_t *)t69) + 8);
            gm_t88rcucg1b7uk t71 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t72 = gm_l3;
            gm_tkn42u7na3t81 t73 = gm_f_expect_type_uld3786(t68, GM_CONV(gm_csguankko4lmhd, t71), t72);
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto me3;
mn59:;
ma8:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t76 != ((uint8_t)8ULL)) goto mn75;
        gm_t08olq97rscn3 t77 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t77;
        {
            gm_tkn42u7na3t81 t79 = gm_l3;
            gm_t88rcucg1b7uk t80;
            if (((((uint8_t *)&t79))[4] != 0)) {
                gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t79) + 0));
                t80 = t81;
            } else {
                gm_tkn42u7na3t81 t82;
                memset(&t82, 0, sizeof t82);
                uint8_t t83 = 0;
                F.ret = t82;
                goto gm_fret;
            }
            gm_l8 = t80;
        }
        {
            gm_t88rcucg1b7uk t84 = gm_l8;
            gm_l9 = t84;
        }
        {
            void * t86 = gm_l0;
            void * t87 = (void *)(((uint8_t *)t86) + 8);
            gm_t88rcucg1b7uk t88 = gm_l8;
            gm_t53at80pfdus3 t89;
            gm_f_kind_jcnt4hm((void *)&t89, t87, GM_CONV(gm_csguankko4lmhd, t88));
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t89) + 0));
            if (t90 != ((uint8_t)13ULL)) goto ie85;
            gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t89) + 8));
            gm_l10 = t91;
            {
                gm_t88rcucg1b7uk t93 = gm_l10;
                gm_l9 = t93;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l9;
            gm_tdlpv0q69lov1 t96;
            gm_f_union_variants_a7khaqp((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
            gm_tep2drsl9408b t97;
            if (((((uint8_t *)&t96))[24] != 0)) {
                t97 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t96) + 0));
            } else {
                gm_tkn42u7na3t81 t98;
                memset(&t98, 0, sizeof t98);
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            gm_l11 = t97;
        }
        {
            {
                gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                gm_l12 = t101;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t103 = gm_l13;
                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t105 = (t103 < t104);
                    if (!t105) goto lb102;
                    {
                        uint64_t t108 = gm_l13;
                        uint64_t t109 = (uintptr_t)t108;
                        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_tckdobunaulee t113 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t109 * 72));
                        F.l14 = t113;
                        {
                            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                            uint8_t t118 = (gm_eq0iqiubfm7e(((uint8_t *)&t116), ((uint8_t *)&t117)));
                            if (!t118) goto ie115;
                            {
                                gm_tkn42u7na3t81 t120;
                                uint8_t t122;
                                if ((((((uint8_t *)&F.l14) + 16))[4] != 0)) goto mn124;
                                t122 = ((uint8_t)1);
                                goto me123;
mn124:;
                                t122 = ((uint8_t)0);
                                goto me123;
mn125:;
                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 60, 50);
me123:;
                                if (!t122) goto ie121;
                                gm_t88rcucg1b7uk t127 = gm_l8;
                                gm_t88rcucg1b7uk t128 = t127;
                                gm_tkn42u7na3t81 t129;
                                GM_ST(uint8_t, (uint8_t *)&t129 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t129 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t129), t128);
                                ((uint8_t *)&t129)[4] = 1;
                                t120 = t129;
                                goto ix121;
ie121:;
                                gm_tkn42u7na3t81 t131;
                                memset(&t131, 0, sizeof t131);
                                gm_tkn42u7na3t81 t132 = t131;
                                t120 = t132;
ix121:;
                                uint8_t t133 = 0;
                                F.ret = t120;
                                goto gm_fret;
                            }
                            goto ix115;
ie115:;
ix115:;
                        }
                        goto me111;
mn112:;
                        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 59, 13);
me111:;
                    }
lc102:;
                    {
                        uint64_t t135 = gm_l13;
                        uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                        uint64_t t138 = r136;
                        gm_l13 = t138;
                    }
                }
lb102:;
            }
        }
        {
            gm_tkn42u7na3t81 t139;
            memset(&t139, 0, sizeof t139);
            uint8_t t140 = 0;
            F.ret = t139;
            goto gm_fret;
        }
        goto me3;
mn75:;
ma9:;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t142 != ((uint8_t)0ULL)) goto mn141;
        void * t143 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t143;
        {
            void * t146 = gm_l15;
            if (!(((((uint8_t *)t146) + 48))[8] != 0)) goto ie145;
            gm_tj2m9dbh1ekv6 t147 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t146) + 48));
            gm_l16 = t147;
            {
                void * t149 = gm_l0;
                gm_tj2m9dbh1ekv6 t150 = gm_l16;
                gm_t88rcucg1b7uk t151 = gm_f_suffix_type_icfvn23((void *)NULL, cx, NULL, t149, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t151;
            }
            {
                void * t153 = gm_l0;
                void * t154 = gm_l15;
                gm_t88rcucg1b7uk t155 = gm_l17;
                uint8_t t156 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t153, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)t154) + 0)), t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t157 = (uint8_t)!t156;
                if (!t157) goto ie152;
                {
                    gm_tkn42u7na3t81 t159;
                    memset(&t159, 0, sizeof t159);
                    uint8_t t160 = 0;
                    F.ret = t159;
                    goto gm_fret;
                }
                goto ix152;
ie152:;
ix152:;
            }
            {
                void * t161 = gm_l0;
                gm_t88rcucg1b7uk t162 = gm_l17;
                gm_tkn42u7na3t81 t163 = gm_l3;
                gm_tkn42u7na3t81 t164 = gm_f_expect_type_uld3786(t161, GM_CONV(gm_csguankko4lmhd, t162), t163);
                uint8_t t165 = 0;
                F.ret = t164;
                goto gm_fret;
            }
            goto ix145;
ie145:;
ix145:;
        }
        goto me3;
mn141:;
ma10:;
        uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t167 != ((uint8_t)1ULL)) goto mn166;
        void * t168 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l18 = t168;
        {
            void * t171 = gm_l18;
            if (!(((((uint8_t *)t171) + 96))[1] != 0)) goto ie170;
            gm_t9qkh0rsooc8d t172 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t171) + 96));
            gm_l19 = t172;
            {
                void * t175 = gm_l18;
                uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)t175) + 98));
                if (!t176) goto ie174;
                {
                    gm_tkn42u7na3t81 t178;
                    memset(&t178, 0, sizeof t178);
                    uint8_t t179 = 0;
                    F.ret = t178;
                    goto gm_fret;
                }
                goto ix174;
ie174:;
ix174:;
            }
            {
                void * t180 = gm_l0;
                void * t181 = (void *)(((uint8_t *)t180) + 8);
                uint16_t t182;
                uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                switch (t188) { case ((uint8_t)0ULL): goto ma184; case ((uint8_t)1ULL): goto ma185; case ((uint8_t)2ULL): goto ma186; default: goto ma187; }
ma184:;
                uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                if (t190 != ((uint8_t)0ULL)) goto mn189;
                t182 = ((uint16_t)16ULL);
                goto me183;
mn189:;
ma185:;
                uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                if (t192 != ((uint8_t)1ULL)) goto mn191;
                t182 = ((uint16_t)32ULL);
                goto me183;
mn191:;
ma186:;
                uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                if (t194 != ((uint8_t)2ULL)) goto mn193;
                t182 = ((uint16_t)64ULL);
                goto me183;
mn193:;
ma187:;
                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 74, 42);
me183:;
                gm_t53at80pfdus3 t195;
                memset(&t195, 0, sizeof t195);
                GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)3ULL));
                GM_ST(uint16_t, ((uint8_t *)&t195 + 8), t182);
                gm_t88rcucg1b7uk t196 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t181, &t195);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t196;
            }
            {
                void * t198 = gm_l0;
                void * t199 = gm_l18;
                gm_t88rcucg1b7uk t200 = gm_l20;
                uint8_t t201 = gm_f_float_finite_ak3diof((void *)NULL, cx, NULL, t198, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t199) + 0)), t200);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t202 = (uint8_t)!t201;
                if (!t202) goto ie197;
                {
                    gm_tkn42u7na3t81 t204;
                    memset(&t204, 0, sizeof t204);
                    uint8_t t205 = 0;
                    F.ret = t204;
                    goto gm_fret;
                }
                goto ix197;
ie197:;
ix197:;
            }
            {
                void * t206 = gm_l0;
                gm_t88rcucg1b7uk t207 = gm_l20;
                gm_tkn42u7na3t81 t208 = gm_l3;
                gm_tkn42u7na3t81 t209 = gm_f_expect_type_uld3786(t206, GM_CONV(gm_csguankko4lmhd, t207), t208);
                uint8_t t210 = 0;
                F.ret = t209;
                goto gm_fret;
            }
            goto ix170;
ie170:;
ix170:;
        }
        goto me3;
mn166:;
ma11:;
        uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t212 != ((uint8_t)22ULL)) goto mn211;
        void * t213 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l21 = t213;
        gm_t08olq97rscn3 t214 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l22 = t214;
        {
            void * t217 = gm_l0;
            uint64_t t218 = gm_l1;
            void * t219 = gm_l21;
            gm_tf5srh4mp4b3i t220 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t217, t218, t219);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t221;
            if (((((uint8_t *)&t220))[8] != 0)) goto mn223;
            t221 = ((uint8_t)1);
            goto me222;
mn223:;
            t221 = ((uint8_t)0);
            goto me222;
mn224:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 85, 16);
me222:;
            if (!t221) goto ie216;
            void * t225 = gm_l0;
            uint64_t t226 = gm_l1;
            void * t227 = gm_l21;
            uint8_t t228 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t225, t226, t227, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t228) goto ie216;
            {
                void * t230 = gm_l0;
                void * t231 = GM_LD(void *, (((uint8_t *)t230) + 0));
                void * t232 = GM_LD(void *, (((uint8_t *)t231) + 8));
                uint64_t t233 = GM_LD(uintptr_t, ((((uint8_t *)t232) + 0) + 0) + 8);
                gm_l23 = t233;
            }
            {
                void * t234 = gm_l0;
                uint64_t t235 = gm_l1;
                void * t236 = gm_l21;
                gm_t88rcucg1b7uk t237 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t234, t235, t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t237;
            }
            {
                uint8_t t239 = 0;
                void * t240 = gm_l0;
                void * t241 = GM_LD(void *, (((uint8_t *)t240) + 0));
                void * t242 = GM_LD(void *, (((uint8_t *)t241) + 8));
                uint64_t t243 = GM_LD(uintptr_t, ((((uint8_t *)t242) + 0) + 0) + 8);
                uint64_t t244 = gm_l23;
                uint8_t t245 = (!(t243 == t244));
                if (!t245) {
                    void * t246 = gm_l0;
                    void * t247 = (void *)(((uint8_t *)t246) + 8);
                    gm_t88rcucg1b7uk t248 = gm_l24;
                    uint8_t t249 = gm_f_is_error_8k27gd1(t247, GM_CONV(gm_csguankko4lmhd, t248));
                    t239 = t249;
                } else t239 = 1;
                if (!t239) goto ie238;
                {
                    gm_tkn42u7na3t81 t251;
                    memset(&t251, 0, sizeof t251);
                    uint8_t t252 = 0;
                    F.ret = t251;
                    goto gm_fret;
                }
                goto ix238;
ie238:;
ix238:;
            }
            {
                void * t253 = gm_l0;
                gm_t88rcucg1b7uk t254 = gm_l24;
                gm_tdlpv0q69lov1 t255;
                gm_f_union_variants_a7khaqp((void *)&t255, t253, GM_CONV(gm_csguankko4lmhd, t254));
                gm_tep2drsl9408b t256;
                if (((((uint8_t *)&t255))[24] != 0)) {
                    t256 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t255) + 0));
                } else {
                    gm_tkn42u7na3t81 t257;
                    memset(&t257, 0, sizeof t257);
                    uint8_t t258 = 0;
                    F.ret = t257;
                    goto gm_fret;
                }
                gm_l25 = t256;
            }
            {
                {
                    gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                    gm_l26 = t260;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t262 = gm_l27;
                        uint64_t t263 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint8_t t264 = (t262 < t263);
                        if (!t264) goto lb261;
                        {
                            uint64_t t267 = gm_l27;
                            uint64_t t268 = (uintptr_t)t267;
                            uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                            gm_tckdobunaulee t272 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t268 * 72));
                            F.l28 = t272;
                            {
                                gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                                gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 0));
                                uint8_t t277 = (gm_eq0iqiubfm7e(((uint8_t *)&t275), ((uint8_t *)&t276)));
                                if (!t277) goto ie274;
                                {
                                    uint8_t t280;
                                    if ((((((uint8_t *)&F.l28) + 16))[4] != 0)) goto mn282;
                                    t280 = ((uint8_t)0);
                                    goto me281;
mn282:;
                                    t280 = ((uint8_t)1);
                                    goto me281;
mn283:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 92, 28);
me281:;
                                    if (!t280) goto ie279;
                                    {
                                        gm_tkn42u7na3t81 t285;
                                        memset(&t285, 0, sizeof t285);
                                        uint8_t t286 = 0;
                                        F.ret = t285;
                                        goto gm_fret;
                                    }
                                    goto ix279;
ie279:;
ix279:;
                                }
                                {
                                    gm_tkn42u7na3t81 t287 = gm_l3;
                                    gm_t88rcucg1b7uk t288;
                                    if (((((uint8_t *)&t287))[4] != 0)) {
                                        gm_t88rcucg1b7uk t289 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t287) + 0));
                                        t288 = t289;
                                    } else {
                                        gm_t88rcucg1b7uk t290 = gm_l24;
                                        gm_tkn42u7na3t81 t291;
                                        GM_ST(uint8_t, (uint8_t *)&t291 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t291 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t291), t290);
                                        ((uint8_t *)&t291)[4] = 1;
                                        uint8_t t292 = 0;
                                        F.ret = t291;
                                        goto gm_fret;
                                    }
                                    gm_l29 = t288;
                                }
                                {
                                    gm_tkn42u7na3t81 t293;
                                    uint8_t t295 = 0;
                                    gm_t88rcucg1b7uk t296 = gm_l29;
                                    gm_t88rcucg1b7uk t297 = gm_l24;
                                    uint8_t t298 = (gm_eq9m6mvtmgde(((uint8_t *)&t296), ((uint8_t *)&t297)));
                                    if (!t298) {
                                        void * t299 = gm_l0;
                                        void * t300 = (void *)(((uint8_t *)t299) + 8);
                                        gm_t88rcucg1b7uk t301 = gm_l29;
                                        gm_t53at80pfdus3 t302;
                                        gm_f_kind_jcnt4hm((void *)&t302, t300, GM_CONV(gm_csguankko4lmhd, t301));
                                        gm_t88rcucg1b7uk t303 = gm_l24;
                                        uint8_t t304 = (((GM_LD(uint8_t, ((uint8_t *)&t302) + 0) == ((uint8_t)13ULL)) && gm_eq9m6mvtmgde((((uint8_t *)&t302) + 8), ((uint8_t *)&t303))));
                                        t295 = t304;
                                    } else t295 = 1;
                                    if (!t295) goto ie294;
                                    gm_t88rcucg1b7uk t306 = gm_l29;
                                    gm_t88rcucg1b7uk t307 = t306;
                                    gm_tkn42u7na3t81 t308;
                                    GM_ST(uint8_t, (uint8_t *)&t308 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t308 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t308), t307);
                                    ((uint8_t *)&t308)[4] = 1;
                                    t293 = t308;
                                    goto ix294;
ie294:;
                                    gm_tkn42u7na3t81 t310;
                                    memset(&t310, 0, sizeof t310);
                                    gm_tkn42u7na3t81 t311 = t310;
                                    t293 = t311;
ix294:;
                                    uint8_t t312 = 0;
                                    F.ret = t293;
                                    goto gm_fret;
                                }
                                goto ix274;
ie274:;
ix274:;
                            }
                            goto me270;
mn271:;
                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 90, 17);
me270:;
                        }
lc261:;
                        {
                            uint64_t t314 = gm_l27;
                            uint64_t r315; bool o316 = __builtin_add_overflow(t314, ((uint64_t)1ULL), &r315);
                            uint64_t t317 = r315;
                            gm_l27 = t317;
                        }
                    }
lb261:;
                }
            }
            {
                gm_tkn42u7na3t81 t318;
                memset(&t318, 0, sizeof t318);
                uint8_t t319 = 0;
                F.ret = t318;
                goto gm_fret;
            }
            goto ix216;
ie216:;
ix216:;
        }
        goto me3;
mn211:;
ma12:;
        uint8_t t321 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t321 != ((uint8_t)19ULL)) goto mn320;
        void * t322 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l30 = t322;
        gm_b16_8 t323 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l31 = t323;
        void * t324 = gm_l0;
        uint64_t t325 = gm_l1;
        void * t326 = gm_l30;
        gm_b16_8 t327 = gm_l31;
        gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&t327));
        gm_tkn42u7na3t81 t329 = gm_l3;
        gm_tkn42u7na3t81 t330 = gm_f_const_construction_61mg03r((void *)NULL, cx, NULL, t324, t325, t326, t328, t329);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t331 = 0;
        F.ret = t330;
        goto gm_fret;
        goto me3;
mn320:;
ma13:;
        uint8_t t333 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t333 != ((uint8_t)28ULL)) goto mn332;
        void * t334 = gm_l0;
        uint64_t t335 = gm_l1;
        void * t336 = gm_l2;
        gm_tkn42u7na3t81 t337 = gm_l3;
        gm_tkn42u7na3t81 t338 = gm_f_const_array_kn5cgp9((void *)NULL, cx, NULL, t334, t335, t336, t337);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t339 = 0;
        F.ret = t338;
        goto gm_fret;
        goto me3;
mn332:;
ma14:;
        goto me3;
mn340:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 43, 5);
me3:;
    }
    {
        gm_tkn42u7na3t81 t342 = gm_l3;
        gm_t88rcucg1b7uk t343;
        if (((((uint8_t *)&t342))[4] != 0)) {
            gm_t88rcucg1b7uk t344 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t342) + 0));
            t343 = t344;
        } else {
            gm_tkn42u7na3t81 t345;
            memset(&t345, 0, sizeof t345);
            uint8_t t346 = 0;
            F.ret = t345;
            goto gm_fret;
        }
        gm_l32 = t343;
    }
    {
        void * t348 = gm_l2;
        uint8_t t349;
        uint8_t t355 = GM_LD(uint8_t, ((((uint8_t *)t348) + 8) + 0));
        switch (t355) { case ((uint8_t)0ULL): goto ma353; case ((uint8_t)1ULL): goto ma353; case ((uint8_t)2ULL): goto ma353; case ((uint8_t)3ULL): goto ma353; case ((uint8_t)4ULL): goto ma353; case ((uint8_t)5ULL): goto ma353; case ((uint8_t)6ULL): goto ma351; case ((uint8_t)7ULL): goto ma353; case ((uint8_t)8ULL): goto ma353; case ((uint8_t)9ULL): goto ma353; case ((uint8_t)10ULL): goto ma353; case ((uint8_t)11ULL): goto ma353; case ((uint8_t)12ULL): goto ma353; case ((uint8_t)13ULL): goto ma353; case ((uint8_t)14ULL): goto ma353; case ((uint8_t)15ULL): goto ma353; case ((uint8_t)16ULL): goto ma353; case ((uint8_t)17ULL): goto ma353; case ((uint8_t)18ULL): goto ma353; case ((uint8_t)19ULL): goto ma353; case ((uint8_t)20ULL): goto ma353; case ((uint8_t)21ULL): goto ma353; case ((uint8_t)22ULL): goto ma352; case ((uint8_t)23ULL): goto ma353; case ((uint8_t)24ULL): goto ma353; case ((uint8_t)25ULL): goto ma353; case ((uint8_t)26ULL): goto ma353; case ((uint8_t)27ULL): goto ma353; case ((uint8_t)28ULL): goto ma353; case ((uint8_t)29ULL): goto ma353; case ((uint8_t)30ULL): goto ma353; case ((uint8_t)31ULL): goto ma353; case ((uint8_t)32ULL): goto ma353; case ((uint8_t)33ULL): goto ma353; case ((uint8_t)34ULL): goto ma353; case ((uint8_t)35ULL): goto ma353; case ((uint8_t)36ULL): goto ma353; case ((uint8_t)37ULL): goto ma353; case ((uint8_t)38ULL): goto ma353; case ((uint8_t)39ULL): goto ma353; case ((uint8_t)40ULL): goto ma353; case ((uint8_t)41ULL): goto ma353; case ((uint8_t)42ULL): goto ma353; case ((uint8_t)43ULL): goto ma353; case ((uint8_t)44ULL): goto ma353; case ((uint8_t)45ULL): goto ma353; case ((uint8_t)46ULL): goto ma353; case ((uint8_t)47ULL): goto ma353; case ((uint8_t)48ULL): goto ma353; case ((uint8_t)49ULL): goto ma353; case ((uint8_t)50ULL): goto ma353; default: goto ma353; }
ma351:;
        uint8_t t357 = GM_LD(uint8_t, ((((uint8_t *)t348) + 8) + 0));
        if (t357 != ((uint8_t)6ULL)) goto mn356;
        t349 = ((uint8_t)1);
        goto me350;
mn356:;
ma352:;
        uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)t348) + 8) + 0));
        if (t359 != ((uint8_t)22ULL)) goto mn358;
        t349 = ((uint8_t)1);
        goto me350;
mn358:;
ma353:;
        t349 = ((uint8_t)0);
        goto me350;
mn360:;
ma354:;
        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 105, 8);
me350:;
        if (!t349) goto ie347;
        void * t361 = gm_l0;
        uint64_t t362 = gm_l1;
        void * t363 = gm_l2;
        gm_t88rcucg1b7uk t364 = gm_l32;
        gm_t9805qtonbsag t365 = gm_f_fn_ref_7alnaqh((void *)NULL, cx, NULL, t361, t362, t363, t364);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t366;
        if (((((uint8_t *)&t365))[4] != 0)) goto mn368;
        t366 = ((uint8_t)0);
        goto me367;
mn368:;
        t366 = ((uint8_t)1);
        goto me367;
mn369:;
        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 105, 45);
me367:;
        if (!t366) goto ie347;
        {
            gm_t88rcucg1b7uk t371 = gm_l32;
            gm_tkn42u7na3t81 t372;
            GM_ST(uint8_t, (uint8_t *)&t372 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t372 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t372), t371);
            ((uint8_t *)&t372)[4] = 1;
            uint8_t t373 = 0;
            F.ret = t372;
            goto gm_fret;
        }
        goto ix347;
ie347:;
ix347:;
    }
    {
        gm_tkn42u7na3t81 t374;
        void * t376 = gm_l0;
        uint64_t t377 = gm_l1;
        void * t378 = gm_l2;
        gm_t88rcucg1b7uk t379 = gm_l32;
        uint8_t t380 = gm_f_untyped_fits_tp6gphv((void *)NULL, cx, NULL, t376, t377, t378, t379);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t380) goto ie375;
        gm_t88rcucg1b7uk t382 = gm_l32;
        gm_t88rcucg1b7uk t383 = t382;
        gm_tkn42u7na3t81 t384;
        GM_ST(uint8_t, (uint8_t *)&t384 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t384 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t384), t383);
        ((uint8_t *)&t384)[4] = 1;
        t374 = t384;
        goto ix375;
ie375:;
        gm_tkn42u7na3t81 t386;
        memset(&t386, 0, sizeof t386);
        gm_tkn42u7na3t81 t387 = t386;
        t374 = t387;
ix375:;
        uint8_t t388 = 0;
        F.ret = t374;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_construction_61mg03r(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3, gm_tkn42u7na3t81 a4) {
    struct gm_fr_const_construction_61mg03r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_tkn42u7na3t81 gm_l4; /* expected */
    uint64_t gm_l5; /* errors */
    gm_t88rcucg1b7uk gm_l6; /* t */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l8; /* gargs */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    gm_tf5srh4mp4b3i gm_l18; /* found */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    uint64_t gm_l23; /* i */
    gm_b16_8 gm_l24; /* defaults */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    uint8_t gm_l28; /* g */
    GM_STACK_CHECK(&F, 1869u);
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
        void * t4 = gm_l2;
        uint8_t t5 = gm_f_calls_on_value_q0pa6h5(t4);
        if (!t5) {
            void * t6 = gm_l0;
            uint64_t t7 = gm_l1;
            void * t8 = gm_l2;
            uint8_t t9 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t6, t7, t8, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = (uint8_t)!t9;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_tkn42u7na3t81 t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 8));
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 0) + 0) + 8);
        gm_l5 = t17;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = gm_l1;
        void * t20 = gm_l2;
        gm_t88rcucg1b7uk t21 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t21;
    }
    {
        uint8_t t23 = 0;
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 8));
        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 0) + 0) + 8);
        uint64_t t28 = gm_l5;
        uint8_t t29 = (!(t27 == t28));
        if (!t29) {
            void * t30 = gm_l0;
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            gm_t88rcucg1b7uk t32 = gm_l6;
            uint8_t t33 = gm_f_is_error_8k27gd1(t31, GM_CONV(gm_csguankko4lmhd, t32));
            t23 = t33;
        } else t23 = 1;
        if (!t23) goto ie22;
        {
            gm_tkn42u7na3t81 t35;
            memset(&t35, 0, sizeof t35);
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 8);
        gm_t88rcucg1b7uk t40 = gm_l6;
        gm_t53at80pfdus3 t41;
        gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
        if (t42 != ((uint8_t)17ULL)) goto ie37;
        uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)&t41) + 8) + 0));
        gm_l7 = t43;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)&t41) + 8) + 8));
        gm_l8 = t44;
        {
            uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            uint8_t t48 = (t47 > ((uint64_t)0ULL));
            if (!t48) goto ie46;
            {
                gm_tkn42u7na3t81 t50;
                memset(&t50, 0, sizeof t50);
                uint8_t t51 = 0;
                F.ret = t50;
                goto gm_fret;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            uint32_t t53 = gm_l7;
            uint64_t t54 = (uint64_t)t53;
            uint64_t t55 = (uintptr_t)t54;
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 1088) + 0) + 8);
            if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_src2eo2k3vc53po, 189, 21);
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 1088) + 0)) + t55 * 184) + 24) + 0));
            if (t58 != ((uint8_t)0ULL)) goto ie52;
            gm_tpautr4br7h3l t59 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 1088) + 0)) + t55 * 184) + 24) + 8));
            F.l9 = t59;
            {
                uint32_t t62 = gm_l7;
                uint64_t t63 = (uint64_t)t62;
                uint64_t t64 = (uintptr_t)t63;
                void * t65 = gm_l0;
                uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 1088) + 0) + 8);
                if (t64 >= (uintptr_t)(t66)) gm_trap_index(t64, t66, gm_src2eo2k3vc53po, 190, 8);
                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 1088) + 0)) + t64 * 184) + 112));
                if (!t67) goto ie61;
                {
                    gm_tkn42u7na3t81 t69;
                    memset(&t69, 0, sizeof t69);
                    uint8_t t70 = 0;
                    F.ret = t69;
                    goto gm_fret;
                }
                goto ix61;
ie61:;
ix61:;
            }
            {
                gm_f_list_s83jn2q((void *)&F.l10, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    gm_l11 = t72;
                }
                {
                    gm_l12 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t74 = gm_l12;
                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        uint8_t t76 = (t74 < t75);
                        if (!t76) goto lb73;
                        {
                            uint64_t t79 = gm_l12;
                            uint64_t t80 = (uintptr_t)t79;
                            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                            {
                                void * t85 = (void *)((uint8_t *)&F.l10);
                                gm_tftb1odc739t5 t86 = gm_f_push_vd1290p(t85, ((uint8_t)0));
                                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_src2eo2k3vc53po, 193, 27);
                                gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                            }
                            goto me82;
mn83:;
                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 193, 5);
me82:;
                        }
lc73:;
                        {
                            uint64_t t89 = gm_l12;
                            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                            uint64_t t92 = r90;
                            gm_l12 = t92;
                        }
                    }
lb73:;
                }
            }
            {
                {
                    gm_b16_8 t94 = gm_l3;
                    gm_l13 = t94;
                }
                {
                    gm_l14 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t96 = gm_l14;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        uint8_t t98 = (t96 < t97);
                        if (!t98) goto lb95;
                        {
                            uint64_t t101 = gm_l14;
                            uint64_t t102 = (uintptr_t)t101;
                            uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            gm_t7te2tdpebbt4 t106 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t102 * 144));
                            F.l15 = t106;
                            {
                                uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)&F.l15) + 0) + 0));
                                if (t109 != ((uint8_t)1ULL)) goto ie108;
                                gm_t08olq97rscn3 t110 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l15) + 0) + 8) + 0));
                                gm_l16 = t110;
                                gm_tc3oht4ksth9d t111 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l15) + 0) + 8) + 32));
                                F.l17 = t111;
                                {
                                    gm_tf5srh4mp4b3i t113;
                                    memset(&t113, 0, sizeof t113);
                                    gm_l18 = t113;
                                }
                                {
                                    {
                                        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                                        gm_l19 = t116;
                                    }
                                    {
                                        gm_l20 = ((uint64_t)0ULL);
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t118 = gm_l20;
                                            uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                            uint8_t t120 = (t118 < t119);
                                            if (!t120) goto lb117;
                                            {
                                                gm_t8ta482bl6be0 t123;
                                                uint64_t t124 = gm_l20;
                                                GM_ST(uint64_t, (uint8_t *)&t123 + 0, t124);
                                                uint64_t t125 = gm_l20;
                                                uint64_t t126 = (uintptr_t)t125;
                                                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                                memcpy((uint8_t *)&t123 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t126 * 40), 40);
                                                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&t123) + 0));
                                                gm_l21 = t130;
                                                F.l22 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t123) + 8));
                                                {
                                                    gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                                                    gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                                                    uint8_t t135 = (gm_eq0iqiubfm7e(((uint8_t *)&t133), ((uint8_t *)&t134)));
                                                    if (!t135) goto ie132;
                                                    {
                                                        uint64_t t137 = gm_l21;
                                                        gm_tf5srh4mp4b3i t138;
                                                        GM_ST(uint8_t, (uint8_t *)&t138 + 9, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t138 + 10, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t138 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t138), t137);
                                                        ((uint8_t *)&t138)[8] = 1;
                                                        gm_l18 = t138;
                                                    }
                                                    {
                                                        goto lb117;
                                                    }
                                                    goto ix132;
ie132:;
ix132:;
                                                }
                                                goto me128;
mn129:;
                                                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 197, 9);
me128:;
                                            }
lc117:;
                                            {
                                                uint64_t t140 = gm_l20;
                                                uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                                                uint64_t t143 = r141;
                                                gm_l20 = t143;
                                            }
                                        }
lb117:;
                                    }
                                }
                                {
                                    gm_tf5srh4mp4b3i t144 = gm_l18;
                                    uint64_t t145;
                                    if (((((uint8_t *)&t144))[8] != 0)) {
                                        uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&t144) + 0));
                                        t145 = t146;
                                    } else {
                                        gm_tkn42u7na3t81 t147;
                                        memset(&t147, 0, sizeof t147);
                                        uint8_t t148 = 0;
                                        F.ret = t147;
                                        goto gm_fret;
                                    }
                                    gm_l23 = t145;
                                }
                                {
                                    uint8_t t150 = 0;
                                    uint64_t t151 = gm_l23;
                                    uint64_t t152 = (uintptr_t)t151;
                                    uint64_t t153 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                    if (t152 >= (uintptr_t)(t153)) gm_trap_index(t152, t153, gm_src2eo2k3vc53po, 204, 12);
                                    uint8_t t154 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t152 * 1));
                                    if (!t154) {
                                        void * t155 = gm_l0;
                                        uint64_t t156 = gm_l1;
                                        uint32_t t157 = gm_l7;
                                        uint64_t t158 = gm_l23;
                                        uint64_t t159 = (uintptr_t)t158;
                                        uint64_t t160 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                        if (t159 >= (uintptr_t)(t160)) gm_trap_index(t159, t160, gm_src2eo2k3vc53po, 204, 63);
                                        void * t161 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t159 * 40);
                                        uint8_t t162 = gm_f_field_visible_7kr8oue(t155, t156, t157, t161);
                                        uint8_t t163 = (uint8_t)!t162;
                                        t150 = t163;
                                    } else t150 = 1;
                                    if (!t150) goto ie149;
                                    {
                                        gm_tkn42u7na3t81 t165;
                                        memset(&t165, 0, sizeof t165);
                                        uint8_t t166 = 0;
                                        F.ret = t165;
                                        goto gm_fret;
                                    }
                                    goto ix149;
ie149:;
ix149:;
                                }
                                {
                                    uint64_t t167 = gm_l23;
                                    uint64_t t168 = (uintptr_t)t167;
                                    uint64_t t169 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                    if (t168 >= (uintptr_t)(t169)) gm_trap_index(t168, t169, gm_src2eo2k3vc53po, 205, 9);
                                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t168 * 1), ((uint8_t)1));
                                }
                                {
                                    void * t171 = gm_l0;
                                    uint64_t t172 = gm_l1;
                                    void * t173 = (void *)((uint8_t *)&F.l17);
                                    uint64_t t174 = gm_l23;
                                    uint64_t t175 = (uintptr_t)t174;
                                    uint64_t t176 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                    if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_src2eo2k3vc53po, 206, 38);
                                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t175 * 40) + 16));
                                    gm_tkn42u7na3t81 t178;
                                    GM_ST(uint8_t, (uint8_t *)&t178 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t178 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t178), t177);
                                    ((uint8_t *)&t178)[4] = 1;
                                    gm_tkn42u7na3t81 t179 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t171, t172, t173, t178);
                                    if (gm_unw.kind) {
                                        goto gm_ll99;
                                    }
                                    uint8_t t180;
                                    if (((((uint8_t *)&t179))[4] != 0)) goto mn182;
                                    t180 = ((uint8_t)1);
                                    goto me181;
mn182:;
                                    t180 = ((uint8_t)0);
                                    goto me181;
mn183:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 206, 12);
me181:;
                                    if (!t180) goto ie170;
                                    {
                                        gm_tkn42u7na3t81 t185;
                                        memset(&t185, 0, sizeof t185);
                                        uint8_t t186 = 0;
                                        F.ret = t185;
                                        goto gm_fret;
                                    }
                                    goto ix170;
ie170:;
ix170:;
                                }
                                goto ix108;
ie108:;
                                {
                                    gm_tkn42u7na3t81 t188;
                                    memset(&t188, 0, sizeof t188);
                                    uint8_t t189 = 0;
                                    F.ret = t188;
                                    goto gm_fret;
                                }
ix108:;
                            }
                            goto me104;
mn105:;
                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 194, 5);
me104:;
                        }
lc95:;
                        {
                            uint64_t t191 = gm_l14;
                            uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                            uint64_t t194 = r192;
                            gm_l14 = t194;
                        }
                    }
gm_ll99:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
                    }
                    goto gm_lad;
lb95:;
                }
            }
            {
                uint32_t t195 = gm_l7;
                uint64_t t196 = (uint64_t)t195;
                uint64_t t197 = (uintptr_t)t196;
                void * t198 = gm_l0;
                uint64_t t199 = GM_LD(uintptr_t, ((((uint8_t *)t198) + 1088) + 0) + 8);
                if (t197 >= (uintptr_t)(t199)) gm_trap_index(t197, t199, gm_src2eo2k3vc53po, 208, 17);
                gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t198) + 1088) + 0)) + t197 * 184) + 80));
                gm_l24 = t200;
            }
            {
                {
                    gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                    gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                    gm_l25 = t203;
                }
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t205 = gm_l26;
                        uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint8_t t207 = (t205 < t206);
                        if (!t207) goto lb204;
                        {
                            uint64_t t210 = gm_l26;
                            uint64_t t211 = gm_l26;
                            uint64_t t212 = (uintptr_t)t211;
                            uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                            uint8_t t214 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t212 * 1));
                            gm_tf5srh4mp4b3i t215;
                            GM_ST(uint8_t, (uint8_t *)&t215 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t215 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t215 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t215 + 0), t210);
                            GM_ST(uint8_t, ((uint8_t *)&t215 + 8), t214);
                            uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&t215) + 0));
                            gm_l27 = t218;
                            uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&t215) + 8));
                            gm_l28 = t219;
                            {
                                uint8_t t222 = gm_l28;
                                uint8_t t223 = (uint8_t)!t222;
                                if (!t223) goto ie221;
                                uint8_t t224 = 0;
                                uint64_t t225 = gm_l27;
                                uint64_t t226 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                uint8_t t227 = (t225 < t226);
                                if (t227) {
                                    uint64_t t228 = gm_l27;
                                    uint64_t t229 = (uintptr_t)t228;
                                    uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                    if (t229 >= (uintptr_t)(t230)) gm_trap_index(t229, t230, gm_src2eo2k3vc53po, 210, 48);
                                    uint8_t t231 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t229 * 1));
                                    t224 = t231;
                                } else t224 = 0;
                                uint8_t t232 = (uint8_t)!t224;
                                if (!t232) goto ie221;
                                {
                                    gm_tkn42u7na3t81 t234;
                                    memset(&t234, 0, sizeof t234);
                                    uint8_t t235 = 0;
                                    F.ret = t234;
                                    goto gm_fret;
                                }
                                goto ix221;
ie221:;
ix221:;
                            }
                            goto me216;
mn217:;
                            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 209, 5);
me216:;
                        }
lc204:;
                        {
                            uint64_t t237 = gm_l26;
                            uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                            uint64_t t240 = r238;
                            gm_l26 = t240;
                        }
                    }
lb204:;
                }
            }
            {
                void * t241 = gm_l0;
                gm_t88rcucg1b7uk t242 = gm_l6;
                gm_tkn42u7na3t81 t243 = gm_l4;
                gm_tkn42u7na3t81 t244 = gm_f_expect_type_uld3786(t241, GM_CONV(gm_csguankko4lmhd, t242), t243);
                uint8_t t245 = 0;
                F.ret = t244;
                goto gm_fret;
            }
            goto ix52;
ie52:;
            {
                gm_tkn42u7na3t81 t247;
                memset(&t247, 0, sizeof t247);
                uint8_t t248 = 0;
                F.ret = t247;
                goto gm_fret;
            }
ix52:;
        }
        goto ix37;
ie37:;
        {
            gm_tkn42u7na3t81 t250;
            memset(&t250, 0, sizeof t250);
            uint8_t t251 = 0;
            F.ret = t250;
            goto gm_fret;
        }
ix37:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_const_type_7rijs95 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* t */
    GM_STACK_CHECK(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
        gm_l4 = t3;
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l4;
            uint8_t t9 = gm_f_is_error_8k27gd1(t7, GM_CONV(gm_csguankko4lmhd, t8));
            if (!t9) goto ie5;
            {
                gm_tkn42u7na3t81 t11;
                memset(&t11, 0, sizeof t11);
                uint8_t t12 = 0;
                F.ret = t11;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        uint64_t t15 = gm_l1;
        void * t16 = gm_l2;
        gm_tkn42u7na3t81 t17 = gm_l3;
        gm_tkn42u7na3t81 t18 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t14, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t18))[4] != 0)) goto ie13;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t18));
        gm_l5 = t19;
        {
            gm_t88rcucg1b7uk t21 = gm_l5;
            gm_tkn42u7na3t81 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[4] = 1;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie24;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
        gm_l6 = t25;
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 464);
            gm_tkmj998anon0r t29;
            memset((uint8_t *)&t29 + 20, 0, 4);
            uint64_t t30 = gm_l1;
            GM_ST(uint64_t, (uint8_t *)&t29 + 0, t30);
            void * t31 = gm_l2;
            GM_ST(void *, (uint8_t *)&t29 + 8, t31);
            gm_t88rcucg1b7uk t32 = gm_l6;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t29 + 16, t32);
            gm_tftb1odc739t5 t33 = gm_f_push_7jrb8if(t28, &t29);
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_src2eo2k3vc53po, 21, 9);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        {
            gm_t88rcucg1b7uk t35 = gm_l6;
            gm_tkn42u7na3t81 t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36), t35);
            ((uint8_t *)&t36)[4] = 1;
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l2;
        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s14frn0dagfs0));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(24));
        gm_b0_1 t42 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t38, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t43;
        memset(&t43, 0, sizeof t43);
        uint8_t t44 = 0;
        F.ret = t43;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_type_pk33veh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint64_t a3) {
    struct gm_fr_construction_type_pk33veh F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* n */
    void * gm_l4; /* callee */
    uint64_t gm_l5; /* errors */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t3mf58gv9kf3n gm_l7; /* a */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    uint64_t gm_l9; /* len */
    void * gm_l10; /* l */
    uint64_t gm_l11; /* errors */
    uint64_t gm_l12; /* k */
    GM_STACK_CHECK(&F, 665u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)19ULL)) goto mn9;
        void * t11 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t11;
        {
            void * t14 = gm_l0;
            uint64_t t15 = gm_l1;
            void * t16 = gm_l4;
            uint8_t t17 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t14, t15, t16, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = (uint8_t)!t17;
            if (!t18) goto ie13;
            {
                gm_tkn42u7na3t81 t20;
                memset(&t20, 0, sizeof t20);
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 8));
            uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 0) + 0) + 8);
            gm_l5 = t25;
        }
        {
            void * t26 = gm_l0;
            uint64_t t27 = gm_l1;
            void * t28 = gm_l4;
            gm_t88rcucg1b7uk t29 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t26, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t29;
        }
        {
            uint8_t t31 = 0;
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 8));
            uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 0) + 0) + 8);
            uint64_t t36 = gm_l5;
            uint8_t t37 = (!(t35 == t36));
            if (!t37) {
                void * t38 = gm_l0;
                void * t39 = (void *)(((uint8_t *)t38) + 8);
                gm_t88rcucg1b7uk t40 = gm_l6;
                uint8_t t41 = gm_f_is_error_8k27gd1(t39, GM_CONV(gm_csguankko4lmhd, t40));
                t31 = t41;
            } else t31 = 1;
            if (!t31) goto ie30;
            {
                gm_tkn42u7na3t81 t43;
                memset(&t43, 0, sizeof t43);
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            gm_t88rcucg1b7uk t45 = gm_l6;
            gm_tkn42u7na3t81 t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t46), t45);
            ((uint8_t *)&t46)[4] = 1;
            uint8_t t47 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t49 != ((uint8_t)28ULL)) goto mn48;
        gm_t3mf58gv9kf3n t50 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t50;
        {
            void * t52 = gm_l0;
            uint64_t t53 = gm_l1;
            void * t54 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
            uint64_t t55 = gm_l3;
            gm_tkn42u7na3t81 t56 = gm_f_construction_type_pk33veh((void *)NULL, cx, NULL, t52, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t57;
            if (((((uint8_t *)&t56))[4] != 0)) {
                gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t56) + 0));
                t57 = t58;
            } else {
                gm_tkn42u7na3t81 t59;
                memset(&t59, 0, sizeof t59);
                uint8_t t60 = 0;
                F.ret = t59;
                goto gm_fret;
            }
            gm_l8 = t57;
        }
        {
            uint64_t t61 = gm_l3;
            uint64_t t62 = (uint64_t)t61;
            gm_l9 = t62;
        }
        {
            void * t64 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
            if (!(GM_LD(void *, ((uint8_t *)&t64)) != 0)) goto ie63;
            void * t65 = GM_LD(void *, ((uint8_t *)&t64));
            gm_l10 = t65;
            {
                void * t68 = gm_l10;
                uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t68) + 8) + 0));
                if (t69 != ((uint8_t)6ULL)) goto ie67;
                void * t70 = gm_l0;
                uint64_t t71 = gm_l1;
                void * t72 = gm_l10;
                uint8_t t73 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t70, t71, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t73) goto ie67;
                {
                    gm_tkn42u7na3t81 t75;
                    memset(&t75, 0, sizeof t75);
                    uint8_t t76 = 0;
                    F.ret = t75;
                    goto gm_fret;
                }
                goto ix67;
ie67:;
ix67:;
            }
            {
                void * t77 = gm_l0;
                void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
                void * t79 = GM_LD(void *, (((uint8_t *)t78) + 8));
                uint64_t t80 = GM_LD(uintptr_t, ((((uint8_t *)t79) + 0) + 0) + 8);
                gm_l11 = t80;
            }
            {
                void * t81 = gm_l0;
                uint64_t t82 = gm_l1;
                void * t83 = gm_l10;
                gm_t5e6kkuq21nig t84 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t84) + 0));
                if (t87 != ((uint8_t)0ULL)) goto mn86;
                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&t84) + 8));
                gm_l12 = t88;
                {
                    uint64_t t90 = gm_l12;
                    gm_l9 = t90;
                }
                goto me85;
mn86:;
                gm_tkn42u7na3t81 t92;
                memset(&t92, 0, sizeof t92);
                uint8_t t93 = 0;
                F.ret = t92;
                goto gm_fret;
                goto me85;
mn91:;
                gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 169, 17);
me85:;
            }
            {
                void * t95 = gm_l0;
                void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
                void * t97 = GM_LD(void *, (((uint8_t *)t96) + 8));
                uint64_t t98 = GM_LD(uintptr_t, ((((uint8_t *)t97) + 0) + 0) + 8);
                uint64_t t99 = gm_l11;
                uint8_t t100 = (!(t98 == t99));
                if (!t100) goto ie94;
                {
                    gm_tkn42u7na3t81 t102;
                    memset(&t102, 0, sizeof t102);
                    uint8_t t103 = 0;
                    F.ret = t102;
                    goto gm_fret;
                }
                goto ix94;
ie94:;
ix94:;
            }
            goto ix63;
ie63:;
ix63:;
        }
        {
            void * t104 = gm_l0;
            void * t105 = (void *)(((uint8_t *)t104) + 8);
            uint64_t t106 = gm_l9;
            gm_t88rcucg1b7uk t107 = gm_l8;
            gm_t88rcucg1b7uk t108 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t105, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t109), t108);
            ((uint8_t *)&t109)[4] = 1;
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
        }
        goto me3;
mn48:;
ma6:;
        gm_tkn42u7na3t81 t112;
        memset(&t112, 0, sizeof t112);
        uint8_t t113 = 0;
        F.ret = t112;
        goto gm_fret;
        goto me3;
mn111:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 155, 5);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_bool_edv2577(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1) {
    struct gm_fr_expect_bool_edv2577 F;
    void * gm_l0; /* s */
    gm_tkn42u7na3t81 gm_l1; /* expected */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l3; /* x */
    GM_STACK_CHECK(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        gm_tkn42u7na3t81 t5 = gm_l1;
        gm_t88rcucg1b7uk t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_t88rcucg1b7uk t8 = gm_l2;
            gm_tkn42u7na3t81 t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[4] = 1;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        uint8_t t12 = 0;
        gm_t88rcucg1b7uk t13 = gm_l3;
        gm_t88rcucg1b7uk t14 = gm_l2;
        uint8_t t15 = (gm_eq9m6mvtmgde(((uint8_t *)&t13), ((uint8_t *)&t14)));
        if (!t15) {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l3;
            gm_t53at80pfdus3 t19;
            gm_f_kind_jcnt4hm((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
            gm_t88rcucg1b7uk t20 = gm_l2;
            uint8_t t21 = (((GM_LD(uint8_t, ((uint8_t *)&t19) + 0) == ((uint8_t)13ULL)) && gm_eq9m6mvtmgde((((uint8_t *)&t19) + 8), ((uint8_t *)&t20))));
            t12 = t21;
        } else t12 = 1;
        if (!t12) goto ie11;
        {
            gm_t88rcucg1b7uk t23 = gm_l3;
            gm_tkn42u7na3t81 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[4] = 1;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_tkn42u7na3t81 t26;
        memset(&t26, 0, sizeof t26);
        uint8_t t27 = 0;
        F.ret = t26;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_expect_type_uld3786(void * a0, gm_csguankko4lmhd a1, gm_tkn42u7na3t81 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_expect_type_uld3786 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tkn42u7na3t81 gm_l2; /* expected */
    gm_t88rcucg1b7uk gm_l3; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        gm_tkn42u7na3t81 t2 = gm_l2;
        gm_t88rcucg1b7uk t3;
        if (((((uint8_t *)&t2))[4] != 0)) {
            gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_tkn42u7na3t81 t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[4] = 1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l3 = t3;
    }
    {
        gm_tkn42u7na3t81 t8;
        gm_t88rcucg1b7uk t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = gm_l1;
        uint8_t t12 = (gm_eq9m6mvtmgde(((uint8_t *)&t10), ((uint8_t *)&t11)));
        if (!t12) goto ie9;
        gm_t88rcucg1b7uk t14 = gm_l1;
        gm_t88rcucg1b7uk t15 = t14;
        gm_tkn42u7na3t81 t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16), t15);
        ((uint8_t *)&t16)[4] = 1;
        t8 = t16;
        goto ix9;
ie9:;
        gm_tkn42u7na3t81 t18;
        memset(&t18, 0, sizeof t18);
        gm_tkn42u7na3t81 t19 = t18;
        t8 = t19;
ix9:;
        uint8_t t20 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_field_visible_7kr8oue(void * a0, uint64_t a1, uint32_t a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_float_finite_ak3diof(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_float_finite_ak3diof F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint16_t gm_l3; /* bits */
    double gm_l4; /* x */
    float gm_l5; /* x */
    GM_STACK_CHECK(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)3ULL)) goto ie2;
        uint16_t t8 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t8;
        {
            uint16_t t11 = gm_l3;
            uint8_t t12 = ((t11 == ((uint16_t)64ULL)));
            if (!t12) goto ie10;
            {
                double t14 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t14;
            }
            {
                double t15 = gm_l4;
                double t16 = gm_l4;
                double t17 = (t15 - t16);
                uint8_t t18 = ((t17 == ((double)0e0)));
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        {
            uint16_t t21 = gm_l3;
            uint8_t t22 = ((t21 == ((uint16_t)32ULL)));
            if (!t22) goto ie20;
            {
                float t24 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t24;
            }
            {
                float t25 = gm_l5;
                float t26 = gm_l5;
                float t27 = (t25 - t26);
                uint8_t t28 = ((t27 == ((float)0e0)));
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            uint8_t t30 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t32 = 0;
            F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_suffix_type_icfvn23(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tj2m9dbh1ekv6 a1) {
    struct gm_fr_suffix_type_icfvn23 F;
    void * gm_l0; /* s */
    gm_tj2m9dbh1ekv6 gm_l1; /* suffix */
    uint32_t gm_l2; /* bits */
    uint32_t gm_l3; /* bits */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_l2 = t11;
    void * t12 = gm_l0;
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    uint32_t t14 = gm_l2;
    uint16_t t15 = (uint16_t)t14;
    gm_t88rcucg1b7uk t16 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t13, ((uint8_t)0), t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t16;
    goto me2;
mn9:;
ma4:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t18 != ((uint8_t)1ULL)) goto mn17;
    uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_l3 = t19;
    void * t20 = gm_l0;
    void * t21 = (void *)(((uint8_t *)t20) + 8);
    uint32_t t22 = gm_l3;
    uint16_t t23 = (uint16_t)t22;
    gm_t88rcucg1b7uk t24 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t21, ((uint8_t)1), t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t24;
    goto me2;
mn17:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    void * t27 = gm_l0;
    void * t28 = (void *)(((uint8_t *)t27) + 8);
    gm_t88rcucg1b7uk t29 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t29;
    goto me2;
mn25:;
ma6:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t31 != ((uint8_t)3ULL)) goto mn30;
    void * t32 = gm_l0;
    void * t33 = (void *)(((uint8_t *)t32) + 8);
    gm_t53at80pfdus3 t34;
    memset(&t34, 0, sizeof t34);
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
    gm_t88rcucg1b7uk t35 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t33, &t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t35;
    goto me2;
mn30:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 123, 89);
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
__attribute__((aligned(4))) uint8_t gm_f_untyped_fits_tp6gphv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_untyped_fits_tp6gphv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* x */
    uint16_t gm_l9; /* bits */
    GM_STACK_CHECK(&F, 1418u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)13ULL)) goto ie3;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l5 = t9;
        {
            gm_t88rcucg1b7uk t11 = gm_l5;
            gm_l4 = t11;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = gm_l1;
        void * t14 = gm_l2;
        gm_tkv1vap90vht2 t15;
        gm_f_untyped_if_plain_lbrp24p((void *)&t15, (void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5vbkdq81qfs7 t16;
        if (((((uint8_t *)&t15))[208] != 0)) {
            t16 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t15) + 0));
        } else {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l6 = t16;
    }
    {
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
        if (t19 != ((uint8_t)0ULL)) goto ie18;
        gm_t0ialelq3ql1k t20 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l6) + 8));
        F.l7 = t20;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            switch (t29) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma25; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma27; default: goto ma28; }
ma23:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t31 != ((uint8_t)0ULL)) goto mn30;
            gm_tbci5ospk5nnn t32 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l7) + 8));
            F.l8 = t32;
            {
                void * t35 = gm_l0;
                void * t36 = (void *)(((uint8_t *)t35) + 8);
                gm_t88rcucg1b7uk t37 = gm_l4;
                uint8_t t38 = gm_f_is_integer_8d4f9ek(t36, GM_CONV(gm_csguankko4lmhd, t37));
                if (!t38) goto ie34;
                {
                    void * t40 = gm_l0;
                    gm_t88rcucg1b7uk t41 = gm_l4;
                    uint8_t t42 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t40, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), t41);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
                goto ix34;
ie34:;
ix34:;
            }
            {
                void * t45 = gm_l0;
                void * t46 = (void *)(((uint8_t *)t45) + 8);
                gm_t88rcucg1b7uk t47 = gm_l4;
                gm_t53at80pfdus3 t48;
                gm_f_kind_jcnt4hm((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t49 != ((uint8_t)3ULL)) goto ie44;
                uint16_t t50 = GM_LD(uint16_t, (((uint8_t *)&t48) + 8));
                gm_l9 = t50;
                {
                    gm_f_from_int_ofjuoqh((void *)&F.l10, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t52 = 0;
                    void * t53 = gm_l0;
                    gm_t88rcucg1b7uk t54 = gm_l4;
                    uint8_t t55 = gm_f_float_finite_ak3diof((void *)NULL, cx, NULL, t53, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t55) {
                        uint16_t t56 = gm_l9;
                        double t57 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), t56);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tngtuiuskn938 t58;
                        gm_f_from_f64_1parp9j((void *)&t58, (void *)NULL, cx, NULL, t57);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t59 = gm_f_eq_ceufbo4(&t58, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)));
                        t52 = t59;
                    } else t52 = 0;
                    uint8_t t60 = 0;
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix44;
ie44:;
                {
                    uint8_t t62 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
ix44:;
            }
            goto me22;
mn30:;
ma24:;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t64 != ((uint8_t)1ULL)) goto mn63;
            gm_tngtuiuskn938 t65 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l7) + 8));
            F.l11 = t65;
            uint8_t t66 = 0;
            void * t67 = gm_l0;
            void * t68 = (void *)(((uint8_t *)t67) + 8);
            gm_t88rcucg1b7uk t69 = gm_l4;
            gm_t53at80pfdus3 t70;
            gm_f_kind_jcnt4hm((void *)&t70, t68, GM_CONV(gm_csguankko4lmhd, t69));
            uint8_t t71;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
            if (t74 != ((uint8_t)3ULL)) goto mn73;
            t71 = ((uint8_t)1);
            goto me72;
mn73:;
            t71 = ((uint8_t)0);
            goto me72;
mn75:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 233, 29);
me72:;
            if (t71) {
                void * t76 = gm_l0;
                gm_t88rcucg1b7uk t77 = gm_l4;
                uint8_t t78 = gm_f_float_finite_ak3diof((void *)NULL, cx, NULL, t76, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)), t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t66 = t78;
            } else t66 = 0;
            uint8_t t79 = 0;
            F.ret = t66;
            goto gm_fret;
            goto me22;
mn63:;
ma25:;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t81 != ((uint8_t)2ULL)) goto mn80;
            void * t82 = gm_l0;
            void * t83 = (void *)(((uint8_t *)t82) + 8);
            gm_t88rcucg1b7uk t84 = gm_l4;
            gm_t53at80pfdus3 t85;
            gm_f_kind_jcnt4hm((void *)&t85, t83, GM_CONV(gm_csguankko4lmhd, t84));
            uint8_t t86;
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t85) + 0));
            if (t89 != ((uint8_t)3ULL)) goto mn88;
            t86 = ((uint8_t)1);
            goto me87;
mn88:;
            t86 = ((uint8_t)0);
            goto me87;
mn90:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 234, 28);
me87:;
            uint8_t t91 = 0;
            F.ret = t86;
            goto gm_fret;
            goto me22;
mn80:;
ma26:;
            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t93 != ((uint8_t)3ULL)) goto mn92;
            void * t94 = gm_l0;
            void * t95 = (void *)(((uint8_t *)t94) + 8);
            gm_t88rcucg1b7uk t96 = gm_l4;
            gm_t53at80pfdus3 t97;
            gm_f_kind_jcnt4hm((void *)&t97, t95, GM_CONV(gm_csguankko4lmhd, t96));
            uint8_t t98 = ((GM_LD(uint8_t, ((uint8_t *)&t97) + 0) == ((uint8_t)4ULL)));
            uint8_t t99 = 0;
            F.ret = t98;
            goto gm_fret;
            goto me22;
mn92:;
ma27:;
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t101 != ((uint8_t)4ULL)) goto mn100;
            uint8_t t102 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
            goto me22;
mn100:;
ma28:;
            gm_trap("no match arm matched the scrutinee", gm_src2eo2k3vc53po, 225, 5);
me22:;
        }
        goto ix18;
ie18:;
        {
            uint8_t t104 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix18:;
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
inline __attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_untyped_if_plain_lbrp24p F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* errors */
    uint8_t gm_l4; /* was */
    GM_STACK_CHECK(&F, 1015u);
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
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 2536));
        gm_l4 = t7;
    }
    {
        void * t8 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t8) + 2536), ((uint8_t)1));
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = gm_l1;
        void * t11 = gm_l2;
        gm_f_try_untyped_fj0h8i5((void *)&F.l5, (void *)NULL, cx, NULL, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        uint8_t t13 = gm_l4;
        GM_ST(uint8_t, (((uint8_t *)t12) + 2536), t13);
    }
    {
        uint8_t t15 = 0;
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 0) + 0) + 8);
        uint64_t t20 = gm_l3;
        uint8_t t21 = (!(t19 == t20));
        if (!t21) {
            gm_tkv1vap90vht2 t22 = F.l5;
            gm_t5vbkdq81qfs7 t23;
            memset(&t23, 0, sizeof t23);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1ULL));
            gm_tkv1vap90vht2 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 209, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 210, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 212, 0);
            GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[208] = 1;
            uint8_t t25 = (gm_eq0uoi35f1be(((uint8_t *)&t22), ((uint8_t *)&t24)));
            t15 = t25;
        } else t15 = 1;
        if (!t15) goto ie14;
        {
            gm_tkv1vap90vht2 t27;
            memset(&t27, 0, sizeof t27);
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l5), 216);
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
