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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; uint32_t f2; uint8_t pe[4]; } gm_tk20dunvmmkbp;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct gm_csedogq9e4rp5r { uint32_t f0; gm_cseahvg8v7p0vs f1; } gm_csedogq9e4rp5r;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tk20dunvmmkbp) == 24 && _Alignof(gm_tk20dunvmmkbp) == 8 && offsetof(gm_tk20dunvmmkbp, f0) == 0 && offsetof(gm_tk20dunvmmkbp, f1) == 8 && offsetof(gm_tk20dunvmmkbp, f2) == 16, "layout of `(id: u32, env: ?^Frame, cx: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_csedogq9e4rp5r) == 16 && _Alignof(gm_csedogq9e4rp5r) == 4 && offsetof(gm_csedogq9e4rp5r, f0) == 0 && offsetof(gm_csedogq9e4rp5r, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tk20dunvmmkbp v0; gm_td7tfovebirdn v1; gm_t9qkh0rsooc8d v2; } f1; } gm_tte9vgf6noic9;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tiilmfd16v3fc f1; gm_tte9vgf6noic9 f2; uint64_t f3; uint32_t f4; uint8_t p5[4]; void * f5; uint64_t f6; } gm_tjsm4kosd5pev;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tte9vgf6noic9) == 48 && _Alignof(gm_tte9vgf6noic9) == 8 && offsetof(gm_tte9vgf6noic9, f0) == 0 && offsetof(gm_tte9vgf6noic9, f1) == 8, "layout of `Installed`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tjsm4kosd5pev) == 112 && _Alignof(gm_tjsm4kosd5pev) == 8 && offsetof(gm_tjsm4kosd5pev, f0) == 0 && offsetof(gm_tjsm4kosd5pev, f1) == 8 && offsetof(gm_tjsm4kosd5pev, f2) == 32 && offsetof(gm_tjsm4kosd5pev, f3) == 80 && offsetof(gm_tjsm4kosd5pev, f4) == 88 && offsetof(gm_tjsm4kosd5pev, f5) == 96 && offsetof(gm_tjsm4kosd5pev, f6) == 104, "layout of `CtxNode`");
struct gm_fr_call_clause_bjbs6l1 {
    gm_frame h;
    gm_tjsm4kosd5pev l6; /* node */
    gm_trqfvmjtt4sij l21; /* all */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_function_u7ovth6 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_missing_5ejgl53 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_node_a1b30k9 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_value_fm9m0cc {
    gm_frame h;
    gm_tddtjfggdat0d l7; /* sig */
    gm_tjsm4kosd5pev l16; /* node */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_emit_8atsnfj {
    gm_frame h;
    gm_th1g69d7vpipg l3; /* sink */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_frame_cetr4jh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_enter_maqkh2o {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_call_value_f4tfqs3 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_perform_ovt4j0p {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_info_hocist4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_free_decl_values_fko33ae {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_handler_decl_m66mgr8 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_lit_b02boff {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_addr_o51qq3k {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_new_frame_vgm56mt {
    gm_frame h;
    void * ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_perform_4kn6797 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_perform_native_2ppar83 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_truncate_9n3opb7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_j7f0pmi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_with__u1jp2ib {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* w */
    gm_tte9vgf6noic9 l19; /* inst */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_write__eua2bmh {
    gm_frame h;
    gm_th1g69d7vpipg l2; /* w_sink */
    gm_trqfvmjtt4sij l11; /* vals */
    gm_trqfvmjtt4sij l15; /* out */
    gm_tcfa6l7reoosn ret;
};
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_alloc_frame_31ks03n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_call_clause_bjbs6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_call_missing_5ejgl53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_call_node_a1b30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_call_value_fm9m0cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_ctl_perform_kobk825(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_emit_8atsnfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, const void * a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_end_frame_slow_mnbcrps(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_enter_maqkh2o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, void * a3, void * a4, gm_b16_8 a5, void * a6, uint32_t a7, const void * a8, uint8_t a9, gm_b16_8 a10);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_ev_call_value_f4tfqs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_ev_perform_ovt4j0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, uint32_t a3, gm_b16_8 a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_5tfshb9(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint32_t gm_f_find_ctx_1elkgc2(void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5mel2nn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jr8ev5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k34396d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_la70vrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ms0imbl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rlkig7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_spec_7aph60t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) void gm_f_format_tppjqmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) void * gm_f_frame_of_dgl92on(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_decl_values_fko33ae(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_grow_mem_buf_930r21a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_kill_temps_8sgbc55(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_make_fn_info_4dk91jl(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_perform_4kn6797(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, void * a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_pop_ctx_jivp36j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_aaqnel1(void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_push_ctx_1rv0jgf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, const void * a3, uint64_t a4, uint32_t a5, void * a6);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t8up8j4_2(void * a0, gm_csedogq9e4rp5r a1);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_9n3opb7(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_j7f0pmi(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
GM_LONG_FN __attribute__((aligned(4))) void gm_f_vm_run_j1mlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_vm_run_j1mlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_with__u1jp2ib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_with_on_a8m20cg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write__eua2bmh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, void * a6);
extern uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
extern uint8_t gm_cdps8qsftpu[4] __attribute__((aligned(4)));
extern uint8_t gm_k9nqfo1idrl[40] __attribute__((aligned(8)));
extern uint8_t gm_kk1no0qhlfa[40] __attribute__((aligned(8)));
extern uint8_t gm_ko0rgal5bm2[40] __attribute__((aligned(8)));
extern uint8_t gm_krkn5fd15l7[40] __attribute__((aligned(8)));
extern const uint8_t gm_s38rtd27u976k[15];
extern const uint8_t gm_s3p4vlg0qjpu0[46];
extern const uint8_t gm_slg8371oger0u[32];
extern const uint8_t gm_sq5khm5l62ens[37];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr82af5e87dso[54];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6c128h826nsg[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const gm_type gm_tyo86ks9qkvt;
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
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_end_frame_cetr4jh F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        void * t7 = gm_l1;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 96) + 0) + 8);
        uint8_t t9 = (t8 > ((uint64_t)0ULL));
        if (!t9) {
            void * t10 = gm_l1;
            uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 136) + 0) + 8);
            uint8_t t12 = (t11 > ((uint64_t)0ULL));
            t6 = t12;
        } else t6 = 1;
        if (!t6) {
            void * t13 = gm_l1;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 176) + 0) + 8);
            uint8_t t15 = (t14 > ((uint64_t)0ULL));
            t5 = t15;
        } else t5 = 1;
        if (!t5) {
            void * t16 = gm_l1;
            uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 8));
            uint8_t t18 = (!(t17 == ((uint32_t)0ULL)));
            t4 = t18;
        } else t4 = 1;
        if (!t4) {
            void * t19 = gm_l0;
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)t19) + 544));
            uint8_t t21 = (t20 >= ((uint32_t)256ULL));
            t3 = t21;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t23 = gm_l0;
            void * t24 = gm_l1;
            gm_b0_1 t25 = gm_f_end_frame_slow_mnbcrps((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t26;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t28 = gm_l1;
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 536));
        GM_ST(void *, (((uint8_t *)t28) + 248), t30);
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        GM_ST(void *, (((uint8_t *)t31) + 536), t32);
    }
    {
        void * t33 = gm_l0;
        void * t34 = gm_l0;
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)t34) + 544));
        uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 152, 5);
        uint32_t t38 = r36;
        GM_ST(uint32_t, (((uint8_t *)t33) + 544), t38);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_fn_info_hocist4 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* x */
    GM_STACK_CHECK(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l2 = t4;
    }
    {
        uint64_t t6 = gm_l2;
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 720) + 0) + 8);
        uint8_t t9 = (t6 < t8);
        if (!t9) goto ie5;
        {
            uint64_t t12 = gm_l2;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 720) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src6hq50r4rk7e6, 19, 17);
            void * t16 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 720) + 0)) + t13 * 8));
            if (!(GM_LD(void *, ((uint8_t *)&t16)) != 0)) goto ie11;
            void * t17 = GM_LD(void *, ((uint8_t *)&t16));
            gm_l3 = t17;
            {
                void * t19 = gm_l3;
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t21 = gm_l0;
        uint32_t t22 = gm_l1;
        void * t23 = gm_f_make_fn_info_4dk91jl((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = 0;
        F.ret = t23;
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
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_decl_m66mgr8 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 544) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 132, 83);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 544) + 0)) + t3 * 64);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_b02boff F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 504) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 129, 76);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 504) + 0)) + t3 * 40);
    F.ret = t7;
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2p7d2kv();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3) {
    struct gm_fr_local_addr_o51qq3k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l4 = t4;
    }
    {
        uint64_t t6 = gm_l4;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l2;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6hq50r4rk7e6, 193, 8);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 64)) + t7 * 8));
        uint8_t t11 = (!(t10 == ((uint64_t)0ULL)));
        if (!t11) goto ie5;
        {
            uint64_t t13 = gm_l4;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l2;
            uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 64) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src6hq50r4rk7e6, 193, 36);
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 64)) + t14 * 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t17);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l1;
        void * t20 = gm_l2;
        uint32_t t21 = gm_l3;
        gm_f_local_alloc_qm9q4fo((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t18, t19, t20, t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_new_frame_vgm56mt F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* fn_id */
    void * gm_l2; /* info */
    gm_b16_8 gm_l3; /* ret */
    void * gm_l4; /* env */
    gm_b16_8 gm_l5; /* store */
    uint64_t gm_l6; /* n */
    void * gm_l7; /* fr */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l2;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 120) + 8);
        gm_l6 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 536));
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_b16_8 t10 = gm_l3;
            void * t11 = gm_f_alloc_frame_31ks03n((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t6 = t11;
        }
        gm_l7 = t6;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l7;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 248));
        GM_ST(void *, (((uint8_t *)t12) + 536), t14);
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l0;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 544));
        uint32_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint32_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 93, 5);
        uint32_t t20 = r18;
        GM_ST(uint32_t, (((uint8_t *)t15) + 544), t20);
    }
    {
        void * t22 = gm_l7;
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 80) + 8);
        uint64_t t24 = gm_l6;
        uint8_t t25 = (t23 < t24);
        if (!t25) goto ie21;
        {
            void * t27 = gm_l0;
            void * t28 = gm_l7;
            uint64_t t29 = gm_l6;
            gm_b0_1 t30 = gm_f_grow_mem_buf_930r21a((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t31 = gm_l7;
        void * t32 = gm_l7;
        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)t32) + 80) + 8);
        void * t34 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t32) + 80)));
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = gm_l6;
        uint64_t t37 = (uintptr_t)t36;
        if (t35 > t37 || t37 > (uintptr_t)t33) gm_trap_slice(t35, t37, t33, gm_src6hq50r4rk7e6, 96, 14);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t34 + t35 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
        GM_ST(gm_b16_8, (((uint8_t *)t31) + 64), t38);
    }
    {
        void * t39 = gm_l7;
        void * t40 = gm_l0;
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 760));
        GM_ST(uint64_t, (((uint8_t *)t39) + 0), t41);
    }
    {
        void * t42 = gm_l7;
        uint32_t t43 = gm_l1;
        GM_ST(uint32_t, (((uint8_t *)t42) + 12), t43);
    }
    {
        void * t44 = gm_l7;
        void * t45 = gm_l2;
        GM_ST(void *, (((uint8_t *)t44) + 16), t45);
    }
    {
        void * t46 = gm_l7;
        gm_b16_8 t47 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t46) + 24), t47);
    }
    {
        void * t48 = gm_l7;
        void * t49 = gm_l4;
        GM_ST(void *, (((uint8_t *)t48) + 40), t49);
    }
    {
        void * t50 = gm_l7;
        gm_b16_8 t51 = gm_l5;
        GM_ST(gm_b16_8, (((uint8_t *)t50) + 48), t51);
    }
    {
        void * t52 = gm_l7;
        gm_t5lf9i3n1r3co t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t52) + 216), t53);
    }
    {
        void * t54 = gm_l7;
        GM_ST(uint8_t, (((uint8_t *)t54) + 240), ((uint8_t)0));
    }
    {
        void * t55 = gm_l0;
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 760));
        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 105, 5);
        uint64_t t60 = r58;
        GM_ST(uint64_t, (((uint8_t *)t55) + 760), t60);
    }
    {
        void * t61 = gm_l7;
        GM_ST(uint32_t, (((uint8_t *)t61) + 8), ((uint32_t)0ULL));
    }
    {
        void * t62 = gm_l7;
        uint8_t t63 = 0;
        F.ret = t62;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_truncate_9n3opb7(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_9n3opb7 F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
inline __attribute__((aligned(4))) void gm_f_truncate_j7f0pmi(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_j7f0pmi F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 16)));
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 144u);
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
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t7);
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
__attribute__((aligned(4))) void gm_f_call_clause_bjbs6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_call_clause_bjbs6l1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* n */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* dst */
    gm_t5lf9i3n1r3co gm_l7; /* installing */
    void * gm_l8; /* fr */
    uint32_t gm_l9; /* h */
    void * gm_l10; /* env */
    uint32_t gm_l11; /* cx */
    uint32_t gm_l12; /* clause */
    gm_b16_8 gm_l13; /* decls */
    gm_b16_8 gm_l14; /* value */
    uint32_t gm_l15; /* outer */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* d */
    void * gm_l19; /* info */
    uint32_t gm_l20; /* clause */
    gm_t9qkh0rsooc8d gm_l22; /* kind */
    GM_STACK_CHECK(&F, 1128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 632) + 0));
        gm_b16_8 t4 = t3;
        uint32_t t5 = gm_l2;
        uint64_t t6 = (uint64_t)t5;
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        uint64_t t8 = (uintptr_t)t7;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src6c128h826nsg, 174, 13);
        gm_tjsm4kosd5pev t10 = GM_LD(gm_tjsm4kosd5pev, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t4)) + t8 * 112));
        F.l6 = t10;
    }
    {
        gm_t5lf9i3n1r3co t11;
        memset(&t11, 0, sizeof t11);
        gm_l7 = t11;
    }
    {
        void * t13 = GM_LD(void *, (((uint8_t *)&F.l6) + 96));
        if (!(GM_LD(void *, ((uint8_t *)&t13)) != 0)) goto ie12;
        void * t14 = GM_LD(void *, ((uint8_t *)&t13));
        gm_l8 = t14;
        {
            void * t16 = gm_l8;
            uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 88));
            gm_tbl8il92l97ut t18;
            GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t18 + 0), t16);
            GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)) + 20, 0);
            ((uint8_t *)&(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)))[16] = 1;
            GM_ST(gm_tbl8il92l97ut, (uint8_t *)&(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)) + 0, t18);
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; default: goto ma23; }
ma20:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
        if (t26 != ((uint8_t)0ULL)) goto mn25;
        uint32_t t27 = GM_LD(uint32_t, (((((uint8_t *)&F.l6) + 32) + 8) + 0));
        gm_l9 = t27;
        void * t28 = GM_LD(void *, (((((uint8_t *)&F.l6) + 32) + 8) + 8));
        gm_l10 = t28;
        uint32_t t29 = GM_LD(uint32_t, (((((uint8_t *)&F.l6) + 32) + 8) + 16));
        gm_l11 = t29;
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
            uint32_t t33 = gm_l9;
            void * t34 = gm_f_handler_lit_b02boff(t32, t33);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t34) + 24));
            gm_b16_8 t36 = t35;
            uint32_t t37 = gm_l3;
            uint64_t t38 = (uint64_t)t37;
            uint64_t t39 = gm_f_us_cuv01fp(t38);
            uint64_t t40 = (uintptr_t)t39;
            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&t36) + 8);
            if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src6c128h826nsg, 179, 23);
            uint32_t t42 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t36)) + t40 * 4));
            gm_l12 = t42;
        }
        {
            void * t43 = gm_l0;
            gm_tl874t2atnlh3 t44 = gm_l1;
            uint32_t t45 = gm_l12;
            gm_b16_8 t46 = gm_l4;
            void * t47 = gm_l10;
            uint32_t t48 = gm_l11;
            gm_b16_8 t49 = gm_l5;
            gm_f_call_function_u7ovth6((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t43, t44, t45, t46, t47, t48, &(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)), ((uint8_t)1), t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me19;
mn25:;
ma21:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
        if (t51 != ((uint8_t)1ULL)) goto mn50;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l6) + 32) + 8) + 0));
        gm_l13 = t52;
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l6) + 32) + 8) + 16));
        gm_l14 = t53;
        uint32_t t54 = GM_LD(uint32_t, (((((uint8_t *)&F.l6) + 32) + 8) + 32));
        gm_l15 = t54;
        {
            {
                gm_b16_8 t57 = gm_l13;
                gm_l16 = t57;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t59 = gm_l17;
                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t61 = (t59 < t60);
                    if (!t61) goto lb58;
                    {
                        uint64_t t64 = gm_l17;
                        uint64_t t65 = (uintptr_t)t64;
                        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint32_t t67 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t65 * 4));
                        uint32_t t70 = GM_LD(uint32_t, ((uint8_t *)&t67));
                        gm_l18 = t70;
                        {
                            void * t72 = gm_l0;
                            void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
                            uint32_t t74 = gm_l18;
                            void * t75 = gm_f_handler_decl_m66mgr8(t73, t74);
                            gm_l19 = t75;
                        }
                        {
                            uint8_t t77 = 0;
                            void * t78 = gm_l19;
                            uint32_t t79 = GM_LD(uint32_t, ((((uint8_t *)t78) + 0) + 0));
                            uint32_t t80 = GM_LD(uint32_t, ((((uint8_t *)&F.l6) + 8) + 0));
                            uint8_t t81 = (!(t79 == t80));
                            if (!t81) {
                                void * t82 = gm_l19;
                                void * t83 = (void *)(((uint8_t *)t82) + 0);
                                void * t84 = (void *)(((uint8_t *)&F.l6) + 8);
                                int32_t t85 = gm_f_cmp_ref_8ntrqll(t83, t84);
                                uint8_t t86 = (!(t85 == ((int32_t)0LL)));
                                t77 = t86;
                            } else t77 = 1;
                            if (!t77) goto ie76;
                            {
                                goto lc58;
                            }
                            goto ix76;
ie76:;
ix76:;
                        }
                        {
                            void * t88 = gm_l19;
                            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)t88) + 48));
                            gm_b16_8 t90 = t89;
                            uint32_t t91 = gm_l3;
                            uint64_t t92 = (uint64_t)t91;
                            uint64_t t93 = gm_f_us_cuv01fp(t92);
                            uint64_t t94 = (uintptr_t)t93;
                            uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&t90) + 8);
                            if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_src6c128h826nsg, 187, 27);
                            uint32_t t96 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t90)) + t94 * 4));
                            gm_l20 = t96;
                        }
                        {
                            gm_f_list_fufbgf7((void *)&F.l21, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll62;
                            }
                        }
                        {
                        }
                        {
                            void * t98 = (void *)((uint8_t *)&F.l21);
                            gm_b16_8 t99 = gm_l14;
                            gm_tftb1odc739t5 t100 = gm_f_push_rluch9r(t98, t99);
                            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_src6c128h826nsg, 190, 17);
                            gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                        }
                        {
                            void * t102 = (void *)((uint8_t *)&F.l21);
                            gm_b16_8 t103 = gm_l4;
                            gm_tftb1odc739t5 t104 = gm_f_extend_5tfshb9(t102, t103);
                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_src6c128h826nsg, 191, 17);
                            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                        }
                        {
                            void * t106 = gm_l0;
                            gm_tl874t2atnlh3 t107 = gm_l1;
                            uint32_t t108 = gm_l20;
                            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                            uint32_t t111 = gm_l15;
                            gm_b16_8 t112 = gm_l5;
                            gm_f_call_function_u7ovth6((void *)&F.ret, (void *)NULL, cx, NULL, t106, t107, t108, t110, ((void *)0), t111, &(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l7)), ((uint8_t)0), t112);
                            if (gm_unw.kind) {
                                goto gm_ld71_1;
                            }
                            uint8_t t113 = ((((uint8_t *)&F.ret))[0] != 0);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t113;
                            goto gm_ld71_1;
                        }
                        {
                            void * t114 = (void *)((uint8_t *)&F.l21);
                            gm_f_deinit_tq06jtm(t114);
                        }
                        goto gm_ls115;
