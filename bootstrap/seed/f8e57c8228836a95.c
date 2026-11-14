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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
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
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_toceffb697p0u f2; } gm_tqs4ilbobv1qq;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqs4ilbobv1qq) == 56 && _Alignof(gm_tqs4ilbobv1qq) == 8 && offsetof(gm_tqs4ilbobv1qq, f0) == 0 && offsetof(gm_tqs4ilbobv1qq, f1) == 24 && offsetof(gm_tqs4ilbobv1qq, f2) == 40, "layout of `MatchReport`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; uint32_t v1; gm_tngtuiuskn938 v2; gm_b16_8 v3; } f1; } gm_taevtln836oa8;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
_Static_assert(sizeof(gm_taevtln836oa8) == 104 && _Alignof(gm_taevtln836oa8) == 8 && offsetof(gm_taevtln836oa8, f0) == 0 && offsetof(gm_taevtln836oa8, f1) == 8, "layout of `Ctor`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_taevtln836oa8 f1; gm_b16_8 f2; } gm_tbq1t03a53gpo;
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_tbq1t03a53gpo v1; } f1; } gm_t8oufsr65o5ha;
_Static_assert(sizeof(gm_tbq1t03a53gpo) == 128 && _Alignof(gm_tbq1t03a53gpo) == 8 && offsetof(gm_tbq1t03a53gpo, f0) == 0 && offsetof(gm_tbq1t03a53gpo, f1) == 8 && offsetof(gm_tbq1t03a53gpo, f2) == 112, "layout of `(ty: Ty, ctor: Ctor, subs: []DPat)`");
_Static_assert(sizeof(gm_t8oufsr65o5ha) == 136 && _Alignof(gm_t8oufsr65o5ha) == 8 && offsetof(gm_t8oufsr65o5ha, f0) == 0 && offsetof(gm_t8oufsr65o5ha, f1) == 8, "layout of `DPat`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_all_ctors_o7aijfe {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tngtuiuskn938 l9; /* r */
    gm_tbci5ospk5nnn l10; /* min */
    gm_tbci5ospk5nnn l11; /* end */
    gm_trqfvmjtt4sij l12; /* cuts */
    gm_t8oufsr65o5ha l15; /* d */
    gm_taevtln836oa8 l16; /* c */
    gm_tbci5ospk5nnn l17; /* lo */
    gm_tbci5ospk5nnn l18; /* hi */
    gm_trqfvmjtt4sij l19; /* uniq */
    gm_tbci5ospk5nnn l22; /* c */
    gm_tbci5ospk5nnn l26; /* a */
    gm_tbci5ospk5nnn l27; /* b */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_alternatives_r0dto2k {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_check_match_mj577a4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* rows */
    gm_trqfvmjtt4sij l5; /* unreachable */
    gm_t8oufsr65o5ha l14; /* alt */
    gm_trqfvmjtt4sij l15; /* one */
    gm_t8oufsr65o5ha l18; /* alt */
    gm_trqfvmjtt4sij l19; /* row */
    gm_trqfvmjtt4sij l20; /* q */
    gm_tqs4ilbobv1qq ret;
};
struct gm_fr_column_covers_gn61qhr {
    gm_frame h;
    gm_t8oufsr65o5ha l4; /* d */
    gm_taevtln836oa8 l5; /* hc */
    uint8_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_covers_59m49eg {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* a */
    gm_tbci5ospk5nnn l3; /* b */
    gm_tbci5ospk5nnn l4; /* x */
    gm_tbci5ospk5nnn l5; /* y */
    uint8_t ret;
};
struct gm_fr_ctor_eq_fhbfduc {
    gm_frame h;
    gm_tbci5ospk5nnn l4; /* lo */
    gm_tbci5ospk5nnn l5; /* hi */
    gm_tbci5ospk5nnn l6; /* l2 */
    gm_tbci5ospk5nnn l7; /* h2 */
    uint8_t ret;
};
struct gm_fr_dpat_ty_2ipsk5e {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6rs1n5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c8h0sd2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kvdmouk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_sv0bumj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_eht4gub {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_lfqasgq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_j6p61la {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_word_n4uc7oo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_bounds_7npnb0b {
    gm_frame h;
    gm_tbci5ospk5nnn l7; /* half */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_irrefutable_lpkmeii {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* rows */
    gm_t8oufsr65o5ha l5; /* d */
    gm_trqfvmjtt4sij l6; /* row */
    gm_trqfvmjtt4sij l7; /* q */
    uint8_t ret;
};
struct gm_fr_is_name_char_eanps9t {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_99597qp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ahp090e {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_furncbs {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ghuba9i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
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
struct gm_fr_lower_product_5gakcnb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tbci5ospk5nnn l11; /* v */
    gm_tbci5ospk5nnn l13; /* lo */
    gm_tbci5ospk5nnn l14; /* hi */
    gm_tbci5ospk5nnn l16; /* h */
    gm_trqfvmjtt4sij l22; /* subs */
    gm_trqfvmjtt4sij l27; /* first */
    gm_trqfvmjtt4sij l31; /* combos */
    gm_trqfvmjtt4sij l36; /* next */
    gm_t8oufsr65o5ha l42; /* o */
    gm_trqfvmjtt4sij l43; /* c2 */
    gm_b16_8 ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_nested_present_binding_r48665i {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_33sq608 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_95uqvlq {
    gm_frame h;
    gm_taevtln836oa8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h9qsb4d {
    gm_frame h;
    gm_t8oufsr65o5ha l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s94euqb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rebuild_90secge {
    gm_frame h;
    gm_taevtln836oa8 l1; /* c */
    gm_trqfvmjtt4sij l5; /* out */
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1dn6qmo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h5h9b2d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_trq7lbp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vf4k680 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_witness_fmsa425 {
    gm_frame h;
    gm_taevtln836oa8 l3; /* c */
    gm_tbci5ospk5nnn l6; /* lo */
    gm_tbci5ospk5nnn l7; /* hi */
    gm_trqfvmjtt4sij l9; /* out */
    gm_trqfvmjtt4sij l13; /* inner */
    gm_trqfvmjtt4sij l18; /* parts */
    gm_t0mglksrkfa5e l23; /* f */
    gm_b16_8 ret;
};
struct gm_fr_sort_by_sbaob8l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_specialize_si0k4v5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_taevtln836oa8 l7; /* hc */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_sub_types_o2rpif3 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l11; /* f */
    gm_b16_8 ret;
};
struct gm_fr_useful_ge1iad5 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* column */
    gm_taevtln836oa8 l9; /* c */
    gm_trqfvmjtt4sij l10; /* pieces */
    gm_tbci5ospk5nnn l11; /* lo */
    gm_tbci5ospk5nnn l12; /* hi */
    gm_trqfvmjtt4sij l13; /* with_head */
    gm_taevtln836oa8 l17; /* p */
    gm_tbci5ospk5nnn l18; /* x */
    gm_tbci5ospk5nnn l19; /* y */
    gm_taevtln836oa8 l22; /* piece */
    gm_trqfvmjtt4sij l24; /* spec_rows */
    gm_taevtln836oa8 l36; /* c */
    gm_taevtln836oa8 l39; /* c */
    gm_trqfvmjtt4sij l41; /* spec_rows */
    gm_trqfvmjtt4sij l48; /* default */
    gm_t8oufsr65o5ha l53; /* first */
    gm_taevtln836oa8 l56; /* c */
    gm_trqfvmjtt4sij l57; /* subs */
    gm_trqfvmjtt4sij l61; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_wrap_each_2nbqohg {
    gm_frame h;
    gm_taevtln836oa8 l1; /* c */
    gm_t8oufsr65o5ha l6; /* d */
    gm_trqfvmjtt4sij l7; /* subs */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_all_ctors_o7aijfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_alternatives_r0dto2k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_column_covers_gn61qhr(gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_covers_59m49eg(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_ctor_eq_fhbfduc(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6rs1n5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c8h0sd2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvdmouk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sv0bumj(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_08505jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0gv6kss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hj7ahc4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i83sdhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) uint8_t gm_f_has_word_n4uc7oo(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_int_bounds_7npnb0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_introsort_jhfnj7n(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_name_char_eanps9t(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_99597qp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ahp090e(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_furncbs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ghuba9i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_product_5gakcnb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_nested_present_binding_r48665i(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_33sq608(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_95uqvlq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h9qsb4d(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s94euqb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_rebuild_90secge(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, uint64_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1dn6qmo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h5h9b2d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_trq7lbp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vf4k680(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_witness_fmsa425(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sort_by_sbaob8l(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_specialize_si0k4v5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_types_o2rpif3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_useful_ge1iad5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_wrap_each_2nbqohg(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src8lmgjh8bsvgp[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t3, t4, t6);
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
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
        gm_tftb1odc739t5 t5 = gm_f_extend_7hs67uj(t2, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t7);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dpat_ty_2ipsk5e F;
    void * gm_l0; /* d */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t5 != ((uint8_t)0ULL)) goto mn4;
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1) + 8));
    gm_l1 = t6;
    gm_t88rcucg1b7uk t7 = gm_l1;
    t2 = t7;
    goto me3;
mn4:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t1) + 8) + 0));
    gm_l2 = t10;
    gm_t88rcucg1b7uk t11 = gm_l2;
    t2 = t11;
    goto me3;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 27, 40);
me3:;
    F.ret = t2;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6rs1n5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6rs1n5s F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c8h0sd2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c8h0sd2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvdmouk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kvdmouk F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sv0bumj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_sv0bumj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_2onqfca(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 4);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_eht4gub F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_1dn6qmo(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 136)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 136);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_lfqasgq F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_j4iu2lk(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 4);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 4)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_j6p61la F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    int32_t t3 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)t1)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)t2)));
    uint8_t t4 = (t3 < ((int32_t)0LL));
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
inline __attribute__((aligned(4))) void gm_f_list_99597qp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_99597qp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6rs1n5s();
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
inline __attribute__((aligned(4))) void gm_f_list_ahp090e(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ahp090e F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kvdmouk();
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
inline __attribute__((aligned(4))) void gm_f_list_furncbs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_furncbs F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_sv0bumj();
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
inline __attribute__((aligned(4))) void gm_f_list_ghuba9i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ghuba9i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c8h0sd2();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_33sq608(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_33sq608 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_c8h0sd2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_trq7lbp(t10, ((uint64_t)1ULL), t12);
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
        gm_tbci5ospk5nnn t21 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)t20 + t18 * 48), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_95uqvlq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_95uqvlq F;
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
            gm_b16_8 t11 = gm_f_empty_slice_kvdmouk();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h5h9b2d(t10, ((uint64_t)1ULL), t12);
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
        gm_taevtln836oa8 t21 = F.l1;
        GM_ST(gm_taevtln836oa8, ((uint8_t *)t20 + t18 * 104), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h9qsb4d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h9qsb4d F;
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
            gm_b16_8 t11 = gm_f_empty_slice_6rs1n5s();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_1dn6qmo(t10, ((uint64_t)1ULL), t12);
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
        gm_t8oufsr65o5ha t21 = F.l1;
        GM_ST(gm_t8oufsr65o5ha, ((uint8_t *)t20 + t18 * 136), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
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
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s94euqb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s94euqb F;
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
            gm_b16_8 t11 = gm_f_empty_slice_sv0bumj();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_vf4k680(t10, ((uint64_t)1ULL), t12);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1dn6qmo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1dn6qmo F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h5h9b2d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h5h9b2d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_trq7lbp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_trq7lbp F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vf4k680(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vf4k680 F;
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint64_t t4 = gm_l1;
    gm_trqfvmjtt4sij t5;
    gm_f_mag_shl_5df643p((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t5);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_sbaob8l(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_sbaob8l F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_jhfnj7n(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t5 = (uint8_t)!t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t3, t5, t7);
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
__attribute__((aligned(4))) void gm_f_all_ctors_o7aijfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_all_ctors_o7aijfe F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* column */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* uv */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* k */
    GM_STACK_CHECK(&F, 3216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_ahp090e((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l4;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma8:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t18 != ((uint8_t)4ULL)) goto mn17;
        {
            void * t20 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t21;
            memset(&t21, 0, sizeof t21);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t21 + 8), ((uint8_t)0));
            gm_tftb1odc739t5 t22 = gm_f_push_95uqvlq(t20, &t21);
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_src8lmgjh8bsvgp, 244, 13);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        {
            void * t24 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t25 + 8), ((uint8_t)1));
            gm_tftb1odc739t5 t26 = gm_f_push_95uqvlq(t24, &t25);
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src8lmgjh8bsvgp, 245, 13);
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        }
        goto me7;
mn17:;
ma9:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t29 != ((uint8_t)13ULL)) goto mn28;
        {
            void * t31 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)1ULL));
            gm_tftb1odc739t5 t33 = gm_f_push_95uqvlq(t31, &t32);
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_src8lmgjh8bsvgp, 248, 13);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        {
            void * t35 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t36;
            memset(&t36, 0, sizeof t36);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)2ULL));
            gm_tftb1odc739t5 t37 = gm_f_push_95uqvlq(t35, &t36);
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src8lmgjh8bsvgp, 249, 13);
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
        }
        goto me7;
mn28:;
ma10:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t40 != ((uint8_t)14ULL)) goto mn39;
        {
            void * t42 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t43;
            memset(&t43, 0, sizeof t43);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)3ULL));
            gm_tftb1odc739t5 t44 = gm_f_push_95uqvlq(t42, &t43);
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_src8lmgjh8bsvgp, 252, 13);
            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
        }
        {
            void * t46 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t47;
            memset(&t47, 0, sizeof t47);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)4ULL));
            gm_tftb1odc739t5 t48 = gm_f_push_95uqvlq(t46, &t47);
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src8lmgjh8bsvgp, 253, 13);
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
        goto me7;
mn39:;
ma11:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t51 != ((uint8_t)12ULL)) goto mn50;
        void * t52 = (void *)((uint8_t *)&F.l3);
        gm_taevtln836oa8 t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)6ULL));
        gm_tftb1odc739t5 t54 = gm_f_push_95uqvlq(t52, &t53);
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src8lmgjh8bsvgp, 255, 22);
        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        goto me7;
mn50:;
ma12:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t57 != ((uint8_t)5ULL)) goto mn56;
        goto me7;
mn56:;
ma13:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t60 != ((uint8_t)17ULL)) goto mn59;
        {
            void * t63 = gm_l0;
            gm_t88rcucg1b7uk t64 = gm_l1;
            gm_th2lqbo77pvsc t65;
            gm_f_struct_fields_chqu3it((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
            uint8_t t66;
            if (((((uint8_t *)&t65))[16] != 0)) goto mn68;
            t66 = ((uint8_t)0);
            goto me67;
mn68:;
            t66 = ((uint8_t)1);
            goto me67;
mn69:;
            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 258, 16);
me67:;
            if (!t66) goto ie62;
            {
                void * t71 = (void *)((uint8_t *)&F.l3);
                gm_taevtln836oa8 t72;
                memset(&t72, 0, sizeof t72);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)6ULL));
                gm_tftb1odc739t5 t73 = gm_f_push_95uqvlq(t71, &t72);
                if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_src8lmgjh8bsvgp, 259, 17);
                gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
            }
            goto ix62;
ie62:;
            void * t76 = gm_l0;
            gm_t88rcucg1b7uk t77 = gm_l1;
            gm_tdlpv0q69lov1 t78;
            gm_f_union_variants_a7khaqp((void *)&t78, t76, GM_CONV(gm_csguankko4lmhd, t77));
            if (!((((uint8_t *)&t78))[24] != 0)) goto ie75;
            gm_l5 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t78) + 0));
            {
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t81 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
                    gm_l6 = t81;
                }
                {
                    for (;;) {
                        uint64_t t83 = gm_l7;
                        uint64_t t84 = gm_l6;
                        uint8_t t85 = (t83 < t84);
                        if (!t85) goto lb82;
                        {
                            uint64_t t88 = gm_l7;
                            uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&t88));
                            gm_l8 = t91;
                            {
                                void * t93 = (void *)((uint8_t *)&F.l3);
                                uint64_t t94 = gm_l8;
                                uint32_t t95 = (uint32_t)t94;
                                gm_taevtln836oa8 t96;
                                memset(&t96, 0, sizeof t96);
                                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)5ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t96 + 8), t95);
                                gm_tftb1odc739t5 t97 = gm_f_push_95uqvlq(t93, &t96);
                                if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_src8lmgjh8bsvgp, 261, 47);
                                gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                            }
                            goto me89;
mn90:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 261, 17);
me89:;
                        }
lc82:;
                        {
                            uint64_t t100 = gm_l7;
                            uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                            uint64_t t103 = r101;
                            gm_l7 = t103;
                        }
                    }
lb82:;
                }
            }
            goto ix75;
ie75:;
            {
                gm_th2lqbo77pvsc t105;
                memset(&t105, 0, sizeof t105);
                uint8_t t106 = 0;
                F.ret = t105;
                goto gm_fret;
            }