gm_ld71_1:;
                        {
                            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                            void * t116 = (void *)((uint8_t *)&F.l21);
                            gm_f_deinit_tq06jtm(t116);
                            gm_unw = gm_saved;
                        }
                        goto gm_ll62;
gm_ls115:;
                        goto me68;
mn69:;
                        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 184, 13);
me68:;
                    }
lc58:;
                    {
                        uint64_t t118 = gm_l17;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l17 = t121;
                    }
                }
gm_ll62:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb58; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc58; }
                }
                goto gm_lad;
lb58:;
            }
        }
        {
            void * t122 = gm_l0;
            gm_tl874t2atnlh3 t123 = gm_l1;
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sr82af5e87dso));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(53));
            gm_t76123gfet73g t125;
            gm_f_trap_qh84l9d((void *)&t125, (void *)NULL, cx, NULL, t122, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t125);
            goto gm_fdone;
        }
        goto me19;
mn50:;
ma22:;
        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
        if (t127 != ((uint8_t)2ULL)) goto mn126;
        gm_t9qkh0rsooc8d t128 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l6) + 32) + 8));
        gm_l22 = t128;
        void * t129 = gm_l0;
        gm_tl874t2atnlh3 t130 = gm_l1;
        gm_t9qkh0rsooc8d t131 = gm_l22;
        uint32_t t132 = gm_l3;
        gm_b16_8 t133 = gm_l4;
        gm_b16_8 t134 = gm_l5;
        gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t129, t130, t131, t132, t133, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me19;
mn126:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 177, 5);
me19:;
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
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8) {
    struct gm_fr_call_function_u7ovth6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* f */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* env */
    uint32_t gm_l5; /* cx */
    gm_t5lf9i3n1r3co gm_l6; /* installing */
    uint8_t gm_l7; /* is_clause_lit */
    gm_b16_8 gm_l8; /* dst */
    void * gm_l9; /* info */
    void * gm_l10; /* body */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* store */
    uint64_t gm_l13; /* i */
    gm_b16_8 gm_l14; /* x */
    uint64_t gm_l15; /* off */
    gm_tcfa6l7reoosn gm_l16; /* r */
    GM_STACK_CHECK(&F, 1165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    memcpy(&gm_l6, a6, sizeof gm_l6);
    gm_l7 = a7;
    gm_l8 = a8;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l2;
        void * t4 = gm_f_fn_info_hocist4((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t4;
    }
    {
        void * t5 = gm_l9;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7;
        if ((GM_LD(void *, ((uint8_t *)&t6)) != 0)) {
            void * t8 = GM_LD(void *, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            gm_tl874t2atnlh3 t10 = gm_l1;
            uint32_t t11 = gm_l2;
            gm_f_call_missing_5ejgl53((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l10 = t7;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t14 = gm_l9;
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)t14) + 8) + 8);
        uint8_t t16 = (!(t13 == t15));
        if (!t16) goto ie12;
        {
            void * t18 = gm_l0;
            gm_tl874t2atnlh3 t19 = gm_l1;
            gm_tgbgti82mf879 t20;
            void * t21 = gm_l0;
            uint32_t t22 = gm_l2;
            void * t23 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t24);
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            GM_ST(uint64_t, (uint8_t *)&t20 + 16, t25);
            void * t26 = gm_l9;
            uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 8) + 8);
            GM_ST(uint64_t, (uint8_t *)&t20 + 24, t27);
            gm_tabb710d9mq2e t28;
            gm_f_format_5mel2nn((void *)&t28, (void *)NULL, cx, NULL, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_src6c128h826nsg, 104, 63);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_t76123gfet73g t31;
            gm_f_trap_qh84l9d((void *)&t31, (void *)NULL, cx, NULL, t18, t19, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t31);
            goto gm_fdone;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t33 = gm_l9;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 144));
        uint64_t t35 = GM_LD(uint64_t, gm_c1id0n0l1pf);
        uint8_t t36 = (t34 > t35);
        if (!t36) goto ie32;
        {
            void * t38 = gm_l0;
            gm_tl874t2atnlh3 t39 = gm_l1;
            gm_t3877pk7lv748 t40;
            void * t41 = gm_l0;
            uint32_t t42 = gm_l2;
            void * t43 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t43) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t40 + 0, t44);
            void * t45 = gm_l9;
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 144));
            GM_ST(uint64_t, (uint8_t *)&t40 + 16, t46);
            gm_tabb710d9mq2e t47;
            gm_f_format_jr8ev5r((void *)&t47, (void *)NULL, cx, NULL, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_src6c128h826nsg, 105, 58);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_t76123gfet73g t50;
            gm_f_trap_qh84l9d((void *)&t50, (void *)NULL, cx, NULL, t38, t39, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t50);
            goto gm_fdone;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t51 = gm_l0;
        gm_t2lqu8jsj511d t52 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t51));
        gm_l11 = t52;
    }
    {
        void * t53 = gm_l0;
        void * t54 = gm_l9;
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 64));
        gm_b16_8 t56 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t53, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t56;
    }
    {
        gm_l13 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t58 = gm_l13;
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t60 = (t58 < t59);
            if (!t60) goto lb57;
            {
                uint64_t t63 = gm_l13;
                uint64_t t64 = (uintptr_t)t63;
                uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_src6c128h826nsg, 110, 14);
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t64 * 16));
                gm_l14 = t66;
            }
            {
                void * t67 = gm_l9;
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)t67) + 72));
                gm_b16_8 t69 = t68;
                uint64_t t70 = gm_l13;
                uint64_t t71 = (uintptr_t)t70;
                void * t72 = gm_l9;
                uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)t72) + 8) + 8);
                if (t71 >= (uintptr_t)(t73)) gm_trap_index(t71, t73, gm_src6c128h826nsg, 111, 35);
                uint32_t t74 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t72) + 8)) + t71 * 4));
                uint64_t t75 = (uint64_t)t74;
                uint64_t t76 = gm_f_us_cuv01fp(t75);
                uint64_t t77 = (uintptr_t)t76;
                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&t69) + 8);
                if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_src6c128h826nsg, 111, 19);
                uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t69)) + t77 * 8));
                uint64_t t80 = gm_f_us_cuv01fp(t79);
                gm_l15 = t80;
            }
            {
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                void * t82 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)));
                uint64_t t83 = gm_l15;
                uint64_t t84 = (uintptr_t)t83;
                uint64_t t85 = gm_l15;
                uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint64_t r87; bool o88 = __builtin_add_overflow(t85, t86, &r87);
                if (o88) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 112, 20);
                uint64_t t89 = r87;
                uint64_t t90 = (uintptr_t)t89;
                if (t84 > t90 || t90 > (uintptr_t)t81) gm_trap_slice(t84, t90, t81, gm_src6c128h826nsg, 112, 9);
                gm_b16_8 t91;
                GM_ST(void *, (uint8_t *)&t91, (void *)(((uint8_t *)t82 + t84 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(t90 - t84));
                gm_b16_8 t92 = gm_l14;
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                gm_f_copy_from_f7j6p05(t91, t93);
            }
            {
                uint64_t t94 = gm_l13;
                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                if (o96) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 113, 9);
                uint64_t t97 = r95;
                gm_l13 = t97;
            }
lc57:;
        }
lb57:;
    }
    {
        void * t98 = gm_l0;
        gm_tl874t2atnlh3 t99 = gm_l1;
        uint32_t t100 = gm_l2;
        void * t101 = gm_l9;
        void * t102 = gm_l10;
        gm_b16_8 t103 = gm_l12;
        void * t104 = gm_l4;
        uint32_t t105 = gm_l5;
        uint8_t t106 = gm_l7;
        gm_b16_8 t107 = gm_l8;
        gm_f_enter_maqkh2o((void *)&gm_l16, (void *)NULL, cx, NULL, t98, t99, t100, t101, t102, t103, t104, t105, &(*(gm_t5lf9i3n1r3co *)((uint8_t *)&gm_l6)), t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t108 = gm_l0;
        gm_t2lqu8jsj511d t109 = gm_l11;
        gm_f_release_jn5kqdi(t108, GM_CONV(gm_csc7lc6jlkqkuq, t109));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l16), 32);
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
inline __attribute__((aligned(4))) void gm_f_call_missing_5ejgl53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2) {
    struct gm_fr_call_missing_5ejgl53 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* f */
    void * gm_l3; /* def */
    GM_STACK_CHECK(&F, 342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l2;
        void * t4 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l3;
        uint8_t t7;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t10 != ((uint8_t)3ULL)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 71, 8);
me8:;
        if (!t7) goto ie5;
        {
            void * t13 = gm_l0;
            gm_tl874t2atnlh3 t14 = gm_l1;
            void * t15 = gm_l3;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 0));
            gm_tnpifv20irnph t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
            gm_tabb710d9mq2e t18;
            gm_f_format_rlkig7n((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_src6c128h826nsg, 71, 62);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_t76123gfet73g t21;
            gm_f_unsupported_70vvk1e((void *)&t21, (void *)NULL, cx, NULL, t13, t14, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t21);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t22 = gm_l0;
        gm_tl874t2atnlh3 t23 = gm_l1;
        void * t24 = gm_l3;
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 0));
        gm_tnpifv20irnph t26;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_tppjqmg((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src6c128h826nsg, 72, 26);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_t76123gfet73g t30;
        gm_f_trap_qh84l9d((void *)&t30, (void *)NULL, cx, NULL, t22, t23, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t30);
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
__attribute__((aligned(4))) void gm_f_call_node_a1b30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_call_node_a1b30k9 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint32_t gm_l2; /* f */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    void * gm_l6; /* info */
    void * gm_l7; /* body */
    gm_t2lqu8jsj511d gm_l8; /* mk */
    gm_b16_8 gm_l9; /* store */
    uint64_t gm_l10; /* i */
    void * gm_l11; /* x */
    uint64_t gm_l12; /* off */
    gm_t76123gfet73g gm_l13; /* fl */
    gm_tcfa6l7reoosn gm_l14; /* r */
    GM_STACK_CHECK(&F, 1291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l2;
        void * t4 = gm_f_fn_info_hocist4((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l6;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7;
        if ((GM_LD(void *, ((uint8_t *)&t6)) != 0)) {
            void * t8 = GM_LD(void *, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            void * t10 = gm_l4;
            gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 112));
            uint32_t t12 = gm_l2;
            gm_f_call_missing_5ejgl53((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t9, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l7 = t7;
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t15 = gm_l6;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 8) + 8);
        uint8_t t17 = (!(t14 == t16));
        if (!t17) goto ie13;
        {
            void * t19 = gm_l0;
            void * t20 = gm_l4;
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 112));
            gm_tgbgti82mf879 t22;
            void * t23 = gm_l0;
            uint32_t t24 = gm_l2;
            void * t25 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t22 + 0, t26);
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            GM_ST(uint64_t, (uint8_t *)&t22 + 16, t27);
            void * t28 = gm_l6;
            uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 8) + 8);
            GM_ST(uint64_t, (uint8_t *)&t22 + 24, t29);
            gm_tabb710d9mq2e t30;
            gm_f_format_5mel2nn((void *)&t30, (void *)NULL, cx, NULL, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src6c128h826nsg, 9, 65);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_t76123gfet73g t33;
            gm_f_trap_qh84l9d((void *)&t33, (void *)NULL, cx, NULL, t19, t21, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t33);
            goto gm_fdone;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t35 = gm_l6;
        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 144));
        uint64_t t37 = GM_LD(uint64_t, gm_c1id0n0l1pf);
        uint8_t t38 = (t36 > t37);
        if (!t38) goto ie34;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l4;
            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 112));
            gm_t3877pk7lv748 t43;
            void * t44 = gm_l0;
            uint32_t t45 = gm_l2;
            void * t46 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t47);
            void * t48 = gm_l6;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 144));
            GM_ST(uint64_t, (uint8_t *)&t43 + 16, t49);
            gm_tabb710d9mq2e t50;
            gm_f_format_jr8ev5r((void *)&t50, (void *)NULL, cx, NULL, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_src6c128h826nsg, 10, 60);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_t76123gfet73g t53;
            gm_f_trap_qh84l9d((void *)&t53, (void *)NULL, cx, NULL, t40, t42, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t53);
            goto gm_fdone;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t54 = gm_l0;
        gm_t2lqu8jsj511d t55 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t54));
        gm_l8 = t55;
    }
    {
        void * t56 = gm_l0;
        void * t57 = gm_l6;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 64));
        gm_b16_8 t59 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t56, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t59;
    }
    {
        gm_l10 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t61 = gm_l10;
            uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t63 = (t61 < t62);
            if (!t63) goto lb60;
            {
                uint64_t t66 = gm_l10;
                uint64_t t67 = (uintptr_t)t66;
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src6c128h826nsg, 16, 14);
                void * t69 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t67 * 8));
                gm_l11 = t69;
            }
            {
                void * t70 = gm_l6;
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t70) + 72));
                gm_b16_8 t72 = t71;
                uint64_t t73 = gm_l10;
                uint64_t t74 = (uintptr_t)t73;
                void * t75 = gm_l6;
                uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)t75) + 8) + 8);
                if (t74 >= (uintptr_t)(t76)) gm_trap_index(t74, t76, gm_src6c128h826nsg, 17, 35);
                uint32_t t77 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t75) + 8)) + t74 * 4));
                uint64_t t78 = (uint64_t)t77;
                uint64_t t79 = gm_f_us_cuv01fp(t78);
                uint64_t t80 = (uintptr_t)t79;
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&t72) + 8);
                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_src6c128h826nsg, 17, 19);
                uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t72)) + t80 * 8));
                uint64_t t83 = gm_f_us_cuv01fp(t82);
                gm_l12 = t83;
            }
            {
                uint64_t t84 = gm_l10;
                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                if (o86) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 18, 9);
                uint64_t t87 = r85;
                gm_l10 = t87;
            }
            {
                void * t88 = gm_l0;
                gm_tbl8il92l97ut t89 = gm_l1;
                void * t90 = gm_l11;
                gm_b16_8 t91 = gm_l9;
                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&t91) + 8);
                void * t93 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t91)));
                uint64_t t94 = gm_l12;
                uint64_t t95 = (uintptr_t)t94;
                uint64_t t96 = gm_l12;
                void * t97 = gm_l11;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 104));
                uint64_t t99 = gm_f_us_cuv01fp(t98);
                uint64_t r100; bool o101 = __builtin_add_overflow(t96, t99, &r100);
                if (o101) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 19, 31);
                uint64_t t102 = r100;
                uint64_t t103 = (uintptr_t)t102;
                if (t95 > t103 || t103 > (uintptr_t)t92) gm_trap_slice(t95, t103, t92, gm_src6c128h826nsg, 19, 20);
                gm_b16_8 t104;
                GM_ST(void *, (uint8_t *)&t104, (void *)(((uint8_t *)t93 + t95 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(t103 - t95));
                gm_tcfa6l7reoosn t105;
                gm_f_ev_25jfdar((void *)&t105, (void *)NULL, cx, NULL, t88, t89, t90, t104);
                if (gm_unw.kind) {
                    goto gm_ll64;
                }
                gm_b0_1 t106;
                if (!((((uint8_t *)&t105))[0] != 0)) {
                    gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 8));
                    t106 = t107;
                } else {
                    gm_l13 = (*(gm_t76123gfet73g *)(((uint8_t *)&t105) + 8));
                    {
                        void * t109 = gm_l0;
                        gm_t2lqu8jsj511d t110 = gm_l8;
                        gm_f_release_jn5kqdi(t109, GM_CONV(gm_csc7lc6jlkqkuq, t110));
                    }
                    const uint8_t *t111 = ((uint8_t *)&gm_l13);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t111, 24);
                    goto gm_fdone;
                }
            }
lc60:;
        }
gm_ll64:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
        }
        goto gm_lad;
lb60:;
    }
    {
        void * t112 = gm_l0;
        void * t113 = gm_l4;
        gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t113) + 112));
        uint32_t t115 = gm_l2;
        void * t116 = gm_l6;
        void * t117 = gm_l7;
        gm_b16_8 t118 = gm_l9;
        uint32_t t119 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        gm_t5lf9i3n1r3co t120;
        memset(&t120, 0, sizeof t120);
        gm_b16_8 t121 = gm_l5;
        gm_f_enter_maqkh2o((void *)&gm_l14, (void *)NULL, cx, NULL, t112, t114, t115, t116, t117, t118, ((void *)0), t119, &t120, ((uint8_t)0), t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t122 = gm_l0;
        gm_t2lqu8jsj511d t123 = gm_l8;
        gm_f_release_jn5kqdi(t122, GM_CONV(gm_csc7lc6jlkqkuq, t123));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l14), 32);
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
__attribute__((aligned(4))) void gm_f_call_value_fm9m0cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_call_value_fm9m0cc F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* cv */
    gm_t88rcucg1b7uk gm_l4; /* cty */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* dst */
    uint32_t gm_l8; /* f */
    void * gm_l9; /* env */
    uint32_t gm_l10; /* ctx */
    uint32_t gm_l11; /* base */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_tiilmfd16v3fc gm_l14; /* eff */
    uint32_t gm_l15; /* k */
    gm_tcfa6l7reoosn gm_l17; /* r */
    uint64_t gm_l18; /* addr */
    uint32_t gm_l19; /* f */
    GM_STACK_CHECK(&F, 1240u);
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
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l4;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)15ULL)) goto ie2;
        F.l7 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t5) + 8));
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            gm_b16_8 t10 = gm_l3;
            uint64_t t11 = gm_f_word_o2rcrhs(t9, t10, ((uint64_t)0ULL));
            gm_t9805qtonbsag t12 = gm_f_fn_from_addr_7a6bpqd(t8, t11);
            uint32_t t13;
            if (((((uint8_t *)&t12))[4] != 0)) {
                uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&t12) + 0));
                t13 = t14;
            } else {
                void * t15 = gm_l0;
                gm_tl874t2atnlh3 t16 = gm_l2;
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s3p4vlg0qjpu0));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(45));
                gm_t76123gfet73g t18;
                gm_f_trap_qh84l9d((void *)&t18, (void *)NULL, cx, NULL, t15, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t18);
                goto gm_fdone;
            }
            gm_l8 = t13;
        }
        {
            void * t19 = gm_l0;
            void * t20 = gm_l0;
            gm_b16_8 t21 = gm_l3;
            uint64_t t22 = gm_f_word_o2rcrhs(t20, t21, ((uint64_t)1ULL));
            void * t23 = gm_f_frame_of_dgl92on(t19, t22);
            gm_l9 = t23;
        }
        {
            void * t24 = gm_l0;
            gm_b16_8 t25 = gm_l3;
            uint64_t t26 = gm_f_word_o2rcrhs(t24, t25, ((uint64_t)2ULL));
            uint32_t t27 = (uint32_t)t26;
            gm_l10 = t27;
        }
        {
            uint32_t t28 = gm_l10;
            gm_l11 = t28;
        }
        {
            {
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 24) + 0));
                gm_l12 = t30;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t32 = gm_l13;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t34 = (t32 < t33);
                    if (!t34) goto lb31;
                    {
                        uint64_t t37 = gm_l13;
                        uint64_t t38 = (uintptr_t)t37;
                        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_tiilmfd16v3fc t42 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t38 * 24));
                        gm_l14 = t42;
                        {
                            void * t44 = gm_l0;
                            uint32_t t45 = gm_l1;
                            void * t46 = (void *)((uint8_t *)&gm_l14);
                            uint32_t t47 = gm_f_find_ctx_1elkgc2(t44, t45, t46);
                            gm_l15 = t47;
                        }
                        {
                            uint32_t t49 = gm_l15;
                            uint8_t t50 = ((t49 == ((uint32_t)0ULL)));
                            if (!t50) goto ie48;
                            {
                                goto lc31;
                            }
                            goto ix48;
ie48:;
ix48:;
                        }
                        {
                            void * t52 = gm_l0;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((((uint8_t *)t52) + 632) + 0));
                            gm_b16_8 t54 = t53;
                            uint32_t t55 = gm_l15;
                            uint64_t t56 = (uint64_t)t55;
                            uint64_t t57 = gm_f_us_cuv01fp(t56);
                            uint64_t t58 = (uintptr_t)t57;
                            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&t54) + 8);
                            if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_src6c128h826nsg, 87, 21);
                            gm_tjsm4kosd5pev t60 = GM_LD(gm_tjsm4kosd5pev, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t54)) + t58 * 112));
                            F.l16 = t60;
                        }
                        {
                            void * t61 = gm_l0;
                            uint32_t t62 = gm_l10;
                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 80));
                            uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)&F.l16) + 88));
                            void * t65 = GM_LD(void *, (((uint8_t *)&F.l16) + 96));
                            uint32_t t66 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t61, t62, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)&F.l16) + 8)), &(*(gm_tte9vgf6noic9 *)(((uint8_t *)&F.l16) + 32)), t63, t64, t65);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            gm_l10 = t66;
                        }
                        goto me40;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 84, 9);
me40:;
                    }
lc31:;
                    {
                        uint64_t t68 = gm_l13;
                        uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                        uint64_t t71 = r69;
                        gm_l13 = t71;
                    }
                }
gm_ll35:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
                }
                goto gm_lad;
lb31:;
            }
        }
        {
            void * t72 = gm_l0;
            gm_tl874t2atnlh3 t73 = gm_l2;
            uint32_t t74 = gm_l8;
            gm_b16_8 t75 = gm_l5;
            void * t76 = gm_l9;
            uint32_t t77 = gm_l10;
            gm_t5lf9i3n1r3co t78;
            memset(&t78, 0, sizeof t78);
            gm_b16_8 t79 = gm_l6;
            gm_f_call_function_u7ovth6((void *)&gm_l17, (void *)NULL, cx, NULL, t72, t73, t74, t75, t76, t77, &t78, ((uint8_t)0), t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t80 = gm_l0;
            uint32_t t81 = gm_l10;
            uint32_t t82 = gm_l11;
            gm_b0_1 t83 = gm_f_pop_ctx_jivp36j((void *)NULL, cx, NULL, t80, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memmove(gm_out, ((uint8_t *)&gm_l17), 32);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t84 = gm_l0;
        gm_b16_8 t85 = gm_l3;
        uint64_t t86 = gm_f_ptr_of_b77b4c9(t84, t85);
        gm_l18 = t86;
    }
    {
        uint64_t t88 = gm_l18;
        uint8_t t89 = ((t88 == ((uint64_t)0ULL)));
        if (!t89) goto ie87;
        {
            void * t91 = gm_l0;
            gm_tl874t2atnlh3 t92 = gm_l2;
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sq5khm5l62ens));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(36));
            gm_t76123gfet73g t94;
            gm_f_trap_qh84l9d((void *)&t94, (void *)NULL, cx, NULL, t91, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t94);
            goto gm_fdone;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        void * t95 = gm_l0;
        uint64_t t96 = gm_l18;
        gm_t9805qtonbsag t97 = gm_f_fn_from_addr_7a6bpqd(t95, t96);
        uint32_t t98;
        if (((((uint8_t *)&t97))[4] != 0)) {
            uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)&t97) + 0));
            t98 = t99;
        } else {
            void * t100 = gm_l0;
            gm_tl874t2atnlh3 t101 = gm_l2;
            uint64_t t102 = gm_l18;
            gm_t4ce0jd3tdro4 t103;
            GM_ST(uint64_t, ((uint8_t *)&t103 + 0), t102);
            gm_tabb710d9mq2e t104;
            gm_f_format_k34396d((void *)&t104, (void *)NULL, cx, NULL, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_src6c128h826nsg, 96, 57);
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
            gm_t76123gfet73g t107;
            gm_f_trap_qh84l9d((void *)&t107, (void *)NULL, cx, NULL, t100, t101, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t107);
            goto gm_fdone;
        }
        gm_l19 = t98;
    }
    {
        void * t108 = gm_l0;
        gm_tl874t2atnlh3 t109 = gm_l2;
        uint32_t t110 = gm_l19;
        gm_b16_8 t111 = gm_l5;
        uint32_t t112 = gm_l1;
        gm_t5lf9i3n1r3co t113;
        memset(&t113, 0, sizeof t113);
        gm_b16_8 t114 = gm_l6;
        gm_f_call_function_u7ovth6((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t108, t109, t110, t111, ((void *)0), t112, &t113, ((uint8_t)0), t114);
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
__attribute__((aligned(4))) void gm_f_emit_8atsnfj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, const void * a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6) {
    struct gm_fr_emit_8atsnfj F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l4; /* sv */
    gm_t88rcucg1b7uk gm_l5; /* sty */
    gm_b16_8 gm_l6; /* data */
    uint64_t gm_l7; /* addr */
    gm_t2lqu8jsj511d gm_l8; /* mk */
    gm_b16_8 gm_l9; /* sl */
    gm_tpmdr2hmdhsqr gm_l10; /* args */
    gm_tiilmfd16v3fc gm_l11; /* effect_ */
    uint32_t gm_l12; /* op */
    gm_tcfa6l7reoosn gm_l13; /* r */
    GM_STACK_CHECK(&F, 892u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        uint64_t t5 = (uint64_t)t4;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)1ULL));
        gm_t833u93mr4iob t7;
        gm_f_alloc_mem_0vu6kcs((void *)&t7, (void *)NULL, cx, NULL, t2, t3, t5, ((uint64_t)1ULL), t6, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_tcfa6l7reoosn t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t8 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t7) + 8)));
            F.ret = t8;
            goto gm_fret;
        }
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t7) + 8));
        gm_l7 = t9;
    }
    {
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 184);
            uint64_t t13 = gm_l7;
            gm_b16_8 t14 = gm_l6;
            gm_tgfdnufetdnfc t15;
            gm_f_init_9fuf99v((void *)&t15, t12, t13, t14);
        }
    }
    {
        void * t16 = gm_l0;
        gm_t2lqu8jsj511d t17 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t16));
        gm_l8 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 176));
        uint64_t r21; bool o22 = __builtin_mul_overflow(((uint64_t)2ULL), t20, &r21);
        if (o22) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 352, 17);
        uint64_t t23 = r21;
        gm_b16_8 t24 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t18, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t24;
    }
    {
        void * t25 = gm_l0;
        gm_b16_8 t26 = gm_l9;
        uint64_t t27 = gm_l7;
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        uint64_t t29 = (uint64_t)t28;
        gm_f_put_slice_ko14loi(t25, t26, t27, t29);
    }
    {
        gm_b16_8 t30 = gm_l9;
        gm_tpmdr2hmdhsqr t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
        gm_l10 = t31;
    }
    {
        gm_tcfa6l7reoosn t32;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t35 != ((uint8_t)1ULL)) goto mn34;
        void * t36 = gm_l0;
        uint32_t t37 = gm_l1;
        gm_tl874t2atnlh3 t38 = gm_l2;
        gm_b16_8 t39 = gm_l4;
        gm_t88rcucg1b7uk t40 = gm_l5;
        void * t41 = ((uint8_t *)&gm_l10);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src6c128h826nsg, 356, 51);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t41 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_b16_8 t44 = gm_f_empty_slice_ijtp9lo();
        gm_tcfa6l7reoosn t45;
        gm_f_call_value_fm9m0cc((void *)&t45, (void *)NULL, cx, NULL, t36, t37, t38, t39, t40, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t32 = t45;
        goto me33;
mn34:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t47 != ((uint8_t)0ULL)) goto mn46;
        gm_tiilmfd16v3fc t48 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)a3) + 8) + 0));
        gm_l11 = t48;
        uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)a3) + 8) + 24));
        gm_l12 = t49;
        void * t50 = gm_l0;
        uint32_t t51 = gm_l1;
        gm_tl874t2atnlh3 t52 = gm_l2;
        void * t53 = (void *)((uint8_t *)&gm_l11);
        uint32_t t54 = gm_l12;
        void * t55 = ((uint8_t *)&gm_l10);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src6c128h826nsg, 357, 68);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t55 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_b16_8 t58 = gm_f_empty_slice_ijtp9lo();
        gm_tcfa6l7reoosn t59;
        gm_f_perform_4kn6797((void *)&t59, (void *)NULL, cx, NULL, t50, t51, t52, t53, t54, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t32 = t59;
        goto me33;
mn46:;
        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 355, 10);