ix75:;
ix62:;
        }
        goto me7;
mn59:;
ma14:;
        {
            void * t109 = gm_l4;
            gm_t88rcucg1b7uk t110 = gm_l1;
            void * t111 = gm_l0;
            void * t112 = GM_LD(void *, (((uint8_t *)t111) + 0));
            void * t113 = (void *)(((uint8_t *)t112) + 88);
            uint32_t t114 = gm_f_ptr_bits_glps5i7(t113);
            gm_tvegkb6ouf3m0 t115;
            gm_f_int_bounds_7npnb0b((void *)&t115, (void *)NULL, cx, NULL, t109, t110, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t116;
            if (((((uint8_t *)&t115))[96] != 0)) {
                t116 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t115) + 0));
            } else {
                gm_th2lqbo77pvsc t117;
                memset(&t117, 0, sizeof t117);
                uint8_t t118 = 0;
                F.ret = t117;
                goto gm_fret;
            }
            F.l9 = t116;
        }
        {
            gm_tbci5ospk5nnn t119 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l9) + 0));
            F.l10 = t119;
        }
        {
            gm_tbci5ospk5nnn t120 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l9) + 48));
            gm_tbci5ospk5nnn t121;
            gm_f_one_08qg1op((void *)&t121, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_420lj6o((void *)&F.l11, (void *)NULL, cx, NULL, &t120, &t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_ghuba9i((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t122 = (void *)((uint8_t *)&F.l12);
            gm_tftb1odc739t5 t123 = gm_f_push_33sq608(t122, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_src8lmgjh8bsvgp, 271, 13);
            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
        }
        {
            void * t125 = (void *)((uint8_t *)&F.l12);
            gm_tftb1odc739t5 t126 = gm_f_push_33sq608(t125, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_src8lmgjh8bsvgp, 272, 13);
            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
        }
        {
            {
                gm_b16_8 t129 = gm_l2;
                gm_l13 = t129;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t131 = gm_l14;
                    uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t133 = (t131 < t132);
                    if (!t133) goto lb130;
                    {
                        uint64_t t136 = gm_l14;
                        uint64_t t137 = (uintptr_t)t136;
                        uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_t8oufsr65o5ha t141 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t137 * 136));
                        F.l15 = t141;
                        {
                            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                            if (t144 != ((uint8_t)1ULL)) goto ie143;
                            gm_taevtln836oa8 t145 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)&F.l15) + 8) + 8));
                            F.l16 = t145;
                            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            if (t146 != ((uint8_t)7ULL)) goto ie143;
                            gm_tbci5ospk5nnn t147 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l16) + 8) + 0));
                            F.l17 = t147;
                            gm_tbci5ospk5nnn t148 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l16) + 8) + 48));
                            F.l18 = t148;
                            {
                                void * t150 = (void *)((uint8_t *)&F.l12);
                                gm_tftb1odc739t5 t151 = gm_f_push_33sq608(t150, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
                                if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_src8lmgjh8bsvgp, 275, 21);
                                gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
                            }
                            {
                                void * t153 = (void *)((uint8_t *)&F.l12);
                                gm_tbci5ospk5nnn t154 = F.l18;
                                gm_tbci5ospk5nnn t155;
                                gm_f_one_08qg1op((void *)&t155, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll134;
                                }
                                gm_tbci5ospk5nnn t156;
                                gm_f_add_420lj6o((void *)&t156, (void *)NULL, cx, NULL, &t154, &t155);
                                if (gm_unw.kind) {
                                    goto gm_ll134;
                                }
                                gm_tftb1odc739t5 t157 = gm_f_push_33sq608(t153, &t156);
                                if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_src8lmgjh8bsvgp, 276, 21);
                                gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
                            }
                            goto ix143;
ie143:;
ix143:;
                        }
                        goto me139;
mn140:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 273, 13);
me139:;
                    }
lc130:;
                    {
                        uint64_t t160 = gm_l14;
                        uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                        uint64_t t163 = r161;
                        gm_l14 = t163;
                    }
                }
gm_ll134:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb130; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc130; }
                }
                goto gm_lad;
lb130:;
            }
        }
        {
            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_closure t165 = ((gm_closure){GM_PTR_TAG(gm_f_fn_j6p61la), NULL, cx});
            gm_f_sort_by_sbaob8l(t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_ghuba9i((void *)&F.l19, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                gm_l20 = t167;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t169 = gm_l21;
                    uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t171 = (t169 < t170);
                    if (!t171) goto lb168;
                    {
                        uint64_t t174 = gm_l21;
                        uint64_t t175 = (uintptr_t)t174;
                        uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_tbci5ospk5nnn t179 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t175 * 48));
                        F.l22 = t179;
                        {
                            uint8_t t182 = 0;
                            uint64_t t183 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            uint8_t t184 = ((t183 == ((uint64_t)0ULL)));
                            if (!t184) {
                                uint64_t t185 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                                uint64_t r186; bool o187 = __builtin_sub_overflow(t185, ((uint64_t)1ULL), &r186);
                                if (o187) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 282, 54);
                                uint64_t t188 = r186;
                                uint64_t t189 = (uintptr_t)t188;
                                uint64_t t190 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                                if (t189 >= (uintptr_t)(t190)) gm_trap_index(t189, t190, gm_src8lmgjh8bsvgp, 282, 43);
                                int32_t t191 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t189 * 48)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                                uint8_t t192 = (!(t191 == ((int32_t)0LL)));
                                t182 = t192;
                            } else t182 = 1;
                            if (!t182) goto ie181;
                            {
                                void * t194 = (void *)((uint8_t *)&F.l19);
                                gm_tftb1odc739t5 t195 = gm_f_push_33sq608(t194, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                                if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_src8lmgjh8bsvgp, 282, 92);
                                gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
                            }
                            goto ix181;
ie181:;
ix181:;
                        }
                        goto me177;
mn178:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 281, 13);
me177:;
                    }
lc168:;
                    {
                        uint64_t t198 = gm_l21;
                        uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                        uint64_t t201 = r199;
                        gm_l21 = t201;
                    }
                }
lb168:;
            }
        }
        {
            {
                gm_l24 = ((uint64_t)1ULL);
            }
            {
                uint64_t t203 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                gm_l23 = t203;
            }
            {
                for (;;) {
                    uint64_t t205 = gm_l24;
                    uint64_t t206 = gm_l23;
                    uint8_t t207 = (t205 < t206);
                    if (!t207) goto lb204;
                    {
                        uint64_t t210 = gm_l24;
                        uint64_t t213 = GM_LD(uint64_t, ((uint8_t *)&t210));
                        gm_l25 = t213;
                        {
                            uint64_t t215 = gm_l25;
                            uint64_t r216; bool o217 = __builtin_sub_overflow(t215, ((uint64_t)1ULL), &r216);
                            if (o217) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 285, 33);
                            uint64_t t218 = r216;
                            uint64_t t219 = (uintptr_t)t218;
                            uint64_t t220 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            if (t219 >= (uintptr_t)(t220)) gm_trap_index(t219, t220, gm_src8lmgjh8bsvgp, 285, 22);
                            gm_tbci5ospk5nnn t221 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t219 * 48));
                            F.l26 = t221;
                        }
                        {
                            uint64_t t222 = gm_l25;
                            uint64_t t223 = (uintptr_t)t222;
                            uint64_t t224 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            if (t223 >= (uintptr_t)(t224)) gm_trap_index(t223, t224, gm_src8lmgjh8bsvgp, 286, 22);
                            gm_tbci5ospk5nnn t225 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t223 * 48));
                            F.l27 = t225;
                        }
                        {
                            int32_t t227 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
                            uint8_t t228 = (t227 >= ((int32_t)0LL));
                            if (!t228) goto ie226;
                            int32_t t229 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                            uint8_t t230 = (t229 <= ((int32_t)0LL));
                            if (!t230) goto ie226;
                            int32_t t231 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)));
                            uint8_t t232 = (t231 < ((int32_t)0LL));
                            if (!t232) goto ie226;
                            {
                                void * t234 = (void *)((uint8_t *)&F.l3);
                                gm_tngtuiuskn938 t235;
                                memcpy((uint8_t *)&t235 + 0, ((uint8_t *)&F.l26), 48);
                                gm_tbci5ospk5nnn t236 = F.l27;
                                gm_tbci5ospk5nnn t237;
                                gm_f_one_08qg1op((void *)&t237, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll208;
                                }
                                gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t235 + 48)), (void *)NULL, cx, NULL, &t236, &t237);
                                if (gm_unw.kind) {
                                    goto gm_ll208;
                                }
                                gm_taevtln836oa8 t238;
                                GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)7ULL));
                                GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t238 + 8), t235);
                                gm_tftb1odc739t5 t239 = gm_f_push_95uqvlq(t234, &t238);
                                if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_src8lmgjh8bsvgp, 287, 75);
                                gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
                            }
                            goto ix226;
ie226:;
ix226:;
                        }
                        goto me211;
mn212:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 284, 13);
me211:;
                    }
lc204:;
                    {
                        uint64_t t242 = gm_l24;
                        uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                        uint64_t t245 = r243;
                        gm_l24 = t245;
                    }
                }
gm_ll208:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb204; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc204; }
                }
                goto gm_lad;
lb204:;
            }
        }
        goto me7;
mn107:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 242, 5);
me7:;
    }
    {
        gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t246);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_alternatives_r0dto2k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_alternatives_r0dto2k F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_b16_8 gm_l2; /* ps */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* q */
    GM_STACK_CHECK(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)10ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_f_list_99597qp((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t8 = gm_l2;
                gm_l4 = t8;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    {
                        uint64_t t15 = gm_l5;
                        uint64_t t16 = (uintptr_t)t15;
                        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        void * t18 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 8));
                        void * t21 = GM_LD(void *, ((uint8_t *)&t18));
                        gm_l6 = t21;
                        {
                            void * t23 = (void *)((uint8_t *)&F.l3);
                            void * t24 = gm_l0;
                            void * t25 = gm_l6;
                            gm_b16_8 t26 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t24, t25);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                            gm_tftb1odc739t5 t28 = gm_f_extend_eht4gub(t23, t27);
                            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_src8lmgjh8bsvgp, 143, 23);
                            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 143, 9);
me19:;
                    }
lc9:;
                    {
                        uint64_t t31 = gm_l5;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        uint64_t t34 = r32;
                        gm_l5 = t34;
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
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = gm_l1;
        gm_b16_8 t39 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t40 = 0;
        F.ret = t39;
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
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_check_match_mj577a4 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* pats */
    gm_b16_8 gm_l3; /* guarded */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* p */
    gm_b16_8 gm_l10; /* alts */
    uint8_t gm_l11; /* any_useful */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_th2lqbo77pvsc gm_l21; /* missing */
    gm_toceffb697p0u gm_l22; /* present_only */
    gm_b16_8 gm_l23; /* w */
    gm_b16_8 gm_l24; /* m */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    void * gm_l27; /* p */
    gm_tl874t2atnlh3 gm_l28; /* at */
    GM_STACK_CHECK(&F, 1609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_furncbs((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = gm_l7;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    void * t14 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t12 * 8));
                    gm_t22qobfhrnoif t15;
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(void *, ((uint8_t *)&t15 + 8), t14);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l8 = t18;
                    void * t19 = GM_LD(void *, (((uint8_t *)&t15) + 8));
                    gm_l9 = t19;
                    {
                        void * t21 = gm_l0;
                        void * t22 = gm_l9;
                        gm_b16_8 t23 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l10 = t23;
                    }
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t25 = gm_l10;
                            gm_l12 = t25;
                        }
                        {
                            gm_l13 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t27 = gm_l13;
                                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                uint8_t t29 = (t27 < t28);
                                if (!t29) goto lb26;
                                {
                                    uint64_t t32 = gm_l13;
                                    uint64_t t33 = (uintptr_t)t32;
                                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    gm_t8oufsr65o5ha t37 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t33 * 136));
                                    F.l14 = t37;
                                    {
                                        gm_f_list_99597qp((void *)&F.l15, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll30;
                                        }
                                    }
                                    {
                                        void * t39 = (void *)((uint8_t *)&F.l15);
                                        gm_tftb1odc739t5 t40 = gm_f_push_h9qsb4d(t39, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l14)));
                                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src8lmgjh8bsvgp, 57, 13);
                                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                                    }
                                    {
                                        void * t43 = gm_l0;
                                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                                        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                        gm_th2lqbo77pvsc t48;
                                        gm_f_useful_ge1iad5((void *)&t48, (void *)NULL, cx, NULL, t43, t45, t47);
                                        if (gm_unw.kind) {
                                            goto gm_ll30;
                                        }
                                        uint8_t t49;
                                        if (((((uint8_t *)&t48))[16] != 0)) goto mn51;
                                        t49 = ((uint8_t)0);
                                        goto me50;
mn51:;
                                        t49 = ((uint8_t)1);
                                        goto me50;
mn52:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 58, 16);
me50:;
                                        if (!t49) goto ie42;
                                        {
                                            gm_l11 = ((uint8_t)1);
                                        }
                                        goto ix42;
ie42:;
ix42:;
                                    }
                                    goto me35;
mn36:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 55, 9);
me35:;
                                }
lc26:;
                                {
                                    uint64_t t55 = gm_l13;
                                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                    uint64_t t58 = r56;
                                    gm_l13 = t58;
                                }
                            }
gm_ll30:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
                            }
                            goto gm_ll8;
lb26:;
                        }
                    }
                    {
                        uint8_t t60 = gm_l11;
                        uint8_t t61 = (uint8_t)!t60;
                        if (!t61) goto ie59;
                        {
                            void * t63 = (void *)((uint8_t *)&F.l5);
                            uint64_t t64 = gm_l8;
                            gm_tftb1odc739t5 t65 = gm_f_push_ucgmlo2(t63, t64);
                            if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_src8lmgjh8bsvgp, 60, 29);
                            gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                        }
                        goto ix59;
ie59:;
ix59:;
                    }
                    {
                        uint64_t t68 = gm_l8;
                        uint64_t t69 = (uintptr_t)t68;
                        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_src8lmgjh8bsvgp, 61, 16);
                        uint8_t t71 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t69 * 1));
                        uint8_t t72 = (uint8_t)!t71;
                        if (!t72) goto ie67;
                        {
                            {
                                gm_b16_8 t75 = gm_l10;
                                gm_l16 = t75;
                            }
                            {
                                gm_l17 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t77 = gm_l17;
                                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                    uint8_t t79 = (t77 < t78);
                                    if (!t79) goto lb76;
                                    {
                                        uint64_t t82 = gm_l17;
                                        uint64_t t83 = (uintptr_t)t82;
                                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                        gm_t8oufsr65o5ha t87 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t83 * 136));
                                        F.l18 = t87;
                                        {
                                            gm_f_list_99597qp((void *)&F.l19, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_ll80;
                                            }
                                        }
                                        {
                                            void * t89 = (void *)((uint8_t *)&F.l19);
                                            gm_tftb1odc739t5 t90 = gm_f_push_h9qsb4d(t89, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l18)));
                                            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_src8lmgjh8bsvgp, 64, 17);
                                            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                                        }
                                        {
                                            void * t92 = (void *)((uint8_t *)&F.l4);
                                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                                            gm_tftb1odc739t5 t94 = gm_f_push_s94euqb(t92, t93);
                                            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_src8lmgjh8bsvgp, 65, 17);
                                            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                                        }
                                        goto me85;
mn86:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 62, 13);
me85:;
                                    }
lc76:;
                                    {
                                        uint64_t t97 = gm_l17;
                                        uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                                        uint64_t t100 = r98;
                                        gm_l17 = t100;
                                    }
                                }
gm_ll80:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb76; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc76; }
                                }
                                goto gm_ll8;
lb76:;
                            }
                        }
                        goto ix67;