me33:;
        gm_l13 = t32;
    }
    {
        void * t60 = gm_l0;
        gm_t2lqu8jsj511d t61 = gm_l8;
        gm_f_release_jn5kqdi(t60, GM_CONV(gm_csc7lc6jlkqkuq, t61));
    }
    {
        {
            void * t63 = gm_l0;
            void * t64 = (void *)(((uint8_t *)t63) + 184);
            uint64_t t65 = gm_l7;
            uint8_t t66 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l13), 32);
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
__attribute__((aligned(4))) void gm_f_enter_maqkh2o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, void * a3, void * a4, gm_b16_8 a5, void * a6, uint32_t a7, const void * a8, uint8_t a9, gm_b16_8 a10) {
    struct gm_fr_enter_maqkh2o F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* f */
    void * gm_l3; /* info */
    void * gm_l4; /* body */
    gm_b16_8 gm_l5; /* store */
    void * gm_l6; /* env */
    uint32_t gm_l7; /* cx */
    gm_t5lf9i3n1r3co gm_l8; /* installing */
    uint8_t gm_l9; /* is_clause_lit */
    gm_b16_8 gm_l10; /* dst */
    uint8_t gm_l11; /* marker */
    uint8_t gm_l12; /* on_fiber */
    uint64_t gm_l13; /* tm */
    void * gm_l14; /* fr */
    void * gm_l15; /* c */
    uint8_t gm_l16; /* frameless */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint32_t gm_l19; /* p */
    gm_t76123gfet73g gm_l20; /* fl */
    void * gm_l21; /* c */
    gm_tcfa6l7reoosn gm_l22; /* r */
    uint64_t gm_l23; /* id */
    gm_t76123gfet73g gm_l24; /* fl */
    uint64_t gm_l25; /* frame */
    void * gm_l26; /* t */
    GM_STACK_CHECK(&F, 1700u);
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
    memcpy(&gm_l8, a8, sizeof gm_l8);
    gm_l9 = a9;
    gm_l10 = a10;
    {
        {
            gm_l11 = ((uint8_t)0ULL);
        }
        {
            uint8_t t4 = 0;
            void * t5 = gm_l0;
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 768));
            uint32_t t7 = GM_LD(uint32_t, gm_cdps8qsftpu);
            uint8_t t8 = (t6 >= t7);
            if (!t8) {
                void * t9 = (void *)((uint8_t *)&gm_l11);
                uint64_t t10 = (uintptr_t)t9;
                void * t11 = gm_l0;
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 776));
                uint8_t t13 = (t10 < t12);
                t4 = t13;
            } else t4 = 1;
            if (!t4) goto ie3;
            {
                void * t15 = gm_l0;
                gm_tl874t2atnlh3 t16 = gm_l1;
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s38rtd27u976k));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(14));
                gm_t76123gfet73g t18;
                gm_f_trap_qh84l9d((void *)&t18, (void *)NULL, cx, NULL, t15, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t18);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t20 = gm_l0;
            uint64_t t21 = GM_LD(uintptr_t, (((((uint8_t *)t20) + 1656) + 0) + 0) + 8);
            uint8_t t22 = (t21 > ((uint64_t)1ULL));
            if (!t22) goto ie19;
            {
                void * t24 = gm_l0;
                gm_tl874t2atnlh3 t25 = gm_l1;
                gm_tcfa6l7reoosn t26;
                gm_f_tick_6bsfu3n((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t26))[0] != 0)) {
                    gm_tcfa6l7reoosn t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                    ((uint8_t *)&t27)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t26) + 8)));
                    F.ret = t27;
                    goto gm_fret;
                }
                gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            void * t29 = gm_l0;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 1288));
            uint64_t t31 = (uint64_t)~((uint64_t)0ULL);
            uint8_t t32 = (!(t30 == t31));
            gm_l12 = t32;
        }
        {
            uint8_t t34 = gm_l12;
            if (!t34) goto ie33;
            void * t35 = gm_l3;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 144));
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 1288));
            uint8_t t39 = (t36 > t38);
            if (!t39) goto ie33;
            void * t40 = gm_l0;
            uint8_t t41 = gm_f_fast_23em7sv(t40);
            uint8_t t42 = (uint8_t)!t41;
            if (!t42) goto ie33;
            {
                void * t44 = gm_l0;
                gm_tl874t2atnlh3 t45 = gm_l1;
                gm_b16_8 t46;
                GM_ST(void *, (uint8_t *)&t46, (void *)(gm_slg8371oger0u));
                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(31));
                gm_t76123gfet73g t47;
                gm_f_trap_qh84l9d((void *)&t47, (void *)NULL, cx, NULL, t44, t45, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t47);
                goto gm_fdone;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            void * t48 = gm_l0;
            uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 416) + 0) + 8);
            gm_l13 = t49;
        }
        {
            void * t50 = gm_l0;
            uint32_t t51 = gm_l2;
            void * t52 = gm_l3;
            gm_b16_8 t53 = gm_l10;
            void * t54 = gm_l6;
            gm_b16_8 t55 = gm_l5;
            void * t56 = gm_f_new_frame_vgm56mt((void *)NULL, cx, NULL, t50, t51, t52, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t56;
        }
        {
            void * t57 = gm_l14;
            gm_t5lf9i3n1r3co t58 = gm_l8;
            GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t57) + 216), t58);
        }
        {
            void * t59 = gm_l14;
            uint8_t t60 = gm_l9;
            GM_ST(uint8_t, (((uint8_t *)t59) + 240), t60);
        }
        {
            uint8_t t61;
            void * t63 = gm_l3;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 56));
            if (!(GM_LD(void *, ((uint8_t *)&t64)) != 0)) goto ie62;
            void * t65 = GM_LD(void *, ((uint8_t *)&t64));
            gm_l15 = t65;
            void * t67 = gm_l15;
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t67) + 32));
            uint8_t t69 = t68;
            t61 = t69;
            goto ix62;
ie62:;
            uint8_t t71 = ((uint8_t)0);
            t61 = t71;
ix62:;
            gm_l16 = t61;
        }
        {
            void * t73 = gm_l3;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t73) + 24));
            if (!t74) goto ie72;
            uint8_t t75 = gm_l16;
            uint8_t t76 = (uint8_t)!t75;
            if (!t76) goto ie72;
            {
                {
                    void * t79 = gm_l3;
                    gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)t79) + 8));
                    gm_l17 = t80;
                }
                {
                    gm_l18 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t82 = gm_l18;
                        uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        uint8_t t84 = (t82 < t83);
                        if (!t84) goto lb81;
                        {
                            uint64_t t87 = gm_l18;
                            uint64_t t88 = (uintptr_t)t87;
                            uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                            uint32_t t90 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t88 * 4));
                            uint32_t t93 = GM_LD(uint32_t, ((uint8_t *)&t90));
                            gm_l19 = t93;
                            {
                                void * t96 = gm_l3;
                                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)t96) + 120));
                                gm_b16_8 t98 = t97;
                                uint32_t t99 = gm_l19;
                                uint64_t t100 = (uint64_t)t99;
                                uint64_t t101 = gm_f_us_cuv01fp(t100);
                                uint64_t t102 = (uintptr_t)t101;
                                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&t98) + 8);
                                if (t102 >= (uintptr_t)(t103)) gm_trap_index(t102, t103, gm_src6c128h826nsg, 136, 16);
                                uint8_t t104 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t98)) + t102 * 1));
                                if (!t104) goto ie95;
                                {
                                    {
                                        void * t107 = gm_l0;
                                        gm_tl874t2atnlh3 t108 = gm_l1;
                                        void * t109 = gm_l14;
                                        uint32_t t110 = gm_l19;
                                        gm_t833u93mr4iob t111;
                                        gm_f_local_addr_o51qq3k((void *)&t111, (void *)NULL, cx, NULL, t107, t108, t109, t110);
                                        if (gm_unw.kind) {
                                            goto gm_ll85;
                                        }
                                        uint64_t t112;
                                        if (!((((uint8_t *)&t111))[0] != 0)) {
                                            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&t111) + 8));
                                            t112 = t113;
                                        } else {
                                            gm_l20 = (*(gm_t76123gfet73g *)(((uint8_t *)&t111) + 8));
                                            {
                                                void * t115 = gm_l0;
                                                void * t116 = gm_l14;
                                                gm_b0_1 t117 = gm_f_end_frame_cetr4jh((void *)NULL, cx, NULL, t115, t116);
                                                if (gm_unw.kind) {
                                                    goto gm_ll85;
                                                }
                                            }
                                            const uint8_t *t118 = ((uint8_t *)&gm_l20);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                            memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t118, 24);
                                            goto gm_fdone;
                                        }
                                    }
                                }
                                goto ix95;
ie95:;
ix95:;
                            }
                            goto me91;
mn92:;
                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 135, 9);
me91:;
                        }
lc81:;
                        {
                            uint64_t t120 = gm_l18;
                            uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                            uint64_t t123 = r121;
                            gm_l18 = t123;
                        }
                    }
gm_ll85:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
                    }
                    goto gm_lad;
lb81:;
                }
            }
            goto ix72;
ie72:;
ix72:;
        }
        {
            uint8_t t125 = gm_l12;
            if (!t125) goto ie124;
            {
                void * t127 = gm_l0;
                void * t128 = gm_l0;
                uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 1288));
                void * t130 = gm_l3;
                uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)t130) + 144));
                uint64_t r132; bool o133 = __builtin_sub_overflow(t129, t131, &r132);
                if (o133) gm_trap("integer overflow in `-`", gm_src6c128h826nsg, 144, 19);
                uint64_t t134 = r132;
                GM_ST(uint64_t, (((uint8_t *)t127) + 1288), t134);
            }
            goto ix124;
ie124:;
ix124:;
        }
        {
            void * t135 = gm_l0;
            void * t136 = gm_l0;
            uint32_t t137 = GM_LD(uint32_t, (((uint8_t *)t136) + 768));
            uint32_t r138; bool o139 = __builtin_add_overflow(t137, ((uint32_t)1ULL), &r138);
            if (o139) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 145, 5);
            uint32_t t140 = r138;
            GM_ST(uint32_t, (((uint8_t *)t135) + 768), t140);
        }
        {
            gm_tcfa6l7reoosn t141;
            void * t143 = gm_l3;
            void * t144 = GM_LD(void *, (((uint8_t *)t143) + 56));
            if (!(GM_LD(void *, ((uint8_t *)&t144)) != 0)) goto ie142;
            void * t145 = GM_LD(void *, ((uint8_t *)&t144));
            gm_l21 = t145;
            void * t147 = gm_l0;
            void * t148 = gm_l14;
            uint32_t t149 = gm_l7;
            gm_tbl8il92l97ut t150;
            GM_ST(uint32_t, (uint8_t *)&t150 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t150 + 0), t148);
            GM_ST(uint32_t, ((uint8_t *)&t150 + 8), t149);
            void * t151 = gm_l21;
            gm_b16_8 t152 = gm_l10;
            gm_tcfa6l7reoosn t153;
            gm_f_vm_run_j1mlbuk((void *)&t153, (void *)NULL, cx, NULL, t147, t150, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t154 = t153;
            t141 = t154;
            goto ix142;
ie142:;
            void * t156 = gm_l0;
            void * t157 = gm_l14;
            uint32_t t158 = gm_l7;
            gm_tbl8il92l97ut t159;
            GM_ST(uint32_t, (uint8_t *)&t159 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t159 + 0), t157);
            GM_ST(uint32_t, ((uint8_t *)&t159 + 8), t158);
            void * t160 = gm_l4;
            gm_b16_8 t161 = gm_l10;
            gm_tcfa6l7reoosn t162;
            gm_f_ev_25jfdar((void *)&t162, (void *)NULL, cx, NULL, t156, t159, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t163 = t162;
            t141 = t163;
ix142:;
            gm_l22 = t141;
        }
        {
            void * t164 = gm_l0;
            void * t165 = gm_l0;
            uint32_t t166 = GM_LD(uint32_t, (((uint8_t *)t165) + 768));
            uint32_t r167; bool o168 = __builtin_sub_overflow(t166, ((uint32_t)1ULL), &r167);
            if (o168) gm_trap("integer overflow in `-`", gm_src6c128h826nsg, 147, 5);
            uint32_t t169 = r167;
            GM_ST(uint32_t, (((uint8_t *)t164) + 768), t169);
        }
        {
            uint8_t t171 = gm_l12;
            if (!t171) goto ie170;
            {
                void * t173 = gm_l0;
                void * t174 = gm_l0;
                uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)t174) + 1288));
                void * t176 = gm_l3;
                uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)t176) + 144));
                uint64_t r178; bool o179 = __builtin_add_overflow(t175, t177, &r178);
                if (o179) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 148, 19);
                uint64_t t180 = r178;
                GM_ST(uint64_t, (((uint8_t *)t173) + 1288), t180);
            }
            goto ix170;