ie67:;
ix67:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 52, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t102 = gm_l7;
                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                    uint64_t t105 = r103;
                    gm_l7 = t105;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        gm_f_list_99597qp((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t106 = (void *)((uint8_t *)&F.l20);
        gm_t88rcucg1b7uk t107 = gm_l1;
        gm_t8oufsr65o5ha t108;
        memset(&t108, 0, sizeof t108);
        GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t108 + 8), t107);
        gm_tftb1odc739t5 t109 = gm_f_push_h9qsb4d(t106, &t108);
        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_src8lmgjh8bsvgp, 70, 5);
        gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
    }
    {
        gm_th2lqbo77pvsc t111;
        memset(&t111, 0, sizeof t111);
        gm_l21 = t111;
    }
    {
        gm_toceffb697p0u t112;
        memset(&t112, 0, sizeof t112);
        gm_l22 = t112;
    }
    {
        void * t114 = gm_l0;
        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
        gm_th2lqbo77pvsc t119;
        gm_f_useful_ge1iad5((void *)&t119, (void *)NULL, cx, NULL, t114, t116, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t119))[16] != 0)) goto ie113;
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        gm_l23 = t120;
        {
            void * t122 = gm_l0;
            uint64_t t123 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t123 >= (uintptr_t)(t124)) gm_trap_index(t123, t124, gm_src8lmgjh8bsvgp, 74, 31);
            void * t125 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t123 * 136);
            gm_b16_8 t126 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t122, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t126;
        }
        {
            gm_b16_8 t127 = gm_l24;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l21)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l21)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l21)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l21)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l21)) + 0, t127);
        }
        {
            gm_b16_8 t129 = gm_l24;
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(4));
            uint8_t t131 = gm_f_has_word_n4uc7oo(t129, t130);
            if (!t131) goto ie128;
            {
                {
                    gm_b16_8 t134 = gm_l2;
                    gm_l25 = t134;
                }
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t136 = gm_l26;
                        uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint8_t t138 = (t136 < t137);
                        if (!t138) goto lb135;
                        {
                            uint64_t t141 = gm_l26;
                            uint64_t t142 = (uintptr_t)t141;
                            uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                            void * t144 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t142 * 8));
                            void * t147 = GM_LD(void *, ((uint8_t *)&t144));
                            gm_l27 = t147;
                            {
                                void * t150 = gm_l27;
                                gm_toceffb697p0u t151 = gm_f_nested_present_binding_r48665i(t150, ((uint8_t)1));
                                if (!((((uint8_t *)&t151))[12] != 0)) goto ie149;
                                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t151));
                                gm_l28 = t152;
                                {
                                    gm_tl874t2atnlh3 t154 = gm_l28;
                                    gm_toceffb697p0u t155;
                                    GM_ST(uint8_t, (uint8_t *)&t155 + 13, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t155 + 14, 0);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t155), t154);
                                    ((uint8_t *)&t155)[12] = 1;
                                    gm_l22 = t155;
                                }
                                {
                                    goto lb135;
                                }
                                goto ix149;
ie149:;
ix149:;
                            }
                            goto me145;
mn146:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 77, 13);
me145:;
                        }
lc135:;
                        {
                            uint64_t t157 = gm_l26;
                            uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                            uint64_t t160 = r158;
                            gm_l26 = t160;
                        }
                    }
lb135:;
                }
            }
            goto ix128;
ie128:;
ix128:;
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        memcpy((uint8_t *)&(*(gm_tqs4ilbobv1qq *)gm_out) + 0, ((uint8_t *)&gm_l21), 24);
        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tqs4ilbobv1qq *)gm_out) + 24, t161);
        gm_toceffb697p0u t162 = gm_l22;
        GM_ST(gm_toceffb697p0u, (uint8_t *)&(*(gm_tqs4ilbobv1qq *)gm_out) + 40, t162);
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
inline __attribute__((aligned(4))) uint8_t gm_f_column_covers_gn61qhr(gm_b16_8 a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_column_covers_gn61qhr F;
    gm_b16_8 gm_l0; /* column */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 383u);
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
                    gm_t8oufsr65o5ha t15 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 136));
                    F.l4 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        gm_taevtln836oa8 t19 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)&F.l4) + 8) + 8));
                        F.l5 = t19;
                        void * t20 = (void *)((uint8_t *)&F.l5);
                        void * t21 = gm_l1;
                        uint8_t t22 = gm_f_covers_59m49eg(t20, t21);
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
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 434, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_covers_59m49eg(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_covers_59m49eg F;
    void * gm_l0; /* head */
    void * gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 258u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
        if (t4 != ((uint8_t)7ULL)) goto ie2;
        gm_tbci5ospk5nnn t5 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t3) + 8) + 0));
        F.l2 = t5;
        gm_tbci5ospk5nnn t6 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t3) + 8) + 48));
        F.l3 = t6;
        void * t7 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 0));
        if (t8 != ((uint8_t)7ULL)) goto ie2;
        gm_tbci5ospk5nnn t9 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t7) + 8) + 0));
        F.l4 = t9;
        gm_tbci5ospk5nnn t10 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t7) + 8) + 48));
        F.l5 = t10;
        {
            uint8_t t12 = 0;
            int32_t t13 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
            uint8_t t14 = (t13 <= ((int32_t)0LL));
            if (t14) {
                int32_t t15 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                uint8_t t16 = (t15 <= ((int32_t)0LL));
                t12 = t16;
            } else t12 = 0;
            uint8_t t17 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        uint8_t t20 = gm_f_ctor_eq_fhbfduc(t18, t19);
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ctor_eq_fhbfduc(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ctor_eq_fhbfduc F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* x */
    uint32_t gm_l3; /* x */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 431u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn15;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t1) + 8));
    gm_l2 = t17;
    void * t18 = gm_l1;
    uint8_t t19 = gm_l2;
    uint8_t t20 = (((GM_LD(uint8_t, ((uint8_t *)t18) + 0) == ((uint8_t)0ULL)) && ((GM_LD(uint8_t, (((uint8_t *)t18) + 8)) != 0) == (t19 != 0))));
    t2 = t20;
    goto me3;
mn15:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    void * t23 = gm_l1;
    uint8_t t24 = ((GM_LD(uint8_t, ((uint8_t *)t23) + 0) == ((uint8_t)1ULL)));
    t2 = t24;
    goto me3;
mn21:;
ma6:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    void * t27 = gm_l1;
    uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)t27) + 0) == ((uint8_t)2ULL)));
    t2 = t28;
    goto me3;
mn25:;
ma7:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t30 != ((uint8_t)3ULL)) goto mn29;
    void * t31 = gm_l1;
    uint8_t t32 = ((GM_LD(uint8_t, ((uint8_t *)t31) + 0) == ((uint8_t)3ULL)));
    t2 = t32;
    goto me3;
mn29:;
ma8:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t34 != ((uint8_t)4ULL)) goto mn33;
    void * t35 = gm_l1;
    uint8_t t36 = ((GM_LD(uint8_t, ((uint8_t *)t35) + 0) == ((uint8_t)4ULL)));
    t2 = t36;
    goto me3;
mn33:;
ma9:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t38 != ((uint8_t)5ULL)) goto mn37;
    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)t1) + 8));
    gm_l3 = t39;
    void * t40 = gm_l1;
    uint32_t t41 = gm_l3;
    uint8_t t42 = (((GM_LD(uint8_t, ((uint8_t *)t40) + 0) == ((uint8_t)5ULL)) && (GM_LD(uint32_t, (((uint8_t *)t40) + 8)) == t41)));
    t2 = t42;
    goto me3;
mn37:;
ma10:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t44 != ((uint8_t)6ULL)) goto mn43;
    void * t45 = gm_l1;
    uint8_t t46 = ((GM_LD(uint8_t, ((uint8_t *)t45) + 0) == ((uint8_t)6ULL)));
    t2 = t46;
    goto me3;
mn43:;
ma11:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t48 != ((uint8_t)7ULL)) goto mn47;
    gm_tbci5ospk5nnn t49 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t1) + 8) + 0));
    F.l4 = t49;
    gm_tbci5ospk5nnn t50 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t1) + 8) + 48));
    F.l5 = t50;
    uint8_t t51;
    void * t53 = gm_l1;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t53) + 0));
    if (t54 != ((uint8_t)7ULL)) goto ie52;
    gm_tbci5ospk5nnn t55 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t53) + 8) + 0));
    F.l6 = t55;
    gm_tbci5ospk5nnn t56 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t53) + 8) + 48));
    F.l7 = t56;
    uint8_t t58 = 0;
    uint8_t t59 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
    if (t59) {
        uint8_t t60 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
        t58 = t60;
    } else t58 = 0;
    uint8_t t61 = t58;
    t51 = t61;
    goto ix52;
ie52:;
    uint8_t t63 = ((uint8_t)0);
    t51 = t63;
ix52:;
    t2 = t51;
    goto me3;
mn47:;
ma12:;
    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t65 != ((uint8_t)8ULL)) goto mn64;
    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_l8 = t66;
    uint8_t t67;
    void * t69 = gm_l1;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 0));
    if (t70 != ((uint8_t)8ULL)) goto ie68;
    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t69) + 8));
    gm_l9 = t71;
    gm_b16_8 t73 = gm_l8;
    gm_b16_8 t74 = gm_l9;
    uint8_t t75 = (gm_eq0iqiubfm7e(((uint8_t *)&t73), ((uint8_t *)&t74)));
    uint8_t t76 = t75;
    t67 = t76;
    goto ix68;
ie68:;
    uint8_t t78 = ((uint8_t)0);
    t67 = t78;
ix68:;
    t2 = t67;
    goto me3;
mn64:;
ma13:;
    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 322, 58);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_word_n4uc7oo(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_word_n4uc7oo F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t3;
        }
        {
            uint64_t t4 = gm_l4;
            uint64_t t5 = gm_l3;
            uint8_t t6 = (t4 <= t5);
            gm_l5 = t6;
        }
        {
            for (;;) {
                uint8_t t8 = gm_l5;
                if (!t8) goto lb7;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l6 = t14;
                    {
                        uint64_t t17 = gm_l6;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t19 = (t17 < t18);
                        if (!t19) goto ie16;
                        uint64_t t20 = gm_l6;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_src8lmgjh8bsvgp, 92, 42);
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t21 * 1));
                        uint8_t t24 = gm_f_is_name_char_eanps9t(t23);
                        if (!t24) goto ie16;
                        {
                            goto lc7;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    {
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t29 = gm_l2;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = gm_l6;
                        uint64_t t32 = (uintptr_t)t31;
                        if (t30 > t32 || t32 > (uintptr_t)t27) gm_trap_slice(t30, t32, t27, gm_src8lmgjh8bsvgp, 93, 12);
                        gm_b16_8 t33;
                        GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t28 + t30 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t30));
                        gm_b16_8 t34 = gm_l1;
                        uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                        if (!t35) goto ie26;
                        {
                            uint8_t t37 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        uint64_t t38 = gm_l6;
                        uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                        if (o40) gm_trap("integer overflow in `+`", gm_src8lmgjh8bsvgp, 94, 17);
                        uint64_t t41 = r39;
                        gm_l2 = t41;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 91, 5);
me12:;
                }
lc7:;
                uint64_t t43 = gm_l4;
                uint64_t t44 = gm_l3;
                uint8_t t45 = ((t43 == t44));
                if (!t45) goto ie42;
                {
                    gm_l5 = ((uint8_t)0);
                }
                goto ix42;
ie42:;
                {
                    uint64_t t48 = gm_l4;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l4 = t51;
                }
ix42:;
            }
lb7:;
        }
    }
    {
        uint8_t t52 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_int_bounds_7npnb0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2) {
    struct gm_fr_int_bounds_7npnb0b F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* ptr_bits */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* bits */
    uint8_t gm_l5; /* sg */
    uint16_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 748u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l5 = t14;
        uint16_t t15 = GM_LD(uint16_t, ((((uint8_t *)&t4) + 8) + 2));
        gm_l6 = t15;
        {
            uint8_t t17 = gm_l5;
            gm_l3 = t17;
        }
        {
            uint16_t t18 = gm_l6;
            uint64_t t19 = (uint64_t)t18;
            gm_l4 = t19;
        }
        goto me5;
mn12:;
ma7:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t21 != ((uint8_t)1ULL)) goto mn20;
        {
            uint32_t t23 = gm_l2;
            uint64_t t24 = (uint64_t)t23;
            gm_l4 = t24;
        }
        goto me5;
mn20:;
ma8:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t26 != ((uint8_t)2ULL)) goto mn25;
        {
            gm_l3 = ((uint8_t)1);
        }
        {
            uint32_t t28 = gm_l2;
            uint64_t t29 = (uint64_t)t28;
            gm_l4 = t29;
        }
        goto me5;
mn25:;
ma9:;
        gm_tvegkb6ouf3m0 t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
        goto me5;
mn30:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 298, 5);
me5:;
    }
    {
        uint8_t t34 = gm_l3;
        if (!t34) goto ie33;
        {
            gm_tbci5ospk5nnn t36;
            gm_f_one_08qg1op((void *)&t36, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t37 = gm_l4;
            uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 311, 34);
            uint64_t t40 = r38;
            gm_f_shl_qqecl81((void *)&F.l7, (void *)NULL, cx, NULL, &t36, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tngtuiuskn938 t41;
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t41 + 0)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t42 = F.l7;
            gm_tbci5ospk5nnn t43;
            gm_f_one_08qg1op((void *)&t43, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t41 + 48)), (void *)NULL, cx, NULL, &t42, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 97, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 98, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 100, 0);
            ((uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out))[96] = 1;
            GM_ST(gm_tngtuiuskn938, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 0, t41);
            goto gm_fdone;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        gm_tngtuiuskn938 t44;
        gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t44 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t45;
        gm_f_one_08qg1op((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t46 = gm_l4;
        gm_tbci5ospk5nnn t47;
        gm_f_shl_qqecl81((void *)&t47, (void *)NULL, cx, NULL, &t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t48;
        gm_f_one_08qg1op((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t44 + 48)), (void *)NULL, cx, NULL, &t47, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 97, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 98, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 100, 0);
        ((uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out))[96] = 1;
        GM_ST(gm_tngtuiuskn938, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 0, t44);
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
inline __attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_irrefutable_lpkmeii F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 559u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_furncbs((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            void * t4 = gm_l1;
            gm_b16_8 t5 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t8oufsr65o5ha t17 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 136));
                    F.l5 = t17;
                    {
                        gm_f_list_99597qp((void *)&F.l6, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        void * t19 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t20 = gm_f_push_h9qsb4d(t19, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l5)));
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src8lmgjh8bsvgp, 132, 9);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    {
                        void * t22 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_tftb1odc739t5 t24 = gm_f_push_s94euqb(t22, t23);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src8lmgjh8bsvgp, 133, 9);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 130, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        gm_f_list_99597qp((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l7);
        void * t32 = gm_l1;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
        gm_t8oufsr65o5ha t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 8), t33);
        gm_tftb1odc739t5 t35 = gm_f_push_h9qsb4d(t31, &t34);
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_src8lmgjh8bsvgp, 136, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    {
        void * t37 = gm_l0;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_th2lqbo77pvsc t42;
        gm_f_useful_ge1iad5((void *)&t42, (void *)NULL, cx, NULL, t37, t39, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43;
        if (((((uint8_t *)&t42))[16] != 0)) goto mn45;
        t43 = ((uint8_t)1);
        goto me44;
mn45:;
        t43 = ((uint8_t)0);
        goto me44;
mn46:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 137, 12);
me44:;
        uint8_t t47 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_name_char_eanps9t(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_name_char_eanps9t F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    uint8_t t4 = 0;
    {
        uint8_t t6 = gm_l0;
        gm_l1 = t6;
    }
    uint8_t t7 = 0;
    uint8_t t8 = gm_l1;
    uint8_t t9 = (((uint8_t)97ULL) <= t8);
    if (t9) {
        uint8_t t10 = gm_l1;
        uint8_t t11 = (t10 <= ((uint8_t)122ULL));
        t7 = t11;
    } else t7 = 0;
    uint8_t t12 = t7;
    if (!t12) {
        {
            uint8_t t14 = gm_l0;
            gm_l2 = t14;
        }
        uint8_t t15 = 0;
        uint8_t t16 = gm_l2;
        uint8_t t17 = (((uint8_t)65ULL) <= t16);
        if (t17) {
            uint8_t t18 = gm_l2;
            uint8_t t19 = (t18 <= ((uint8_t)90ULL));
            t15 = t19;
        } else t15 = 0;
        uint8_t t20 = t15;
        t4 = t20;
    } else t4 = 1;
    if (!t4) {
        {
            uint8_t t22 = gm_l0;
            gm_l3 = t22;
        }
        uint8_t t23 = 0;
        uint8_t t24 = gm_l3;
        uint8_t t25 = (((uint8_t)48ULL) <= t24);
        if (t25) {
            uint8_t t26 = gm_l3;
            uint8_t t27 = (t26 <= ((uint8_t)57ULL));
            t23 = t27;
        } else t23 = 0;
        uint8_t t28 = t23;
        t3 = t28;
    } else t3 = 1;
    if (!t3) {
        uint8_t t29 = gm_l0;
        uint8_t t30 = ((t29 == ((uint8_t)95ULL)));
        t2 = t30;
    } else t2 = 1;
    if (!t2) {
        uint8_t t31 = gm_l0;
        uint8_t t32 = (t31 >= ((uint8_t)128ULL));
        t1 = t32;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_product_5gakcnb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_lower_product_5gakcnb F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l4; /* no_subs */
    gm_b16_8 gm_l5; /* ps */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* q */
    void * gm_l9; /* e */
    uint8_t gm_l10; /* b */
    gm_b16_8 gm_l12; /* b */
    uint8_t gm_l15; /* inclusive */
    void * gm_l17; /* q */
    void * gm_l18; /* q */
    void * gm_l19; /* q */
    uint32_t gm_l20; /* v */
    gm_b16_8 gm_l21; /* subs_tys */
    uint32_t gm_l23; /* v */
    void * gm_l24; /* q */
    gm_b16_8 gm_l25; /* fs */
    gm_b16_8 gm_l26; /* tys */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t88rcucg1b7uk gm_l30; /* t */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l34; /* f */
    gm_b16_8 gm_l35; /* options */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l39; /* c */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l46; /* subs */
    GM_STACK_CHECK(&F, 6630u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        gm_f_list_99597qp((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_f_empty_slice_6rs1n5s();
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l1;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma8; default: goto ma18; }
ma7:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t23 != ((uint8_t)0ULL)) goto pa22;
        goto po21;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t25 != ((uint8_t)1ULL)) goto pa24;
        goto po21;
pa24:;
        goto mn20;
po21:;
        void * t26 = (void *)((uint8_t *)&F.l3);
        gm_t88rcucg1b7uk t27 = gm_l2;
        gm_t8oufsr65o5ha t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 8), t27);
        gm_tftb1odc739t5 t29 = gm_f_push_h9qsb4d(t26, &t28);
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_src8lmgjh8bsvgp, 155, 28);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
        goto me6;
mn20:;
ma8:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t32 != ((uint8_t)10ULL)) goto mn31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 8) + 8));
        gm_l5 = t33;
        {
            gm_b16_8 t35 = gm_l5;
            gm_l6 = t35;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t37 = gm_l7;
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t39 = (t37 < t38);
                if (!t39) goto lb36;
                {
                    uint64_t t42 = gm_l7;
                    uint64_t t43 = (uintptr_t)t42;
                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    void * t45 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t43 * 8));
                    void * t48 = GM_LD(void *, ((uint8_t *)&t45));
                    gm_l8 = t48;
                    {
                        void * t50 = (void *)((uint8_t *)&F.l3);
                        void * t51 = gm_l0;
                        void * t52 = gm_l8;
                        gm_b16_8 t53 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_ll40;
                        }
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                        gm_tftb1odc739t5 t55 = gm_f_extend_eht4gub(t50, t54);
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src8lmgjh8bsvgp, 156, 34);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 156, 20);
me46:;
                }
lc36:;
                {
                    uint64_t t58 = gm_l7;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l7 = t61;
                }
            }
gm_ll40:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
            }
            goto gm_lad;
lb36:;
        }
        goto me6;
mn31:;
ma9:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t63 != ((uint8_t)2ULL)) goto mn62;
        void * t64 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l9 = t64;
        void * t65 = gm_l9;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
        switch (t72) { case ((uint8_t)0ULL): goto ma68; case ((uint8_t)1ULL): goto ma70; case ((uint8_t)2ULL): goto ma70; case ((uint8_t)3ULL): goto ma70; case ((uint8_t)4ULL): goto ma67; case ((uint8_t)5ULL): goto ma69; case ((uint8_t)6ULL): goto ma70; case ((uint8_t)7ULL): goto ma70; case ((uint8_t)8ULL): goto ma70; case ((uint8_t)9ULL): goto ma70; case ((uint8_t)10ULL): goto ma70; case ((uint8_t)11ULL): goto ma70; case ((uint8_t)12ULL): goto ma70; case ((uint8_t)13ULL): goto ma70; case ((uint8_t)14ULL): goto ma70; case ((uint8_t)15ULL): goto ma70; case ((uint8_t)16ULL): goto ma70; case ((uint8_t)17ULL): goto ma70; case ((uint8_t)18ULL): goto ma70; case ((uint8_t)19ULL): goto ma70; case ((uint8_t)20ULL): goto ma70; case ((uint8_t)21ULL): goto ma70; case ((uint8_t)22ULL): goto ma70; case ((uint8_t)23ULL): goto ma70; case ((uint8_t)24ULL): goto ma70; case ((uint8_t)25ULL): goto ma70; case ((uint8_t)26ULL): goto ma70; case ((uint8_t)27ULL): goto ma70; case ((uint8_t)28ULL): goto ma70; case ((uint8_t)29ULL): goto ma70; case ((uint8_t)30ULL): goto ma70; case ((uint8_t)31ULL): goto ma70; case ((uint8_t)32ULL): goto ma70; case ((uint8_t)33ULL): goto ma70; case ((uint8_t)34ULL): goto ma70; case ((uint8_t)35ULL): goto ma70; case ((uint8_t)36ULL): goto ma70; case ((uint8_t)37ULL): goto ma70; case ((uint8_t)38ULL): goto ma70; case ((uint8_t)39ULL): goto ma70; case ((uint8_t)40ULL): goto ma70; case ((uint8_t)41ULL): goto ma70; case ((uint8_t)42ULL): goto ma70; case ((uint8_t)43ULL): goto ma70; case ((uint8_t)44ULL): goto ma70; case ((uint8_t)45ULL): goto ma70; case ((uint8_t)46ULL): goto ma70; case ((uint8_t)47ULL): goto ma70; case ((uint8_t)48ULL): goto ma70; case ((uint8_t)49ULL): goto ma70; case ((uint8_t)50ULL): goto ma70; case ((uint8_t)51ULL): goto ma70; case ((uint8_t)52ULL): goto ma70; case ((uint8_t)53ULL): goto ma70; case ((uint8_t)54ULL): goto ma70; case ((uint8_t)55ULL): goto ma70; case ((uint8_t)56ULL): goto ma70; case ((uint8_t)57ULL): goto ma70; case ((uint8_t)58ULL): goto ma70; default: goto ma70; }
ma67:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
        if (t74 != ((uint8_t)4ULL)) goto mn73;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 8));
        gm_l10 = t75;
        void * t76 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t77;
        memset((uint8_t *)&t77 + 4, 0, 4);
        gm_t88rcucg1b7uk t78 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t77 + 0, t78);
        uint8_t t79 = gm_l10;
        gm_taevtln836oa8 t80;
        memset(&t80, 0, sizeof t80);
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t80 + 8), t79);
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t77 + 8, t80);
        gm_b16_8 t81 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t77 + 112, t81);
        gm_t8oufsr65o5ha t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t82 + 8), t77);
        gm_tftb1odc739t5 t83 = gm_f_push_h9qsb4d(t76, &t82);
        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_src8lmgjh8bsvgp, 158, 25);
        gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
        goto me66;
mn73:;
ma68:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
        if (t86 != ((uint8_t)0ULL)) goto mn85;
        gm_tbci5ospk5nnn t87 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t65) + 8) + 8));
        F.l11 = t87;
        void * t88 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t89;
        memset((uint8_t *)&t89 + 4, 0, 4);
        gm_t88rcucg1b7uk t90 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t89 + 0, t90);
        gm_tngtuiuskn938 t91;
        memcpy((uint8_t *)&t91 + 0, ((uint8_t *)&F.l11), 48);
        memcpy((uint8_t *)&t91 + 48, ((uint8_t *)&F.l11), 48);
        gm_taevtln836oa8 t92;
        GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)7ULL));
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t92 + 8), t91);
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t89 + 8, t92);
        gm_b16_8 t93 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t89 + 112, t93);
        gm_t8oufsr65o5ha t94;
        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t94 + 8), t89);
        gm_tftb1odc739t5 t95 = gm_f_push_h9qsb4d(t88, &t94);
        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_src8lmgjh8bsvgp, 159, 24);
        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
        goto me66;
mn85:;
ma69:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
        if (t98 != ((uint8_t)5ULL)) goto mn97;
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)t65) + 8) + 8));
        gm_l12 = t99;
        void * t100 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t101;
        memset((uint8_t *)&t101 + 4, 0, 4);
        gm_t88rcucg1b7uk t102 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t101 + 0, t102);
        gm_b16_8 t103 = gm_l12;
        gm_taevtln836oa8 t104;
        memset(&t104, 0, sizeof t104);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)8ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t104 + 8), t103);
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t101 + 8, t104);
        gm_b16_8 t105 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t101 + 112, t105);
        gm_t8oufsr65o5ha t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t106 + 8), t101);
        gm_tftb1odc739t5 t107 = gm_f_push_h9qsb4d(t100, &t106);
        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_src8lmgjh8bsvgp, 160, 24);
        gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
        goto me66;
mn97:;
ma70:;
        void * t110 = (void *)((uint8_t *)&F.l3);
        gm_t88rcucg1b7uk t111 = gm_l2;
        gm_t8oufsr65o5ha t112;
        memset(&t112, 0, sizeof t112);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112 + 8), t111);
        gm_tftb1odc739t5 t113 = gm_f_push_h9qsb4d(t110, &t112);
        if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_src8lmgjh8bsvgp, 161, 18);
        gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
        goto me66;
mn109:;
ma71:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 157, 22);
me66:;
        goto me6;
mn62:;
ma10:;
        uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t116 != ((uint8_t)3ULL)) goto mn115;
        gm_tbci5ospk5nnn t117 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t5) + 8) + 8) + 0));
        F.l13 = t117;
        gm_tbci5ospk5nnn t118 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t5) + 8) + 8) + 48));
        F.l14 = t118;
        uint8_t t119 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 8) + 96));
        gm_l15 = t119;
        {
            gm_tbci5ospk5nnn t121;
            uint8_t t123 = gm_l15;
            if (!t123) goto ie122;
            gm_tbci5ospk5nnn t125 = F.l14;
            gm_tbci5ospk5nnn t126 = t125;
            t121 = t126;
            goto ix122;
ie122:;
            gm_tbci5ospk5nnn t128 = F.l14;
            gm_tbci5ospk5nnn t129;
            gm_f_one_08qg1op((void *)&t129, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t130;
            gm_f_sub_pktm5cr((void *)&t130, (void *)NULL, cx, NULL, &t128, &t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t131 = t130;
            t121 = t131;
ix122:;
            F.l16 = t121;
        }
        {
            void * t132 = (void *)((uint8_t *)&F.l3);
            gm_tbq1t03a53gpo t133;
            memset((uint8_t *)&t133 + 4, 0, 4);
            gm_t88rcucg1b7uk t134 = gm_l2;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t133 + 0, t134);
            gm_tngtuiuskn938 t135;
            memcpy((uint8_t *)&t135 + 0, ((uint8_t *)&F.l13), 48);
            memcpy((uint8_t *)&t135 + 48, ((uint8_t *)&F.l16), 48);
            gm_taevtln836oa8 t136;
            GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)7ULL));
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t136 + 8), t135);
            GM_ST(gm_taevtln836oa8, (uint8_t *)&t133 + 8, t136);
            gm_b16_8 t137 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t133 + 112, t137);
            gm_t8oufsr65o5ha t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t138 + 8), t133);
            gm_tftb1odc739t5 t139 = gm_f_push_h9qsb4d(t132, &t138);
            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_src8lmgjh8bsvgp, 165, 13);
            gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
        }
        goto me6;
mn115:;
ma11:;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t142 != ((uint8_t)4ULL)) goto mn141;
        void * t143 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t144;
        memset((uint8_t *)&t144 + 4, 0, 4);
        gm_t88rcucg1b7uk t145 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t144 + 0, t145);
        gm_taevtln836oa8 t146;
        memset(&t146, 0, sizeof t146);
        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)1ULL));
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t144 + 8, t146);
        gm_b16_8 t147 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 112, t147);
        gm_t8oufsr65o5ha t148;
        GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t148 + 8), t144);
        gm_tftb1odc739t5 t149 = gm_f_push_h9qsb4d(t143, &t148);
        if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_src8lmgjh8bsvgp, 167, 18);
        gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
        goto me6;
mn141:;
ma12:;
        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t152 != ((uint8_t)5ULL)) goto mn151;
        void * t153 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l17 = t153;
        gm_t88rcucg1b7uk t154 = gm_l2;
        gm_taevtln836oa8 t155;
        memset(&t155, 0, sizeof t155);
        GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)2ULL));
        void * t156 = gm_l0;
        void * t157 = gm_l17;
        gm_b16_8 t158 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t159 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t160 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t154, &t155, t158, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn151:;
ma13:;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t162 != ((uint8_t)6ULL)) goto mn161;
        void * t163 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l18 = t163;
        gm_t88rcucg1b7uk t164 = gm_l2;
        gm_taevtln836oa8 t165;
        memset(&t165, 0, sizeof t165);
        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)3ULL));
        void * t166 = gm_l0;
        void * t167 = gm_l18;
        gm_b16_8 t168 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t166, t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t169 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t170 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t164, &t165, t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn161:;
ma14:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t172 != ((uint8_t)7ULL)) goto mn171;
        void * t173 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l19 = t173;
        gm_t88rcucg1b7uk t174 = gm_l2;
        gm_taevtln836oa8 t175;
        memset(&t175, 0, sizeof t175);
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)4ULL));
        void * t176 = gm_l0;
        void * t177 = gm_l19;
        gm_b16_8 t178 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t176, t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t179 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t180 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t174, &t175, t178, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn171:;
ma15:;
        uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t182 != ((uint8_t)8ULL)) goto mn181;
        uint32_t t183 = GM_LD(uint32_t, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l20 = t183;
        if ((GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8)) != 0)) goto mn181;
        {
            void * t185 = gm_l0;
            gm_t88rcucg1b7uk t186 = gm_l2;
            uint32_t t187 = gm_l20;
            gm_taevtln836oa8 t188;
            memset(&t188, 0, sizeof t188);
            GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)5ULL));
            GM_ST(uint32_t, ((uint8_t *)&t188 + 8), t187);
            void * t189 = (void *)((uint8_t *)&t188);
            gm_b16_8 t190 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t185, t186, t189);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t190;
        }
        {
            gm_f_list_99597qp((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t192 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
            uint8_t t193 = (t192 > ((uint64_t)0ULL));
            if (!t193) goto ie191;
            {
                void * t195 = (void *)((uint8_t *)&F.l22);
                uint64_t t196 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                if (t196 >= (uintptr_t)(t197)) gm_trap_index(t196, t197, gm_src8lmgjh8bsvgp, 174, 51);
                gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t196 * 4));
                gm_t8oufsr65o5ha t199;
                memset(&t199, 0, sizeof t199);
                GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t199 + 8), t198);
                gm_tftb1odc739t5 t200 = gm_f_push_h9qsb4d(t195, &t199);
                if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 1, gm_src8lmgjh8bsvgp, 174, 35);
                gm_b0_1 t201 = GM_LD(gm_b0_1, (((uint8_t *)&t200) + 1));
            }
            goto ix191;
ie191:;
ix191:;
        }
        {
            void * t202 = (void *)((uint8_t *)&F.l3);
            gm_tbq1t03a53gpo t203;
            memset((uint8_t *)&t203 + 4, 0, 4);
            gm_t88rcucg1b7uk t204 = gm_l2;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t203 + 0, t204);
            uint32_t t205 = gm_l20;
            gm_taevtln836oa8 t206;
            memset(&t206, 0, sizeof t206);
            GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)5ULL));
            GM_ST(uint32_t, ((uint8_t *)&t206 + 8), t205);
            GM_ST(gm_taevtln836oa8, (uint8_t *)&t203 + 8, t206);
            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t203 + 112, t207);
            gm_t8oufsr65o5ha t208;
            GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t208 + 8), t203);
            gm_tftb1odc739t5 t209 = gm_f_push_h9qsb4d(t202, &t208);
            if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 1, gm_src8lmgjh8bsvgp, 175, 13);
            gm_b0_1 t210 = GM_LD(gm_b0_1, (((uint8_t *)&t209) + 1));
        }
        goto me6;
mn181:;
ma16:;
        uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t212 != ((uint8_t)8ULL)) goto mn211;
        uint32_t t213 = GM_LD(uint32_t, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l23 = t213;
        if (!(GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8)) != 0)) goto mn211;
        void * t214 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l24 = t214;
        gm_t88rcucg1b7uk t215 = gm_l2;
        uint32_t t216 = gm_l23;
        gm_taevtln836oa8 t217;
        memset(&t217, 0, sizeof t217);
        GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)5ULL));
        GM_ST(uint32_t, ((uint8_t *)&t217 + 8), t216);
        void * t218 = gm_l0;
        void * t219 = gm_l24;
        gm_b16_8 t220 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t218, t219);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t221 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t222 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t215, &t217, t220, t221);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn211:;