ie170:;
ix170:;
        }
        {
            void * t182 = gm_l0;
            uint64_t t183 = GM_LD(uintptr_t, ((((uint8_t *)t182) + 416) + 0) + 8);
            uint64_t t184 = gm_l13;
            uint8_t t185 = (t183 > t184);
            if (!t185) goto ie181;
            {
                void * t187 = gm_l0;
                uint64_t t188 = gm_l13;
                gm_b0_1 t189 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t187, t188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix181;
ie181:;
ix181:;
        }
        {
            void * t190 = gm_l14;
            uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)t190) + 0));
            gm_l23 = t191;
        }
        {
            void * t192 = gm_l0;
            void * t193 = gm_l14;
            gm_b0_1 t194 = gm_f_end_frame_cetr4jh((void *)NULL, cx, NULL, t192, t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tcfa6l7reoosn t195 = gm_l22;
            gm_b0_1 t196;
            if (!((((uint8_t *)&t195))[0] != 0)) {
                gm_b0_1 t197 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 8));
                t196 = t197;
            } else {
                gm_l24 = (*(gm_t76123gfet73g *)(((uint8_t *)&t195) + 8));
                {
                    uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    if (t200 != ((uint8_t)2ULL)) goto ie199;
                    uint64_t t201 = GM_LD(uint64_t, ((((uint8_t *)&gm_l24) + 8) + 0));
                    gm_l25 = t201;
                    uint64_t t202 = gm_l25;
                    uint64_t t203 = gm_l23;
                    uint8_t t204 = ((t202 == t203));
                    if (!t204) goto ie199;
                    {
                        gm_b0_1 t206;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t206);
                        goto gm_fdone;
                    }
                    goto ix199;
ie199:;
ix199:;
                }
                {
                    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    if (t208 != ((uint8_t)4ULL)) goto ie207;
                    void * t209 = GM_LD(void *, (((uint8_t *)&gm_l24) + 8));
                    gm_l26 = t209;
                    void * t210 = gm_l0;
                    uint8_t t211 = ((GM_LD(uint8_t, (((uint8_t *)t210) + 88) + 0) == ((uint8_t)0ULL)));
                    if (!t211) goto ie207;
                    {
                        void * t213 = gm_l26;
                        void * t214 = (void *)(((uint8_t *)t213) + 32);
                        uint32_t t215 = gm_l2;
                        gm_tl874t2atnlh3 t216 = gm_l1;
                        gm_t9d4sf0vqn7dt t217;
                        GM_ST(uint32_t, ((uint8_t *)&t217 + 0), t215);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t217 + 4), t216);
                        gm_tftb1odc739t5 t218 = gm_f_push_t8up8j4_2(t214, GM_CONV(gm_csedogq9e4rp5r, t217));
                        if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_src6c128h826nsg, 154, 50);
                        gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
                    }
                    goto ix207;
ie207:;
ix207:;
                }
                const uint8_t *t220 = ((uint8_t *)&gm_l24);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t220, 24);
                goto gm_fdone;
            }
        }
    }
    gm_b0_1 t221;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t221);
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
__attribute__((aligned(4))) void gm_f_ev_call_value_f4tfqs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_ev_call_value_f4tfqs3 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_b16_8 gm_l7; /* cv */
    uint64_t gm_l8; /* base */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l11; /* x */
    gm_b16_8 gm_l12; /* b */
    gm_t76123gfet73g gm_l13; /* f */
    gm_tcfa6l7reoosn gm_l14; /* r */
    GM_STACK_CHECK(&F, 983u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l6 = t4;
        }
        {
            void * t5 = gm_l0;
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            gm_b16_8 t8 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t8;
        }
        {
            void * t9 = gm_l0;
            gm_tbl8il92l97ut t10 = gm_l1;
            void * t11 = gm_l2;
            gm_b16_8 t12 = gm_l7;
            gm_tcfa6l7reoosn t13;
            gm_f_ev_25jfdar((void *)&t13, (void *)NULL, cx, NULL, t9, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_tcfa6l7reoosn t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                ((uint8_t *)&t14)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t13) + 8)));
                F.ret = t14;
                goto gm_fret;
            }
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
        }
        {
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 552) + 0) + 8);
            gm_l8 = t17;
        }
        {
            {
                gm_b16_8 t19 = gm_l3;
                gm_l9 = t19;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t21 = gm_l10;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t23 = (t21 < t22);
                    if (!t23) goto lb20;
                    {
                        uint64_t t26 = gm_l10;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        void * t29 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t27 * 8));
                        void * t32 = GM_LD(void *, ((uint8_t *)&t29));
                        gm_l11 = t32;
                        {
                            void * t34 = gm_l0;
                            void * t35 = gm_l11;
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 104));
                            gm_b16_8 t37 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t34, t36);
                            if (gm_unw.kind) {
                                goto gm_ll24;
                            }
                            gm_l12 = t37;
                        }
                        {
                            void * t38 = gm_l0;
                            gm_tbl8il92l97ut t39 = gm_l1;
                            void * t40 = gm_l11;
                            gm_b16_8 t41 = gm_l12;
                            gm_tcfa6l7reoosn t42;
                            gm_f_ev_25jfdar((void *)&t42, (void *)NULL, cx, NULL, t38, t39, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_ll24;
                            }
                            gm_b0_1 t43;
                            if (!((((uint8_t *)&t42))[0] != 0)) {
                                gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 8));
                                t43 = t44;
                            } else {
                                gm_l13 = (*(gm_t76123gfet73g *)(((uint8_t *)&t42) + 8));
                                {
                                    void * t46 = gm_l0;
                                    void * t47 = (void *)(((uint8_t *)t46) + 552);
                                    uint64_t t48 = gm_l8;
                                    gm_f_truncate_j7f0pmi(t47, t48);
                                }
                                const uint8_t *t49 = ((uint8_t *)&gm_l13);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t49, 24);
                                goto gm_fdone;
                            }
                        }
                        {
                            void * t50 = gm_l0;
                            void * t51 = (void *)(((uint8_t *)t50) + 552);
                            gm_b16_8 t52 = gm_l12;
                            gm_tftb1odc739t5 t53 = gm_f_push_rluch9r(t51, t52);
                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_src6c128h826nsg, 60, 9);
                            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
                        }
                        goto me30;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 54, 5);
me30:;
                    }
lc20:;
                    {
                        uint64_t t56 = gm_l10;
                        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                        uint64_t t59 = r57;
                        gm_l10 = t59;
                    }
                }
gm_ll24:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
                }
                goto gm_lad;
lb20:;
            }
        }
        {
            void * t60 = gm_l0;
            uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
            void * t62 = gm_l4;
            gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 112));
            gm_b16_8 t64 = gm_l7;
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            void * t66 = gm_l2;
            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + 96));
            void * t68 = gm_l0;
            uint64_t t69 = GM_LD(uintptr_t, ((((uint8_t *)t68) + 552) + 0) + 8);
            void * t70 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t68) + 552) + 0)));
            uint64_t t71 = gm_l8;
            uint64_t t72 = (uintptr_t)t71;
            void * t73 = gm_l0;
            uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 552) + 0) + 8);
            uint64_t t75 = (uintptr_t)t74;
            if (t72 > t75 || t75 > (uintptr_t)t69) gm_trap_slice(t72, t75, t69, gm_src6c128h826nsg, 62, 52);
            gm_b16_8 t76;
            GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t70 + t72 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(t75 - t72));
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            gm_b16_8 t78 = gm_l5;
            gm_f_call_value_fm9m0cc((void *)&gm_l14, (void *)NULL, cx, NULL, t60, t61, t63, t65, t67, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t79 = gm_l0;
            void * t80 = (void *)(((uint8_t *)t79) + 552);
            uint64_t t81 = gm_l8;
            gm_f_truncate_j7f0pmi(t80, t81);
        }
        {
            gm_tcfa6l7reoosn t82 = gm_l14;
            if (((((uint8_t *)&t82))[0] != 0)) {
                gm_tcfa6l7reoosn t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                ((uint8_t *)&t83)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t83 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t82) + 8)));
                F.ret = t83;
                goto gm_fret;
            }
            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 8));
        }
        {
            void * t85 = gm_l0;
            gm_t2lqu8jsj511d t86 = gm_l6;
            gm_f_release_jn5kqdi(t85, GM_CONV(gm_csc7lc6jlkqkuq, t86));
        }
    }
    gm_b0_1 t87;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t87);
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
__attribute__((aligned(4))) void gm_f_ev_perform_ovt4j0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, uint32_t a3, gm_b16_8 a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_ev_perform_ovt4j0p F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tiilmfd16v3fc gm_l2; /* effect_ */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* args */
    void * gm_l5; /* n */
    gm_b16_8 gm_l6; /* dst */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    uint64_t gm_l8; /* base */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l11; /* x */
    gm_b16_8 gm_l12; /* b */
    gm_t76123gfet73g gm_l13; /* f */
    gm_tcfa6l7reoosn gm_l14; /* r */
    GM_STACK_CHECK(&F, 803u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l7 = t4;
        }
        {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 552) + 0) + 8);
            gm_l8 = t6;
        }
        {
            {
                gm_b16_8 t8 = gm_l4;
                gm_l9 = t8;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l10;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    {
                        uint64_t t15 = gm_l10;
                        uint64_t t16 = (uintptr_t)t15;
                        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        void * t18 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t16 * 8));
                        void * t21 = GM_LD(void *, ((uint8_t *)&t18));
                        gm_l11 = t21;
                        {
                            void * t23 = gm_l0;
                            void * t24 = gm_l11;
                            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 104));
                            gm_b16_8 t26 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t23, t25);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_l12 = t26;
                        }
                        {
                            void * t27 = gm_l0;
                            gm_tbl8il92l97ut t28 = gm_l1;
                            void * t29 = gm_l11;
                            gm_b16_8 t30 = gm_l12;
                            gm_tcfa6l7reoosn t31;
                            gm_f_ev_25jfdar((void *)&t31, (void *)NULL, cx, NULL, t27, t28, t29, t30);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_b0_1 t32;
                            if (!((((uint8_t *)&t31))[0] != 0)) {
                                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
                                t32 = t33;
                            } else {
                                gm_l13 = (*(gm_t76123gfet73g *)(((uint8_t *)&t31) + 8));
                                {
                                    void * t35 = gm_l0;
                                    void * t36 = (void *)(((uint8_t *)t35) + 552);
                                    uint64_t t37 = gm_l8;
                                    gm_f_truncate_j7f0pmi(t36, t37);
                                }
                                const uint8_t *t38 = ((uint8_t *)&gm_l13);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t38, 24);
                                goto gm_fdone;
                            }
                        }
                        {
                            void * t39 = gm_l0;
                            void * t40 = (void *)(((uint8_t *)t39) + 552);
                            gm_b16_8 t41 = gm_l12;
                            gm_tftb1odc739t5 t42 = gm_f_push_rluch9r(t40, t41);
                            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src6c128h826nsg, 40, 9);
                            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 34, 5);
me19:;
                    }
lc9:;
                    {
                        uint64_t t45 = gm_l10;
                        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                        uint64_t t48 = r46;
                        gm_l10 = t48;
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
        {
            void * t49 = gm_l0;
            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
            void * t51 = gm_l5;
            gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 112));
            void * t53 = (void *)((uint8_t *)&gm_l2);
            uint32_t t54 = gm_l3;
            void * t55 = gm_l0;
            uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 552) + 0) + 8);
            void * t57 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 552) + 0)));
            uint64_t t58 = gm_l8;
            uint64_t t59 = (uintptr_t)t58;
            void * t60 = gm_l0;
            uint64_t t61 = GM_LD(uintptr_t, ((((uint8_t *)t60) + 552) + 0) + 8);
            uint64_t t62 = (uintptr_t)t61;
            if (t59 > t62 || t62 > (uintptr_t)t56) gm_trap_slice(t59, t62, t56, gm_src6c128h826nsg, 42, 48);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t57 + t59 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62 - t59));
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_b16_8 t65 = gm_l6;
            gm_f_perform_4kn6797((void *)&gm_l14, (void *)NULL, cx, NULL, t49, t50, t52, t53, t54, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t66 = gm_l0;
            void * t67 = (void *)(((uint8_t *)t66) + 552);
            uint64_t t68 = gm_l8;
            gm_f_truncate_j7f0pmi(t67, t68);
        }
        {
            gm_tcfa6l7reoosn t69 = gm_l14;
            if (((((uint8_t *)&t69))[0] != 0)) {
                gm_tcfa6l7reoosn t70;
                GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                ((uint8_t *)&t70)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t70 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t69) + 8)));
                F.ret = t70;
                goto gm_fret;
            }
            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 8));
        }
        {
            void * t72 = gm_l0;
            gm_t2lqu8jsj511d t73 = gm_l7;
            gm_f_release_jn5kqdi(t72, GM_CONV(gm_csc7lc6jlkqkuq, t73));
        }
    }
    gm_b0_1 t74;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t74);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_free_decl_values_fko33ae(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_free_decl_values_fko33ae F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* from */
    uint32_t gm_l2; /* until */
    uint32_t gm_l3; /* c */
    gm_th2lqbo77pvsc gm_l4; /* last */
    void * gm_l5; /* n */
    gm_b16_8 gm_l6; /* value */
    gm_b16_8 gm_l7; /* l */
    GM_STACK_CHECK(&F, 312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        gm_th2lqbo77pvsc t3;
        memset(&t3, 0, sizeof t3);
        gm_l4 = t3;
    }
    {
        for (;;) {
            uint32_t t5 = gm_l3;
            uint32_t t6 = gm_l2;
            uint8_t t7 = (!(t5 == t6));
            if (!t7) goto lb4;
            uint32_t t8 = gm_l3;
            uint8_t t9 = (!(t8 == ((uint32_t)0ULL)));
            if (!t9) goto lb4;
            {
                void * t12 = gm_l0;
                gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 632) + 0));
                gm_b16_8 t14 = t13;
                uint32_t t15 = gm_l3;
                uint64_t t16 = (uint64_t)t15;
                uint64_t t17 = gm_f_us_cuv01fp(t16);
                uint64_t t18 = (uintptr_t)t17;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&t14) + 8);
                if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_src6c128h826nsg, 305, 15);
                void * t20 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t14)) + t18 * 112);
                gm_l5 = t20;
            }
            {
                void * t22 = gm_l5;
                uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 32) + 0));
                if (t23 != ((uint8_t)1ULL)) goto ie21;
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((((uint8_t *)t22) + 32) + 8) + 16));
                gm_l6 = t24;
                {
                    if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie26;
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_l7 = t27;
                    void * t28 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    void * t29 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint8_t t30 = ((t28 == t29));
                    if (!t30) goto ie26;
                    goto ix26;
ie26:;
                    {
                        gm_b16_8 t33 = gm_l6;
                        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                        gm_b0_1 t35 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t34, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ko0rgal5bm2)));
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
ix26:;
                }
                {
                    gm_b16_8 t36 = gm_l6;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 0, t36);
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                void * t37 = gm_l5;
                uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)t37) + 0));
                gm_l3 = t38;
            }
lc4:;
        }
gm_ll10:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
        }
        goto gm_lad;