ma17:;
        uint8_t t224 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t224 != ((uint8_t)9ULL)) goto mn223;
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 8) + 8));
        gm_l25 = t225;
        {
            void * t227 = gm_l0;
            gm_t88rcucg1b7uk t228 = gm_l2;
            gm_taevtln836oa8 t229;
            memset(&t229, 0, sizeof t229);
            GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)6ULL));
            void * t230 = (void *)((uint8_t *)&t229);
            gm_b16_8 t231 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t227, t228, t230);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t231;
        }
        {
            gm_f_list_99597qp((void *)&F.l27, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t233 = gm_l26;
                gm_l28 = t233;
            }
            {
                gm_l29 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t235 = gm_l29;
                    uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    uint8_t t237 = (t235 < t236);
                    if (!t237) goto lb234;
                    {
                        uint64_t t240 = gm_l29;
                        uint64_t t241 = (uintptr_t)t240;
                        uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                        gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t241 * 4));
                        gm_l30 = t245;
                        {
                            void * t247 = (void *)((uint8_t *)&F.l27);
                            gm_t88rcucg1b7uk t248 = gm_l30;
                            gm_t8oufsr65o5ha t249;
                            memset(&t249, 0, sizeof t249);
                            GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t249 + 8), t248);
                            gm_tftb1odc739t5 t250 = gm_f_push_h9qsb4d(t247, &t249);
                            if (((((uint8_t *)&t250))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t250) + 1, gm_src8lmgjh8bsvgp, 181, 28);
                            gm_b0_1 t251 = GM_LD(gm_b0_1, (((uint8_t *)&t250) + 1));
                        }
                        goto me243;
mn244:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 181, 13);
me243:;
                    }
lc234:;
                    {
                        uint64_t t253 = gm_l29;
                        uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                        uint64_t t256 = r254;
                        gm_l29 = t256;
                    }
                }
lb234:;
            }
        }
        {
            gm_f_list_furncbs((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t257 = (void *)((uint8_t *)&F.l31);
            gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
            gm_tftb1odc739t5 t259 = gm_f_push_s94euqb(t257, t258);
            if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_src8lmgjh8bsvgp, 183, 13);
            gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
        }
        {
            {
                gm_b16_8 t262 = gm_l25;
                gm_l32 = t262;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t264 = gm_l33;
                    uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t266 = (t264 < t265);
                    if (!t266) goto lb263;
                    {
                        uint64_t t269 = gm_l33;
                        uint64_t t270 = (uintptr_t)t269;
                        uint64_t t271 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        gm_ttggq7ng4kp3p t274 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t270 * 16));
                        gm_l34 = t274;
                        {
                            void * t276 = gm_l0;
                            void * t277 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                            gm_b16_8 t278 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t276, t277);
                            if (gm_unw.kind) {
                                goto gm_ll267;
                            }
                            gm_l35 = t278;
                        }
                        {
                            gm_f_list_furncbs((void *)&F.l36, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll267;
                            }
                        }
                        {
                            {
                                gm_b16_8 t280 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_l37 = t280;
                            }
                            {
                                gm_l38 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t282 = gm_l38;
                                    uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                    uint8_t t284 = (t282 < t283);
                                    if (!t284) goto lb281;
                                    {
                                        uint64_t t287 = gm_l38;
                                        uint64_t t288 = (uintptr_t)t287;
                                        uint64_t t289 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                        gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t288 * 16));
                                        gm_l39 = t292;
                                        {
                                            {
                                                gm_b16_8 t295 = gm_l35;
                                                gm_l40 = t295;
                                            }
                                            {
                                                gm_l41 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t297 = gm_l41;
                                                    uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                                    uint8_t t299 = (t297 < t298);
                                                    if (!t299) goto lb296;
                                                    {
                                                        uint64_t t302 = gm_l41;
                                                        uint64_t t303 = (uintptr_t)t302;
                                                        uint64_t t304 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                                        gm_t8oufsr65o5ha t307 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t303 * 136));
                                                        F.l42 = t307;
                                                        {
                                                            gm_f_list_99597qp((void *)&F.l43, (void *)NULL, cx, NULL);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll300;
                                                            }
                                                        }
                                                        {
                                                            void * t309 = (void *)((uint8_t *)&F.l43);
                                                            gm_b16_8 t310 = gm_l39;
                                                            gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&t310));
                                                            gm_tftb1odc739t5 t312 = gm_f_extend_eht4gub(t309, t311);
                                                            if (((((uint8_t *)&t312))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t312) + 1, gm_src8lmgjh8bsvgp, 190, 25);
                                                            gm_b0_1 t313 = GM_LD(gm_b0_1, (((uint8_t *)&t312) + 1));
                                                        }
                                                        {
                                                            uint32_t t315 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 0));
                                                            uint64_t t316 = (uint64_t)t315;
                                                            uint64_t t317 = GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8);
                                                            uint8_t t318 = (t316 < t317);
                                                            if (!t318) goto ie314;
                                                            {
                                                                uint32_t t320 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 0));
                                                                uint64_t t321 = (uint64_t)t320;
                                                                uint64_t t322 = (uintptr_t)t321;
                                                                uint64_t t323 = GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8);
                                                                if (t322 >= (uintptr_t)(t323)) gm_trap_index(t322, t323, gm_src8lmgjh8bsvgp, 191, 60);
                                                                gm_t8oufsr65o5ha t324 = F.l42;
                                                                GM_ST(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l43) + 0)) + t322 * 136), t324);
                                                            }
                                                            goto ix314;
ie314:;
ix314:;
                                                        }
                                                        {
                                                            void * t325 = (void *)((uint8_t *)&F.l36);
                                                            gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                                                            gm_tftb1odc739t5 t327 = gm_f_push_s94euqb(t325, t326);
                                                            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 1, gm_src8lmgjh8bsvgp, 192, 25);
                                                            gm_b0_1 t328 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
                                                        }
                                                        goto me305;
mn306:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 188, 21);
me305:;
                                                    }
lc296:;
                                                    {
                                                        uint64_t t330 = gm_l41;
                                                        uint64_t r331; bool o332 = __builtin_add_overflow(t330, ((uint64_t)1ULL), &r331);
                                                        uint64_t t333 = r331;
                                                        gm_l41 = t333;
                                                    }
                                                }
gm_ll300:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb296; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc296; }
                                                }
                                                goto gm_ll285;
lb296:;
                                            }
                                        }
                                        goto me290;
mn291:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 187, 17);
me290:;
                                    }
lc281:;
                                    {
                                        uint64_t t335 = gm_l38;
                                        uint64_t r336; bool o337 = __builtin_add_overflow(t335, ((uint64_t)1ULL), &r336);
                                        uint64_t t338 = r336;
                                        gm_l38 = t338;
                                    }
                                }
gm_ll285:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb281; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc281; }
                                }
                                goto gm_ll267;
lb281:;
                            }
                        }
                        {
                            gm_trqfvmjtt4sij t339 = F.l36;
                            F.l31 = t339;
                        }
                        goto me272;
mn273:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 184, 13);
me272:;
                    }
lc263:;
                    {
                        uint64_t t341 = gm_l33;
                        uint64_t r342; bool o343 = __builtin_add_overflow(t341, ((uint64_t)1ULL), &r342);
                        uint64_t t344 = r342;
                        gm_l33 = t344;
                    }
                }
gm_ll267:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb263; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc263; }
                }
                goto gm_lad;
lb263:;
            }
        }
        {
            {
                gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                gm_l44 = t346;
            }
            {
                gm_l45 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t348 = gm_l45;
                    uint64_t t349 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    uint8_t t350 = (t348 < t349);
                    if (!t350) goto lb347;
                    {
                        uint64_t t353 = gm_l45;
                        uint64_t t354 = (uintptr_t)t353;
                        uint64_t t355 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t354 * 16));
                        gm_l46 = t358;
                        {
                            void * t360 = (void *)((uint8_t *)&F.l3);
                            gm_tbq1t03a53gpo t361;
                            memset((uint8_t *)&t361 + 4, 0, 4);
                            gm_t88rcucg1b7uk t362 = gm_l2;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t361 + 0, t362);
                            gm_taevtln836oa8 t363;
                            memset(&t363, 0, sizeof t363);
                            GM_ST(uint8_t, ((uint8_t *)&t363 + 0), ((uint8_t)6ULL));
                            GM_ST(gm_taevtln836oa8, (uint8_t *)&t361 + 8, t363);
                            gm_b16_8 t364 = gm_l46;
                            GM_ST(gm_b16_8, (uint8_t *)&t361 + 112, t364);
                            gm_t8oufsr65o5ha t365;
                            GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t365 + 8), t361);
                            gm_tftb1odc739t5 t366 = gm_f_push_h9qsb4d(t360, &t365);
                            if (((((uint8_t *)&t366))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t366) + 1, gm_src8lmgjh8bsvgp, 197, 40);
                            gm_b0_1 t367 = GM_LD(gm_b0_1, (((uint8_t *)&t366) + 1));
                        }
                        goto me356;
mn357:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 197, 13);
me356:;
                    }
lc347:;
                    {
                        uint64_t t369 = gm_l45;
                        uint64_t r370; bool o371 = __builtin_add_overflow(t369, ((uint64_t)1ULL), &r370);
                        uint64_t t372 = r370;
                        gm_l45 = t372;
                    }
                }
lb347:;
            }
        }
        goto me6;
mn223:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 154, 5);
me6:;
    }
    {
        gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t374 = 0;
        F.ret = t373;
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_nested_present_binding_r48665i(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nested_present_binding_r48665i F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* top */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    gm_b16_8 gm_l5; /* alts */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* q */
    gm_tl874t2atnlh3 gm_l9; /* at */
    void * gm_l10; /* q */
    gm_b16_8 gm_l11; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l14; /* f */
    gm_tl874t2atnlh3 gm_l15; /* at */
    GM_STACK_CHECK(&F, 733u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma6; default: goto ma10; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)5ULL)) goto pa15;
        void * t17 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t17;
        goto po14;
pa15:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)6ULL)) goto pa18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t20;
        goto po14;
pa18:;
        goto mn13;
po14:;
        {
            uint8_t t23 = gm_l1;
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto ie22;
            void * t25 = gm_l2;
            uint8_t t26;
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            if (t29 != ((uint8_t)1ULL)) goto mn28;
            t26 = ((uint8_t)1);
            goto me27;
mn28:;
            t26 = ((uint8_t)0);
            goto me27;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 106, 28);
me27:;
            if (!t26) goto ie22;
            {
                void * t32 = gm_l2;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 120));
                gm_toceffb697p0u t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t34), t33);
                ((uint8_t *)&t34)[12] = 1;
                uint8_t t35 = 0;
                F.ret = t34;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            void * t36 = gm_l2;
            uint8_t t37 = gm_l1;
            gm_toceffb697p0u t38 = gm_f_nested_present_binding_r48665i(t36, t37);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto me3;
mn13:;
ma5:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)7ULL)) goto mn40;
        void * t42 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t42;
        void * t43 = gm_l4;
        uint8_t t44 = gm_l1;
        gm_toceffb697p0u t45 = gm_f_nested_present_binding_r48665i(t43, t44);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me3;
mn40:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)10ULL)) goto mn47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t49;
        {
            {
                gm_b16_8 t52 = gm_l5;
                gm_l6 = t52;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l7;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l7;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        void * t62 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t60 * 8));
                        void * t65 = GM_LD(void *, ((uint8_t *)&t62));
                        gm_l8 = t65;
                        {
                            void * t68 = gm_l8;
                            uint8_t t69 = gm_l1;
                            gm_toceffb697p0u t70 = gm_f_nested_present_binding_r48665i(t68, t69);
                            if (!((((uint8_t *)&t70))[12] != 0)) goto ie67;
                            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t70));
                            gm_l9 = t71;
                            {
                                gm_tl874t2atnlh3 t73 = gm_l9;
                                gm_toceffb697p0u t74;
                                GM_ST(uint8_t, (uint8_t *)&t74 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t74 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t74), t73);
                                ((uint8_t *)&t74)[12] = 1;
                                uint8_t t75 = 0;
                                F.ret = t74;
                                goto gm_fret;
                            }
                            goto ix67;
ie67:;
ix67:;
                        }
                        goto me63;
mn64:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 111, 13);
me63:;
                    }
lc53:;
                    {
                        uint64_t t77 = gm_l7;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        uint64_t t80 = r78;
                        gm_l7 = t80;
                    }
                }
lb53:;
            }
        }
        goto me3;
mn47:;
ma7:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t82 != ((uint8_t)8ULL)) goto mn81;
        if ((GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8)) != 0)) goto mn81;
        goto me3;
mn81:;
ma8:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t85 != ((uint8_t)8ULL)) goto mn84;
        if (!(GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8)) != 0)) goto mn84;
        void * t86 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t86;
        void * t87 = gm_l10;
        gm_toceffb697p0u t88 = gm_f_nested_present_binding_r48665i(t87, ((uint8_t)0));
        uint8_t t89 = 0;
        F.ret = t88;
        goto gm_fret;
        goto me3;
mn84:;
ma9:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t91 != ((uint8_t)9ULL)) goto mn90;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t92;
        {
            {
                gm_b16_8 t95 = gm_l11;
                gm_l12 = t95;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t97 = gm_l13;
                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t99 = (t97 < t98);
                    if (!t99) goto lb96;
                    {
                        uint64_t t102 = gm_l13;
                        uint64_t t103 = (uintptr_t)t102;
                        uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_ttggq7ng4kp3p t107 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t103 * 16));
                        gm_l14 = t107;
                        {
                            void * t110 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                            gm_toceffb697p0u t111 = gm_f_nested_present_binding_r48665i(t110, ((uint8_t)0));
                            if (!((((uint8_t *)&t111))[12] != 0)) goto ie109;
                            gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t111));
                            gm_l15 = t112;
                            {
                                gm_tl874t2atnlh3 t114 = gm_l15;
                                gm_toceffb697p0u t115;
                                GM_ST(uint8_t, (uint8_t *)&t115 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t115 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t115), t114);
                                ((uint8_t *)&t115)[12] = 1;
                                uint8_t t116 = 0;
                                F.ret = t115;
                                goto gm_fret;
                            }
                            goto ix109;
ie109:;
ix109:;
                        }
                        goto me105;
mn106:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 118, 13);
me105:;
                    }
lc96:;
                    {
                        uint64_t t118 = gm_l13;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l13 = t121;
                    }
                }
lb96:;
            }
        }
        goto me3;
mn90:;
ma10:;
        goto me3;
mn122:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 104, 5);
me3:;
    }
    {
        gm_toceffb697p0u t124;
        memset(&t124, 0, sizeof t124);
        uint8_t t125 = 0;
        F.ret = t124;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rebuild_90secge(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_rebuild_90secge F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    uint64_t gm_l2; /* arity */
    gm_b16_8 gm_l3; /* w */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l2;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t4 = (t2 < t3 ? t2 : t3);
        gm_l4 = t4;
    }
    {
        gm_f_list_99597qp((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l5);
        gm_tbq1t03a53gpo t6;
        memset((uint8_t *)&t6 + 4, 0, 4);
        gm_t88rcucg1b7uk t7 = gm_l0;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t6 + 0, t7);
        memcpy((uint8_t *)&t6 + 8, ((uint8_t *)&F.l1), 104);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t10 = gm_l4;
        uint64_t t11 = (uintptr_t)t10;
        if ((uintptr_t)0 > t11 || t11 > (uintptr_t)t8) gm_trap_slice((uintptr_t)0, t11, t8, gm_src8lmgjh8bsvgp, 444, 27);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t9 + (uintptr_t)0 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(t11 - (uintptr_t)0));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 112, t12);
        gm_t8oufsr65o5ha t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t13 + 8), t6);
        gm_tftb1odc739t5 t14 = gm_f_push_h9qsb4d(t5, &t13);
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_src8lmgjh8bsvgp, 444, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        void * t16 = (void *)((uint8_t *)&F.l5);
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t19 = gm_l4;
        uint64_t t20 = (uintptr_t)t19;
        if (t20 > (uintptr_t)t17 || (uintptr_t)t17 > (uintptr_t)t17) gm_trap_slice(t20, (uintptr_t)t17, t17, gm_src8lmgjh8bsvgp, 445, 16);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t18 + t20 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)((uintptr_t)t17 - t20));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_tftb1odc739t5 t23 = gm_f_extend_eht4gub(t16, t22);
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src8lmgjh8bsvgp, 445, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        uint8_t t26 = 0;
        F.ret = t25;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_show_witness_fmsa425(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_witness_fmsa425 F;
    void * gm_l0; /* s */
    void * gm_l1; /* d */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l4; /* subs */
    uint8_t gm_l5; /* b */
    gm_b16_8 gm_l8; /* b */
    uint32_t gm_l10; /* v */
    gm_b16_8 gm_l11; /* name */
    gm_tep2drsl9408b gm_l12; /* uv */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* id */
    gm_b16_8 gm_l19; /* fields */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 2297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
        if (t4 != ((uint8_t)1ULL)) goto ie2;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t3) + 8) + 0));
        gm_l2 = t5;
        gm_taevtln836oa8 t6 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)t3) + 8) + 8));
        F.l3 = t6;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 112));
        gm_l4 = t7;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t19) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma15; default: goto ma18; }
ma10:;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t21 != ((uint8_t)0ULL)) goto mn20;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
            gm_l5 = t22;
            gm_b16_8 t23;
            uint8_t t25 = gm_l5;
            if (!t25) goto ie24;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(4));
            gm_b16_8 t28 = t27;
            t23 = t28;
            goto ix24;
ie24:;
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
            gm_b16_8 t31 = t30;
            t23 = t31;
ix24:;
            uint8_t t32 = 0;
            F.ret = t23;
            goto gm_fret;
            goto me9;
mn20:;
ma11:;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t34 != ((uint8_t)1ULL)) goto mn33;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(4));
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
            goto me9;
mn33:;
ma12:;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t40 != ((uint8_t)2ULL)) goto pa39;
            goto po38;
pa39:;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t42 != ((uint8_t)3ULL)) goto pa41;
            goto po38;
pa41:;
            goto mn37;
po38:;
            void * t43 = gm_l0;
            uint64_t t44 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_src8lmgjh8bsvgp, 455, 47);
            void * t46 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t44 * 136);
            gm_b16_8 t47 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t43, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
            goto me9;
mn37:;
ma13:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t50 != ((uint8_t)4ULL)) goto mn49;
            void * t51 = gm_l0;
            uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_src8lmgjh8bsvgp, 456, 59);
            void * t54 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t52 * 136);
            gm_b16_8 t55 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t51, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t56;
            GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
            gm_tabb710d9mq2e t57;
            gm_f_format_08505jg((void *)&t57, (void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_src8lmgjh8bsvgp, 456, 24);
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
            goto me9;
mn49:;
ma14:;
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t62 != ((uint8_t)7ULL)) goto mn61;
            gm_tbci5ospk5nnn t63 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l3) + 8) + 0));
            F.l6 = t63;
            gm_tbci5ospk5nnn t64 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l3) + 8) + 48));
            F.l7 = t64;
            {
                uint8_t t67 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (!t67) goto ie66;
                {
                    gm_b16_8 t69 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t70 = 0;
                    F.ret = t69;
                    goto gm_fret;
                }
                goto ix66;
ie66:;
ix66:;
            }
            {
                gm_t6j6smj7qoctu t71;
                gm_b16_8 t72 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
                gm_b16_8 t73 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t73);
                gm_tabb710d9mq2e t74;
                gm_f_format_hj7ahc4((void *)&t74, (void *)NULL, cx, NULL, &t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_src8lmgjh8bsvgp, 459, 20);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto me9;
mn61:;
ma15:;
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t79 != ((uint8_t)8ULL)) goto mn78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_l8 = t80;
            {
                gm_f_list_uvfu1k1((void *)&F.l9, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t82 = gm_l8;
                void * t83 = (void *)((uint8_t *)&F.l9);
                gm_b0_1 t84 = gm_f_write_debug_string_pns0400((void *)NULL, cx, NULL, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                uint8_t t87 = 0;
                F.ret = t86;
                goto gm_fret;
            }
            goto me9;
mn78:;
ma16:;
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t89 != ((uint8_t)5ULL)) goto mn88;
            uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
            gm_l10 = t90;
            {
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sfqk3rro4g7t0));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(1));
                gm_l11 = t92;
            }
            {
                void * t94 = gm_l0;
                gm_t88rcucg1b7uk t95 = gm_l2;
                gm_tdlpv0q69lov1 t96;
                gm_f_union_variants_a7khaqp((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
                if (!((((uint8_t *)&t96))[24] != 0)) goto ie93;
                gm_l12 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t96) + 0));
                {
                    uint32_t t98 = gm_l10;
                    uint64_t t99 = (uint64_t)t98;
                    uint64_t t100 = (uintptr_t)t99;
                    uint64_t t101 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 0) + 8);
                    if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_src8lmgjh8bsvgp, 468, 52);
                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l12) + 0)) + t100 * 72) + 0));
                    gm_l11 = t102;
                }
                goto ix93;
ie93:;
ix93:;
            }
            {
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t105 = ((t104 == ((uint64_t)0ULL)));
                if (!t105) goto ie103;
                {
                    gm_b16_8 t107 = gm_l11;
                    gm_tnpifv20irnph t108;
                    GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
                    gm_tabb710d9mq2e t109;
                    gm_f_format_g0ibdjv((void *)&t109, (void *)NULL, cx, NULL, t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_src8lmgjh8bsvgp, 469, 39);
                    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                    uint8_t t112 = 0;
                    F.ret = t111;
                    goto gm_fret;
                }
                goto ix103;
ie103:;
ix103:;
            }
            {
                gm_t6j6smj7qoctu t113;
                gm_b16_8 t114 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
                void * t115 = gm_l0;
                uint64_t t116 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_src8lmgjh8bsvgp, 470, 65);
                void * t118 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t116 * 136);
                gm_b16_8 t119 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t115, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t119);
                gm_tabb710d9mq2e t120;
                gm_f_format_i83sdhh((void *)&t120, (void *)NULL, cx, NULL, &t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 8, gm_src8lmgjh8bsvgp, 470, 20);
                gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
                gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                uint8_t t123 = 0;
                F.ret = t122;
                goto gm_fret;
            }
            goto me9;
mn88:;
ma17:;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t125 != ((uint8_t)6ULL)) goto mn124;
            {
                gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_l14 = t128;
                }
                {
                    for (;;) {
                        uint64_t t130 = gm_l15;
                        uint64_t t131 = gm_l14;
                        uint8_t t132 = (t130 < t131);
                        if (!t132) goto lb129;
                        {
                            uint64_t t135 = gm_l15;
                            uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                            gm_l16 = t138;
                            {
                                void * t140 = (void *)((uint8_t *)&F.l13);
                                void * t141 = gm_l0;
                                uint64_t t142 = gm_l16;
                                uint64_t t143 = (uintptr_t)t142;
                                uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                if (t143 >= (uintptr_t)(t144)) gm_trap_index(t143, t144, gm_src8lmgjh8bsvgp, 474, 64);
                                void * t145 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t143 * 136);
                                gm_b16_8 t146 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t141, t145);
                                if (gm_unw.kind) {
                                    goto gm_ll133;
                                }
                                gm_tftb1odc739t5 t147 = gm_f_push_3ef746b(t140, t146);
                                if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_src8lmgjh8bsvgp, 474, 36);
                                gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
                            }
                            goto me136;
mn137:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 474, 13);
me136:;
                        }
lc129:;
                        {
                            uint64_t t150 = gm_l15;
                            uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                            uint64_t t153 = r151;
                            gm_l15 = t153;
                        }
                    }
gm_ll133:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb129; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc129; }
                    }
                    goto gm_lad;
lb129:;
                }
            }
            {
                void * t155 = gm_l0;
                void * t156 = (void *)(((uint8_t *)t155) + 8);
                gm_t88rcucg1b7uk t157 = gm_l2;
                gm_t53at80pfdus3 t158;
                gm_f_kind_jcnt4hm((void *)&t158, t156, GM_CONV(gm_csguankko4lmhd, t157));
                uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&t158) + 0));
                if (t159 != ((uint8_t)17ULL)) goto ie154;
                uint32_t t160 = GM_LD(uint32_t, ((((uint8_t *)&t158) + 8) + 0));
                gm_l17 = t160;
                {
                    gm_f_list_epvducg((void *)&F.l18, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t163 = gm_l0;
                    gm_t88rcucg1b7uk t164 = gm_l2;
                    gm_th2lqbo77pvsc t165;
                    gm_f_struct_fields_chqu3it((void *)&t165, t163, GM_CONV(gm_csguankko4lmhd, t164));
                    if (!((((uint8_t *)&t165))[16] != 0)) goto ie162;
                    gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                    gm_l19 = t166;
                    {
                        {
                            gm_b16_8 t169 = gm_l19;
                            gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                            gm_l20 = t170;
                        }
                        {
                            gm_l21 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t172 = gm_l21;
                                uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                uint8_t t174 = (t172 < t173);
                                if (!t174) goto lb171;
                                {
                                    gm_t8ta482bl6be0 t177;
                                    uint64_t t178 = gm_l21;
                                    GM_ST(uint64_t, (uint8_t *)&t177 + 0, t178);
                                    uint64_t t179 = gm_l21;
                                    uint64_t t180 = (uintptr_t)t179;
                                    uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    memcpy((uint8_t *)&t177 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t180 * 40), 40);
                                    uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&t177) + 0));
                                    gm_l22 = t184;
                                    F.l23 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t177) + 8));
                                    {
                                        uint64_t t187 = gm_l22;
                                        uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                        uint8_t t189 = (t187 < t188);
                                        if (!t189) goto ie186;
                                        uint64_t t190 = gm_l22;
                                        uint64_t t191 = (uintptr_t)t190;
                                        uint64_t t192 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                        if (t191 >= (uintptr_t)(t192)) gm_trap_index(t191, t192, gm_src8lmgjh8bsvgp, 479, 52);
                                        uint8_t t193 = (!((GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t191 * 16) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t191 * 16)), gm_sfdbfrv46lihd, 1) == 0));
                                        if (!t193) goto ie186;
                                        {
                                            void * t195 = (void *)((uint8_t *)&F.l18);
                                            gm_t6j6smj7qoctu t196;
                                            gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t196 + 0, t197);
                                            uint64_t t198 = gm_l22;
                                            uint64_t t199 = (uintptr_t)t198;
                                            uint64_t t200 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                            if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_src8lmgjh8bsvgp, 479, 120);
                                            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t199 * 16));
                                            GM_ST(gm_b16_8, (uint8_t *)&t196 + 16, t201);
                                            gm_tabb710d9mq2e t202;
                                            gm_f_format_f6rro2t((void *)&t202, (void *)NULL, cx, NULL, &t196);
                                            if (gm_unw.kind) {
                                                goto gm_ll175;
                                            }
                                            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 8, gm_src8lmgjh8bsvgp, 479, 91);
                                            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&t202) + 8));
                                            gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                                            gm_tftb1odc739t5 t205 = gm_f_push_3ef746b(t195, t204);
                                            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_src8lmgjh8bsvgp, 479, 80);
                                            gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
                                        }
                                        goto ix186;
ie186:;
ix186:;
                                    }
                                    goto me182;
mn183:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 478, 21);
me182:;
                                }
lc171:;
                                {
                                    uint64_t t208 = gm_l21;
                                    uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                                    uint64_t t211 = r209;
                                    gm_l21 = t211;
                                }
                            }
gm_ll175:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb171; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc171; }
                            }
                            goto gm_lad;
lb171:;
                        }
                    }
                    goto ix162;
ie162:;
ix162:;
                }
                {
                    gm_t6j6smj7qoctu t212;
                    uint32_t t213 = gm_l17;
                    uint64_t t214 = (uint64_t)t213;
                    uint64_t t215 = (uintptr_t)t214;
                    void * t216 = gm_l0;
                    uint64_t t217 = GM_LD(uintptr_t, ((((uint8_t *)t216) + 1088) + 0) + 8);
                    if (t215 >= (uintptr_t)(t217)) gm_trap_index(t215, t217, gm_src8lmgjh8bsvgp, 482, 45);
                    gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t216) + 1088) + 0)) + t215 * 184) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t218);
                    gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                    gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                    gm_b16_8 t221;
                    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sg5gdnrith2i6));
                    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(2));
                    gm_tabb710d9mq2e t222;
                    gm_f_join_sfknljb((void *)&t222, (void *)NULL, cx, NULL, t220, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 8, gm_src8lmgjh8bsvgp, 482, 75);
                    gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&t222) + 8));
                    GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t223);
                    gm_tabb710d9mq2e t224;
                    gm_f_format_99gc605((void *)&t224, (void *)NULL, cx, NULL, &t212);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_src8lmgjh8bsvgp, 482, 24);
                    gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                    uint8_t t227 = 0;
                    F.ret = t226;
                    goto gm_fret;
                }
                goto ix154;
ie154:;
ix154:;
            }
            {
                gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t231;
                gm_f_join_sfknljb((void *)&t231, (void *)NULL, cx, NULL, t229, t230);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 8, gm_src8lmgjh8bsvgp, 484, 39);
                gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&t231) + 8));
                gm_tnpifv20irnph t233;
                GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t232);
                gm_tabb710d9mq2e t234;
                gm_f_format_0gv6kss((void *)&t234, (void *)NULL, cx, NULL, t233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_src8lmgjh8bsvgp, 484, 20);
                gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                uint8_t t237 = 0;
                F.ret = t236;
                goto gm_fret;
            }
            goto me9;
mn124:;
ma18:;
            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 452, 5);
me9:;
        }
        goto ix2;
ie2:;
        {
            gm_b16_8 t239;
            GM_ST(void *, (uint8_t *)&t239, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(1));
            uint8_t t240 = 0;
            F.ret = t239;
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
inline __attribute__((aligned(4))) void gm_f_specialize_si0k4v5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_specialize_si0k4v5 F;
    gm_b16_8 gm_l0; /* row */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* arity_tys */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_b16_8 gm_l8; /* subs */
    GM_STACK_CHECK(&F, 634u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_th2lqbo77pvsc t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_99597qp((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src8lmgjh8bsvgp, 339, 11);
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        {
            gm_b16_8 t14 = gm_l2;
            gm_l4 = t14;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l5;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l5;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 4));
                    gm_l6 = t26;
                    {
                        void * t28 = (void *)((uint8_t *)&F.l3);
                        gm_t88rcucg1b7uk t29 = gm_l6;
                        gm_t8oufsr65o5ha t30;
                        memset(&t30, 0, sizeof t30);
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30 + 8), t29);
                        gm_tftb1odc739t5 t31 = gm_f_push_h9qsb4d(t28, &t30);
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_src8lmgjh8bsvgp, 340, 42);
                        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 340, 21);
me24:;
                }
lc15:;
                {
                    uint64_t t34 = gm_l5;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l5 = t37;
                }
            }
lb15:;
        }
        goto me10;
mn11:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 0));
        if (t39 != ((uint8_t)1ULL)) goto mn38;
        gm_taevtln836oa8 t40 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 8) + 8));
        F.l7 = t40;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 8) + 112));
        gm_l8 = t41;
        {
            void * t44 = (void *)((uint8_t *)&F.l7);
            void * t45 = gm_l1;
            uint8_t t46 = gm_f_covers_59m49eg(t44, t45);
            uint8_t t47 = (uint8_t)!t46;
            if (!t47) goto ie43;
            {
                gm_th2lqbo77pvsc t49;
                memset(&t49, 0, sizeof t49);
                uint8_t t50 = 0;
                F.ret = t49;
                goto gm_fret;
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            void * t51 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t52 = gm_l8;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_tftb1odc739t5 t54 = gm_f_extend_eht4gub(t51, t53);
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src8lmgjh8bsvgp, 343, 13);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        goto me10;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 339, 5);
me10:;
    }
    {
        void * t56 = (void *)((uint8_t *)&F.l3);
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t58 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t59 = (uintptr_t)((uint64_t)1ULL);
        if (t59 > (uintptr_t)t57 || (uintptr_t)t57 > (uintptr_t)t57) gm_trap_slice(t59, (uintptr_t)t57, t57, gm_src8lmgjh8bsvgp, 346, 16);
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t58 + t59 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)((uintptr_t)t57 - t59));
        gm_tftb1odc739t5 t61 = gm_f_extend_eht4gub(t56, t60);
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_src8lmgjh8bsvgp, 346, 5);
        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
    }
    {
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t63);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_types_o2rpif3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_sub_types_o2rpif3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* c */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* err */
    gm_t88rcucg1b7uk gm_l6; /* ok */
    gm_b16_8 gm_l7; /* elems */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint32_t gm_l12; /* v */
    gm_tep2drsl9408b gm_l13; /* uv */
    gm_t88rcucg1b7uk gm_l14; /* p */
    GM_STACK_CHECK(&F, 673u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_nof7vfj((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)13ULL)) goto mn14;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l4 = t16;
        void * t18 = gm_l2;
        uint8_t t19 = ((GM_LD(uint8_t, ((uint8_t *)t18) + 0) == ((uint8_t)2ULL)));
        if (!t19) goto ie17;
        {
            void * t21 = (void *)((uint8_t *)&F.l3);
            gm_t88rcucg1b7uk t22 = gm_l4;
            gm_tftb1odc739t5 t23 = gm_f_push_k3l9nic(t21, GM_CONV(gm_csguankko4lmhd, t22));
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src8lmgjh8bsvgp, 215, 42);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        goto ix17;
ie17:;
ix17:;
        goto me6;
mn14:;
ma8:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t26 != ((uint8_t)14ULL)) goto mn25;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
        gm_l5 = t27;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l6 = t28;
        {
            void * t31 = gm_l2;
            uint8_t t32 = ((GM_LD(uint8_t, ((uint8_t *)t31) + 0) == ((uint8_t)3ULL)));
            if (!t32) goto ie30;
            {
                void * t34 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t35 = gm_l6;
                gm_tftb1odc739t5 t36 = gm_f_push_k3l9nic(t34, GM_CONV(gm_csguankko4lmhd, t35));
                if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_src8lmgjh8bsvgp, 217, 28);
                gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t39 = gm_l2;
            uint8_t t40 = ((GM_LD(uint8_t, ((uint8_t *)t39) + 0) == ((uint8_t)4ULL)));
            if (!t40) goto ie38;
            {
                void * t42 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t43 = gm_l5;
                gm_tftb1odc739t5 t44 = gm_f_push_k3l9nic(t42, GM_CONV(gm_csguankko4lmhd, t43));
                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_src8lmgjh8bsvgp, 218, 29);
                gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
            }
            goto ix38;