lb4:;
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
inline __attribute__((aligned(4))) void gm_f_perform_4kn6797(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, void * a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_perform_4kn6797 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* effect_ */
    uint32_t gm_l4; /* op */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* dst */
    uint32_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 422u);
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
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        void * t4 = gm_l3;
        uint32_t t5 = gm_f_find_ctx_1elkgc2(t2, t3, t4);
        gm_l7 = t5;
    }
    {
        uint32_t t7 = gm_l7;
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        if (!t8) goto ie6;
        {
            void * t10 = gm_l0;
            gm_tl874t2atnlh3 t11 = gm_l2;
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 8));
            void * t14 = (void *)(((uint8_t *)t13) + 8);
            void * t15 = gm_l3;
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 0));
            gm_b16_8 t17 = gm_f_effect_name_prpp0ip(t14, t16);
            gm_tnpifv20irnph t18;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_ms0imbl((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src6c128h826nsg, 167, 38);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_t76123gfet73g t22;
            gm_f_trap_qh84l9d((void *)&t22, (void *)NULL, cx, NULL, t10, t11, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l3;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 0));
        uint32_t t28 = gm_l4;
        uint8_t t29 = gm_f_is_ctl_oclllo3(t25, t27, t28);
        if (!t29) goto ie23;
        {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = gm_l2;
            uint32_t t33 = gm_l7;
            uint32_t t34 = gm_l4;
            gm_b16_8 t35 = gm_l5;
            gm_b16_8 t36 = gm_l6;
            gm_f_ctl_perform_kobk825((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t31, t32, t33, t34, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t37 = gm_l0;
        gm_tl874t2atnlh3 t38 = gm_l2;
        uint32_t t39 = gm_l7;
        uint32_t t40 = gm_l4;
        gm_b16_8 t41 = gm_l5;
        gm_b16_8 t42 = gm_l6;
        gm_f_call_clause_bjbs6l1((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t37, t38, t39, t40, t41, t42);
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
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_perform_native_2ppar83 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* dst */
    gm_b16_8 gm_l6; /* data */
    gm_b16_8 gm_l7; /* t */
    uint64_t gm_l8; /* ps */
    gm_b16_8 gm_l9; /* table */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* s */
    uint64_t gm_l14; /* addr */
    uint64_t gm_l15; /* taddr */
    gm_b16_8 gm_l16; /* saved */
    gm_b16_8 gm_l17; /* name */
    gm_b16_8 gm_l18; /* v */
    uint64_t gm_l19; /* addr */
    gm_t51b2en4qp02s gm_l20; /* sl */
    gm_b16_8 gm_l21; /* s */
    gm_t88rcucg1b7uk gm_l22; /* st */
    GM_STACK_CHECK(&F, 2474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t7 != ((uint8_t)0ULL)) goto pa6;
            goto po5;
pa6:;
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t9 != ((uint8_t)1ULL)) goto pa8;
            goto po5;
pa8:;
            goto mn4;
po5:;
            {
                void * t11 = gm_l0;
                gm_tl874t2atnlh3 t12 = gm_l1;
                uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_src6c128h826nsg, 203, 45);
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t13 * 16));
                gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
                gm_td0ept12e7q8b t17;
                gm_f_read_string_ttuk4gr((void *)&t17, (void *)NULL, cx, NULL, t11, t12, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t17))[0] != 0)) {
                    gm_tcfa6l7reoosn t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                    ((uint8_t *)&t18)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t18 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t17) + 8)));
                    F.ret = t18;
                    goto gm_fret;
                }
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
                gm_l6 = t19;
            }
            {
                void * t20 = gm_l0;
                uint32_t t21;
                uint8_t t23 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
                if (!t23) goto ie22;
                uint32_t t25 = ((uint32_t)1ULL);
                t21 = t25;
                goto ix22;
ie22:;
                uint32_t t27 = ((uint32_t)2ULL);
                t21 = t27;
ix22:;
                gm_b16_8 t28 = gm_l6;
                gm_b0_1 t29 = gm_f_host_write_r26no87((void *)NULL, cx, NULL, t20, t21, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t30 = gm_l6;
                gm_b0_1 t31 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t30, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kk1no0qhlfa)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me3;
mn4:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t33 != ((uint8_t)2ULL)) goto mn32;
            uint32_t t34 = gm_l3;
            uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&t34));
            if (!(t37 == ((uint32_t)0ULL))) goto mn36;
            {
                void * t40 = gm_l0;
                if (!(((((uint8_t *)t40) + 1064))[16] != 0)) goto ie39;
                gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 1064));
                gm_l7 = t41;
                {
                    gm_b16_8 t43 = gm_l5;
                    gm_b16_8 t44 = gm_l7;
                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                    gm_f_copy_from_f7j6p05(t43, t45);
                }
                {
                    gm_b0_1 t46;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t46);
                    goto gm_fdone;
                }
                goto ix39;
ie39:;
ix39:;
            }
            {
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)t47) + 176));
                uint64_t t49 = gm_f_us_cuv01fp(t48);
                gm_l8 = t49;
            }
            {
                void * t50 = gm_l0;
                void * t51 = gm_l0;
                uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)t51) + 1048) + 8);
                uint64_t r53; bool o54 = __builtin_mul_overflow(t52, ((uint64_t)2ULL), &r53);
                if (o54) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 214, 36);
                uint64_t t55 = r53;
                uint64_t t56 = gm_l8;
                uint64_t r57; bool o58 = __builtin_mul_overflow(t55, t56, &r57);
                if (o58) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 214, 36);
                uint64_t t59 = r57;
                uint64_t t60 = (uint64_t)t59;
                gm_b16_8 t61 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t50, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t61;
            }
            {
                {
                    void * t63 = gm_l0;
                    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 1048));
                    gm_l10 = t64;
                }
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t66 = gm_l11;
                        uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint8_t t68 = (t66 < t67);
                        if (!t68) goto lb65;
                        {
                            gm_tf1bjg81u0glv t71;
                            uint64_t t72 = gm_l11;
                            GM_ST(uint64_t, (uint8_t *)&t71 + 0, t72);
                            uint64_t t73 = gm_l11;
                            uint64_t t74 = (uintptr_t)t73;
                            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t74 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t71 + 8, t76);
                            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&t71) + 0));
                            gm_l12 = t79;
                            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
                            gm_l13 = t80;
                            {
                                void * t82 = gm_l0;
                                gm_tl874t2atnlh3 t83 = gm_l1;
                                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint64_t t85 = (uint64_t)t84;
                                gm_t9qkh0rsooc8d t86;
                                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)3ULL));
                                gm_t833u93mr4iob t87;
                                gm_f_alloc_mem_0vu6kcs((void *)&t87, (void *)NULL, cx, NULL, t82, t83, t85, ((uint64_t)1ULL), t86, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_ll69;
                                }
                                if (((((uint8_t *)&t87))[0] != 0)) {
                                    gm_tcfa6l7reoosn t88;
                                    GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                                    ((uint8_t *)&t88)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t88 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t87) + 8)));
                                    F.ret = t88;
                                    goto gm_fret;
                                }
                                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&t87) + 8));
                                gm_l14 = t89;
                            }
                            {
                                {
                                    void * t91 = gm_l0;
                                    void * t92 = (void *)(((uint8_t *)t91) + 184);
                                    uint64_t t93 = gm_l14;
                                    gm_b16_8 t94 = gm_l13;
                                    gm_tgfdnufetdnfc t95;
                                    gm_f_init_9fuf99v((void *)&t95, t92, t93, t94);
                                }
                            }
                            {
                                void * t96 = gm_l0;
                                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                void * t98 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)));
                                uint64_t t99 = gm_l12;
                                uint64_t r100; bool o101 = __builtin_mul_overflow(t99, ((uint64_t)2ULL), &r100);
                                if (o101) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 218, 39);
                                uint64_t t102 = r100;
                                uint64_t t103 = gm_l8;
                                uint64_t r104; bool o105 = __builtin_mul_overflow(t102, t103, &r104);
                                if (o105) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 218, 39);
                                uint64_t t106 = r104;
                                uint64_t t107 = (uintptr_t)t106;
                                uint64_t t108 = gm_l12;
                                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                                if (o110) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 218, 52);
                                uint64_t t111 = r109;
                                uint64_t r112; bool o113 = __builtin_mul_overflow(t111, ((uint64_t)2ULL), &r112);
                                if (o113) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 218, 51);
                                uint64_t t114 = r112;
                                uint64_t t115 = gm_l8;
                                uint64_t r116; bool o117 = __builtin_mul_overflow(t114, t115, &r116);
                                if (o117) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 218, 51);
                                uint64_t t118 = r116;
                                uint64_t t119 = (uintptr_t)t118;
                                if (t107 > t119 || t119 > (uintptr_t)t97) gm_trap_slice(t107, t119, t97, gm_src6c128h826nsg, 218, 33);
                                gm_b16_8 t120;
                                GM_ST(void *, (uint8_t *)&t120, (void *)(((uint8_t *)t98 + t107 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(t119 - t107));
                                uint64_t t121 = gm_l14;
                                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint64_t t123 = (uint64_t)t122;
                                gm_f_put_slice_ko14loi(t96, t120, t121, t123);
                            }
                            goto me77;
mn78:;
                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 215, 17);
me77:;
                        }
lc65:;
                        {
                            uint64_t t125 = gm_l11;
                            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                            uint64_t t128 = r126;
                            gm_l11 = t128;
                        }
                    }
gm_ll69:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
                    }
                    goto gm_lad;
lb65:;
                }
            }
            {
                void * t129 = gm_l0;
                gm_tl874t2atnlh3 t130 = gm_l1;
                uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint64_t t132 = (uint64_t)t131;
                gm_t9qkh0rsooc8d t133;
                GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)2ULL));
                gm_t833u93mr4iob t134;
                gm_f_alloc_mem_0vu6kcs((void *)&t134, (void *)NULL, cx, NULL, t129, t130, t132, ((uint64_t)16ULL), t133, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t134))[0] != 0)) {
                    gm_tcfa6l7reoosn t135;
                    GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                    ((uint8_t *)&t135)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t135 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t134) + 8)));
                    F.ret = t135;
                    goto gm_fret;
                }
                uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&t134) + 8));
                gm_l15 = t136;
            }
            {
                {
                    void * t138 = gm_l0;
                    void * t139 = (void *)(((uint8_t *)t138) + 184);
                    uint64_t t140 = gm_l15;
                    gm_b16_8 t141 = gm_l9;
                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                    gm_tgfdnufetdnfc t143;
                    gm_f_init_9fuf99v((void *)&t143, t139, t140, t142);
                }
            }
            {
                void * t144 = gm_l0;
                gm_b16_8 t145 = gm_l5;
                uint64_t t146 = gm_l15;
                void * t147 = gm_l0;
                uint64_t t148 = GM_LD(uintptr_t, (((uint8_t *)t147) + 1048) + 8);
                uint64_t t149 = (uint64_t)t148;
                gm_f_put_slice_ko14loi(t144, t145, t146, t149);
            }
            {
                uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                gm_tabb710d9mq2e t151;
                gm_f_make_2ip78bj((void *)&t151, (void *)NULL, cx, NULL, t150, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krkn5fd15l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 8, gm_src6c128h826nsg, 223, 26);
                gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t151) + 8));
                gm_l16 = t152;
            }
            {
                gm_b16_8 t153 = gm_l16;
                gm_b16_8 t154 = gm_l5;
                gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                gm_f_copy_from_f7j6p05(t153, t155);
            }
            {
                void * t156 = gm_l0;
                gm_b16_8 t157 = gm_l16;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t156) + 1064)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t156) + 1064)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t156) + 1064)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t156) + 1064)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)t156) + 1064)) + 0, t157);
            }
            goto me35;
mn36:;
            uint32_t t159 = GM_LD(uint32_t, ((uint8_t *)&t34));
            if (!(t159 == ((uint32_t)1ULL))) goto mn158;
            {
                void * t161 = gm_l0;
                gm_tl874t2atnlh3 t162 = gm_l1;
                uint64_t t163 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_src6c128h826nsg, 228, 49);
                gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t163 * 16));
                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                gm_td0ept12e7q8b t167;
                gm_f_read_string_ttuk4gr((void *)&t167, (void *)NULL, cx, NULL, t161, t162, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t167))[0] != 0)) {
                    gm_tcfa6l7reoosn t168;
                    GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                    ((uint8_t *)&t168)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t168 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t167) + 8)));
                    F.ret = t168;
                    goto gm_fret;
                }
                gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 8));
                gm_l17 = t169;
            }
            {
                gm_b16_8 t170 = gm_l17;
                gm_node *pn171 = gm_find(cx, 621690230);
                if (!pn171) gm_trap("no handler installed for effect `Env`", gm_src6c128h826nsg, 229, 22);
                gm_th2lqbo77pvsc t172;
                if (pn171->kind == GM_NATIVE) {
                    const uint8_t *ev; size_t evn;
                    bool evok = gm_env_var(GM_LD(void *, &t170), GM_LD(uintptr_t, (uint8_t *)&t170 + 8), &ev, &evn);
                    gm_b16_8 t173;
                    GM_ST(void *, (uint8_t *)&t173, (void *)(ev));
                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(evn));
                    gm_th2lqbo77pvsc t174;
                    GM_ST(uint8_t, (uint8_t *)&t174 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t174 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t174 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t174), t173);
                    ((uint8_t *)&t174)[16] = 1;
                    gm_th2lqbo77pvsc t175;
                    memset(&t175, 0, sizeof t175);
                    t172 = evok ? t174 : t175;
                } else {
                    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn171->clauses[1])((void *)&t172, pn171->env, pn171->hctx, pn171, t170);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t176;
                if (((((uint8_t *)&t172))[16] != 0)) {
                    gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t172) + 0));
                    t176 = t177;
                } else {
                    {
                        gm_b16_8 t179 = gm_l5;
                        gm_f_fill_abmf5g5(t179, ((uint8_t)0ULL));
                    }
                    gm_b0_1 t180;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t180);
                    goto gm_fdone;
                }
                gm_l18 = t176;
            }
            {
                void * t181 = gm_l0;
                gm_tl874t2atnlh3 t182 = gm_l1;
                uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint64_t t184 = (uint64_t)t183;
                gm_t9qkh0rsooc8d t185;
                GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)3ULL));
                gm_t833u93mr4iob t186;
                gm_f_alloc_mem_0vu6kcs((void *)&t186, (void *)NULL, cx, NULL, t181, t182, t184, ((uint64_t)1ULL), t185, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t186))[0] != 0)) {
                    gm_tcfa6l7reoosn t187;
                    GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                    ((uint8_t *)&t187)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t187 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t186) + 8)));
                    F.ret = t187;
                    goto gm_fret;
                }
                uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&t186) + 8));
                gm_l19 = t188;
            }
            {
                {
                    void * t190 = gm_l0;
                    void * t191 = (void *)(((uint8_t *)t190) + 184);
                    uint64_t t192 = gm_l19;
                    gm_b16_8 t193 = gm_l18;
                    gm_tgfdnufetdnfc t194;
                    gm_f_init_9fuf99v((void *)&t194, t191, t192, t193);
                }
            }
            {
            }
            {
                void * t195 = ((uint8_t *)&gm_l20);
                uint64_t t196 = (uintptr_t)((uint64_t)0ULL);
                void * t197 = gm_l0;
                uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)t197) + 176));
                uint64_t r199; bool o200 = __builtin_mul_overflow(((uint64_t)2ULL), t198, &r199);
                if (o200) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 236, 31);
                uint64_t t201 = r199;
                uint64_t t202 = gm_f_us_cuv01fp(t201);
                uint64_t t203 = (uintptr_t)t202;
                if (t196 > t203 || t203 > (uintptr_t)16) gm_trap_slice(t196, t203, 16, gm_src6c128h826nsg, 236, 22);
                gm_b16_8 t204;
                GM_ST(void *, (uint8_t *)&t204, (void *)(((uint8_t *)t195 + t196 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(t203 - t196));
                gm_l21 = t204;
            }
            {
                void * t205 = gm_l0;
                gm_b16_8 t206 = gm_l21;
                uint64_t t207 = gm_l19;
                uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint64_t t209 = (uint64_t)t208;
                gm_f_put_slice_ko14loi(t205, t206, t207, t209);
            }
            {
                void * t210 = gm_l0;
                void * t211 = GM_LD(void *, (((uint8_t *)t210) + 8));
                void * t212 = (void *)(((uint8_t *)t211) + 8);
                gm_t88rcucg1b7uk t213 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t213;
            }
            {
                void * t214 = gm_l0;
                void * t215 = gm_l0;
                void * t216 = GM_LD(void *, (((uint8_t *)t215) + 8));
                void * t217 = (void *)(((uint8_t *)t216) + 8);
                gm_t88rcucg1b7uk t218 = gm_l22;
                gm_t88rcucg1b7uk t219 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t217, t218);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t220 = gm_l5;
                gm_b16_8 t221 = gm_l21;
                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                gm_b0_1 t223 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t214, t219, t220, t222);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me35;
mn158:;
            uint32_t t225 = GM_LD(uint32_t, ((uint8_t *)&t34));
            if (!(t225 == ((uint32_t)2ULL))) goto mn224;
            uint8_t t226;
            uint64_t t228 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t228 >= (uintptr_t)(t229)) gm_trap_index(t228, t229, gm_src6c128h826nsg, 241, 35);
            uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t228 * 16) + 8);
            uint8_t t231 = (t230 > ((uint64_t)0ULL));
            if (!t231) goto ie227;
            uint64_t t233 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t234 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t234 >= (uintptr_t)(t235)) gm_trap_index(t234, t235, gm_src6c128h826nsg, 241, 53);
            uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t234 * 16) + 8);
            if (t233 >= (uintptr_t)(t236)) gm_trap_index(t233, t236, gm_src6c128h826nsg, 241, 53);
            uint8_t t237 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t234 * 16)) + t233 * 1));
            uint8_t t238 = t237;
            t226 = t238;
            goto ix227;
ie227:;
            uint8_t t240 = ((uint8_t)0ULL);
            t226 = t240;
ix227:;
            gm_t76123gfet73g t241;
            GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t241 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t241 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t241 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t241 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)3ULL));
            GM_ST(uint8_t, ((uint8_t *)&t241 + 8), t226);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t241);
            goto gm_fdone;
            goto me35;
mn224:;
            void * t243 = gm_l0;
            gm_tl874t2atnlh3 t244 = gm_l1;
            uint32_t t245 = gm_l3;
            gm_t88rcucg1b7uk t246;
            GM_ST(uint32_t, ((uint8_t *)&t246 + 0), t245);
            gm_tabb710d9mq2e t247;
            gm_f_format_la70vrc((void *)&t247, (void *)NULL, cx, NULL, t246);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t247))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t247) + 8, gm_src6c128h826nsg, 242, 39);
            gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&t247) + 8));
            gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&t248));
            gm_t76123gfet73g t250;
            gm_f_trap_qh84l9d((void *)&t250, (void *)NULL, cx, NULL, t243, t244, t249);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t250);
            goto gm_fdone;
            goto me35;
mn242:;
            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 207, 17);
me35:;
            goto me3;
mn32:;
            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 201, 5);
me3:;
        }
    }
    gm_b0_1 t251;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t251);
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
__attribute__((aligned(4))) void gm_f_with__u1jp2ib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, void * a3, gm_b16_8 a4) {
    struct gm_fr_with__u1jp2ib F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l3; /* e */
    gm_b16_8 gm_l4; /* dst */
    void * gm_l5; /* fr */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_b16_8 gm_l7; /* stack */
    void * gm_l8; /* s */
    uint32_t gm_l9; /* cx */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t4lnf71mruqpt gm_l12; /* h */
    void * gm_l13; /* x */
    gm_b16_8 gm_l14; /* hb */
    uint32_t gm_l15; /* id */
    uint32_t gm_l16; /* i */
    uint32_t gm_l17; /* i */
    gm_tiilmfd16v3fc gm_l18; /* eff */
    gm_b16_8 gm_l20; /* decls */
    void * gm_l21; /* value */
    gm_b16_8 gm_l22; /* vb */
    gm_b16_8 gm_l23; /* kept */
    uint32_t gm_l24; /* outer */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint32_t gm_l27; /* d */
    gm_tiilmfd16v3fc gm_l28; /* eff */
    uint64_t gm_l29; /* depth */
    gm_tcfa6l7reoosn gm_l30; /* r */
    gm_t76123gfet73g gm_l31; /* f */
    uint64_t gm_l32; /* frame */
    uint32_t gm_l33; /* label */
    GM_STACK_CHECK(&F, 2384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_l5 = t3;
        }
        {
            void * t4 = gm_l0;
            gm_t2lqu8jsj511d t5 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t4));
            gm_l6 = t5;
        }
        {
            gm_b16_8 t6 = gm_f_empty_slice_ijtp9lo();
            gm_l7 = t6;
        }
        {
            void * t8 = GM_LD(void *, (((uint8_t *)a2) + 24));
            if (!(GM_LD(void *, ((uint8_t *)&t8)) != 0)) goto ie7;
            void * t9 = GM_LD(void *, ((uint8_t *)&t8));
            gm_l8 = t9;
            {
                void * t11 = gm_l0;
                void * t12 = gm_l8;
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 104));
                gm_b16_8 t14 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t11, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t14;
            }
            {
                void * t15 = gm_l0;
                gm_tbl8il92l97ut t16 = gm_l1;
                void * t17 = gm_l8;
                gm_b16_8 t18 = gm_l7;
                gm_tcfa6l7reoosn t19;
                gm_f_ev_25jfdar((void *)&t19, (void *)NULL, cx, NULL, t15, t16, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t19))[0] != 0)) {
                    gm_tcfa6l7reoosn t20;
                    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                    ((uint8_t *)&t20)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t20 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t19) + 8)));
                    F.ret = t20;
                    goto gm_fret;
                }
                gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
            }
            goto ix7;
ie7:;
ix7:;
        }
        {
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
            gm_l9 = t22;
        }
        {
            {
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
                gm_l10 = t24;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l11;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l11;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t4lnf71mruqpt t36 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t32 * 32));
                        gm_l12 = t36;
                        {
                            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                            if (t40 != ((uint8_t)0ULL)) goto mn39;
                            void * t41 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                            gm_l13 = t41;
                            {
                                void * t43 = gm_l0;
                                void * t44 = gm_l13;
                                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 104));
                                gm_b16_8 t46 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t43, t45);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                gm_l14 = t46;
                            }
                            {
                                void * t47 = gm_l0;
                                void * t48 = gm_l5;
                                uint32_t t49 = gm_l9;
                                gm_tbl8il92l97ut t50;
                                GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t50 + 0), t48);
                                GM_ST(uint32_t, ((uint8_t *)&t50 + 8), t49);
                                void * t51 = gm_l13;
                                gm_b16_8 t52 = gm_l14;
                                gm_tcfa6l7reoosn t53;
                                gm_f_ev_25jfdar((void *)&t53, (void *)NULL, cx, NULL, t47, t50, t51, t52);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                if (((((uint8_t *)&t53))[0] != 0)) {
                                    gm_tcfa6l7reoosn t54;
                                    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                                    ((uint8_t *)&t54)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t54 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t53) + 8)));
                                    F.ret = t54;
                                    goto gm_fret;
                                }
                                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 8));
                            }
                            {
                                gm_l15 = ((uint32_t)0ULL);
                            }
                            {
                                void * t57 = gm_l13;
                                uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t57) + 0) + 0));
                                if (t58 != ((uint8_t)20ULL)) goto ie56;
                                uint32_t t59 = GM_LD(uint32_t, ((((uint8_t *)t57) + 0) + 8));
                                gm_l16 = t59;
                                {
                                    uint32_t t61 = gm_l16;
                                    gm_l15 = t61;
                                }
                                goto ix56;
ie56:;
                                void * t63 = gm_l0;
                                void * t64 = gm_l13;
                                gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 96));
                                gm_t53at80pfdus3 t66;
                                gm_f_kind_htv6093((void *)&t66, t63, GM_CONV(gm_csguankko4lmhd, t65));
                                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
                                if (t67 != ((uint8_t)25ULL)) goto ie62;
                                uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)&t66) + 8));
                                gm_l17 = t68;
                                {
                                    uint32_t t70 = gm_l17;
                                    gm_l15 = t70;
                                }
                                goto ix62;
ie62:;
ix62:;
ix56:;
                            }
                            {
                                void * t71 = gm_l0;
                                void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
                                uint32_t t73 = gm_l15;
                                void * t74 = gm_f_handler_lit_b02boff(t72, t73);
                                gm_tiilmfd16v3fc t75 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t74) + 0));
                                gm_l18 = t75;
                            }
                            {
                                GM_ST(uint8_t, (uint8_t *)&F.l19 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l19 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l19 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l19 + 32, 0);
                                GM_ST(uint64_t, (uint8_t *)&F.l19 + 40, 0);
                                GM_ST(uint8_t, (uint8_t *)&F.l19 + 0, ((uint8_t)0ULL));
                                memset((uint8_t *)&(*(gm_tk20dunvmmkbp *)((uint8_t *)&F.l19 + 8)) + 4, 0, 4);
                                memset((uint8_t *)&(*(gm_tk20dunvmmkbp *)((uint8_t *)&F.l19 + 8)) + 20, 0, 4);
                                uint32_t t76 = gm_l15;
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tk20dunvmmkbp *)((uint8_t *)&F.l19 + 8)) + 0, t76);
                                void * t77 = gm_l0;
                                void * t78 = gm_l0;
                                gm_b16_8 t79 = gm_l14;
                                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                                uint64_t t81 = gm_f_word_o2rcrhs(t78, t80, ((uint64_t)0ULL));
                                void * t82 = gm_f_frame_of_dgl92on(t77, t81);
                                GM_ST(void *, (uint8_t *)&(*(gm_tk20dunvmmkbp *)((uint8_t *)&F.l19 + 8)) + 8, t82);
                                void * t83 = gm_l0;
                                gm_b16_8 t84 = gm_l14;
                                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                                uint64_t t86 = gm_f_word_o2rcrhs(t83, t85, ((uint64_t)1ULL));
                                uint32_t t87 = (uint32_t)t86;
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tk20dunvmmkbp *)((uint8_t *)&F.l19 + 8)) + 16, t87);
                            }
                            {
                                void * t88 = gm_l0;
                                uint32_t t89 = gm_l9;
                                void * t90 = gm_l5;
                                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t90) + 0));
                                uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
                                void * t93 = gm_l5;
                                uint32_t t94 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t88, t89, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l18)), &(*(gm_tte9vgf6noic9 *)((uint8_t *)&F.l19)), t91, t92, t93);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                gm_l9 = t94;
                            }
                            goto me38;
mn39:;
                            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                            if (t96 != ((uint8_t)1ULL)) goto mn95;
                            gm_b16_8 t97 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l12) + 8) + 0));
                            gm_l20 = t97;
                            void * t98 = GM_LD(void *, ((((uint8_t *)&gm_l12) + 8) + 16));
                            gm_l21 = t98;
                            {
                                void * t100 = gm_l0;
                                void * t101 = gm_l21;
                                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)t101) + 104));
                                gm_b16_8 t103 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t100, t102);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                gm_l22 = t103;
                            }
                            {
                                void * t104 = gm_l0;
                                void * t105 = gm_l5;
                                uint32_t t106 = gm_l9;
                                gm_tbl8il92l97ut t107;
                                GM_ST(uint32_t, (uint8_t *)&t107 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t107 + 0), t105);
                                GM_ST(uint32_t, ((uint8_t *)&t107 + 8), t106);
                                void * t108 = gm_l21;
                                gm_b16_8 t109 = gm_l22;
                                gm_tcfa6l7reoosn t110;
                                gm_f_ev_25jfdar((void *)&t110, (void *)NULL, cx, NULL, t104, t107, t108, t109);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                if (((((uint8_t *)&t110))[0] != 0)) {
                                    gm_tcfa6l7reoosn t111;
                                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                                    ((uint8_t *)&t111)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t111 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t110) + 8)));
                                    F.ret = t111;
                                    goto gm_fret;
                                }
                                gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 8));
                            }
                            {
                                uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                gm_tabb710d9mq2e t114;
                                gm_f_make_2ip78bj((void *)&t114, (void *)NULL, cx, NULL, t113, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k9nqfo1idrl)));
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_src6c128h826nsg, 276, 25);
                                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                                gm_l23 = t115;
                            }
                            {
                                gm_b16_8 t116 = gm_l23;
                                gm_b16_8 t117 = gm_l22;
                                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                                gm_f_copy_from_f7j6p05(t116, t118);
                            }
                            {
                                uint32_t t119 = gm_l9;
                                gm_l24 = t119;
                            }
                            {
                                {
                                    gm_b16_8 t121 = gm_l20;
                                    gm_l25 = t121;
                                }
                                {
                                    gm_l26 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t123 = gm_l26;
                                        uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                        uint8_t t125 = (t123 < t124);
                                        if (!t125) goto lb122;
                                        {
                                            uint64_t t128 = gm_l26;
                                            uint64_t t129 = (uintptr_t)t128;
                                            uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                            uint32_t t131 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t129 * 4));
                                            uint32_t t134 = GM_LD(uint32_t, ((uint8_t *)&t131));
                                            gm_l27 = t134;
                                            {
                                                void * t136 = gm_l0;
                                                void * t137 = GM_LD(void *, (((uint8_t *)t136) + 0));
                                                uint32_t t138 = gm_l27;
                                                void * t139 = gm_f_handler_decl_m66mgr8(t137, t138);
                                                gm_tiilmfd16v3fc t140 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t139) + 0));
                                                gm_l28 = t140;
                                            }
                                            {
                                                void * t141 = gm_l0;
                                                uint32_t t142 = gm_l9;
                                                gm_td7tfovebirdn t143;
                                                memset((uint8_t *)&t143 + 36, 0, 4);
                                                gm_b16_8 t144 = gm_l20;
                                                gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                                                GM_ST(gm_b16_8, (uint8_t *)&t143 + 0, t145);
                                                gm_b16_8 t146 = gm_l23;
                                                GM_ST(gm_b16_8, (uint8_t *)&t143 + 16, t146);
                                                uint32_t t147 = gm_l24;
                                                GM_ST(uint32_t, (uint8_t *)&t143 + 32, t147);
                                                gm_tte9vgf6noic9 t148;
                                                GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)1ULL));
                                                GM_ST(gm_td7tfovebirdn, ((uint8_t *)&t148 + 8), t143);
                                                void * t149 = gm_l5;
                                                uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t149) + 0));
                                                uint32_t t151 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
                                                void * t152 = gm_l5;
                                                uint32_t t153 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t141, t142, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l28)), &t148, t150, t151, t152);
                                                if (gm_unw.kind) {
                                                    goto gm_ll126;
                                                }
                                                gm_l9 = t153;
                                            }
                                            goto me132;
mn133:;
                                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 279, 17);
me132:;
                                        }
lc122:;
                                        {
                                            uint64_t t155 = gm_l26;
                                            uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                                            uint64_t t158 = r156;
                                            gm_l26 = t158;
                                        }
                                    }
gm_ll126:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb122; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc122; }
                                    }
                                    goto gm_ll29;
lb122:;
                                }
                            }
                            goto me38;
mn95:;
                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 262, 9);
me38:;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 261, 5);
me34:;
                    }
lc25:;
                    {
                        uint64_t t160 = gm_l11;
                        uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                        uint64_t t163 = r161;
                        gm_l11 = t163;
                    }
                }
gm_ll29:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
                }
                goto gm_lad;