ie38:;
ix38:;
        }
        goto me6;
mn25:;
ma9:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t47 != ((uint8_t)12ULL)) goto mn46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l7 = t48;
        void * t50 = gm_l2;
        uint8_t t51 = ((GM_LD(uint8_t, ((uint8_t *)t50) + 0) == ((uint8_t)6ULL)));
        if (!t51) goto ie49;
        {
            void * t53 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t54 = gm_l7;
            gm_tftb1odc739t5 t55 = gm_f_extend_lfqasgq(t53, t54);
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src8lmgjh8bsvgp, 220, 48);
            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
        }
        goto ix49;
ie49:;
ix49:;
        goto me6;
mn46:;
ma10:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t58 != ((uint8_t)17ULL)) goto mn57;
        {
            void * t61 = gm_l2;
            uint8_t t62 = ((GM_LD(uint8_t, ((uint8_t *)t61) + 0) == ((uint8_t)6ULL)));
            if (!t62) goto ie60;
            {
                void * t65 = gm_l0;
                gm_t88rcucg1b7uk t66 = gm_l1;
                gm_th2lqbo77pvsc t67;
                gm_f_struct_fields_chqu3it((void *)&t67, t65, GM_CONV(gm_csguankko4lmhd, t66));
                if (!((((uint8_t *)&t67))[16] != 0)) goto ie64;
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                gm_l8 = t68;
                {
                    {
                        gm_b16_8 t71 = gm_l8;
                        gm_l9 = t71;
                    }
                    {
                        gm_l10 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t73 = gm_l10;
                            uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            uint8_t t75 = (t73 < t74);
                            if (!t75) goto lb72;
                            {
                                uint64_t t78 = gm_l10;
                                uint64_t t79 = (uintptr_t)t78;
                                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                gm_t0mglksrkfa5e t83 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t79 * 40));
                                F.l11 = t83;
                                {
                                    void * t85 = (void *)((uint8_t *)&F.l3);
                                    gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                                    gm_tftb1odc739t5 t87 = gm_f_push_k3l9nic(t85, GM_CONV(gm_csguankko4lmhd, t86));
                                    if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_src8lmgjh8bsvgp, 224, 39);
                                    gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                                }
                                goto me81;
mn82:;
                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 224, 21);
me81:;
                            }
lc72:;
                            {
                                uint64_t t90 = gm_l10;
                                uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                                uint64_t t93 = r91;
                                gm_l10 = t93;
                            }
                        }
lb72:;
                    }
                }
                goto ix64;
ie64:;
ix64:;
            }
            goto ix60;
ie60:;
            void * t95 = gm_l2;
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t95) + 0));
            if (t96 != ((uint8_t)5ULL)) goto ie94;
            uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)t95) + 8));
            gm_l12 = t97;
            {
                void * t100 = gm_l0;
                gm_t88rcucg1b7uk t101 = gm_l1;
                gm_tdlpv0q69lov1 t102;
                gm_f_union_variants_a7khaqp((void *)&t102, t100, GM_CONV(gm_csguankko4lmhd, t101));
                if (!((((uint8_t *)&t102))[24] != 0)) goto ie99;
                gm_l13 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t102) + 0));
                {
                    uint32_t t105 = gm_l12;
                    uint64_t t106 = (uint64_t)t105;
                    uint64_t t107 = (uintptr_t)t106;
                    uint64_t t108 = GM_LD(uintptr_t, (((uint8_t *)&gm_l13) + 0) + 8);
                    if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_src8lmgjh8bsvgp, 228, 29);
                    if (!(((((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l13) + 0)) + t107 * 72) + 16))[4] != 0)) goto ie104;
                    gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l13) + 0)) + t107 * 72) + 16));
                    gm_l14 = t109;
                    {
                        void * t111 = (void *)((uint8_t *)&F.l3);
                        gm_t88rcucg1b7uk t112 = gm_l14;
                        gm_tftb1odc739t5 t113 = gm_f_push_k3l9nic(t111, GM_CONV(gm_csguankko4lmhd, t112));
                        if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_src8lmgjh8bsvgp, 228, 61);
                        gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                    }
                    goto ix104;
ie104:;
ix104:;
                }
                goto ix99;
ie99:;
ix99:;
            }
            goto ix94;
ie94:;
ix94:;
ix60:;
        }
        goto me6;
mn57:;
ma11:;
        goto me6;
mn115:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 214, 5);
me6:;
    }
    {
        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t118 = 0;
        F.ret = t117;
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
__attribute__((aligned(4))) void gm_f_useful_ge1iad5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_useful_ge1iad5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* rows */
    gm_b16_8 gm_l2; /* q */
    void * gm_l3; /* head */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* r */
    gm_b16_8 gm_l14; /* all */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l23; /* tys */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* r */
    gm_b16_8 gm_l28; /* x */
    gm_b16_8 gm_l29; /* spec_q */
    gm_b16_8 gm_l30; /* w */
    gm_th2lqbo77pvsc gm_l31; /* ctors */
    uint8_t gm_l32; /* complete */
    gm_b16_8 gm_l33; /* cs */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l40; /* tys */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l44; /* r */
    gm_b16_8 gm_l45; /* x */
    gm_b16_8 gm_l46; /* spec_q */
    gm_b16_8 gm_l47; /* w */
    gm_b16_8 gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    gm_b16_8 gm_l51; /* r */
    gm_b16_8 gm_l52; /* w */
    gm_b16_8 gm_l54; /* <tmp> */
    uint64_t gm_l55; /* <tmp> */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    gm_t88rcucg1b7uk gm_l60; /* t */
    GM_STACK_CHECK(&F, 4392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
            if (!t8) goto ie6;
            {
                gm_b16_8 t10 = gm_f_empty_slice_6rs1n5s();
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t10);
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_src8lmgjh8bsvgp, 357, 14);
        void * t15 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 136);
        gm_l3 = t15;
    }
    {
        void * t16 = gm_l3;
        gm_t88rcucg1b7uk t17 = GM_CONV(gm_t88rcucg1b7uk, gm_f_dpat_ty_2ipsk5e(t16));
        gm_l4 = t17;
    }
    {
        gm_f_list_99597qp((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t19 = gm_l1;
            gm_l6 = t19;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t21 = gm_l7;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t23 = (t21 < t22);
                if (!t23) goto lb20;
                {
                    uint64_t t26 = gm_l7;
                    uint64_t t27 = (uintptr_t)t26;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t27 * 16));
                    gm_l8 = t31;
                    {
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t35 = (t34 > ((uint64_t)0ULL));
                        if (!t35) goto ie33;
                        {
                            void * t37 = (void *)((uint8_t *)&F.l5);
                            uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_src8lmgjh8bsvgp, 361, 36);
                            gm_tftb1odc739t5 t40 = gm_f_push_h9qsb4d(t37, &(*(gm_t8oufsr65o5ha *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t38 * 136)));
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src8lmgjh8bsvgp, 361, 24);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 360, 5);
me29:;
                }
lc20:;
                {
                    uint64_t t43 = gm_l7;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l7 = t46;
                }
            }
lb20:;
        }
    }
    {
        void * t47 = gm_l3;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t47) + 0));
        if (t50 != ((uint8_t)1ULL)) goto mn49;
        gm_taevtln836oa8 t51 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)t47) + 8) + 8));
        F.l9 = t51;
        {
            gm_f_list_ahp090e((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            if (t54 != ((uint8_t)7ULL)) goto ie53;
            gm_tbci5ospk5nnn t55 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l9) + 8) + 0));
            F.l11 = t55;
            gm_tbci5ospk5nnn t56 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l9) + 8) + 48));
            F.l12 = t56;
            {
                gm_f_list_99597qp((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t58 = (void *)((uint8_t *)&F.l13);
                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                gm_tftb1odc739t5 t61 = gm_f_extend_eht4gub(t58, t60);
                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_src8lmgjh8bsvgp, 369, 17);
                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
            }
            {
                void * t63 = (void *)((uint8_t *)&F.l13);
                void * t64 = gm_l3;
                gm_tftb1odc739t5 t65 = gm_f_push_h9qsb4d(t63, &(*(gm_t8oufsr65o5ha *)((uint8_t *)t64)));
                if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_src8lmgjh8bsvgp, 370, 17);
                gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
            }
            {
                void * t67 = gm_l0;
                gm_t88rcucg1b7uk t68 = gm_l4;
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                gm_th2lqbo77pvsc t71;
                gm_f_all_ctors_o7aijfe((void *)&t71, (void *)NULL, cx, NULL, t67, t68, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t72;
                if (((((uint8_t *)&t71))[16] != 0)) {
                    gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 0));
                    t72 = t73;
                } else {
                    gm_b16_8 t74 = gm_f_empty_slice_kvdmouk();
                    t72 = t74;
                }
                gm_l14 = t72;
            }
            {
                {
                    gm_b16_8 t76 = gm_l14;
                    gm_l15 = t76;
                }
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t78 = gm_l16;
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t80 = (t78 < t79);
                        if (!t80) goto lb77;
                        {
                            uint64_t t83 = gm_l16;
                            uint64_t t84 = (uintptr_t)t83;
                            uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            gm_taevtln836oa8 t88 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t84 * 104));
                            F.l17 = t88;
                            {
                                uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                                if (t91 != ((uint8_t)7ULL)) goto ie90;
                                gm_tbci5ospk5nnn t92 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l17) + 8) + 0));
                                F.l18 = t92;
                                gm_tbci5ospk5nnn t93 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l17) + 8) + 48));
                                F.l19 = t93;
                                int32_t t94 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                                uint8_t t95 = (t94 >= ((int32_t)0LL));
                                if (!t95) goto ie90;
                                int32_t t96 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l12)));
                                uint8_t t97 = (t96 <= ((int32_t)0LL));
                                if (!t97) goto ie90;
                                {
                                    void * t99 = (void *)((uint8_t *)&F.l10);
                                    gm_tftb1odc739t5 t100 = gm_f_push_95uqvlq(t99, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l17)));
                                    if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_src8lmgjh8bsvgp, 373, 82);
                                    gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                                }
                                goto ix90;
ie90:;
ix90:;
                            }
                            goto me86;
mn87:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 372, 17);
me86:;
                        }
lc77:;
                        {
                            uint64_t t103 = gm_l16;
                            uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                            uint64_t t106 = r104;
                            gm_l16 = t106;
                        }
                    }
lb77:;
                }
            }
            goto ix53;
ie53:;
            {
                void * t108 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t109 = gm_f_push_95uqvlq(t108, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l9)));
                if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_src8lmgjh8bsvgp, 376, 17);
                gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
            }
ix53:;
        }
        {
            {
                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_l20 = t112;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t114 = gm_l21;
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t116 = (t114 < t115);
                    if (!t116) goto lb113;
                    {
                        uint64_t t119 = gm_l21;
                        uint64_t t120 = (uintptr_t)t119;
                        uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_taevtln836oa8 t124 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t120 * 104));
                        F.l22 = t124;
                        {
                            void * t126 = gm_l0;
                            gm_t88rcucg1b7uk t127 = gm_l4;
                            void * t128 = (void *)((uint8_t *)&F.l22);
                            gm_b16_8 t129 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t126, t127, t128);
                            if (gm_unw.kind) {
                                goto gm_ll117;
                            }
                            gm_l23 = t129;
                        }
                        {
                            gm_f_list_furncbs((void *)&F.l24, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll117;
                            }
                        }
                        {
                            {
                                gm_b16_8 t131 = gm_l1;
                                gm_l25 = t131;
                            }
                            {
                                gm_l26 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t133 = gm_l26;
                                    uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                    uint8_t t135 = (t133 < t134);
                                    if (!t135) goto lb132;
                                    {
                                        uint64_t t138 = gm_l26;
                                        uint64_t t139 = (uintptr_t)t138;
                                        uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t139 * 16));
                                        gm_l27 = t143;
                                        {
                                            gm_b16_8 t146 = gm_l27;
                                            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                                            void * t148 = (void *)((uint8_t *)&F.l22);
                                            gm_b16_8 t149 = gm_l23;
                                            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                                            gm_th2lqbo77pvsc t151;
                                            gm_f_specialize_si0k4v5((void *)&t151, (void *)NULL, cx, NULL, t147, t148, t150);
                                            if (gm_unw.kind) {
                                                goto gm_ll136;
                                            }
                                            if (!((((uint8_t *)&t151))[16] != 0)) goto ie145;
                                            gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                                            gm_l28 = t152;
                                            {
                                                void * t154 = (void *)((uint8_t *)&F.l24);
                                                gm_b16_8 t155 = gm_l28;
                                                gm_tftb1odc739t5 t156 = gm_f_push_s94euqb(t154, t155);
                                                if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_src8lmgjh8bsvgp, 382, 58);
                                                gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                                            }
                                            goto ix145;
ie145:;
ix145:;
                                        }
                                        goto me141;
mn142:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 381, 17);
me141:;
                                    }
lc132:;
                                    {
                                        uint64_t t159 = gm_l26;
                                        uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                                        uint64_t t162 = r160;
                                        gm_l26 = t162;
                                    }
                                }
gm_ll136:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb132; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc132; }
                                }
                                goto gm_ll117;
lb132:;
                            }
                        }
                        {
                            gm_b16_8 t163 = gm_l2;
                            void * t164 = (void *)((uint8_t *)&F.l22);
                            gm_b16_8 t165 = gm_l23;
                            gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                            gm_th2lqbo77pvsc t167;
                            gm_f_specialize_si0k4v5((void *)&t167, (void *)NULL, cx, NULL, t163, t164, t166);
                            if (gm_unw.kind) {
                                goto gm_ll117;
                            }
                            gm_b16_8 t168;
                            if (((((uint8_t *)&t167))[16] != 0)) {
                                gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 0));
                                t168 = t169;
                            } else {
                                gm_th2lqbo77pvsc t170;
                                memset(&t170, 0, sizeof t170);
                                uint8_t t171 = 0;
                                F.ret = t170;
                                goto gm_fret;
                            }
                            gm_l29 = t168;
                        }
                        {
                            void * t173 = gm_l0;
                            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                            gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                            gm_b16_8 t176 = gm_l29;
                            gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                            gm_th2lqbo77pvsc t178;
                            gm_f_useful_ge1iad5((void *)&t178, (void *)NULL, cx, NULL, t173, t175, t177);
                            if (gm_unw.kind) {
                                goto gm_ll117;
                            }
                            if (!((((uint8_t *)&t178))[16] != 0)) goto ie172;
                            gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                            gm_l30 = t179;
                            {
                                gm_t88rcucg1b7uk t181 = gm_l4;
                                uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                gm_b16_8 t183 = gm_l30;
                                gm_b16_8 t184 = gm_f_rebuild_90secge((void *)NULL, cx, NULL, t181, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l22)), t182, t183);
                                if (gm_unw.kind) {
                                    goto gm_ll117;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t184);
                                goto gm_fdone;
                            }
                            goto ix172;
ie172:;
ix172:;
                        }
                        goto me122;
mn123:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 378, 13);
me122:;
                    }
lc113:;
                    {
                        uint64_t t186 = gm_l21;
                        uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                        uint64_t t189 = r187;
                        gm_l21 = t189;
                    }
                }
gm_ll117:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb113; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc113; }
                }
                goto gm_lad;
lb113:;
            }
        }
        {
            gm_th2lqbo77pvsc t190;
            memset(&t190, 0, sizeof t190);
            uint8_t t191 = 0;
            F.ret = t190;
            goto gm_fret;
        }
        goto me48;
mn49:;
        uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)t47) + 0));
        if (t193 != ((uint8_t)0ULL)) goto mn192;
        {
            void * t195 = gm_l0;
            gm_t88rcucg1b7uk t196 = gm_l4;
            gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
            gm_f_all_ctors_o7aijfe((void *)&gm_l31, (void *)NULL, cx, NULL, t195, t196, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l32 = ((uint8_t)0);
        }
        {
            if (!((((uint8_t *)&gm_l31))[16] != 0)) goto ie199;
            gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
            gm_l33 = t200;
            {
                gm_l32 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t203 = gm_l33;
                    gm_l34 = t203;
                }
                {
                    gm_l35 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t205 = gm_l35;
                        uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        uint8_t t207 = (t205 < t206);
                        if (!t207) goto lb204;
                        {
                            uint64_t t210 = gm_l35;
                            uint64_t t211 = (uintptr_t)t210;
                            uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            gm_taevtln836oa8 t215 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t211 * 104));
                            F.l36 = t215;
                            {
                                gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                                gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                                void * t220 = (void *)((uint8_t *)&F.l36);
                                uint8_t t221 = gm_f_column_covers_gn61qhr(t219, t220);
                                uint8_t t222 = (uint8_t)!t221;
                                if (!t222) goto ie217;
                                {
                                    gm_l32 = ((uint8_t)0);
                                }
                                goto ix217;
ie217:;
ix217:;
                            }
                            goto me213;
mn214:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 394, 17);
me213:;
                        }
lc204:;
                        {
                            uint64_t t225 = gm_l35;
                            uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                            uint64_t t228 = r226;
                            gm_l35 = t228;
                        }
                    }
lb204:;
                }
            }
            goto ix199;
ie199:;
ix199:;
        }
        {
            uint8_t t230 = gm_l32;
            if (!t230) goto ie229;
            {
                {
                    gm_th2lqbo77pvsc t233 = gm_l31;
                    gm_b16_8 t234;
                    if (((((uint8_t *)&t233))[16] != 0)) {
                        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t233) + 0));
                        t234 = t235;
                    } else {
                        gm_b16_8 t236 = gm_f_empty_slice_kvdmouk();
                        t234 = t236;
                    }
                    gm_l37 = t234;
                }
                {
                    gm_l38 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t238 = gm_l38;
                        uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        uint8_t t240 = (t238 < t239);
                        if (!t240) goto lb237;
                        {
                            uint64_t t243 = gm_l38;
                            uint64_t t244 = (uintptr_t)t243;
                            uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            gm_taevtln836oa8 t248 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t244 * 104));
                            F.l39 = t248;
                            {
                                void * t250 = gm_l0;
                                gm_t88rcucg1b7uk t251 = gm_l4;
                                void * t252 = (void *)((uint8_t *)&F.l39);
                                gm_b16_8 t253 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t250, t251, t252);
                                if (gm_unw.kind) {
                                    goto gm_ll241;
                                }
                                gm_l40 = t253;
                            }
                            {
                                gm_f_list_furncbs((void *)&F.l41, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll241;
                                }
                            }
                            {
                                {
                                    gm_b16_8 t255 = gm_l1;
                                    gm_l42 = t255;
                                }
                                {
                                    gm_l43 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t257 = gm_l43;
                                        uint64_t t258 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        uint8_t t259 = (t257 < t258);
                                        if (!t259) goto lb256;
                                        {
                                            uint64_t t262 = gm_l43;
                                            uint64_t t263 = (uintptr_t)t262;
                                            uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                            gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t263 * 16));
                                            gm_l44 = t267;
                                            {
                                                gm_b16_8 t270 = gm_l44;
                                                gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                                                void * t272 = (void *)((uint8_t *)&F.l39);
                                                gm_b16_8 t273 = gm_l40;
                                                gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&t273));
                                                gm_th2lqbo77pvsc t275;
                                                gm_f_specialize_si0k4v5((void *)&t275, (void *)NULL, cx, NULL, t271, t272, t274);
                                                if (gm_unw.kind) {
                                                    goto gm_ll260;
                                                }
                                                if (!((((uint8_t *)&t275))[16] != 0)) goto ie269;
                                                gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                                                gm_l45 = t276;
                                                {
                                                    void * t278 = (void *)((uint8_t *)&F.l41);
                                                    gm_b16_8 t279 = gm_l45;
                                                    gm_tftb1odc739t5 t280 = gm_f_push_s94euqb(t278, t279);
                                                    if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_src8lmgjh8bsvgp, 403, 58);
                                                    gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
                                                }
                                                goto ix269;
ie269:;
ix269:;
                                            }
                                            goto me265;
mn266:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 402, 21);
me265:;
                                        }
lc256:;
                                        {
                                            uint64_t t283 = gm_l43;
                                            uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                                            uint64_t t286 = r284;
                                            gm_l43 = t286;
                                        }
                                    }
gm_ll260:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb256; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc256; }
                                    }
                                    goto gm_ll241;
lb256:;
                                }
                            }
                            {
                                gm_b16_8 t287 = gm_l2;
                                void * t288 = (void *)((uint8_t *)&F.l39);
                                gm_b16_8 t289 = gm_l40;
                                gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
                                gm_th2lqbo77pvsc t291;
                                gm_f_specialize_si0k4v5((void *)&t291, (void *)NULL, cx, NULL, t287, t288, t290);
                                if (gm_unw.kind) {
                                    goto gm_ll241;
                                }
                                gm_b16_8 t292;
                                if (((((uint8_t *)&t291))[16] != 0)) {
                                    gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&t291) + 0));
                                    t292 = t293;
                                } else {
                                    gm_th2lqbo77pvsc t294;
                                    memset(&t294, 0, sizeof t294);
                                    uint8_t t295 = 0;
                                    F.ret = t294;
                                    goto gm_fret;
                                }
                                gm_l46 = t292;
                            }
                            {
                                void * t297 = gm_l0;
                                gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
                                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                                gm_b16_8 t300 = gm_l46;
                                gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
                                gm_th2lqbo77pvsc t302;
                                gm_f_useful_ge1iad5((void *)&t302, (void *)NULL, cx, NULL, t297, t299, t301);
                                if (gm_unw.kind) {
                                    goto gm_ll241;
                                }
                                if (!((((uint8_t *)&t302))[16] != 0)) goto ie296;
                                gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                                gm_l47 = t303;
                                {
                                    gm_t88rcucg1b7uk t305 = gm_l4;
                                    uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                    gm_b16_8 t307 = gm_l47;
                                    gm_b16_8 t308 = gm_f_rebuild_90secge((void *)NULL, cx, NULL, t305, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l39)), t306, t307);
                                    if (gm_unw.kind) {
                                        goto gm_ll241;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t308);
                                    goto gm_fdone;
                                }
                                goto ix296;
ie296:;
ix296:;
                            }
                            goto me246;
mn247:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 399, 17);
me246:;
                        }
lc237:;
                        {
                            uint64_t t310 = gm_l38;
                            uint64_t r311; bool o312 = __builtin_add_overflow(t310, ((uint64_t)1ULL), &r311);
                            uint64_t t313 = r311;
                            gm_l38 = t313;
                        }
                    }
gm_ll241:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb237; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc237; }
                    }
                    goto gm_lad;
lb237:;
                }
            }
            {
                gm_th2lqbo77pvsc t314;
                memset(&t314, 0, sizeof t314);
                uint8_t t315 = 0;
                F.ret = t314;
                goto gm_fret;
            }
            goto ix229;
ie229:;
ix229:;
        }
        {
            gm_f_list_furncbs((void *)&F.l48, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t317 = gm_l1;
                gm_l49 = t317;
            }
            {
                gm_l50 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t319 = gm_l50;
                    uint64_t t320 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                    uint8_t t321 = (t319 < t320);
                    if (!t321) goto lb318;
                    {
                        uint64_t t324 = gm_l50;
                        uint64_t t325 = (uintptr_t)t324;
                        uint64_t t326 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                        gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t325 * 16));
                        gm_l51 = t329;
                        {
                            uint64_t t332 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                            uint8_t t333 = (t332 > ((uint64_t)0ULL));
                            if (!t333) goto ie331;
                            uint64_t t334 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t335 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                            if (t334 >= (uintptr_t)(t335)) gm_trap_index(t334, t335, gm_src8lmgjh8bsvgp, 412, 34);
                            uint8_t t336;
                            uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t334 * 136) + 0));
                            if (t339 != ((uint8_t)0ULL)) goto mn338;
                            t336 = ((uint8_t)1);
                            goto me337;
mn338:;
                            t336 = ((uint8_t)0);
                            goto me337;
mn340:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 412, 34);
me337:;
                            if (!t336) goto ie331;
                            {
                                void * t342 = (void *)((uint8_t *)&F.l48);
                                uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                                void * t344 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)));
                                uint64_t t345 = (uintptr_t)((uint64_t)1ULL);
                                if (t345 > (uintptr_t)t343 || (uintptr_t)t343 > (uintptr_t)t343) gm_trap_slice(t345, (uintptr_t)t343, t343, gm_src8lmgjh8bsvgp, 412, 66);
                                gm_b16_8 t346;
                                GM_ST(void *, (uint8_t *)&t346, (void *)(((uint8_t *)t344 + t345 * 136)));
                                GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)((uintptr_t)t343 - t345));
                                gm_tftb1odc739t5 t347 = gm_f_push_s94euqb(t342, t346);
                                if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 1, gm_src8lmgjh8bsvgp, 412, 53);
                                gm_b0_1 t348 = GM_LD(gm_b0_1, (((uint8_t *)&t347) + 1));
                            }
                            goto ix331;
ie331:;
ix331:;
                        }
                        goto me327;
mn328:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 411, 13);
me327:;
                    }
lc318:;
                    {
                        uint64_t t350 = gm_l50;
                        uint64_t r351; bool o352 = __builtin_add_overflow(t350, ((uint64_t)1ULL), &r351);
                        uint64_t t353 = r351;
                        gm_l50 = t353;
                    }
                }
lb318:;
            }
        }
        {
            void * t354 = gm_l0;
            gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 0));
            gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
            uint64_t t357 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t358 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t359 = (uintptr_t)((uint64_t)1ULL);
            if (t359 > (uintptr_t)t357 || (uintptr_t)t357 > (uintptr_t)t357) gm_trap_slice(t359, (uintptr_t)t357, t357, gm_src8lmgjh8bsvgp, 414, 43);
            gm_b16_8 t360;
            GM_ST(void *, (uint8_t *)&t360, (void *)(((uint8_t *)t358 + t359 * 136)));
            GM_ST(uintptr_t, (uint8_t *)&t360 + 8, (uintptr_t)((uintptr_t)t357 - t359));
            gm_th2lqbo77pvsc t361;
            gm_f_useful_ge1iad5((void *)&t361, (void *)NULL, cx, NULL, t354, t356, t360);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t362;
            if (((((uint8_t *)&t361))[16] != 0)) {
                gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)&t361) + 0));
                t362 = t363;
            } else {
                gm_th2lqbo77pvsc t364;
                memset(&t364, 0, sizeof t364);
                uint8_t t365 = 0;
                F.ret = t364;
                goto gm_fret;
            }
            gm_l52 = t362;
        }
        {
            gm_t88rcucg1b7uk t366 = gm_l4;
            memset(&F.l53, 0, sizeof F.l53);
            GM_ST(uint8_t, (uint8_t *)&F.l53 + 0, ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l53 + 8, t366);
        }
        {
            {
                gm_th2lqbo77pvsc t368 = gm_l31;
                gm_b16_8 t369;
                if (((((uint8_t *)&t368))[16] != 0)) {
                    gm_b16_8 t370 = GM_LD(gm_b16_8, (((uint8_t *)&t368) + 0));
                    t369 = t370;
                } else {
                    gm_b16_8 t371 = gm_f_empty_slice_kvdmouk();
                    t369 = t371;
                }
                gm_l54 = t369;
            }
            {
                gm_l55 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t373 = gm_l55;
                    uint64_t t374 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                    uint8_t t375 = (t373 < t374);
                    if (!t375) goto lb372;
                    {
                        uint64_t t378 = gm_l55;
                        uint64_t t379 = (uintptr_t)t378;
                        uint64_t t380 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                        gm_taevtln836oa8 t383 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l54)) + t379 * 104));
                        F.l56 = t383;
                        {
                            gm_b16_8 t386 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_b16_8 t387 = GM_LD(gm_b16_8, ((uint8_t *)&t386));
                            void * t388 = (void *)((uint8_t *)&F.l56);
                            uint8_t t389 = gm_f_column_covers_gn61qhr(t387, t388);
                            uint8_t t390 = (uint8_t)!t389;
                            if (!t390) goto ie385;
                            {
                                gm_f_list_99597qp((void *)&F.l57, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll376;
                                }
                            }
                            {
                                {
                                    void * t393 = gm_l0;
                                    gm_t88rcucg1b7uk t394 = gm_l4;
                                    void * t395 = (void *)((uint8_t *)&F.l56);
                                    gm_b16_8 t396 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t393, t394, t395);
                                    if (gm_unw.kind) {
                                        goto gm_ll376;
                                    }
                                    gm_l58 = t396;
                                }
                                {
                                    gm_l59 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t398 = gm_l59;
                                        uint64_t t399 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                                        uint8_t t400 = (t398 < t399);
                                        if (!t400) goto lb397;
                                        {
                                            uint64_t t403 = gm_l59;
                                            uint64_t t404 = (uintptr_t)t403;
                                            uint64_t t405 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                                            gm_t88rcucg1b7uk t408 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l58)) + t404 * 4));
                                            gm_l60 = t408;
                                            {
                                                void * t410 = (void *)((uint8_t *)&F.l57);
                                                gm_t88rcucg1b7uk t411 = gm_l60;
                                                gm_t8oufsr65o5ha t412;
                                                memset(&t412, 0, sizeof t412);
                                                GM_ST(uint8_t, ((uint8_t *)&t412 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t412 + 8), t411);
                                                gm_tftb1odc739t5 t413 = gm_f_push_h9qsb4d(t410, &t412);
                                                if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 1, gm_src8lmgjh8bsvgp, 420, 53);
                                                gm_b0_1 t414 = GM_LD(gm_b0_1, (((uint8_t *)&t413) + 1));
                                            }
                                            goto me406;
mn407:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 420, 21);
me406:;
                                        }
lc397:;
                                        {
                                            uint64_t t416 = gm_l59;
                                            uint64_t r417; bool o418 = __builtin_add_overflow(t416, ((uint64_t)1ULL), &r417);
                                            uint64_t t419 = r417;
                                            gm_l59 = t419;
                                        }
                                    }
lb397:;
                                }
                            }
                            {
                                gm_tbq1t03a53gpo t420;
                                memset((uint8_t *)&t420 + 4, 0, 4);
                                gm_t88rcucg1b7uk t421 = gm_l4;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t420 + 0, t421);
                                memcpy((uint8_t *)&t420 + 8, ((uint8_t *)&F.l56), 104);
                                gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t420 + 112, t422);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)) + 0, ((uint8_t)1ULL));
                                GM_ST(gm_tbq1t03a53gpo, (uint8_t *)&(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)) + 8, t420);
                            }
                            {
                                goto lb372;
                            }
                            goto ix385;
ie385:;
ix385:;
                        }
                        goto me381;
mn382:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 417, 13);
me381:;
                    }
lc372:;
                    {
                        uint64_t t424 = gm_l55;
                        uint64_t r425; bool o426 = __builtin_add_overflow(t424, ((uint64_t)1ULL), &r425);
                        uint64_t t427 = r425;
                        gm_l55 = t427;
                    }
                }
gm_ll376:;
                if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb372; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc372; }
                }
                goto gm_lad;
lb372:;
            }
        }
        {
            gm_f_list_99597qp((void *)&F.l61, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t428 = (void *)((uint8_t *)&F.l61);
            gm_tftb1odc739t5 t429 = gm_f_push_h9qsb4d(t428, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)));
            if (((((uint8_t *)&t429))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t429) + 1, gm_src8lmgjh8bsvgp, 426, 13);
            gm_b0_1 t430 = GM_LD(gm_b0_1, (((uint8_t *)&t429) + 1));
        }
        {
            void * t431 = (void *)((uint8_t *)&F.l61);
            gm_b16_8 t432 = gm_l52;
            gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
            gm_tftb1odc739t5 t434 = gm_f_extend_eht4gub(t431, t433);
            if (((((uint8_t *)&t434))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t434) + 1, gm_src8lmgjh8bsvgp, 427, 13);
            gm_b0_1 t435 = GM_LD(gm_b0_1, (((uint8_t *)&t434) + 1));
        }
        {
            gm_b16_8 t436 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t436);
            goto gm_fdone;
        }
        goto me48;
mn192:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 363, 5);
me48:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_wrap_each_2nbqohg(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_wrap_each_2nbqohg F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_b16_8 gm_l2; /* ds */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t8oufsr65o5ha t15 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 136));
                    F.l6 = t15;
                    {
                        gm_f_list_99597qp((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t17 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t18 = gm_f_push_h9qsb4d(t17, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l6)));
                        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_src8lmgjh8bsvgp, 206, 9);
                        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
                    }
                    {
                        void * t20 = gm_l3;
                        gm_tbq1t03a53gpo t21;
                        memset((uint8_t *)&t21 + 4, 0, 4);
                        gm_t88rcucg1b7uk t22 = gm_l0;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t21 + 0, t22);
                        memcpy((uint8_t *)&t21 + 8, ((uint8_t *)&F.l1), 104);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t21 + 112, t23);
                        gm_t8oufsr65o5ha t24;
                        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t24 + 8), t21);
                        gm_tftb1odc739t5 t25 = gm_f_push_h9qsb4d(t20, &t24);
                        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_src8lmgjh8bsvgp, 207, 9);
                        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 204, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l5;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l5 = t31;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
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