lb25:;
            }
        }
        {
            void * t164 = gm_l5;
            void * t165 = (void *)(((uint8_t *)t164) + 136);
            gm_tiilmfd16v3fc t166;
            memset((uint8_t *)&t166 + 4, 0, 4);
            uint32_t t167 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
            GM_ST(uint32_t, (uint8_t *)&t166 + 0, t167);
            gm_b16_8 t168 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t166 + 8, t168);
            gm_tftb1odc739t5 t169 = gm_f_push_aaqnel1(t165, &t166);
            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_src6c128h826nsg, 286, 5);
            gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
        }
        {
            void * t171 = gm_l5;
            uint64_t t172 = GM_LD(uintptr_t, ((((uint8_t *)t171) + 136) + 0) + 8);
            gm_l29 = t172;
        }
        {
            gm_tcfa6l7reoosn t173;
            void * t175 = GM_LD(void *, (((uint8_t *)a2) + 24));
            uint8_t t176;
            if ((GM_LD(void *, ((uint8_t *)&t175)) != 0)) goto mn178;
            t176 = ((uint8_t)0);
            goto me177;
mn178:;
            t176 = ((uint8_t)1);
            goto me177;
mn179:;
            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 288, 13);
me177:;
            if (!t176) goto ie174;
            void * t181 = gm_l0;
            void * t182 = gm_l5;
            uint32_t t183 = gm_l9;
            gm_tbl8il92l97ut t184;
            GM_ST(uint32_t, (uint8_t *)&t184 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t184 + 0), t182);
            GM_ST(uint32_t, ((uint8_t *)&t184 + 8), t183);
            gm_b16_8 t185 = gm_l7;
            void * t186 = gm_l3;
            gm_b16_8 t187 = gm_l4;
            gm_tcfa6l7reoosn t188;
            gm_f_with_on_a8m20cg((void *)&t188, (void *)NULL, cx, NULL, t181, t184, &(*(gm_t710lsrq1m8gk *)((uint8_t *)a2)), t185, t186, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t189 = t188;
            t173 = t189;
            goto ix174;
ie174:;
            void * t191 = gm_l0;
            void * t192 = gm_l5;
            uint32_t t193 = gm_l9;
            gm_tbl8il92l97ut t194;
            GM_ST(uint32_t, (uint8_t *)&t194 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t194 + 0), t192);
            GM_ST(uint32_t, ((uint8_t *)&t194 + 8), t193);
            void * t195 = GM_LD(void *, (((uint8_t *)a2) + 32));
            gm_b16_8 t196 = gm_l4;
            gm_tcfa6l7reoosn t197;
            gm_f_ev_25jfdar((void *)&t197, (void *)NULL, cx, NULL, t191, t194, t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t198 = t197;
            t173 = t198;
ix174:;
            gm_l30 = t173;
        }
        {
            void * t199 = gm_l5;
            void * t200 = (void *)(((uint8_t *)t199) + 136);
            uint64_t t201 = gm_l29;
            uint64_t r202; bool o203 = __builtin_sub_overflow(t201, ((uint64_t)1ULL), &r202);
            if (o203) gm_trap("integer overflow in `-`", gm_src6c128h826nsg, 289, 24);
            uint64_t t204 = r202;
            gm_f_truncate_9n3opb7(t200, t204);
        }
        {
            void * t205 = gm_l0;
            uint32_t t206 = gm_l9;
            uint32_t t207 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
            gm_b0_1 t208 = gm_f_free_decl_values_fko33ae((void *)NULL, cx, NULL, t205, t206, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t209 = gm_l0;
            uint32_t t210 = gm_l9;
            uint32_t t211 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
            gm_b0_1 t212 = gm_f_pop_ctx_jivp36j((void *)NULL, cx, NULL, t209, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t213 = gm_l0;
            gm_t2lqu8jsj511d t214 = gm_l6;
            gm_f_release_jn5kqdi(t213, GM_CONV(gm_csc7lc6jlkqkuq, t214));
        }
        {
            gm_tcfa6l7reoosn t215 = gm_l30;
            gm_b0_1 t216;
            if (!((((uint8_t *)&t215))[0] != 0)) {
                gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t215) + 8));
                t216 = t217;
            } else {
                gm_l31 = (*(gm_t76123gfet73g *)(((uint8_t *)&t215) + 8));
                {
                    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                    if (t220 != ((uint8_t)0ULL)) goto ie219;
                    uint64_t t221 = GM_LD(uint64_t, ((((uint8_t *)&gm_l31) + 8) + 0));
                    gm_l32 = t221;
                    uint32_t t222 = GM_LD(uint32_t, ((((uint8_t *)&gm_l31) + 8) + 8));
                    gm_l33 = t222;
                    uint64_t t223 = gm_l32;
                    void * t224 = gm_l5;
                    uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)t224) + 0));
                    uint8_t t226 = ((t223 == t225));
                    if (!t226) goto ie219;
                    uint32_t t227 = gm_l33;
                    uint32_t t228 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
                    uint8_t t229 = ((t227 == t228));
                    if (!t229) goto ie219;
                    {
                        gm_b0_1 t231;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t231);
                        goto gm_fdone;
                    }
                    goto ix219;
ie219:;
ix219:;
                }
                const uint8_t *t232 = ((uint8_t *)&gm_l31);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t232, 24);
                goto gm_fdone;
            }
        }
    }
    gm_b0_1 t233;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t233);
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
__attribute__((aligned(4))) void gm_f_write__eua2bmh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, void * a6) {
    struct gm_fr_write__eua2bmh F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l3; /* pieces */
    gm_b16_8 gm_l4; /* specs */
    gm_b16_8 gm_l5; /* args */
    void * gm_l6; /* e */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* sink */
    gm_t88rcucg1b7uk gm_l9; /* sink_ty */
    void * gm_l10; /* x */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    void * gm_l16; /* arg */
    uint8_t gm_l17; /* custom */
    gm_tl572d7579hns gm_l18; /* r */
    gm_b16_8 gm_l19; /* msg */
    GM_STACK_CHECK(&F, 1515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l7 = t4;
        }
        {
            gm_b16_8 t5 = gm_f_empty_slice_ijtp9lo();
            gm_l8 = t5;
        }
        {
            void * t6 = gm_l6;
            gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 96));
            gm_l9 = t7;
        }
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t9 != ((uint8_t)1ULL)) goto ie8;
            void * t10 = GM_LD(void *, (((uint8_t *)a2) + 8));
            gm_l10 = t10;
            {
                void * t12 = gm_l0;
                void * t13 = gm_l10;
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 104));
                gm_b16_8 t15 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t12, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t15;
            }
            {
                void * t16 = gm_l10;
                gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 96));
                gm_l9 = t17;
            }
            {
                void * t18 = gm_l0;
                gm_tbl8il92l97ut t19 = gm_l1;
                void * t20 = gm_l10;
                gm_b16_8 t21 = gm_l8;
                gm_tcfa6l7reoosn t22;
                gm_f_ev_25jfdar((void *)&t22, (void *)NULL, cx, NULL, t18, t19, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t22))[0] != 0)) {
                    gm_tcfa6l7reoosn t23;
                    GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                    ((uint8_t *)&t23)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t23 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t22) + 8)));
                    F.ret = t23;
                    goto gm_fret;
                }
                gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 8));
            }
            goto ix8;
ie8:;
ix8:;
        }
        {
            gm_f_list_fufbgf7((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t26 = gm_l0;
            gm_tbl8il92l97ut t27 = gm_l1;
            gm_b16_8 t28 = gm_l5;
            void * t29 = (void *)((uint8_t *)&F.l11);
            gm_tcfa6l7reoosn t30;
            gm_f_ev_args_j1qj6gl((void *)&t30, (void *)NULL, cx, NULL, t26, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_ld2_1;
            }
            if (((((uint8_t *)&t30))[0] != 0)) {
                gm_tcfa6l7reoosn t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t31 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t30) + 8)));
                F.ret = t31;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld2_1;
            }
            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 8));
        }
        {
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l12 = t34;
            }
            {
                for (;;) {
                    uint64_t t36 = gm_l13;
                    uint64_t t37 = gm_l12;
                    uint8_t t38 = (t36 < t37);
                    if (!t38) goto lb35;
                    {
                        uint64_t t41 = gm_l13;
                        uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&t41));
                        gm_l14 = t44;
                        {
                            uint64_t t47 = gm_l14;
                            uint64_t t48 = (uintptr_t)t47;
                            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_src6c128h826nsg, 332, 12);
                            uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t48 * 16) + 8);
                            uint8_t t51 = (t50 > ((uint64_t)0ULL));
                            if (!t51) goto ie46;
                            {
                                void * t53 = gm_l0;
                                uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                                void * t55 = gm_l6;
                                gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 112));
                                gm_b16_8 t57 = gm_l8;
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                gm_t88rcucg1b7uk t59 = gm_l9;
                                uint64_t t60 = gm_l14;
                                uint64_t t61 = (uintptr_t)t60;
                                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t61 >= (uintptr_t)(t62)) gm_trap_index(t61, t62, gm_src6c128h826nsg, 332, 80);
                                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t61 * 16));
                                gm_tcfa6l7reoosn t64;
                                gm_f_emit_8atsnfj((void *)&t64, (void *)NULL, cx, NULL, t53, t54, t56, &(*(gm_th1g69d7vpipg *)((uint8_t *)a2)), t58, t59, t63);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                                if (((((uint8_t *)&t64))[0] != 0)) {
                                    gm_tcfa6l7reoosn t65;
                                    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                                    ((uint8_t *)&t65)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t65 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t64) + 8)));
                                    F.ret = t65;
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                    goto gm_ll39;
                                }
                                gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 8));
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        {
                            uint64_t t68 = gm_l14;
                            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            uint8_t t70 = (t68 < t69);
                            if (!t70) goto ie67;
                            {
                                gm_f_list_uvfu1k1((void *)&F.l15, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                            }
                            {
                            }
                            {
                                uint64_t t73 = gm_l14;
                                uint64_t t74 = (uintptr_t)t73;
                                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                if (t74 >= (uintptr_t)(t75)) gm_trap_index(t74, t75, gm_src6c128h826nsg, 336, 20);
                                void * t76 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t74 * 8));
                                gm_l16 = t76;
                            }
                            {
                                gm_l17 = ((uint8_t)0);
                            }
                            {
                                uint64_t t78 = gm_l14;
                                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                uint8_t t80 = (t78 < t79);
                                if (!t80) goto ie77;
                                uint64_t t81 = gm_l14;
                                uint64_t t82 = (uintptr_t)t81;
                                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_src6c128h826nsg, 338, 59);
                                void * t84 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t82 * 24);
                                uint8_t t85 = gm_f_is_default_spec_cdd8jng(t84);
                                uint8_t t86 = (uint8_t)!t85;
                                if (!t86) goto ie77;
                                {
                                    gm_l17 = ((uint8_t)1);
                                }
                                goto ix77;
ie77:;
ix77:;
                            }
                            {
                                gm_tl572d7579hns t88;
                                uint8_t t90 = gm_l17;
                                if (!t90) goto ie89;
                                void * t92 = gm_l0;
                                void * t93 = (void *)((uint8_t *)&F.l15);
                                void * t94 = gm_l16;
                                gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 96));
                                uint64_t t96 = gm_l14;
                                uint64_t t97 = (uintptr_t)t96;
                                uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_src6c128h826nsg, 339, 58);
                                gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t97 * 16));
                                uint64_t t100 = gm_l14;
                                uint64_t t101 = (uintptr_t)t100;
                                uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_src6c128h826nsg, 339, 74);
                                void * t103 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t101 * 24);
                                gm_tl572d7579hns t104;
                                gm_f_format_spec_7aph60t((void *)&t104, (void *)NULL, cx, NULL, t92, t93, t95, t99, t103);
                                if (gm_unw.kind) {
                                    goto gm_ld71_1;
                                }
                                gm_tl572d7579hns t105 = t104;
                                t88 = t105;
                                goto ix89;
ie89:;
                                void * t107 = gm_l0;
                                void * t108 = (void *)((uint8_t *)&F.l15);
                                void * t109 = gm_l16;
                                gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t109) + 96));
                                uint64_t t111 = gm_l14;
                                uint64_t t112 = (uintptr_t)t111;
                                uint64_t t113 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                if (t112 >= (uintptr_t)(t113)) gm_trap_index(t112, t113, gm_src6c128h826nsg, 339, 122);
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t112 * 16));
                                gm_tl572d7579hns t115;
                                gm_f_format_value_vmle7qr((void *)&t115, (void *)NULL, cx, NULL, t107, t108, t110, t114, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_ld71_1;
                                }
                                gm_tl572d7579hns t116 = t115;
                                t88 = t116;
ix89:;
                                gm_l18 = t88;
                            }
                            {
                                gm_tl572d7579hns t117 = gm_l18;
                                gm_b0_1 t118;
                                if (!((((uint8_t *)&t117))[0] != 0)) {
                                    gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 8));
                                    t118 = t119;
                                } else {
                                    gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                                    gm_l19 = t120;
                                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                                    ((uint8_t *)&F.ret)[0] = 1;
                                    void * t121 = gm_l0;
                                    void * t122 = gm_l16;
                                    gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 112));
                                    gm_b16_8 t124 = gm_l19;
                                    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)((uint8_t *)&F.ret + 8)), (void *)NULL, cx, NULL, t121, t123, t124);
                                    if (gm_unw.kind) {
                                        goto gm_ld71_1;
                                    }
                                    uint8_t t125 = ((((uint8_t *)&F.ret))[0] != 0);
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t125;
                                    goto gm_ld71_1;
                                }
                            }
                            {
                                uint64_t t127 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint8_t t128 = (t127 > ((uint64_t)0ULL));
                                if (!t128) goto ie126;
                                {
                                    void * t130 = gm_l0;
                                    uint32_t t131 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                                    void * t132 = gm_l6;
                                    gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 112));
                                    gm_b16_8 t134 = gm_l8;
                                    gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                                    gm_t88rcucg1b7uk t136 = gm_l9;
                                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                                    gm_tcfa6l7reoosn t139;
                                    gm_f_emit_8atsnfj((void *)&t139, (void *)NULL, cx, NULL, t130, t131, t133, &(*(gm_th1g69d7vpipg *)((uint8_t *)a2)), t135, t136, t138);
                                    if (gm_unw.kind) {
                                        goto gm_ld71_1;
                                    }
                                    if (((((uint8_t *)&t139))[0] != 0)) {
                                        gm_tcfa6l7reoosn t140;
                                        GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                                        ((uint8_t *)&t140)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t140 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t139) + 8)));
                                        F.ret = t140;
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                        goto gm_ld71_1;
                                    }
                                    gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 8));
                                }
                                goto ix126;
ie126:;
ix126:;
                            }
                            {
                                void * t142 = (void *)((uint8_t *)&F.l15);
                                gm_f_deinit_r70pji5(t142);
                            }
                            goto gm_ls143;
gm_ld71_1:;
                            {
                                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                                void * t144 = (void *)((uint8_t *)&F.l15);
                                gm_f_deinit_r70pji5(t144);
                                gm_unw = gm_saved;
                            }
                            goto gm_ll39;
gm_ls143:;
                            goto ix67;
ie67:;
ix67:;
                        }
                        goto me42;
mn43:;
                        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 331, 5);
me42:;
                    }
lc35:;
                    {
                        uint64_t t146 = gm_l13;
                        uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                        uint64_t t149 = r147;
                        gm_l13 = t149;
                    }
                }
gm_ll39:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
                }
                goto gm_ld2_1;
lb35:;
            }
        }
        {
            void * t150 = gm_l0;
            gm_t2lqu8jsj511d t151 = gm_l7;
            gm_f_release_jn5kqdi(t150, GM_CONV(gm_csc7lc6jlkqkuq, t151));
        }
        {
            void * t152 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_tq06jtm(t152);
        }
        goto gm_ls153;
gm_ld2_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t154 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_tq06jtm(t154);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls153:;
    }
    gm_b0_1 t155;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t155);
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
